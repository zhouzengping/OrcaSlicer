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

bool PluginTabPage::adopt_session(std::shared_ptr<orca::plugin::PluginSession> session)
{
    if (m_session || !session)
        return false;

    m_session   = std::move(session);
    m_plugin_id = m_session->id();
    adopt_view(m_session->webview());
    return true;
}

void* PluginTabPage::native_host_handle() const
{
    return WebViewPageBase::native_host_handle();
}

} // GUI
} // Slic3r
