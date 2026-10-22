#ifndef slic3r_PluginCenterPage_hpp_
#define slic3r_PluginCenterPage_hpp_

#include "WebViewPageBase.hpp"

namespace Slic3r {
namespace GUI {

class PluginManager;

/*
 * Fixed "Plugin Center" tab: a pure web page (resources/web/plugin_center)
 * served over the plugin-center custom scheme and wired to the jsbridge API
 * surface registered by PluginManager.
 */
class PluginCenterPage : public WebViewPageBase
{
public:
    explicit PluginCenterPage(wxWindow* parent);

    // Re-navigate with the current locale / dark mode (language switch path).
    void reload_page();

    bool wants_generic_page_state() const override { return true; }

private:
    void setup_page();
};

} // GUI
} // Slic3r

#endif /* slic3r_PluginCenterPage_hpp_ */
