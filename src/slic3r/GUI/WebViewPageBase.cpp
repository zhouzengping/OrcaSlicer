#include "WebViewPageBase.hpp"

#include "SSWCP.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "libslic3r/Utils.hpp"

#include <boost/log/trivial.hpp>

#include <wx/app.h>
#include <wx/sizer.h>
#include <wx/stdpaths.h>

namespace Slic3r {
namespace GUI {

namespace
{

std::string user_local_data_dir()
{
    // WINDOWS ONLY: WebView2 permits a single user data folder per process.
    // Must stay byte-identical to the folder every wxWebViewEdge instance in
    // this process uses (see wv::WebView::Config docs).
    const wxScopedCharBuffer buf = wxStandardPaths::Get().GetUserLocalDataDir().ToUTF8();
    return std::string(buf.data(), buf.length());
}

// Legacy bridge for unmodified flutter pages: they call window.wx.postMessage.
#if defined(_WIN32)
constexpr const char* g_wx_compat_shim =
    "window.wx = window.wx || {postMessage: function(s){chrome.webview.postMessage(s);}};";
#else
constexpr const char* g_wx_compat_shim =
    "window.wx = window.wx || {postMessage: function(s){window.webkit.messageHandlers.wx.postMessage(s);}};";
#endif

/*
 * Downlink sink body: marshal to the UI thread, then run the script once the
 * view is Ready. A view still in Creating state gets one deferred retry; a
 * second miss drops the frame (the page is gone or failed).
 */
void run_script_when_ready(std::weak_ptr<orca::wv::WebView> weak, const std::string& script, bool allow_retry)
{
    wxTheApp->CallAfter([weak, script, allow_retry]()
    {
        std::shared_ptr<orca::wv::WebView> view = weak.lock();
        if (!view)
            return;

        if (view->state() != orca::wv::WebView::State::Ready)
        {
            if (allow_retry)
                run_script_when_ready(weak, script, false);
            return;
        }

        view->run_script(script);
    });
}

} // namespace

WebViewPageBase::WebViewPageBase(wxWindow* parent)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL)
{
    m_host_panel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(m_host_panel, 1, wxEXPAND);
    SetSizer(sizer);

    Bind(wxEVT_SIZE, &WebViewPageBase::on_size, this);
}

WebViewPageBase::~WebViewPageBase()
{
    // Teardown while the native parent HWND is still alive.
    close_view();
}

void* WebViewPageBase::sswcp_handle() const
{
    return m_view.get();
}

bool WebViewPageBase::create_view(const orca::wv::WebView::Config& cfg)
{
    if (m_view)
        return true;

    orca::wv::WebView::Config config = cfg;
    if (config.browser_data_dir.empty())
        config.browser_data_dir = user_local_data_dir();
    // Ride ahead of every page script so flutter pages can post from line one.
    config.extra_init_scripts.insert(config.extra_init_scripts.begin(), g_wx_compat_shim);

    orca::wv::Status status;
    m_view = orca::wv::WebView::create(m_host_panel->GetHandle(), config, status);
    if (!m_view)
    {
        BOOST_LOG_TRIVIAL(error) << "WebViewPageBase: webview create failed: " << status.message;
        return false;
    }

    m_view->events().on_created = [this]()
    {
        apply_bounds();
        SSWCP::register_web_sink(sswcp_handle(), [weak = std::weak_ptr<orca::wv::WebView>(m_view)](const std::string& script)
        {
            run_script_when_ready(weak, script, true);
        });
        on_view_created();
    };
    m_view->events().on_error   = [this](const orca::wv::Status& st) { on_view_error(st); };
    m_view->events().on_message = [this](const std::string& msg) { on_message(msg); };
    m_view->events().on_navigation = [this](const orca::wv::NavigationState& nav) { return on_navigation(nav.url); };
    m_view->events().on_document_loaded = [this](const orca::wv::NavigationState& nav) { on_document_loaded(nav.url); };

    return true;
}

void WebViewPageBase::adopt_view(const std::shared_ptr<orca::wv::WebView>& view)
{
    // Bounds management only; the caller owns the view and its event slots.
    m_view         = view;
    m_view_adopted = true;
    apply_bounds();
}

bool WebViewPageBase::attach_bridge(const orca::jb::BridgeConfig& cfg, const orca::jb::SessionInfo& session)
{
    if (!m_view || m_bridge)
        return false;

    orca::jb::BridgeConfig config = cfg;
    if (!config.dispatcher)
    {
        config.dispatcher = [](std::function<void()> fn)
        {
            wxTheApp->CallAfter(fn);
        };
    }

    m_bridge = std::make_unique<orca::jb::Bridge>();
    const orca::wv::Status status = m_bridge->attach(m_view, config, session);
    if (!status.ok())
    {
        BOOST_LOG_TRIVIAL(error) << "WebViewPageBase: bridge attach failed: " << status.message;
        m_bridge.reset();
        return false;
    }

    return true;
}

void WebViewPageBase::load_url(const wxString& url)
{
    if (m_view)
        m_view->navigate(url.ToUTF8().data());
}

void WebViewPageBase::reload()
{
    if (m_view)
        m_view->reload();
}

void WebViewPageBase::run_script(const wxString& js)
{
    if (m_view)
        m_view->run_script(js.ToUTF8().data());
}

wxString WebViewPageBase::get_url() const
{
    if (!m_view)
        return wxString();

    return wxString::FromUTF8(m_view->url());
}

bool WebViewPageBase::is_ready() const
{
    return m_view && m_view->state() == orca::wv::WebView::State::Ready;
}

void WebViewPageBase::notify_page_state(const std::string& state)
{
    if (m_bridge && m_bridge->attached())
        m_bridge->emit("page.state", orca::jb::Json{{"state", state}});
    else
        wxGetApp().page_state_notify_webview(sswcp_handle(), state);
}

void WebViewPageBase::on_message(const std::string& json)
{
    wxGetApp().on_flutter_wcp_received();
    SSWCP::handle_web_message(json, sswcp_handle());
}

void WebViewPageBase::on_view_created()
{
    // Subclasses navigate here.
}

bool WebViewPageBase::on_navigation(const std::string& /*url*/)
{
    return true;
}

void WebViewPageBase::on_document_loaded(const std::string& /*url*/)
{
}

void WebViewPageBase::apply_bounds()
{
    if (!m_view || m_view->state() != orca::wv::WebView::State::Ready)
        return;

    // set_bounds takes PHYSICAL pixels; convert the logical client size.
    const wxSize client = m_host_panel->GetClientSize();
    const double scale  = m_host_panel->GetContentScaleFactor();
    m_view->set_bounds(0, 0, static_cast<int>(client.GetWidth() * scale), static_cast<int>(client.GetHeight() * scale));
}

void WebViewPageBase::close_view()
{
    if (m_bridge)
        m_bridge->detach();

    if (m_view && !m_view_adopted)
    {
        SSWCP::on_webview_delete(sswcp_handle());
        SSWCP::unregister_web_sink(sswcp_handle());
        m_view->close();
    }

    m_view.reset();
}

void WebViewPageBase::on_size(wxSizeEvent& event)
{
    event.Skip();
    apply_bounds();
}

void WebViewPageBase::on_view_error(const orca::wv::Status& status)
{
    BOOST_LOG_TRIVIAL(error) << "WebViewPageBase: webview error: " << status.message;
}

} // GUI
} // Slic3r
