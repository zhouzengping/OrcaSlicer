#include "PluginCenterPage.hpp"

#include "PluginManager.hpp"

namespace Slic3r {
namespace GUI {

PluginCenterPage::PluginCenterPage(wxWindow* parent)
    : WebViewPageBase(parent)
{
    setup_page();
}

void PluginCenterPage::setup_page()
{
    // Assembled by PluginManager integration: custom scheme, bridge attach
    // with the frozen plugin.* / orca.* API surface, then navigate to
    // https://plugin-center.orca/index.html.
}

void PluginCenterPage::reload_page()
{
    // Re-navigate with fresh locale / dark_mode query parameters.
}

} // GUI
} // Slic3r
