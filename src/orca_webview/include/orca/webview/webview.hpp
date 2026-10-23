#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "orca/webview/export.h"
#include "orca/webview/types.hpp"

namespace orca::wv {

/*
 * Platform-neutral webview over WebView2 (Windows) / WKWebView (macOS) /
 * WebKitGTK (Linux). Embeds into a native parent window without any GUI
 * framework dependency; native_parent is HWND / NSView* / GtkWidget*.
 *
 * Threading: every method must be called on the UI thread and every event
 * callback fires on the UI thread. Creation is asynchronous: state() is
 * Creating until on_created (Ready) or on_error (Failed).
 */
class ORCA_WV_API WebView
{
public:
    struct Config
    {
        std::string user_agent;             // empty = engine default; per-view setting
        ColorScheme color_scheme = ColorScheme::Auto;

        bool devtools    = true;
        bool context_menu = true;

        // WINDOWS ONLY: must equal the user data folder used by every
        // wxWebViewEdge instance in this process, i.e.
        //   wxStandardPaths::Get().GetUserLocalDataDir().ToUTF8()
        // WebView2 allows only ONE user data folder per process; a second
        // environment with a different folder fails. macOS/Linux: per-view
        // independent data dir (may be empty for the platform default).
        std::string browser_data_dir;

        // WINDOWS ONLY: profile partition inside the shared data folder
        // (ICoreWebView2ControllerOptions::ProfileName). Empty = default profile.
        std::string profile_name;

        // WINDOWS ONLY: additional browser arguments for the shared WebView2
        // environment (ICoreWebView2EnvironmentOptions::AdditionalBrowserArguments).
        // Every environment sharing one user data folder in a process must pass
        // byte-identical options, including arguments already set by wxWebViewEdge
        // instances created in this process; a mismatch is undefined behavior.
        // macOS/Linux: ignored.
        std::string additional_browser_arguments;

        bool incognito = false;   // win: IsInPrivateModeEnabled; mac/gtk: ephemeral store

        std::vector<CustomScheme> custom_schemes;

        // Executed at document-start of every navigation, in order, before any
        // page script. The jsbridge SDK is injected this way via add_init_script.
        std::vector<std::string> extra_init_scripts;
    };

    // All callbacks fire on the UI thread. Wire them immediately after
    // create() returns, while state is still Creating, to not miss on_created.
    struct Events
    {
        std::function<void()>                                on_created;
        std::function<void(const Status&)>                   on_error;            // terminal -> state Failed
        std::function<void(const std::string&)>              on_message;          // raw JS -> native message
        std::function<void(const NavigationState&)>          on_document_loaded;
        std::function<void(const std::string&)>              on_title_changed;
        std::function<void(const std::string&)>              on_url_changed;      // origin tracking; consumed by jsbridge
        std::function<bool(const NavigationState&)>          on_navigation;       // return false to veto
        std::function<OpenDecision(const NewWindowRequest&)> on_new_window;       // window.open / target=_blank
    };

    enum class State : uint8_t
    {
        Creating,
        Ready,
        Closed,
        Failed,
    };

    /*
     * Factory. native_parent: HWND / NSView* / GtkWidget*.
     * Returns nullptr and fills status on synchronous failure (runtime missing,
     * bad config, environment conflict). On success creation continues
     * asynchronously: watch events() for on_created / on_error.
     */
    static std::shared_ptr<WebView> create(void* native_parent, const Config& config, Status& status);

    ~WebView();
    WebView(const WebView&) = delete;
    WebView& operator=(const WebView&) = delete;

    State   state() const;
    Events& events();

    /*
     * Detach from the native parent and release engine resources.
     * MUST be called on the UI thread before the native parent is destroyed.
     * Idempotent.
     */
    Status close();

    // Parent client coordinates, PHYSICAL pixels (host handles DPI conversion).
    Status set_bounds(int x, int y, int width, int height);
    Status set_visible(bool visible);

    // ICoreWebView2* / WKWebView* / WebKitWebView*; borrowed pointer for host extensions.
    void* native_handle() const;

    Status        navigate(const std::string& url);
    Status        load_html(const std::string& html, const std::string& base_url);
    Status        reload();
    Status        go_back();
    Status        go_forward();
    std::string   url() const;

    // Document-start script; queued while Creating and flushed once Ready.
    Status add_init_script(const std::string& js);

    /*
     * Evaluate JS in the current document. cb (UI thread) receives the success
     * flag and the JSON-encoded result / error text; cb may be null.
     */
    Status run_script(const std::string& js,
                      std::function<void(bool ok, const std::string& json_or_error)> cb = nullptr);

    /*
     * Push a JSON text to the page over the native message channel.
     * Win: PostWebMessageAsJson (page sees a 'message' event on chrome.webview).
     * mac/gtk: evaluateJavaScript("window.__orcaSdk._recv(<literal>)").
     */
    Status post_message_json(const std::string& json_text);

    static bool        runtime_available();   // WebView2 runtime installed / WebKit present
    static std::string runtime_version();     // engine version string, "" if unknown

private:
    struct Impl;
    explicit WebView(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> m_impl;
};

} // namespace orca::wv
