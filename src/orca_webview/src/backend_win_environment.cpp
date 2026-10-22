#include "backend_win_environment.h"
#include "string_util.h"

#include <atomic>
#include <utility>
#include <vector>

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
bool                      s_environment_creating = false;
std::string               s_pending_folder;
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

Status folder_conflict_status(const std::string& requested, const std::string& in_use)
{
    return Status::make(Errc::InvalidState,
        "WebView2 allows only one user data folder per process: requested '" + requested + "' but '" + in_use + "' is already in use");
}

Status environment_failure_status(HRESULT hr)
{
    Errc code = (hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)) ? Errc::RuntimeMissing : Errc::EnvironmentRejected;
    return Status::make(code, "CreateCoreWebView2EnvironmentWithOptions failed", hr);
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

void acquire_shared_environment(const std::string& browser_data_dir, EnvironmentReadyFn on_ready)
{
    if (s_environment != nullptr)
    {
        if (browser_data_dir != s_environment_folder)
        {
            on_ready(folder_conflict_status(browser_data_dir, s_environment_folder), nullptr);
            return;
        }
        s_environment->AddRef();
        on_ready(Status::make(Errc::Ok), s_environment);
        return;
    }

    if (s_environment_creating)
    {
        if (browser_data_dir != s_pending_folder)
        {
            on_ready(folder_conflict_status(browser_data_dir, s_pending_folder), nullptr);
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
    s_pending_requests.push_back(std::move(on_ready));

    std::wstring folder_wide = utf8_to_wide(browser_data_dir);
    PCWSTR folder_argument = folder_wide.empty() ? nullptr : folder_wide.c_str();

    // Options stay null: AdditionalBrowserArguments must never be set here or
    // the environment would conflict with one wx already created in-process;
    // user agent and color scheme are applied per-view through Settings/Profile.
    EnvironmentCreatedHandler* handler = new EnvironmentCreatedHandler(
        [](HRESULT errorCode, ICoreWebView2Environment* environment)
        {
            s_environment_creating = false;
            if (FAILED(errorCode) || environment == nullptr)
            {
                fail_pending(environment_failure_status(errorCode));
                return;
            }

            s_environment = environment; // keep the callback's reference as the process cache
            s_environment_folder = s_pending_folder;
            std::vector<EnvironmentReadyFn> requests = std::move(s_pending_requests);
            s_pending_requests.clear();
            for (EnvironmentReadyFn& request : requests)
            {
                s_environment->AddRef();
                request(Status::make(Errc::Ok), s_environment);
            }
        });

    HRESULT hr = loader->create_environment(nullptr, folder_argument, nullptr, handler);
    handler->Release();
    if (FAILED(hr))
    {
        s_environment_creating = false;
        fail_pending(environment_failure_status(hr));
    }
}

} // namespace orca::wv::detail
