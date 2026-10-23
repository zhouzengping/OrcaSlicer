#ifndef slic3r_WebViewPageBase_hpp_
#define slic3r_WebViewPageBase_hpp_

#include <memory>
#include <string>

#include <wx/panel.h>

#include "orca/jsbridge/bridge.hpp"
#include "orca/webview/webview.hpp"

namespace Slic3r {
namespace GUI {

/*
 * Shared WebView2 user data folder. WINDOWS ONLY: WebView2 permits a single
 * user data folder per process, so every native webview config (flutter hosts
 * and the plugin host alike) must use this exact byte-identical string.
 */
std::string webview_user_data_dir();

/*
 * AdditionalBrowserArguments matching the wx WebView2 environments created in
 * this process ('--user-agent="..."' built exactly like
 * WebView::CreateWebView does). WINDOWS ONLY: every environment sharing the
 * user data folder must pass byte-identical arguments; snapshot taken once
 * per process. Empty string on other platforms.
 */
std::string webview_browser_arguments();

/*
 * Common host panel for web pages on the orca webview stack. Owns the native
 * view lifecycle (create / resize / close), always injects the legacy wx
 * postMessage shim so unmodified flutter pages keep working, registers the
 * SSWCP downlink sink and feeds uplink messages into SSWCP. Pages speaking
 * the jsbridge SDK additionally call attach_bridge().
 */
class WebViewPageBase : public wxPanel
{
public:
    WebViewPageBase(wxWindow* parent);
    ~WebViewPageBase() override;

    // Opaque identity for the SSWCP channel; stable for the view lifetime.
    void* sswcp_handle() const;

    void    load_url(const wxString& url);
    void    reload();
    void    run_script(const wxString& js);
    wxString get_url() const;
    bool    is_ready() const;
    orca::wv::WebView* view() const { return m_view.get(); }

    // "active" / "inactive" notifications for tab switches.
    virtual void notify_page_state(const std::string& state);
    // Flutter hosts keep the legacy tpHome / tpMonitor notify branch instead.
    virtual bool wants_generic_page_state() const { return false; }

protected:
    /*
     * Creates the native view on this panel. The wx shim is prepended to
     * extra_init_scripts; an empty browser_data_dir defaults to the shared
     * WebView2 user data folder. Returns false on synchronous failure.
     */
    bool create_view(const orca::wv::WebView::Config& cfg);

    /*
     * Adopts a view created elsewhere (e.g. the PluginHost session webview)
     * for bounds management only: no event hooks, no SSWCP sink and no
     * ownership - the caller keeps the view alive and closes it.
     */
    void adopt_view(const std::shared_ptr<orca::wv::WebView>& view);

    // Only for pages speaking the jsbridge SDK (plugin system).
    bool attach_bridge(const orca::jb::BridgeConfig& cfg, const orca::jb::SessionInfo& session);
    orca::jb::Bridge* bridge() const { return m_bridge.get(); }

    // Native window handle a plugin session webview can be parented to.
    void* native_host_handle() const;

    // Uplink hook; default feeds SSWCP and the flutter WCP watchdog.
    virtual void on_message(const std::string& json);
    // Called once the view is Ready; before any navigation started by the subclass.
    virtual void on_view_created();
    // Return false to veto a navigation.
    virtual bool on_navigation(const std::string& url);
    virtual void on_document_loaded(const std::string& url);

    /*
     * Push the host panel client size (physical pixels) into the view.
     * No-op unless the view is Ready; subclasses adopting a still-Creating
     * view chain this into the view's on_created slot for the initial size.
     */
    void apply_bounds();

private:
    void close_view();
    void on_size(wxSizeEvent& event);
    void on_view_error(const orca::wv::Status& status);

    wxPanel*                           m_host_panel = nullptr;
    std::shared_ptr<orca::wv::WebView> m_view;      // owned, or adopted (bounds only)
    bool                               m_view_adopted = false;
    std::unique_ptr<orca::jb::Bridge>  m_bridge;
};

} // GUI
} // Slic3r

#endif /* slic3r_WebViewPageBase_hpp_ */
