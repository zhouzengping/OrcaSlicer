#ifndef slic3r_PluginTabPage_hpp_
#define slic3r_PluginTabPage_hpp_

#include <memory>
#include <string>

#include "orca/plugin/plugin_host.hpp"
#include "WebViewPageBase.hpp"

namespace Slic3r {
namespace GUI {

/*
 * Dynamic tab page hosting one running plugin. Created by MainFrame on
 * open_plugin_tab(), destroyed on close_plugin_tab(); the page adopts the
 * PluginSession assembled by PluginManager and releases it in its destructor
 * while the native HWND is still alive.
 */
class PluginTabPage : public WebViewPageBase
{
public:
    PluginTabPage(wxWindow* parent, std::shared_ptr<orca::plugin::PluginSession> session);

    const std::string& plugin_id() const { return m_plugin_id; }
    orca::plugin::PluginSession* session() const { return m_session.get(); }

    bool wants_generic_page_state() const override { return true; }

private:
    std::string                            m_plugin_id;
    std::shared_ptr<orca::plugin::PluginSession> m_session;
};

} // GUI
} // Slic3r

#endif /* slic3r_PluginTabPage_hpp_ */
