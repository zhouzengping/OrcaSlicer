#pragma once

// Internal (non-exported) platform backend contract for orca_webview.
// One implementation per platform: backend_win.cpp / backend_mac.mm / backend_gtk.cpp.
// All methods are called on the UI thread, mirroring WebView's public contract.

#include <functional>
#include <string>

#include "orca/webview/types.hpp"
#include "orca/webview/webview.hpp"

namespace orca::wv::detail {

using ScriptDoneFn = std::function<void(bool ok, const std::string& json_or_error)>;

/*
 * Event proxy: backend -> WebView::Events forwarding. Implemented by
 * WebView::Impl; virtual so backends never see the public class layout.
 * All methods run on the UI thread.
 */
class EventProxy
{
public:
    virtual ~EventProxy() = default;

    virtual void         report_created() = 0;
    virtual void         report_error(const Status& status) = 0;
    virtual void         report_message(std::string message) = 0;
    virtual void         report_document_loaded(const NavigationState& state) = 0;
    // Non-fatal navigation failure (page error); the state machine is unchanged.
    virtual void         report_navigation_failed(const NavigationState& state, const Status& status) = 0;
    virtual void         report_title(std::string title) = 0;
    virtual void         report_url_changed(std::string url) = 0;
    virtual bool         query_navigation(const NavigationState& state) = 0;   // false = veto
    virtual OpenDecision query_new_window(const NewWindowRequest& request) = 0;
};

/*
 * Platform backend interface. start() kicks off asynchronous creation and
 * returns immediately; completion is reported through the EventProxy
 * (report_created on success, report_error on failure). Every other method
 * must only be called after report_created, except close().
 */
class Backend
{
public:
    virtual ~Backend() = default;

    virtual wv::Status   start() = 0;
    virtual wv::Status   navigate(const std::string& url) = 0;
    virtual wv::Status   load_html(const std::string& html, const std::string& base_url) = 0;
    virtual wv::Status   reload() = 0;
    virtual wv::Status   go_back() = 0;
    virtual wv::Status   go_forward() = 0;
    virtual std::string  url() const = 0;
    virtual wv::Status   set_bounds(int x, int y, int width, int height) = 0;
    virtual wv::Status   set_visible(bool visible) = 0;
    virtual wv::Status   add_init_script(const std::string& js) = 0;
    virtual wv::Status   run_script(const std::string& js, ScriptDoneFn cb) = 0;
    virtual wv::Status   post_message_json(const std::string& json_text) = 0;
    virtual wv::Status   close() = 0;
    virtual void*        native_handle() const = 0;
};

/*
 * Compile-time platform dispatch. The default stub keeps the library
 * buildable before a platform is brought up; backend bring-up replaces the
 * body with:
 *   #if defined(_WIN32)    -> create_backend_win(parent, config, proxy)
 *   #elif defined(__APPLE__) -> create_backend_mac(...)
 *   #else                  -> create_backend_gtk(...)
 */
Backend* make_backend(void* native_parent, const WebView::Config& config, EventProxy& proxy);

/*
 * Engine version string for the platform's web runtime (WebView2 browser
 * version / WKWebView system build / WebKitGTK version); "" when unknown or
 * not installed. Implemented in backend_win.cpp / backend_mac.mm /
 * backend_gtk.cpp.
 */
std::string platform_runtime_version();

} // namespace orca::wv::detail
