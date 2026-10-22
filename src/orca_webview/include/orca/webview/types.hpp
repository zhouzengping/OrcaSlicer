#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "orca/webview/export.h"

namespace orca::wv {

/*
 * Error codes shared by all orca libraries (webview / jsbridge / plugin_host).
 * Numeric values are stable; never reuse or reorder existing entries.
 */
enum class Errc : int32_t
{
    Ok = 0,
    // Caller misuse
    InvalidArgument = 1000,
    NotSupported,
    InvalidState,
    OutOfMemory,
    // Environment / creation
    RuntimeMissing,
    EnvironmentRejected,
    ParentInvalid,
    // Content
    NavigationFailed,
    NavigationCancelled,
    // Scripting / messaging
    ScriptEvalFailed,
    MessageChannelClosed,
    // Catch-all native failure; Status::native carries the platform code
    PlatformFailure = 2000,
};

/*
 * Uniform exception-free error value used across the orca libraries.
 * native keeps the platform status (HRESULT on Windows, GError code on GTK,
 * NSError code on macOS); 0 when not applicable.
 */
struct Status
{
    Errc        code = Errc::Ok;
    std::string message;
    int64_t     native = 0;

    bool ok() const { return code == Errc::Ok; }

    /*
     * Build a failure Status.
     * code: nonzero Errc value
     * message: short English description, log-friendly
     * native: platform status code when available
     */
    static ORCA_WV_API Status make(Errc code, std::string message = "", int64_t native = 0);
};

enum class ColorScheme : uint8_t
{
    Auto,
    Light,
    Dark,
};

struct SchemeRequest;
struct SchemeResponder;

/*
 * A non-http scheme mapped to local content.
 * Windows: virtual host mapping -> https://<scheme>.orca/<entry> (real https origin).
 * macOS/Linux: true custom scheme -> <scheme>://<host>/<entry>.
 */
struct CustomScheme
{
    std::string scheme;       // lowercase, no "://", e.g. "orca-plugin"
    std::string root_folder;  // absolute UTF-8 path of the read-only content root

    // Reserved for callback-based serving; phase 1 serves folders only and
    // returns NotSupported when a handler is set with an empty root_folder.
    std::function<void(const SchemeRequest&, SchemeResponder&)> handler;
};

struct NavigationState
{
    std::string url;
    bool        is_main_frame = true;
};

struct NewWindowRequest
{
    std::string url;
    std::string opener_url;
};

enum class OpenDecision : uint8_t
{
    OpenInWebView,
    Deny,
};

/*
 * Open an url with the system default browser.
 * Returns InvalidArgument for empty urls, PlatformFailure when the launch fails.
 */
ORCA_WV_API Status open_in_system_browser(const std::string& url);

} // namespace orca::wv
