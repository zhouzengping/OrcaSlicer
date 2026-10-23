#include "backend_win_environment.h"
#include "string_util.h"

#include <atomic>
#include <cstdio>
#include <utility>
#include <vector>

#include <wrl/client.h>
#include <WebView2EnvironmentOptions.h>

namespace orca::wv::detail {
namespace {

// One-shot COM handler for environment creation; owns no view state, so it
// can safely complete the process cache even if the requesting view is gone.
class EnvironmentCreatedHandler final : public ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler
{
public:
    using Fn = std::function<void(HRESULT errorCode, ICoreWebView2Environment* environment)>;

    explicit EnvironmentCreatedHandler(Fn fn)
        : m_fn(std::move(fn))
    {
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
    {
        if (ppv == nullptr)
            return E_POINTER;
        if (riid == IID_IUnknown || riid == __uuidof(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler))
        {
            *ppv = static_cast<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*>(this);
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

    HRESULT STDMETHODCALLTYPE Invoke(HRESULT errorCode, ICoreWebView2Environment* environment) override
    {
        Fn fn = std::move(m_fn);
        m_fn = nullptr;
        if (fn)
            fn(errorCode, environment);
        return S_OK;
    }

private:
    std::atomic<ULONG> m_refs = 1;
    Fn                 m_fn;
};

HMODULE       s_loader_module = nullptr;
WebView2Loader s_loader;
bool          s_loader_resolved = false;

// Process-wide cached environment; this reference is never released.
ICoreWebView2Environment* s_environment = nullptr;
std::string               s_environment_folder;
std::string               s_environment_args;
bool                      s_environment_creating = false;
std::string               s_pending_folder;
std::string               s_pending_args;
std::vector<EnvironmentReadyFn> s_pending_requests;

const WebView2Loader* resolve_loader()
{
    if (s_loader_resolved)
        return (s_loader.create_environment != nullptr && s_loader.get_browser_version != nullptr) ? &s_loader : nullptr;

    s_loader_resolved = true;
    s_loader_module = LoadLibraryW(L"WebView2Loader.dll");
    if (s_loader_module == nullptr)
        return nullptr;

    s_loader.create_environment = reinterpret_cast<CreateCoreWebView2EnvironmentWithOptionsFn>(
        GetProcAddress(s_loader_module, "CreateCoreWebView2EnvironmentWithOptions"));
    s_loader.get_browser_version = reinterpret_cast<GetAvailableCoreWebView2BrowserVersionStringFn>(
        GetProcAddress(s_loader_module, "GetAvailableCoreWebView2BrowserVersionString"));
    if (s_loader.create_environment == nullptr || s_loader.get_browser_version == nullptr)
        return nullptr;
    return &s_loader;
}

Status environment_conflict_status(const std::string& requested_folder, const std::string& requested_args,
    const std::string& in_use_folder, const std::string& in_use_args)
{
    return Status::make(Errc::InvalidState,
        "WebView2 allows only one environment per user data folder per process: requested folder '" + requested_folder +
        "' with browser arguments '" + requested_args + "' but '" + in_use_folder + "' with '" + in_use_args + "' is already in use");
}

Status environment_failure_status(HRESULT hr)
{
    Errc code = (hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)) ? Errc::RuntimeMissing : Errc::EnvironmentRejected;
    return Status::make(code, "CreateCoreWebView2EnvironmentWithOptions failed", hr);
}

// Startup-race probe (review-11): environment creation is asynchronous and
// overlaps the wx factory's own environment creations over the same user
// data folder during MainFrame init. These traces make the interleaving and
// any failure HRESULT visible in a debugger / DebugView without adding a
// logging dependency to this wx-free library.
void trace_environment(const char* stage, const std::string& detail, HRESULT hr = S_OK)
{
    char line[320];
    snprintf(line, sizeof(line), "[orca_wv] environment %s hr=0x%08lX %s\n",
        stage, static_cast<unsigned long>(hr), detail.c_str());
    OutputDebugStringA(line);
}

void fail_pending(Status status)
{
    std::vector<EnvironmentReadyFn> requests = std::move(s_pending_requests);
    s_pending_requests.clear();
    for (EnvironmentReadyFn& request : requests)
        request(status, nullptr);
}

} // namespace

const WebView2Loader* webview2_loader()
{
    return resolve_loader();
}

std::string webview2_runtime_version()
{
    const WebView2Loader* loader = resolve_loader();
    if (loader == nullptr)
        return std::string();

    LPWSTR version_wide = nullptr;
    HRESULT hr = loader->get_browser_version(nullptr, &version_wide);
    if (FAILED(hr) || version_wide == nullptr)
        return std::string();

    std::string version = wide_to_utf8(version_wide);
    CoTaskMemFree(version_wide);
    return version;
}

void acquire_shared_environment(const std::string& browser_data_dir, const std::string& additional_browser_arguments, EnvironmentReadyFn on_ready)
{
    if (s_environment != nullptr)
    {
        if (browser_data_dir != s_environment_folder || additional_browser_arguments != s_environment_args)
        {
            on_ready(environment_conflict_status(browser_data_dir, additional_browser_arguments, s_environment_folder, s_environment_args), nullptr);
            return;
        }
        s_environment->AddRef();
        on_ready(Status::make(Errc::Ok), s_environment);
        return;
    }

    if (s_environment_creating)
    {
        if (browser_data_dir != s_pending_folder || additional_browser_arguments != s_pending_args)
        {
            on_ready(environment_conflict_status(browser_data_dir, additional_browser_arguments, s_pending_folder, s_pending_args), nullptr);
            return;
        }
        s_pending_requests.push_back(std::move(on_ready));
        return;
    }

    const WebView2Loader* loader = resolve_loader();
    if (loader == nullptr)
    {
        on_ready(Status::make(Errc::RuntimeMissing, "WebView2Loader.dll not found or exports missing"), nullptr);
        return;
    }

    s_environment_creating = true;
    s_pending_folder = browser_data_dir;
    s_pending_args = additional_browser_arguments;
    s_pending_requests.push_back(std::move(on_ready));

    std::wstring folder_wide = utf8_to_wide(browser_data_dir);
    PCWSTR folder_argument = folder_wide.empty() ? nullptr : folder_wide.c_str();
    trace_environment("create requested", "folder='" + browser_data_dir + "' args='" + additional_browser_arguments + "'");

    // AdditionalBrowserArguments must match the environments wx created in
    // this process. In this tree wx's value is EMPTY: the app's WebViewEdge
    // subclass applies the user agent per view and never reaches
    // wxWebViewEdge's environment-options path (see
    // WebViewPageBase::webview_browser_arguments). Empty args keep nullptr:
    // byte-identical to what orca passed before the plugin stack existed,
    // and free of the TargetCompatibleBrowserVersion floor this SDK header
    // would otherwise bake in (our header says 138.x while wx's bundled SDK
    // bakes 107.x - always constructing the object would trade one mismatch
    // for another and demand a newer runtime than wx does).
    ICoreWebView2EnvironmentOptions* options = nullptr;
    if (!s_pending_args.empty())
    {
        CoreWebView2EnvironmentOptions* made = Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>().Detach();
        made->put_AdditionalBrowserArguments(utf8_to_wide(s_pending_args).c_str());
        options = made;
    }

    EnvironmentCreatedHandler* handler = new EnvironmentCreatedHandler(
        [](HRESULT errorCode, ICoreWebView2Environment* environment)
        {
            s_environment_creating = false;
            if (FAILED(errorCode) || environment == nullptr)
            {
                trace_environment("create failed (async)", "folder='" + s_pending_folder + "' args='" + s_pending_args + "'", errorCode);
                fail_pending(environment_failure_status(errorCode));
                return;
            }

            trace_environment("create completed", "folder='" + s_pending_folder + "'");
            s_environment = environment; // keep the callback's reference as the process cache
            s_environment_folder = s_pending_folder;
            s_environment_args = s_pending_args;
            std::vector<EnvironmentReadyFn> requests = std::move(s_pending_requests);
            s_pending_requests.clear();
            for (EnvironmentReadyFn& request : requests)
            {
                s_environment->AddRef();
                request(Status::make(Errc::Ok), s_environment);
            }
        });

    HRESULT hr = loader->create_environment(nullptr, folder_argument, options, handler);
    if (options != nullptr)
        options->Release();
    handler->Release();
    if (FAILED(hr))
    {
        trace_environment("create failed (sync)", "folder='" + browser_data_dir + "' args='" + additional_browser_arguments + "'", hr);
        s_environment_creating = false;
        fail_pending(environment_failure_status(hr));
    }
}

} // namespace orca::wv::detail
