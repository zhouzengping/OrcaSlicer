#ifndef slic3r_WebViewDevDialog_hpp_
#define slic3r_WebViewDevDialog_hpp_

#include <memory>
#include <string>

#include <wx/dialog.h>
#include <wx/timer.h>

#include "orca/jsbridge/bridge.hpp"
#include "orca/plugin/manifest.hpp"
#include "orca/plugin/permission_store.hpp"
#include "orca/plugin/plugin_host.hpp"
#include "orca/webview/webview.hpp"

class wxButton;
class wxPanel;

namespace Slic3r {
namespace GUI {

/*
 * Developer verification window for the orca webview / jsbridge / plugin_host
 * stack. Hosts the SDK test page from resources/web/orca_sdk_test over a
 * custom scheme, registers the demo.* API surface, pushes periodic events and
 * can load the bundled demo plugin through PluginHost.
 */
class WebViewDevDialog : public wxDialog
{
public:
    WebViewDevDialog(wxWindow* parent);
    ~WebViewDevDialog() override;

private:
    void build_ui();
    void create_webview();
    void register_demo_apis();
    void apply_view_bounds();
    // Bounds of the loaded plugin session webview (shares the host panel).
    void apply_plugin_view_bounds();
    void ensure_plugin_host();
    void prompt_plugin_permissions(const orca::plugin::PluginManifest& manifest);
    void report_failure(const orca::wv::Status& status);

    void on_load_plugin(wxCommandEvent& event);
    void on_unload_plugin(wxCommandEvent& event);
    void on_size(wxSizeEvent& event);
    void on_timer(wxTimerEvent& event);
    void on_close(wxCloseEvent& event);

    wxPanel*               m_host_panel        = nullptr;
    wxButton*              m_load_plugin_btn   = nullptr;
    wxButton*              m_unload_plugin_btn = nullptr;
    wxTimer                m_tick_timer;
    int                    m_tick_count = 0;

    std::shared_ptr<orca::wv::WebView>           m_view;
    std::unique_ptr<orca::jb::Bridge>            m_bridge;
    std::unique_ptr<orca::plugin::PluginHost>    m_plugin_host;
    std::shared_ptr<orca::plugin::PluginSession> m_plugin_session;
    std::string                                   m_plugin_store_path;
};

// Entry point for the Help menu accelerator; no-op unless the session-scoped
// internal_developer_mode switch is on (it is reset on every startup and only
// turned on at runtime from Preferences).
void show_webview_dev_dialog(wxWindow* parent);

} // GUI
} // Slic3r

#endif /* slic3r_WebViewDevDialog_hpp_ */
