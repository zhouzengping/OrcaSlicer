#include "PluginTabPage.hpp"

namespace Slic3r {
namespace GUI {

PluginTabPage::PluginTabPage(wxWindow* parent, std::shared_ptr<orca::plugin::PluginSession> session)
    : WebViewPageBase(parent)
    , m_session(std::move(session))
{
    if (m_session)
    {
        m_plugin_id = m_session->id();
        // The session webview was created by PluginHost on a HWND it was given;
        // adopted here for bounds management only (PluginHost owns lifecycle).
        adopt_view(m_session->webview());
    }
}

} // GUI
} // Slic3r
