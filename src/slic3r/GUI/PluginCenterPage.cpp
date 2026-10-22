#include "PluginCenterPage.hpp"

#include "PluginManager.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "libslic3r/Utils.hpp"

#include <boost/filesystem.hpp>

#include <wx/app.h>

namespace Slic3r {
namespace GUI {

namespace
{

/*
 * The plugin center page is static content shipped with the app
 * (resources/web/plugin_center), so it is served from a custom scheme rooted
 * at that folder rather than from a remote origin: no network round trip, no
 * shared cookie jar with the outside world, and the origin doubles as the
 * content identity for the bridge origin gate. On Windows WebView2 maps the
 * scheme name to the virtual host "<scheme>.orca" (see backend_win.cpp
 * apply_custom_schemes), so the URL is a real https origin there; macOS and
 * Linux keep the custom scheme URL shape (see plugin_host.cpp, which serves
 * plugin content the same way).
 */
#if defined(_WIN32)
constexpr const char* g_entry_url = "https://plugin-center.orca/index.html";
#else
constexpr const char* g_entry_url = "plugin-center://center/index.html";
#endif

} // namespace

PluginCenterPage::PluginCenterPage(wxWindow* parent)
    : WebViewPageBase(parent)
{
    setup_page();
}

void PluginCenterPage::setup_page()
{
    orca::wv::WebView::Config cfg;
    // browser_data_dir stays empty on purpose: create_view fills in the
    // shared WebView2 user data folder, which must stay byte-identical for
    // every webview in this process.
    cfg.custom_schemes.push_back(orca::wv::CustomScheme{
        "plugin-center",
        (boost::filesystem::path(Slic3r::resources_dir()) / "web" / "plugin_center").generic_string()});

    if (!create_view(cfg))
        return;

    // Attach while the view is still Creating so the bridge SDK is queued as
    // an init script and lands at document-start of the first navigation
    // (same attach order as WebViewDevDialog).
    orca::jb::BridgeConfig bridge_cfg;
    // Both platform origins are listed, mirroring the plugin session bridge
    // (plugin_host.cpp): Windows serves the folder through the virtual host
    // "https://plugin-center.orca", macOS/Linux through the custom scheme.
    // Origin matching is exact, so at most one ever applies.
    bridge_cfg.allowed_origins = {"https://plugin-center.orca", "plugin-center://center"};
    // Async responders marshal their completion through the dispatcher; route
    // it to the UI thread explicitly (same body as the attach_bridge default
    // fallback) instead of relying on that default staying correct.
    bridge_cfg.dispatcher = [](std::function<void()> fn) { wxTheApp->CallAfter(fn); };
    // Same value as the BridgeConfig default; pinned explicitly so the center
    // page does not silently follow future default changes.
    bridge_cfg.api_timeout_ms = 30000;

    orca::jb::SessionInfo session;
    session.session_id = "plugin-center";
    session.context    = orca::jb::Json{{"kind", "plugin-center"}};

    if (!attach_bridge(bridge_cfg, session))
        return;

    // The API surface must be registered on the GUI_App-owned manager: it is
    // the same instance MainFrame talks to, so plugin.list running flags and
    // plugin.open / plugin.close stay consistent with the open tabs.
    wxGetApp().plugin_manager()->register_center_apis(bridge());
}

void PluginCenterPage::on_view_created()
{
    // locale / dark_mode reuse the home page query-string convention
    // (GUI_App::get_international_url), so the page reads them the same way
    // on first load and on reload_page().
    load_url(wxGetApp().get_international_url(wxString::FromUTF8(g_entry_url)));
}

void PluginCenterPage::reload_page()
{
    // Language switch path: re-navigate with fresh locale / dark_mode.
    if (!is_ready())
        return;

    load_url(wxGetApp().get_international_url(wxString::FromUTF8(g_entry_url)));
}

} // GUI
} // Slic3r
