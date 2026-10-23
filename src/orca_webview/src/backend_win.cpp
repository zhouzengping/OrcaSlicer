#include "backend_win.h"
#include "backend_win_environment.h"
#include "string_util.h"

#include <atomic>
#include <memory>
#include <utility>

#include <windows.h>
#include <WebView2.h>

namespace orca::wv::detail {
namespace {

// Shared closed flag: keeps async COM callbacks from touching a backend that
// was closed (and possibly destroyed) before creation finished. UI thread only.
struct SharedState
{
    bool closed = false;
};

// Hand-rolled ref-counted COM callback base (no wrl/com_ptr in this project).
// One-shot handlers are owned solely by WebView2 after the call; event
// handlers stay registered for the core's lifetime.
template <typename Interface>
class RefCountedHandler : public Interface
{
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
    {
        if (ppv == nullptr)
            return E_POINTER;
        if (riid == IID_IUnknown || riid == __uuidof(Interface))
        {
            *ppv = static_cast<Interface*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return ++m_refs;
    }

    ULONG STDMETHODCALLTYPE Release() override
    {
        ULONG refs = --m_refs;
        if (refs == 0)
            delete this;
        return refs;
    }

protected:
    RefCountedHandler() = default;
    virtual ~RefCountedHandler() = default;

private:
    std::atomic<ULONG> m_refs = 1;
};

// Repeating event handler (sender, args) invocation.
template <typename Interface, typename Sender, typename Args>
struct EventHandler final : public RefCountedHandler<Interface>
{
    using Fn = std::function<void(Sender* sender, Args* args)>;

    explicit EventHandler(Fn fn)
        : m_fn(std::move(fn))
    {
    }

    HRESULT STDMETHODCALLTYPE Invoke(Sender* sender, Args* args) override
    {
        if (m_fn)
            m_fn(sender, args);
        return S_OK;
    }

private:
    Fn m_fn;
};

using WebMessageReceivedHandler = EventHandler<ICoreWebView2WebMessageReceivedEventHandler, ICoreWebView2, ICoreWebView2WebMessageReceivedEventArgs>;
using NavigationStartingHandler = EventHandler<ICoreWebView2NavigationStartingEventHandler, ICoreWebView2, ICoreWebView2NavigationStartingEventArgs>;
using NavigationCompletedHandler = EventHandler<ICoreWebView2NavigationCompletedEventHandler, ICoreWebView2, ICoreWebView2NavigationCompletedEventArgs>;
using SourceChangedHandler = EventHandler<ICoreWebView2SourceChangedEventHandler, ICoreWebView2, ICoreWebView2SourceChangedEventArgs>;
// This WebView2 header revision exposes no dedicated title args interface; the
// handler receives a bare IUnknown* and the title is read from the sender.
using DocumentTitleChangedHandler = EventHandler<ICoreWebView2DocumentTitleChangedEventHandler, ICoreWebView2, IUnknown>;
using NewWindowRequestedHandler = EventHandler<ICoreWebView2NewWindowRequestedEventHandler, ICoreWebView2, ICoreWebView2NewWindowRequestedEventArgs>;

// One-shot: controller creation completed.
struct ControllerCompletedHandler final : public RefCountedHandler<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>
{
    using Fn = std::function<void(HRESULT errorCode, ICoreWebView2Controller* controller)>;

    explicit ControllerCompletedHandler(Fn fn)
        : m_fn(std::move(fn))
    {
    }

    HRESULT STDMETHODCALLTYPE Invoke(HRESULT errorCode, ICoreWebView2Controller* controller) override
    {
        Fn fn = std::move(m_fn);
        m_fn = nullptr;
        if (fn)
            fn(errorCode, controller);
        return S_OK;
    }

private:
    Fn m_fn;
};

// One-shot: ExecuteScript completed.
struct ScriptCompletedHandler final : public RefCountedHandler<ICoreWebView2ExecuteScriptCompletedHandler>
{
    using Fn = std::function<void(HRESULT errorCode, LPCWSTR result)>;

    explicit ScriptCompletedHandler(Fn fn)
        : m_fn(std::move(fn))
    {
    }

    HRESULT STDMETHODCALLTYPE Invoke(HRESULT errorCode, LPCWSTR result) override
    {
        Fn fn = std::move(m_fn);
        m_fn = nullptr;
        if (fn)
            fn(errorCode, result);
        return S_OK;
    }

private:
    Fn m_fn;
};

// CoTaskMem-allocated string -> UTF-8; always frees a non-null value.
std::string take_task_string(LPWSTR value)
{
    if (value == nullptr)
        return std::string();
    std::string utf8 = wide_to_utf8(value);
    CoTaskMemFree(value);
    return utf8;
}

class BackendWin final : public Backend
{
public:
    BackendWin(void* native_parent, const WebView::Config& config, EventProxy& proxy)
        : m_parent_hwnd(static_cast<HWND>(native_parent))
        , m_config(config)
        , m_proxy(proxy)
        , m_state(std::make_shared<SharedState>())
    {
    }

    ~BackendWin() override
    {
        close();
    }

    Status start() override
    {
        if (!IsWindow(m_parent_hwnd))
            return Status::make(Errc::ParentInvalid, "native parent is not a window");

        m_in_start = true;
        acquire_shared_environment(m_config.browser_data_dir, m_config.additional_browser_arguments,
            [this, state = m_state](Status status, ICoreWebView2Environment* environment)
            {
                if (state->closed)
                {
                    if (environment != nullptr)
                        environment->Release();
                    return;
                }
                on_environment_ready(status, environment);
            });
        m_in_start = false;

        if (!m_sync_start_status.ok())
            return m_sync_start_status;
        return Status::make(Errc::Ok);
    }

    Status navigate(const std::string& url) override
    {
        if (m_core == nullptr)
            return Status::make(Errc::InvalidState, "webview core is not ready");
        HRESULT hr = m_core->Navigate(utf8_to_wide(url).c_str());
        return SUCCEEDED(hr) ? Status::make(Errc::Ok) : Status::make(Errc::PlatformFailure, "Navigate failed", hr);
    }

    Status load_html(const std::string& html, const std::string& /*base_url*/) override
    {
        // WebView2 has no base url for NavigateToString; the document lives on about:blank.
        if (m_core == nullptr)
            return Status::make(Errc::InvalidState, "webview core is not ready");
        HRESULT hr = m_core->NavigateToString(utf8_to_wide(html).c_str());
        return SUCCEEDED(hr) ? Status::make(Errc::Ok) : Status::make(Errc::PlatformFailure, "NavigateToString failed", hr);
    }

    Status reload() override
    {
        if (m_core == nullptr)
            return Status::make(Errc::InvalidState, "webview core is not ready");
        HRESULT hr = m_core->Reload();
        return SUCCEEDED(hr) ? Status::make(Errc::Ok) : Status::make(Errc::PlatformFailure, "Reload failed", hr);
    }

    Status go_back() override
    {
        if (m_core == nullptr)
            return Status::make(Errc::InvalidState, "webview core is not ready");
        HRESULT hr = m_core->GoBack();
        return SUCCEEDED(hr) ? Status::make(Errc::Ok) : Status::make(Errc::PlatformFailure, "GoBack failed", hr);
    }

    Status go_forward() override
    {
        if (m_core == nullptr)
            return Status::make(Errc::InvalidState, "webview core is not ready");
        HRESULT hr = m_core->GoForward();
        return SUCCEEDED(hr) ? Status::make(Errc::Ok) : Status::make(Errc::PlatformFailure, "GoForward failed", hr);
    }

    std::string url() const override
    {
        if (m_core == nullptr)
            return std::string();
        LPWSTR uri = nullptr;
        if (FAILED(m_core->get_Source(&uri)))
            return std::string();
        return take_task_string(uri);
    }

    Status set_bounds(int x, int y, int width, int height) override
    {
        if (m_controller == nullptr)
            return Status::make(Errc::InvalidState, "webview controller is not ready");
        RECT bounds{x, y, x + width, y + height}; // physical pixels per contract
        HRESULT hr = m_controller->put_Bounds(bounds);
        return SUCCEEDED(hr) ? Status::make(Errc::Ok) : Status::make(Errc::PlatformFailure, "put_Bounds failed", hr);
    }

    Status set_visible(bool visible) override
    {
        if (m_controller == nullptr)
            return Status::make(Errc::InvalidState, "webview controller is not ready");
        HRESULT hr = m_controller->put_IsVisible(visible ? TRUE : FALSE);
        return SUCCEEDED(hr) ? Status::make(Errc::Ok) : Status::make(Errc::PlatformFailure, "put_IsVisible failed", hr);
    }

    Status add_init_script(const std::string& js) override
    {
        if (m_core == nullptr)
            return Status::make(Errc::InvalidState, "webview core is not ready");
        // Execution order matches call order; the script id is not needed.
        HRESULT hr = m_core->AddScriptToExecuteOnDocumentCreated(utf8_to_wide(js).c_str(), nullptr);
        return SUCCEEDED(hr) ? Status::make(Errc::Ok) : Status::make(Errc::PlatformFailure, "AddScriptToExecuteOnDocumentCreated failed", hr);
    }

    Status run_script(const std::string& js, ScriptDoneFn cb) override
    {
        if (m_core == nullptr)
            return Status::make(Errc::InvalidState, "webview core is not ready");

        ScriptCompletedHandler* handler = new ScriptCompletedHandler(
            [state = m_state, cb](HRESULT errorCode, LPCWSTR result)
            {
                if (!cb)
                    return;
                if (state->closed)
                {
                    cb(false, "");
                    return;
                }
                if (SUCCEEDED(errorCode))
                    cb(true, result != nullptr ? wide_to_utf8(result) : std::string());
                else
                    cb(false, "");
            });

        HRESULT hr = m_core->ExecuteScript(utf8_to_wide(js).c_str(), handler);
        handler->Release();
        return SUCCEEDED(hr) ? Status::make(Errc::Ok) : Status::make(Errc::PlatformFailure, "ExecuteScript failed", hr);
    }

    Status post_message_json(const std::string& json_text) override
    {
        if (m_core == nullptr)
            return Status::make(Errc::InvalidState, "webview core is not ready");
        // The page SDK receives this as a 'message' event on chrome.webview.
        HRESULT hr = m_core->PostWebMessageAsJson(utf8_to_wide(json_text).c_str());
        return SUCCEEDED(hr) ? Status::make(Errc::Ok) : Status::make(Errc::PlatformFailure, "PostWebMessageAsJson failed", hr);
    }

    Status close() override
    {
        if (m_closed)
            return Status::make(Errc::Ok);

        m_closed = true;
        m_state->closed = true;

        // Drop the core reference first: no event callback can reach us after
        // this, even without removing every event token.
        ICoreWebView2Environment* environment = m_environment;
        ICoreWebView2Controller*  controller  = m_controller;
        ICoreWebView2*            core        = m_core;
        m_environment = nullptr;
        m_controller  = nullptr;
        m_core        = nullptr;

        if (controller != nullptr)
        {
            controller->Close();
            controller->Release();
        }
        if (core != nullptr)
            core->Release();
        if (environment != nullptr)
            environment->Release();
        return Status::make(Errc::Ok);
    }

    void* native_handle() const override
    {
        return m_core;
    }

private:
    void on_environment_ready(Status status, ICoreWebView2Environment* environment)
    {
        if (!status.ok())
        {
            if (environment != nullptr)
                environment->Release();
            fail(status);
            return;
        }

        m_environment = environment; // owns the AddRef'd reference

        HRESULT hr = E_FAIL;
        ICoreWebView2Environment10* environment10 = nullptr;
        if (SUCCEEDED(m_environment->QueryInterface(IID_PPV_ARGS(&environment10))) && environment10 != nullptr)
        {
            ICoreWebView2ControllerOptions* options = nullptr;
            hr = environment10->CreateCoreWebView2ControllerOptions(&options);
            if (SUCCEEDED(hr) && options != nullptr)
            {
                if (!m_config.profile_name.empty())
                    options->put_ProfileName(utf8_to_wide(m_config.profile_name).c_str());
                options->put_IsInPrivateModeEnabled(m_config.incognito ? TRUE : FALSE);

                ControllerCompletedHandler* handler = make_controller_handler();
                hr = environment10->CreateCoreWebView2ControllerWithOptions(m_parent_hwnd, options, handler);
                handler->Release();
                options->Release();
            }
            environment10->Release();
        }
        else
        {
            // Old runtime without controller options: only the default profile works.
            if (!m_config.profile_name.empty() || m_config.incognito)
            {
                fail(Status::make(Errc::NotSupported, "profile_name/incognito need a newer WebView2 runtime"));
                return;
            }
            ControllerCompletedHandler* handler = make_controller_handler();
            hr = m_environment->CreateCoreWebView2Controller(m_parent_hwnd, handler);
            handler->Release();
        }

        if (FAILED(hr))
            fail(Status::make(Errc::PlatformFailure, "CreateCoreWebView2Controller failed", hr));
    }

    ControllerCompletedHandler* make_controller_handler()
    {
        return new ControllerCompletedHandler(
            [this, state = m_state](HRESULT errorCode, ICoreWebView2Controller* controller)
            {
                if (state->closed)
                {
                    if (controller != nullptr)
                        controller->Release();
                    return;
                }
                on_controller_created(errorCode, controller);
            });
    }

    void on_controller_created(HRESULT errorCode, ICoreWebView2Controller* controller)
    {
        if (FAILED(errorCode) || controller == nullptr)
        {
            fail(Status::make(Errc::PlatformFailure, "webview controller creation failed", errorCode));
            return;
        }

        m_controller = controller; // takes over the handler's reference

        ICoreWebView2* core = nullptr;
        if (FAILED(m_controller->get_CoreWebView2(&core)) || core == nullptr)
        {
            fail(Status::make(Errc::PlatformFailure, "get_CoreWebView2 failed"));
            return;
        }
        m_core = core;

        Status status = configure_core();
        if (!status.ok())
        {
            fail(status);
            return;
        }

        m_controller->put_Bounds(RECT{0, 0, 0, 0});
        m_proxy.report_created();
    }

    Status configure_core()
    {
        std::string failures;

        apply_settings(failures);
        register_events(failures);
        apply_custom_schemes(failures);

        if (!failures.empty())
            return Status::make(Errc::PlatformFailure, "webview configuration failed: " + failures);
        return Status::make(Errc::Ok);
    }

    void apply_settings(std::string& failures)
    {
        ICoreWebView2Settings* settings = nullptr;
        HRESULT hr = m_core->get_Settings(&settings);
        if (FAILED(hr) || settings == nullptr)
        {
            failures += "get_Settings ";
            return;
        }

        if (FAILED(settings->put_IsWebMessageEnabled(TRUE)))
            failures += "IsWebMessageEnabled ";
        if (FAILED(settings->put_AreDevToolsEnabled(m_config.devtools ? TRUE : FALSE)))
            failures += "AreDevToolsEnabled ";
        if (FAILED(settings->put_AreDefaultContextMenusEnabled(m_config.context_menu ? TRUE : FALSE)))
            failures += "AreDefaultContextMenusEnabled ";

        if (!m_config.user_agent.empty())
        {
            ICoreWebView2Settings2* settings2 = nullptr;
            if (SUCCEEDED(settings->QueryInterface(IID_PPV_ARGS(&settings2))) && settings2 != nullptr)
            {
                if (FAILED(settings2->put_UserAgent(utf8_to_wide(m_config.user_agent).c_str())))
                    failures += "UserAgent ";
                settings2->Release();
            }
        }
        settings->Release();

        apply_color_scheme(failures);
    }

    // PreferredColorScheme lives on the profile (ICoreWebView2_13::get_Profile),
    // not on Settings in this SDK; a missing profile on old runtimes is skipped.
    void apply_color_scheme(std::string& failures)
    {
        if (m_config.color_scheme == ColorScheme::Auto)
            return;

        ICoreWebView2_13* core13 = nullptr;
        if (FAILED(m_core->QueryInterface(IID_PPV_ARGS(&core13))) || core13 == nullptr)
            return;

        ICoreWebView2Profile* profile = nullptr;
        if (SUCCEEDED(core13->get_Profile(&profile)) && profile != nullptr)
        {
            COREWEBVIEW2_PREFERRED_COLOR_SCHEME scheme = (m_config.color_scheme == ColorScheme::Dark)
                ? COREWEBVIEW2_PREFERRED_COLOR_SCHEME_DARK
                : COREWEBVIEW2_PREFERRED_COLOR_SCHEME_LIGHT;
            if (FAILED(profile->put_PreferredColorScheme(scheme)))
                failures += "PreferredColorScheme ";
            profile->Release();
        }
        core13->Release();
    }

    void register_events(std::string& failures)
    {
        EventRegistrationToken token{};

        WebMessageReceivedHandler* message_handler = new WebMessageReceivedHandler(
            [state = m_state, proxy = &m_proxy](ICoreWebView2* /*sender*/, ICoreWebView2WebMessageReceivedEventArgs* args)
            {
                if (state->closed)
                    return;
                LPWSTR json = nullptr;
                std::string json_text;
                if (SUCCEEDED(args->get_WebMessageAsJson(&json)))
                    json_text = take_task_string(json);
                if (!json_text.empty())
                {
                    proxy->report_message(std::move(json_text));
                    return;
                }
                LPWSTR text = nullptr;
                if (SUCCEEDED(args->TryGetWebMessageAsString(&text)) && text != nullptr)
                    proxy->report_message(take_task_string(text));
            });
        if (FAILED(m_core->add_WebMessageReceived(message_handler, &token)))
            failures += "add_WebMessageReceived ";
        message_handler->Release();

        NavigationStartingHandler* navigation_handler = new NavigationStartingHandler(
            [state = m_state, proxy = &m_proxy](ICoreWebView2* /*sender*/, ICoreWebView2NavigationStartingEventArgs* args)
            {
                if (state->closed)
                    return;
                LPWSTR uri = nullptr;
                if (FAILED(args->get_Uri(&uri)))
                    return;
                NavigationState navigation;
                navigation.url = take_task_string(uri);
                navigation.is_main_frame = true; // NavigationStarting fires for the main frame only
                if (!proxy->query_navigation(navigation))
                    args->put_Cancel(TRUE);
            });
        if (FAILED(m_core->add_NavigationStarting(navigation_handler, &token)))
            failures += "add_NavigationStarting ";
        navigation_handler->Release();

        NavigationCompletedHandler* completed_handler = new NavigationCompletedHandler(
            [state = m_state, proxy = &m_proxy](ICoreWebView2* sender, ICoreWebView2NavigationCompletedEventArgs* args)
            {
                if (state->closed)
                    return;
                BOOL success = FALSE;
                if (FAILED(args->get_IsSuccess(&success)) || !success)
                    return; // page errors are not fatal and are not reported
                LPWSTR uri = nullptr;
                if (FAILED(sender->get_Source(&uri)))
                    return;
                NavigationState navigation;
                navigation.url = take_task_string(uri);
                navigation.is_main_frame = true;
                proxy->report_document_loaded(navigation);
            });
        if (FAILED(m_core->add_NavigationCompleted(completed_handler, &token)))
            failures += "add_NavigationCompleted ";
        completed_handler->Release();

        SourceChangedHandler* source_handler = new SourceChangedHandler(
            [state = m_state, proxy = &m_proxy](ICoreWebView2* sender, ICoreWebView2SourceChangedEventArgs* /*args*/)
            {
                if (state->closed)
                    return;
                LPWSTR uri = nullptr;
                if (SUCCEEDED(sender->get_Source(&uri)))
                    proxy->report_url_changed(take_task_string(uri));
            });
        if (FAILED(m_core->add_SourceChanged(source_handler, &token)))
            failures += "add_SourceChanged ";
        source_handler->Release();

        DocumentTitleChangedHandler* title_handler = new DocumentTitleChangedHandler(
            [state = m_state, proxy = &m_proxy](ICoreWebView2* sender, IUnknown* /*args*/)
            {
                if (state->closed)
                    return;
                LPWSTR title = nullptr;
                if (SUCCEEDED(sender->get_DocumentTitle(&title)))
                    proxy->report_title(take_task_string(title));
            });
        if (FAILED(m_core->add_DocumentTitleChanged(title_handler, &token)))
            failures += "add_DocumentTitleChanged ";
        title_handler->Release();

        NewWindowRequestedHandler* window_handler = new NewWindowRequestedHandler(
            [this, state = m_state](ICoreWebView2* /*sender*/, ICoreWebView2NewWindowRequestedEventArgs* args)
            {
                if (state->closed)
                    return;
                LPWSTR uri = nullptr;
                if (FAILED(args->get_Uri(&uri)))
                    return;
                std::string url = take_task_string(uri);

                NewWindowRequest request;
                request.url = url;
                OpenDecision decision = m_proxy.query_new_window(request);

                // Always mark handled: WebView2 must never spawn its own popup window.
                args->put_Handled(TRUE);
                if (decision == OpenDecision::OpenInWebView && !url.empty() && m_core != nullptr)
                    m_core->Navigate(utf8_to_wide(url).c_str());
            });
        if (FAILED(m_core->add_NewWindowRequested(window_handler, &token)))
            failures += "add_NewWindowRequested ";
        window_handler->Release();
    }

    // Virtual host mapping: <scheme>.orca serves root_folder over a real
    // https origin (https://<scheme>.orca/<entry>). Handler-based schemes are
    // not served in this phase; only folders are mapped.
    void apply_custom_schemes(std::string& failures)
    {
        if (m_config.custom_schemes.empty())
            return;

        ICoreWebView2_3* core3 = nullptr;
        if (FAILED(m_core->QueryInterface(IID_PPV_ARGS(&core3))) || core3 == nullptr)
        {
            failures += "virtual-host-mapping(unsupported-runtime) ";
            return;
        }

        for (const CustomScheme& scheme : m_config.custom_schemes)
        {
            std::wstring host   = utf8_to_wide(scheme.scheme + ".orca");
            std::wstring folder = utf8_to_wide(scheme.root_folder);
            if (FAILED(core3->SetVirtualHostNameToFolderMapping(host.c_str(), folder.c_str(),
                COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW)))
            {
                failures += scheme.scheme + " ";
            }
        }
        core3->Release();
    }

    // Async failures go through report_error; synchronous ones (inside start)
    // are returned by start() so WebView::create can fail the factory call.
    void fail(Status status)
    {
        m_sync_start_status = status;
        if (!m_in_start)
            m_proxy.report_error(status);
    }

    HWND                            m_parent_hwnd = nullptr;
    WebView::Config                 m_config;
    EventProxy&                     m_proxy;
    std::shared_ptr<SharedState>    m_state;

    ICoreWebView2Environment* m_environment = nullptr;
    ICoreWebView2Controller*  m_controller  = nullptr;
    ICoreWebView2*            m_core        = nullptr;

    bool   m_closed = false;
    bool   m_in_start = false;
    Status m_sync_start_status;
};

} // namespace

Backend* create_backend_win(void* native_parent, const WebView::Config& config, EventProxy& proxy)
{
    return new BackendWin(native_parent, config, proxy);
}

std::string platform_runtime_version()
{
    return webview2_runtime_version();
}

} // namespace orca::wv::detail
