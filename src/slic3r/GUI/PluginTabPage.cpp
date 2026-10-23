#include "PluginTabPage.hpp"

namespace Slic3r {
namespace GUI {

PluginTabPage::PluginTabPage(wxWindow* parent)
    : WebViewPageBase(parent)
{
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

void PluginTabPage::notify_page_state(const std::string& state)
{
    // Deliberately not calling the base implementation: its no-bridge
    // fallback routes to the legacy SSWCP subscribers, which a plugin page
    // never has. The session bridge is the page's only event channel.
    if (!m_session)
        return;

    const std::shared_ptr<orca::jb::Bridge> bridge = m_session->bridge();
    if (bridge)
        bridge->emit("page.state", orca::jb::Json{{"state", state}});
}

} // GUI
} // Slic3r
