#pragma once

// Windows-only: WebView2Loader.dll bootstrap and the process-wide
// ICoreWebView2Environment. WebView2 allows a single environment per user
// data folder per process, so the first successfully created environment is
// cached and every later request with a different folder or different
// browser arguments fails with InvalidState.

#include <functional>
#include <string>

#include <windows.h>
#include <WebView2.h>

#include "orca/webview/types.hpp"

namespace orca::wv::detail {

typedef HRESULT (STDMETHODCALLTYPE *CreateCoreWebView2EnvironmentWithOptionsFn)(PCWSTR browserExecutableFolder,
    PCWSTR userDataFolder, ICoreWebView2EnvironmentOptions* environmentOptions,
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* environmentCreatedHandler);

typedef HRESULT (STDMETHODCALLTYPE *GetAvailableCoreWebView2BrowserVersionStringFn)(PCWSTR browserExecutableFolder,
    LPWSTR* versionInfo);

struct WebView2Loader
{
    CreateCoreWebView2EnvironmentWithOptionsFn     create_environment = nullptr;
    GetAvailableCoreWebView2BrowserVersionStringFn get_browser_version = nullptr;
};

/*
 * Loads WebView2Loader.dll once per process (never unloaded) and resolves the
 * two entry points. Returns nullptr when the dll or its exports are missing.
 */
const WebView2Loader* webview2_loader();

// Version string of the installed WebView2 runtime; "" when not installed.
std::string webview2_runtime_version();

using EnvironmentReadyFn = std::function<void(Status status, ICoreWebView2Environment* environment)>;

/*
 * Hands out the process-wide environment for the given user data folder.
 * additional_browser_arguments must be byte-identical across every request in
 * the process (folder or arguments mismatch is undefined behavior per
 * WebView2). on_ready fires on the UI thread, possibly synchronously;
 * environment is AddRef'd for the receiver and null on failure.
 */
void acquire_shared_environment(const std::string& browser_data_dir, const std::string& additional_browser_arguments, EnvironmentReadyFn on_ready);

} // namespace orca::wv::detail
