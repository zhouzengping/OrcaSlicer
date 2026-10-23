#include "backend_gtk.h"

#include <webkit2/webkit2.h>

#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace orca::wv::detail {
namespace {

// Shared closed flag guarding async script callbacks (UI thread only).
struct SharedState
{
    bool closed = false;
};

// Per-scheme registration payload owned by the backend, deleted in close()
// after the webview and its context are gone.
struct SchemeRoot
{
    std::shared_ptr<SharedState> state;
    std::string                  root;
};

struct ScriptCallbackData
{
    std::shared_ptr<SharedState> state;
    ScriptDoneFn                 callback;
};

// One-shot idle payload for the deferred report_created (see BackendGtk::start).
struct CreatedIdleData
{
    std::shared_ptr<SharedState> state;
    EventProxy*                  proxy;
};

gboolean on_created_idle(gpointer user_data)
{
    std::unique_ptr<CreatedIdleData> data(static_cast<CreatedIdleData*>(user_data));
    // The closed flag guards the raw proxy pointer: close() flips it on the
    // same (UI) thread before this backend, and with it the proxy's owner,
    // can be destroyed.
    if (data->state->closed)
        return G_SOURCE_REMOVE;
    data->proxy->report_created();
    return G_SOURCE_REMOVE;
}

struct MimeEntry
{
    const char* ext;
    const char* mime;
};

constexpr MimeEntry g_mime_table[] = {
    {"html", "text/html"}, {"htm", "text/html"},          {"js", "application/javascript"},
    {"css", "text/css"},   {"png", "image/png"},          {"svg", "image/svg+xml"},
    {"json", "application/json"}, {"gif", "image/gif"},   {"ico", "image/x-icon"},
    {"wasm", "application/wasm"},
};

std::string mime_for_path(const std::string& path)
{
    size_t dot = path.rfind('.');
    if (dot == std::string::npos)
        return "application/octet-stream";
    std::string ext = path.substr(dot + 1);
    for (const MimeEntry& entry : g_mime_table)
    {
        if (ext == entry.ext)
            return entry.mime;
    }
    return "application/octet-stream";
}

GError* make_scheme_error(int code, const char* message)
{
    return g_error_new(g_quark_from_static_string("orca-webview"), code, "%s", message);
}

// Reads the file below the scheme root and finishes the request. Only files
// inside root are reachable; traversal is rejected.
void serve_scheme_request(WebKitURISchemeRequest* request, const std::string& root)
{
    const gchar* path = webkit_uri_scheme_request_get_path(request);
    std::string relative = (path != nullptr && path[0] == '/') ? path + 1 : (path != nullptr ? path : "");
    if (relative.find("..") != std::string::npos)
    {
        webkit_uri_scheme_request_finish_error(request, make_scheme_error(403, "path traversal rejected"));
        return;
    }

    std::string full = root + "/" + relative;
    std::ifstream file(full, std::ios::binary);
    if (!file)
    {
        webkit_uri_scheme_request_finish_error(request, make_scheme_error(404, "file not found"));
        return;
    }
    std::vector<char> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    gpointer buffer = g_malloc(data.size());
    if (buffer == nullptr)
    {
        webkit_uri_scheme_request_finish_error(request, make_scheme_error(500, "out of memory"));
        return;
    }
    std::memcpy(buffer, data.data(), data.size());

    // finish() takes ownership of the stream.
    GInputStream* stream = g_memory_input_stream_new_from_data(buffer, static_cast<gssize>(data.size()), g_free);
    webkit_uri_scheme_request_finish(request, stream, static_cast<gint64>(data.size()), mime_for_path(full).c_str());
}

void on_scheme_request(WebKitURISchemeRequest* request, gpointer user_data)
{
    SchemeRoot* scheme = static_cast<SchemeRoot*>(user_data);
    if (scheme->state->closed)
    {
        webkit_uri_scheme_request_finish_error(request, make_scheme_error(500, "webview is closed"));
        return;
    }
    serve_scheme_request(request, scheme->root);
}

class BackendGtk final : public Backend
{
public:
    BackendGtk(void* native_parent, const WebView::Config& config, EventProxy& proxy)
        : m_parent(static_cast<GtkWidget*>(native_parent))
        , m_config(config)
        , m_proxy(proxy)
        , m_state(std::make_shared<SharedState>())
    {
    }

    ~BackendGtk() override
    {
        close();
    }

    Status start() override
    {
        GtkWidget* parent = static_cast<GtkWidget*>(m_parent);
        if (!GTK_IS_CONTAINER(parent))
            return Status::make(Errc::ParentInvalid, "native parent is not a GTK container");

        m_user_content = webkit_user_content_manager_new();
        webkit_user_content_manager_register_script_message_handler(m_user_content, "orca");
        g_signal_connect(m_user_content, "script-message-received::orca", G_CALLBACK(on_script_message), this);

        // Per-view context; schemes must be registered before any load happens.
        m_context = m_config.incognito ? webkit_web_context_new_ephemeral() : webkit_web_context_new();
        for (const CustomScheme& scheme : m_config.custom_schemes)
        {
            SchemeRoot* root = new SchemeRoot{m_state, scheme.root_folder};
            m_scheme_roots.push_back(root);
            webkit_web_context_register_uri_scheme(m_context, scheme.scheme.c_str(), on_scheme_request, root, nullptr);
        }

        m_webview = GTK_WIDGET(g_object_new(WEBKIT_TYPE_WEB_VIEW, "web-context", m_context,
            "user-content-manager", m_user_content, nullptr));
        g_object_ref_sink(m_webview);

        // wxWindow GetHandle() is a GtkWidget*; wxPanel-class parents are
        // GtkFixed subclasses, which is the placement this backend supports.
        if (GTK_IS_FIXED(parent))
            gtk_fixed_put(GTK_FIXED(parent), m_webview, 0, 0);
        else
            gtk_container_add(GTK_CONTAINER(parent), m_webview);
        gtk_widget_show(m_webview);

        WebKitSettings* settings = webkit_web_view_get_settings(WEBKIT_WEB_VIEW(m_webview));
        webkit_settings_set_enable_developer_extras(settings, m_config.devtools);
        if (!m_config.user_agent.empty())
            webkit_settings_set_user_agent(settings, m_config.user_agent.c_str());
        if (!m_config.context_menu)
            g_signal_connect(m_webview, "context-menu", G_CALLBACK(on_context_menu), nullptr);

        g_signal_connect(m_webview, "notify::uri", G_CALLBACK(on_notify_uri), this);
        g_signal_connect(m_webview, "notify::title", G_CALLBACK(on_notify_title), this);
        g_signal_connect(m_webview, "load-changed", G_CALLBACK(on_load_changed), this);
        g_signal_connect(m_webview, "decide-policy", G_CALLBACK(on_decide_policy), this);
        g_signal_connect(m_webview, "create", G_CALLBACK(on_create), this);

        // Defer report_created until start() has returned: hosts wire
        // events().on_created only after WebView::create comes back, so a
        // synchronous report would always fire into an empty slot (Windows
        // already reports from the async controller completion). The idle
        // runs on the UI thread and fires once.
        CreatedIdleData* data = new CreatedIdleData{m_state, &m_proxy};
        g_idle_add(on_created_idle, data);
        return Status::make(Errc::Ok);
    }

    Status navigate(const std::string& url) override
    {
        if (m_webview == nullptr)
            return Status::make(Errc::InvalidState, "webview is not ready");
        webkit_web_view_load_uri(WEBKIT_WEB_VIEW(m_webview), url.c_str());
        return Status::make(Errc::Ok);
    }

    Status load_html(const std::string& html, const std::string& base_url) override
    {
        if (m_webview == nullptr)
            return Status::make(Errc::InvalidState, "webview is not ready");
        webkit_web_view_load_html(WEBKIT_WEB_VIEW(m_webview), html.c_str(),
            base_url.empty() ? nullptr : base_url.c_str());
        return Status::make(Errc::Ok);
    }

    Status reload() override
    {
        if (m_webview == nullptr)
            return Status::make(Errc::InvalidState, "webview is not ready");
        webkit_web_view_reload(WEBKIT_WEB_VIEW(m_webview));
        return Status::make(Errc::Ok);
    }

    Status go_back() override
    {
        if (m_webview == nullptr)
            return Status::make(Errc::InvalidState, "webview is not ready");
        webkit_web_view_go_back(WEBKIT_WEB_VIEW(m_webview));
        return Status::make(Errc::Ok);
    }

    Status go_forward() override
    {
        if (m_webview == nullptr)
            return Status::make(Errc::InvalidState, "webview is not ready");
        webkit_web_view_go_forward(WEBKIT_WEB_VIEW(m_webview));
        return Status::make(Errc::Ok);
    }

    std::string url() const override
    {
        if (m_webview == nullptr)
            return std::string();
        const gchar* uri = webkit_web_view_get_uri(WEBKIT_WEB_VIEW(m_webview));
        return uri != nullptr ? std::string(uri) : std::string();
    }

    Status set_bounds(int x, int y, int width, int height) override
    {
        if (m_webview == nullptr)
            return Status::make(Errc::InvalidState, "webview is not ready");
        gtk_widget_set_size_request(m_webview, width, height);
        GtkWidget* parent = static_cast<GtkWidget*>(m_parent);
        if (GTK_IS_FIXED(parent))
            gtk_fixed_move(GTK_FIXED(parent), m_webview, x, y);
        // Non-fixed containers allocate the child on their own.
        return Status::make(Errc::Ok);
    }

    Status set_visible(bool visible) override
    {
        if (m_webview == nullptr)
            return Status::make(Errc::InvalidState, "webview is not ready");
        gtk_widget_set_visible(m_webview, visible);
        return Status::make(Errc::Ok);
    }

    Status add_init_script(const std::string& js) override
    {
        if (m_user_content == nullptr)
            return Status::make(Errc::InvalidState, "webview is not ready");
        WebKitUserScript* script = webkit_user_script_new(js.c_str(),
            WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES, WEBKIT_USER_SCRIPT_AT_DOCUMENT_START, nullptr, nullptr);
        webkit_user_content_manager_add_script(m_user_content, script);
        webkit_user_script_unref(script);
        return Status::make(Errc::Ok);
    }

    Status run_script(const std::string& js, ScriptDoneFn cb) override
    {
        if (m_webview == nullptr)
            return Status::make(Errc::InvalidState, "webview is not ready");

        ScriptCallbackData* data = new ScriptCallbackData{m_state, std::move(cb)};
#if WEBKIT_CHECK_VERSION(2, 40, 0)
        webkit_web_view_evaluate_javascript(WEBKIT_WEB_VIEW(m_webview), js.c_str(), -1, nullptr, nullptr,
            on_script_finished, data);
#else
        webkit_web_view_run_javascript(WEBKIT_WEB_VIEW(m_webview), js.c_str(), nullptr, on_script_finished, data);
#endif
        return Status::make(Errc::Ok);
    }

    Status post_message_json(const std::string& json_text) override
    {
        if (m_webview == nullptr)
            return Status::make(Errc::InvalidState, "webview is not ready");
        std::string script = "window.__orcaSdk && window.__orcaSdk._recv(" + json_text + ");";
#if WEBKIT_CHECK_VERSION(2, 40, 0)
        webkit_web_view_evaluate_javascript(WEBKIT_WEB_VIEW(m_webview), script.c_str(), -1, nullptr, nullptr,
            nullptr, nullptr);
#else
        webkit_web_view_run_javascript(WEBKIT_WEB_VIEW(m_webview), script.c_str(), nullptr, nullptr, nullptr);
#endif
        return Status::make(Errc::Ok);
    }

    Status close() override
    {
        if (m_closed)
            return Status::make(Errc::Ok);

        m_closed = true;
        m_state->closed = true;

        if (m_webview != nullptr)
        {
            g_signal_handlers_disconnect_by_data(m_webview, this);
            g_signal_handlers_disconnect_by_data(m_user_content, this);
            gtk_widget_destroy(m_webview);
            g_object_unref(m_webview); // drop the ref taken at creation
            m_webview = nullptr;
        }
        if (m_user_content != nullptr)
        {
            g_object_unref(m_user_content);
            m_user_content = nullptr;
        }
        if (m_context != nullptr)
        {
            g_object_unref(m_context);
            m_context = nullptr;
        }
        for (SchemeRoot* root : m_scheme_roots)
            delete root;
        m_scheme_roots.clear();
        return Status::make(Errc::Ok);
    }

    void* native_handle() const override
    {
        return m_webview;
    }

private:
    // ---- GTK signal callbacks (UI thread; disconnected in close()) ----

    static void on_script_message(WebKitUserContentManager* /*manager*/, WebKitJavascriptResult* result,
        gpointer user_data)
    {
        BackendGtk* backend = static_cast<BackendGtk*>(user_data);
        JSCValue* value = webkit_javascript_result_get_js_value(result);
        gchar* text = jsc_value_to_string(value);
        backend->m_proxy.report_message(text != nullptr ? text : "");
        g_free(text);
    }

    static gboolean on_context_menu(WebKitWebView* /*view*/, WebKitContextMenu* /*menu*/,
        GdkEvent* /*event*/, WebKitHitTestResult* /*hit_test_result*/, gboolean /*triggered_with_keyboard*/,
        gpointer /*user_data*/)
    {
        return TRUE; // block the default context menu
    }

    static void on_notify_uri(GObject* object, GParamSpec* /*param*/, gpointer user_data)
    {
        BackendGtk* backend = static_cast<BackendGtk*>(user_data);
        const gchar* uri = webkit_web_view_get_uri(WEBKIT_WEB_VIEW(object));
        backend->m_proxy.report_url_changed(uri != nullptr ? uri : "");
    }

    static void on_notify_title(GObject* object, GParamSpec* /*param*/, gpointer user_data)
    {
        BackendGtk* backend = static_cast<BackendGtk*>(user_data);
        const gchar* title = webkit_web_view_get_title(WEBKIT_WEB_VIEW(object));
        backend->m_proxy.report_title(title != nullptr ? title : "");
    }

    static void on_load_changed(WebKitWebView* view, WebKitLoadEvent load_event, gpointer user_data)
    {
        if (load_event != WEBKIT_LOAD_FINISHED)
            return;
        BackendGtk* backend = static_cast<BackendGtk*>(user_data);
        const gchar* uri = webkit_web_view_get_uri(view);
        NavigationState navigation;
        navigation.url = uri != nullptr ? uri : "";
        navigation.is_main_frame = true;
        backend->m_proxy.report_document_loaded(navigation);
    }

    static gboolean on_decide_policy(WebKitWebView* /*view*/, WebKitPolicyDecision* decision,
        WebKitPolicyDecisionType decision_type, gpointer user_data)
    {
        if (decision_type != WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION)
            return FALSE; // let WebKit decide new-window and response policies

        BackendGtk* backend = static_cast<BackendGtk*>(user_data);
        WebKitNavigationAction* action =
            webkit_navigation_policy_decision_get_navigation_action(WEBKIT_NAVIGATION_POLICY_DECISION(decision));
        const gchar* uri = webkit_uri_request_get_uri(webkit_navigation_action_get_request(action));

        NavigationState navigation;
        navigation.url = uri != nullptr ? uri : "";
        navigation.is_main_frame = true;
        if (!backend->m_proxy.query_navigation(navigation))
        {
            webkit_policy_decision_ignore(decision);
            return TRUE;
        }
        return FALSE; // default action (use)
    }

    static GtkWidget* on_create(WebKitWebView* view, WebKitNavigationAction* action, gpointer user_data)
    {
        BackendGtk* backend = static_cast<BackendGtk*>(user_data);
        const gchar* uri = webkit_uri_request_get_uri(webkit_navigation_action_get_request(action));

        NewWindowRequest request;
        request.url = uri != nullptr ? uri : "";
        // Returning nullptr keeps WebKitGTK from opening its own popup window;
        // the host decides through on_new_window (e.g. system browser).
        if (backend->m_proxy.query_new_window(request) == OpenDecision::OpenInWebView && uri != nullptr)
            webkit_web_view_load_uri(view, uri);
        return nullptr;
    }

    static void on_script_finished(GObject* source, GAsyncResult* result, gpointer user_data)
    {
        std::unique_ptr<ScriptCallbackData> data(static_cast<ScriptCallbackData*>(user_data));
        if (!data->callback)
            return;
        if (data->state->closed)
        {
            data->callback(false, "");
            return;
        }

        GError* error = nullptr;
#if WEBKIT_CHECK_VERSION(2, 40, 0)
        JSCValue* value = webkit_web_view_evaluate_javascript_finish(WEBKIT_WEB_VIEW(source), result, &error);
        if (error != nullptr)
        {
            data->callback(false, error->message != nullptr ? error->message : "");
            g_error_free(error);
            return;
        }
        gchar* json = (value != nullptr) ? jsc_value_to_json(value, 0) : nullptr;
        data->callback(true, json != nullptr ? json : "");
        g_free(json);
        if (value != nullptr)
            jsc_value_unref(value);
#else
        WebKitJavascriptResult* js_result = webkit_web_view_run_javascript_finish(WEBKIT_WEB_VIEW(source), result, &error);
        if (error != nullptr)
        {
            data->callback(false, error->message != nullptr ? error->message : "");
            g_error_free(error);
            return;
        }
        JSCValue* value = webkit_javascript_result_get_js_value(js_result);
        gchar* json = jsc_value_to_json(value, 0);
        data->callback(true, json != nullptr ? json : "");
        g_free(json);
        webkit_javascript_result_unref(js_result);
#endif
    }

    GtkWidget*             m_parent = nullptr;
    WebView::Config        m_config;
    EventProxy&            m_proxy;
    std::shared_ptr<SharedState> m_state;

    GtkWidget*               m_webview = nullptr;
    WebKitWebContext*       m_context = nullptr;
    WebKitUserContentManager* m_user_content = nullptr;
    std::vector<SchemeRoot*> m_scheme_roots;

    bool m_closed = false;
};

} // namespace

Backend* create_backend_gtk(void* native_parent, const WebView::Config& config, EventProxy& proxy)
{
    return new BackendGtk(native_parent, config, proxy);
}

std::string platform_runtime_version()
{
    return "webkit2gtk " + std::to_string(webkit_get_major_version()) + "."
        + std::to_string(webkit_get_minor_version()) + "." + std::to_string(webkit_get_micro_version());
}

} // namespace orca::wv::detail
