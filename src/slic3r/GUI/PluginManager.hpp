#ifndef slic3r_PluginManager_hpp_
#define slic3r_PluginManager_hpp_

#include <memory>
#include <string>
#include <vector>

#include "orca/jsbridge/bridge.hpp"
#include "orca/plugin/plugin_host.hpp"

namespace Slic3r {
namespace GUI {

class PluginCenterPage;
class PluginTabPage;

/*
 * Application-level plugin management (single instance on GUI_App).
 * Owns the PluginHost, the installed-plugin directory
 * (<user local data>/plugins/<plugin_id>/) and the frozen jsbridge API
 * surface exposed to the plugin center page and to plugin sessions:
 *
 *   plugin.list          sync   installed plugins (scan of the directory)
 *   plugin.pick_import   sync   native file dialog (zip | folder)
 *   plugin.import        async  install from the picked path
 *   plugin.remove        async  close if running, then delete the directory
 *   plugin.open          async  permission prompt -> dynamic tab
 *   plugin.close         async  close the plugin's dynamic tab
 *   plugin.install_sample async install the bundled sample plugin
 *   orca.filament.list   sync   filament list of the current project
 *   orca.host.info       sync   host version / platform / locale
 */
class PluginManager
{
public:
    PluginManager();

    // Root dir of installed plugins: <user local data>/plugins.
    std::string plugins_root() const;

    // Register the frozen API surface on the plugin center page bridge.
    void register_center_apis(orca::jb::Bridge* bridge);

    // Lazily create the PluginHost and install the host API surface on it.
    orca::plugin::PluginHost* ensure_host();

    // Permission prompt (unknown tags) -> PluginHost::load -> PluginTabPage.
    std::shared_ptr<PluginTabPage> open_plugin(const std::string& plugin_id, wxWindow* tab_parent);

    // Release the session of a running plugin (tab teardown path).
    bool close_plugin(const std::string& plugin_id);

    bool        is_running(const std::string& plugin_id) const;
    std::vector<std::string> installed_ids() const;

private:
    std::unique_ptr<orca::plugin::PluginHost> m_host;
    std::string                               m_plugins_root;
};

} // GUI
} // Slic3r

#endif /* slic3r_PluginManager_hpp_ */
