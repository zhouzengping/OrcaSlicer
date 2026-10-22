#include "WebViewDevDialog.hpp"

#include "I18N.hpp"
#include "WebViewPageBase.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "libslic3r/Utils.hpp"

#include <boost/filesystem.hpp>

#include <wx/app.h>
#include <wx/button.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>

#include <chrono>
#include <functional>
#include <string>
#include <thread>

namespace Slic3r {
namespace GUI {

namespace
{

// Singleton instance; repeated menu triggers just raise the open window.
WebViewDevDialog* s_dev_dialog = nullptr;

void show_status_error(wxWindow* parent, const orca::wv::Status& status)
{
    wxMessageDialog dlg(parent,
        wxString::Format("code %d\n%s", static_cast<int>(status.code), wxString::FromUTF8(status.message.c_str())),
        _L("Orca Webview Framework (Dev)"), wxOK | wxICON_ERROR);
    dlg.ShowModal();
}

} // namespace

WebViewDevDialog::WebViewDevDialog(wxWindow* parent)
    : wxDialog(parent, wxID_ANY, _L("Orca Webview Framework (Dev)"), wxDefaultPosition, wxSize(960, 720),
          wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
    , m_tick_timer(this)
{
    build_ui();
    create_webview();
}

WebViewDevDialog::~WebViewDevDialog()
{
    if (s_dev_dialog == this)
        s_dev_dialog = nullptr;
}

void WebViewDevDialog::build_ui()
{
    wxBoxSizer* topsizer = new wxBoxSizer(wxVERTICAL);

    // Native host panel: the webview is created directly on its HWND.
    m_host_panel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_SIMPLE);
    m_host_panel->SetBackgroundColour(*wxWHITE);
    topsizer->Add(m_host_panel, 1, wxEXPAND);

    wxBoxSizer* button_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_load_plugin_btn   = new wxButton(this, wxID_ANY, _L("Load demo plugin"));
    m_unload_plugin_btn = new wxButton(this, wxID_ANY, _L("Unload"));
    m_unload_plugin_btn->Enable(false);
    button_sizer->Add(m_load_plugin_btn, 0, wxALL, 5);
    button_sizer->Add(m_unload_plugin_btn, 0, wxALL, 5);
    topsizer->Add(button_sizer, 0, wxALIGN_RIGHT);

    SetSizer(topsizer);

    m_load_plugin_btn->Bind(wxEVT_BUTTON, &WebViewDevDialog::on_load_plugin, this);
    m_unload_plugin_btn->Bind(wxEVT_BUTTON, &WebViewDevDialog::on_unload_plugin, this);
    Bind(wxEVT_SIZE, &WebViewDevDialog::on_size, this);
    Bind(wxEVT_TIMER, &WebViewDevDialog::on_timer, this);
    Bind(wxEVT_CLOSE_WINDOW, &WebViewDevDialog::on_close, this);
}

void WebViewDevDialog::create_webview()
{
    orca::wv::WebView::Config cfg;
    cfg.browser_data_dir = webview_user_data_dir();
    cfg.devtools         = true;

    orca::wv::CustomScheme scheme;
    scheme.scheme      = "orca-sdk-test";
    scheme.root_folder = (boost::filesystem::path(Slic3r::resources_dir()) / "web" / "orca_sdk_test").generic_string();
    cfg.custom_schemes.push_back(scheme);

    orca::wv::Status status;
    m_view = orca::wv::WebView::create(m_host_panel->GetHandle(), cfg, status);
    if (!m_view)
    {
        report_failure(status);
        return;
    }

    m_view->events().on_created = [this]()
    {
        apply_view_bounds();
        m_view->navigate("https://orca-sdk-test.orca/index.html");
    };
    m_view->events().on_error = [this](const orca::wv::Status& st) { report_failure(st); };

    // Attach while the view is still Creating so the SDK is queued as an
    // init script and lands at document-start of the first navigation.
    orca::jb::BridgeConfig bridge_cfg;
    bridge_cfg.allowed_origins = {"https://orca-sdk-test.orca"};
    bridge_cfg.dispatcher      = [](std::function<void()> fn)
    {
        // Responder completions may arrive from worker threads; marshal them
        // back to the UI thread before touching the webview.
        wxTheApp->CallAfter(fn);
    };

    orca::jb::SessionInfo session;
    session.session_id = "dev-window";
    session.context    = orca::jb::Json{{"kind", "dev-window"}};

    m_bridge = std::make_unique<orca::jb::Bridge>();
    const orca::wv::Status attach_status = m_bridge->attach(m_view, bridge_cfg, session);
    if (!attach_status.ok())
    {
        report_failure(attach_status);
        return;
    }

    register_demo_apis();
}

void WebViewDevDialog::register_demo_apis()
{
    orca::jb::ApiDesc echo;
    echo.sync = [](const orca::jb::Json& params) { return orca::jb::ApiOutcome::ok_value(params); };
    m_bridge->register_api("demo.echo", echo);

    // Async + cross-thread: sleep on a worker thread, answer through the
    // Responder, which marshals back through the dispatcher configured above.
    orca::jb::ApiDesc delay;
    delay.async = [](const orca::jb::Json& params, orca::jb::Responder responder)
    {
        const int ms = params.value("ms", 0);
        // Detached on purpose: the Responder holds a weak bridge reference,
        // so a completion racing the dialog teardown is a no-op.
        std::thread([responder, ms]() mutable
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(ms));
            responder.send_result(orca::jb::Json{{"slept_ms", ms}});
        }).detach();
    };
    m_bridge->register_api("demo.delay", delay);

    orca::jb::ApiDesc fail;
    fail.sync = [](const orca::jb::Json&)
    {
        return orca::jb::ApiOutcome::error(orca::jb::RpcErrc::InternalError, "intentional failure");
    };
    m_bridge->register_api("demo.fail", fail);

    orca::jb::ApiDesc host_info;
    host_info.sync = [](const orca::jb::Json&)
    {
        return orca::jb::ApiOutcome::ok_value(orca::jb::Json{
            {"host_version", SLIC3R_VERSION},
            {"runtime", orca::wv::WebView::runtime_version()}});
    };
    m_bridge->register_api("demo.hostInfo", host_info);

    // The dev session installs no permission policy, so a tagged API is
    // denied here; the demo plugin flow shows the granted counterpart.
    orca::jb::ApiDesc permission_protected;
    permission_protected.permission = "demo.special";
    permission_protected.sync       = [](const orca::jb::Json&)
    {
        return orca::jb::ApiOutcome::ok_value(orca::jb::Json{{"granted", true}});
    };
    m_bridge->register_api("demo.permissionProtected", permission_protected);

    orca::jb::ApiDesc tick_start;
    tick_start.sync = [this](const orca::jb::Json&)
    {
        m_tick_timer.Start(3000);
        return orca::jb::ApiOutcome::ok_value(orca::jb::Json{{"ticking", true}});
    };
    m_bridge->register_api("demo.tickStart", tick_start);

    orca::jb::ApiDesc tick_stop;
    tick_stop.sync = [this](const orca::jb::Json&)
    {
        m_tick_timer.Stop();
        return orca::jb::ApiOutcome::ok_value(orca::jb::Json{{"ticking", false}});
    };
    m_bridge->register_api("demo.tickStop", tick_stop);
}

void WebViewDevDialog::apply_view_bounds()
{
    if (!m_view || m_view->state() != orca::wv::WebView::State::Ready)
        return;

    // set_bounds takes PHYSICAL pixels; convert the logical client size.
    const wxSize client = m_host_panel->GetClientSize();
    const double scale  = m_host_panel->GetContentScaleFactor();
    m_view->set_bounds(0, 0, static_cast<int>(client.GetWidth() * scale), static_cast<int>(client.GetHeight() * scale));
}

void WebViewDevDialog::ensure_plugin_host()
{
    if (m_plugin_host)
        return;

    const boost::filesystem::path store_path =
        boost::filesystem::path(webview_user_data_dir()) / "plugin" / "permissions.json";
    boost::system::error_code ec;
    boost::filesystem::create_directories(store_path.parent_path(), ec);
    m_plugin_store_path = store_path.string();

    orca::plugin::HostEnv env;
    env.host_version          = SLIC3R_VERSION;
    env.dispatcher            = [](std::function<void()> fn) { wxTheApp->CallAfter(fn); };
    env.browser_data_dir      = webview_user_data_dir();   // same shared WebView2 folder
    env.permission_store_path = m_plugin_store_path;
    m_plugin_host             = std::make_unique<orca::plugin::PluginHost>(env);

    // Host API surface offered to plugin sessions.
    orca::jb::ApiDesc echo;
    echo.sync = [](const orca::jb::Json& params) { return orca::jb::ApiOutcome::ok_value(params); };
    m_plugin_host->register_api("demo.echo", echo);

    orca::jb::ApiDesc locked;
    locked.permission = "demo.special";
    locked.sync       = [](const orca::jb::Json&)
    {
        return orca::jb::ApiOutcome::ok_value(orca::jb::Json{{"granted", true}});
    };
    m_plugin_host->register_api("demo.permissionProtected", locked);
}

void WebViewDevDialog::prompt_plugin_permissions(const orca::plugin::PluginManifest& manifest)
{
    orca::plugin::PermissionStore store(m_plugin_store_path);
    if (!store.load().ok())
        return;

    bool changed = false;
    for (const std::string& tag : manifest.permissions)
    {
        if (store.query(manifest.id, tag) != orca::plugin::PermissionState::Unknown)
            continue;

        wxMessageDialog dlg(this,
            wxString::Format(_L("Plugin \"%s\" requests the permission:\n\n%s\n\nGrant it now?"),
                wxString::FromUTF8(manifest.name.c_str()), wxString::FromUTF8(tag.c_str())),
            _L("Plugin permission request"), wxYES_NO | wxICON_QUESTION);
        if (dlg.ShowModal() == wxID_YES)
        {
            store.grant(manifest.id, tag);
            changed = true;
        }
    }
    if (changed)
        store.save();
}

void WebViewDevDialog::report_failure(const orca::wv::Status& status)
{
    show_status_error(this, status);
    // Close asynchronously: this can fire from inside the constructor, where
    // a synchronous Close() is not safe.
    wxTheApp->CallAfter([this]() { Close(true); });
}

void WebViewDevDialog::on_load_plugin(wxCommandEvent& /*event*/)
{
    if (m_plugin_session)
        return;

    ensure_plugin_host();

    const boost::filesystem::path manifest_path =
        boost::filesystem::path(Slic3r::resources_dir()) / "web" / "orca_sdk_test" / "demo_plugin" / "plugin.json";

    // Parse first: the permission prompt needs the plugin id and tags.
    const auto manifest = orca::plugin::PluginManifest::load_from_file(manifest_path.string());
    if (!manifest.status.ok())
    {
        show_status_error(this, manifest.status);
        return;
    }
    prompt_plugin_permissions(manifest.value);

    // The plugin webview shares the host panel with the dev view; hide the
    // dev view while the plugin is up and restore it on unload.
    if (m_view)
        m_view->set_visible(false);

    orca::wv::Status status;
    m_plugin_session = m_plugin_host->load(manifest_path.string(), m_host_panel->GetHandle(), status);
    if (!m_plugin_session)
    {
        if (m_view)
            m_view->set_visible(true);
        show_status_error(this, status);
        return;
    }

    m_load_plugin_btn->Enable(false);
    m_unload_plugin_btn->Enable(true);
}

void WebViewDevDialog::on_unload_plugin(wxCommandEvent& /*event*/)
{
    if (!m_plugin_session)
        return;

    const std::string      plugin_id = m_plugin_session->id();
    const orca::wv::Status status    = m_plugin_host->unload(plugin_id);
    m_plugin_session.reset();
    if (!status.ok())
        show_status_error(this, status);

    if (m_view)
    {
        m_view->set_visible(true);
        apply_view_bounds();
    }
    m_load_plugin_btn->Enable(true);
    m_unload_plugin_btn->Enable(false);
}

void WebViewDevDialog::on_size(wxSizeEvent& event)
{
    event.Skip();
    apply_view_bounds();
}

void WebViewDevDialog::on_timer(wxTimerEvent& /*event*/)
{
    if (!m_bridge || !m_bridge->attached())
        return;

    ++m_tick_count;
    m_bridge->emit("demo.tick", orca::jb::Json{{"n", m_tick_count}});
}

void WebViewDevDialog::on_close(wxCloseEvent& event)
{
    // Teardown order: stop producing traffic, detach the bridge, then release
    // the engine while the native parent HWND is still alive.
    if (m_bridge)
        m_bridge->detach();
    if (m_plugin_session)
    {
        m_plugin_host->unload(m_plugin_session->id());
        m_plugin_session.reset();
    }
    if (m_view)
        m_view->close();
    m_tick_timer.Stop();
    event.Skip();

    // Modeless singleton: destroy once the close event has been processed.
    wxTheApp->CallAfter([this]()
    {
        if (s_dev_dialog == this)
            s_dev_dialog = nullptr;
        Destroy();
    });
}

void show_webview_dev_dialog(wxWindow* parent)
{
    // Session-scoped developer switch: MainFrame resets this key on every
    // startup and only the Preferences dialog turns it on at runtime, so the
    // gate has to be evaluated here, not at menu build time.
    if (wxGetApp().app_config->get("internal_developer_mode") != "true")
        return;

    if (s_dev_dialog != nullptr)
    {
        s_dev_dialog->Raise();
        return;
    }

    s_dev_dialog = new WebViewDevDialog(parent);
    s_dev_dialog->Show();
}

} // GUI
} // Slic3r
