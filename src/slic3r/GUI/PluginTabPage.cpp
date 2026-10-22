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

    // The session webview is still Creating here: the apply_bounds() pass in
    // adopt_view() no-ops and the tab's wxEVT_SIZE usually also races the
    // async controller, so nothing would ever size the view. Chain a bounds
    // pass in front of the host's on_created slot (navigate-only); the
    // controller-completed callback is queued on the UI thread, so this
    // synchronous replacement always lands before it runs.
    const std::shared_ptr<orca::wv::WebView> view = m_session->webview();
    std::function<void()> prev_created            = view->events().on_created;
    view->events().on_created                     = [weak_this = weak_from_this(), prev_created]()
    {
        if (std::shared_ptr<PluginTabPage> tab = weak_this.lock())
            tab->apply_bounds();
        if (prev_created)
            prev_created();
    };

    adopt_view(view);
    return true;
}

void* PluginTabPage::native_host_handle() const
{
    return WebViewPageBase::native_host_handle();
}

} // GUI
} // Slic3r
