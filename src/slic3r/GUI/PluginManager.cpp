#include "PluginManager.hpp"

#include "PluginTabPage.hpp"

namespace Slic3r {
namespace GUI {

PluginManager::PluginManager()
{
}

std::string PluginManager::plugins_root() const
{
    return m_plugins_root;
}

void PluginManager::register_center_apis(orca::jb::Bridge* /*bridge*/)
{
}

orca::plugin::PluginHost* PluginManager::ensure_host()
{
    return m_host.get();
}

std::shared_ptr<PluginTabPage> PluginManager::open_plugin(const std::string& /*plugin_id*/, wxWindow* /*tab_parent*/)
{
    return nullptr;
}

bool PluginManager::close_plugin(const std::string& /*plugin_id*/)
{
    return false;
}

bool PluginManager::is_running(const std::string& /*plugin_id*/) const
{
    return false;
}

std::vector<std::string> PluginManager::installed_ids() const
{
    return {};
}

} // GUI
} // Slic3r
