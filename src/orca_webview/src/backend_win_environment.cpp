#include "backend_win_environment.h"
#include "string_util.h"

#include <atomic>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <wrl/client.h>
#include <WebView2EnvironmentOptions.h>

#include <tlhelp32.h>

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
// One sweep-retry per creation campaign: a failed create sweeps orphan
// browsers once and relaunches once; the flag breaks the create-fail-sweep
// recursion before it can loop.
bool                      s_environment_retried = false;
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

// --- Orphan browser sweep -------------------------------------------------
//
// When the app is force-killed, the msedgewebview2.exe trees it spawned can
// outlive it and keep the shared user data folder locked; every later
// CreateCoreWebView2Environment call then dies until those processes are gone
// (review 12: root cause of the plugin center white screen). This sweep only
// ever terminates msedgewebview2.exe processes whose command line carries
// this build's user data folder AND whose parent chain is dead. A tree whose
// root's parent is alive belongs to a running host (another Orca instance or
// a third-party msedgewebview2 embedder such as widgets/VS/CBS) and is never
// touched. Synchronous, UI thread failure path only; one snapshot plus
// command-line reads stay well under a hundred milliseconds.

constexpr ULONG kProcessCommandLineInformationClass = 61;
constexpr LONG  kStatusInfoLengthMismatch            = static_cast<LONG>(0xC0000004);

#ifndef ERROR_BAD_LENGTHS
#define ERROR_BAD_LENGTHS 24
#endif

// The SDK header flavor used here does not expose UNICODE_STRING; mirror its
// fixed ABI layout (USHORT, USHORT, padding, pointer) instead.
struct NtUnicodeString
{
    unsigned short Length;
    unsigned short MaximumLength;
    wchar_t*       Buffer;
};

using NtQueryInformationProcessFn = LONG (WINAPI *)(HANDLE process, ULONG info_class,
    void* info, ULONG info_length, ULONG* return_length);

// Command line of a process handle via ntdll (info class 61, two-phase size
// probe). Returns false on any failure; the caller must then leave the
// process alone rather than guess.
bool read_process_command_line(HANDLE process, std::wstring& command_line)
{
    static NtQueryInformationProcessFn nt_query_information_process = [] () -> NtQueryInformationProcessFn
    {
        HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        if (ntdll == nullptr)
            return nullptr;
        return reinterpret_cast<NtQueryInformationProcessFn>(GetProcAddress(ntdll, "NtQueryInformationProcess"));
    }();
    if (nt_query_information_process == nullptr)
        return false;

    ULONG needed = 0;
    LONG  status = nt_query_information_process(process, kProcessCommandLineInformationClass, nullptr, 0, &needed);
    if (status != kStatusInfoLengthMismatch || needed == 0)
        return false;

    std::vector<unsigned char> info(needed);
    status = nt_query_information_process(process, kProcessCommandLineInformationClass, info.data(), static_cast<ULONG>(info.size()), &needed);
    if (status < 0)
        return false;

    const NtUnicodeString* command = reinterpret_cast<const NtUnicodeString*>(info.data());
    const unsigned char*  first   = info.data();
    const unsigned char*  last    = first + info.size();
    if (command->Buffer == nullptr || command->Length == 0 ||
        reinterpret_cast<const unsigned char*>(command->Buffer) < first ||
        reinterpret_cast<const unsigned char*>(command->Buffer) + command->Length > last)
        return false;
    command_line.assign(command->Buffer, command->Length / sizeof(wchar_t));
    return true;
}

// Lowercases and drops double quotes so a quoted '--user-data-dir="C:\a b"'
// still contains the bare folder as a substring. ASCII-only lowering: the
// flag is ASCII and the folder text is compared against itself on both
// sides, so nothing outside ASCII ever needs case folding.
std::wstring normalize_for_udf_match(const std::wstring& text)
{
    std::wstring normalized;
    normalized.reserve(text.size());
    for (wchar_t character : text)
    {
        if (character == L'"')
            continue;
        if (character >= L'A' && character <= L'Z')
            character = static_cast<wchar_t>(character - L'A' + L'a');
        normalized.push_back(character);
    }
    return normalized;
}

// Finds and terminates the orphan msedgewebview2.exe trees holding the given
// user data folder; returns how many processes were terminated. Never
// terminates a process whose command line does not reference the folder or
// whose parent chain is still alive.
unsigned terminate_orphan_browsers(const std::string& user_data_folder, const char* reason)
{
    if (user_data_folder.empty())
        return 0; // nothing to match against: sweep disabled

    const std::wstring needle = normalize_for_udf_match(L"--user-data-dir=" + utf8_to_wide(user_data_folder));

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE && GetLastError() == ERROR_BAD_LENGTHS)
        snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return 0;

    std::set<DWORD>        live_pids;     // every process in the snapshot
    std::set<DWORD>        webview_pids;  // msedgewebview2.exe processes
    std::map<DWORD, DWORD> webview_parent;
    PROCESSENTRY32W        entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry))
    {
        do
        {
            live_pids.insert(entry.th32ProcessID);
            if (lstrcmpiW(entry.szExeFile, L"msedgewebview2.exe") == 0)
            {
                webview_pids.insert(entry.th32ProcessID);
                webview_parent[entry.th32ProcessID] = entry.th32ParentProcessID;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);

    // UDF ownership needs a readable command line; processes that deny access
    // are skipped and can never become orphan roots.
    std::map<DWORD, bool> holds_udf;
    for (DWORD pid : webview_pids)
    {
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pid);
        if (process == nullptr)
            continue;
        std::wstring command_line;
        const bool   readable = read_process_command_line(process, command_line);
        CloseHandle(process);
        if (readable)
            holds_udf[pid] = normalize_for_udf_match(command_line).find(needle) != std::wstring::npos;
    }

    // Orphan roots: UDF holders whose parent is no longer alive.
    std::set<DWORD> victims;
    for (const auto& pid_match : holds_udf)
    {
        if (!pid_match.second)
            continue;
        if (live_pids.find(webview_parent[pid_match.first]) == live_pids.end())
            victims.insert(pid_match.first);
    }
    if (victims.empty())
        return 0;

    // Grow to every msedgewebview2 descendant of the roots. A child whose
    // command line could not be read still belongs to the tree: its parent
    // chain reaches a root. Nothing outside the webview set and nothing with
    // a live parent chain is ever added.
    for (bool grew = true; grew; )
    {
        grew = false;
        for (DWORD pid : webview_pids)
        {
            if (victims.find(pid) == victims.end() &&
                victims.find(webview_parent[pid]) != victims.end())
            {
                victims.insert(pid);
                grew = true;
            }
        }
    }

    unsigned terminated = 0;
    for (DWORD pid : victims)
    {
        HANDLE process = OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (process == nullptr)
            continue;
        const BOOL killed = TerminateProcess(process, 1);
        CloseHandle(process);
        if (!killed)
            continue;
        ++terminated;
        char detail[96];
        snprintf(detail, sizeof(detail), "pid=%lu reason=%s", static_cast<unsigned long>(pid), reason);
        trace_environment("orphan terminated", detail);
    }
    return terminated;
}

void launch_create(const WebView2Loader* loader);

// Shared failure path of both create failures (async completion and sync
// return): sweep the orphan browser trees still holding the user data folder
// and, when the sweep cleared at least one, relaunch the create once with
// the same pending requests. Everything else fails the pending requests as
// before. s_environment_creating stays true across the retry.
void handle_create_failure(HRESULT hr, const char* stage)
{
    trace_environment(stage, "folder='" + s_pending_folder + "' args='" + s_pending_args + "'", hr);
    if (!s_environment_retried)
    {
        s_environment_retried = true;
        trace_environment("create failed, checking orphan browsers", "folder='" + s_pending_folder + "'");
        const unsigned        cleared = terminate_orphan_browsers(s_pending_folder, "environment-create-failed");
        const WebView2Loader* loader  = cleared > 0 ? resolve_loader() : nullptr;
        if (loader != nullptr)
        {
            trace_environment("create retry", "folder='" + s_pending_folder + "' orphans=" + std::to_string(cleared));
            launch_create(loader);
            return;
        }
    }
    s_environment_creating = false;
    fail_pending(environment_failure_status(hr));
}

// Launches one CreateCoreWebView2EnvironmentWithOptions attempt for the
// pending folder/args. The caller owns s_environment_creating and the pending
// request list; the completion handler and the sync-failure path settle both.
void launch_create(const WebView2Loader* loader)
{
    std::wstring folder_wide = utf8_to_wide(s_pending_folder);
    PCWSTR folder_argument = folder_wide.empty() ? nullptr : folder_wide.c_str();
    trace_environment("create requested", "folder='" + s_pending_folder + "' args='" + s_pending_args + "'");

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
            if (FAILED(errorCode) || environment == nullptr)
            {
                handle_create_failure(errorCode, "create failed (async)");
                return;
            }
            s_environment_creating = false;

            trace_environment("create completed", "folder='" + s_pending_folder + "'");
            // The callback parameter is a borrowed reference; the cache must own
            // its count for the process lifetime so the runtime cannot free the
            // environment under every consumer of the cache.
            environment->AddRef();
            s_environment = environment;
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
        handle_create_failure(hr, "create failed (sync)");
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
    // Each creation campaign gets one sweep-retry chance; see
    // handle_create_failure.
    s_environment_retried = false;

    launch_create(loader);
}

} // namespace orca::wv::detail
