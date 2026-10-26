#include "backend.h"
#include "string_util.h"

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#if defined(_WIN32)
    #include "backend_win.h"
    #include <windows.h>
#elif defined(__APPLE__)
    #include "backend_mac.h"
    #include <cstdlib>
#else
    #include "backend_gtk.h"
    #include <cstdlib>
    #include <unistd.h>
#endif

namespace orca::wv::detail {

Backend* make_backend(void* native_parent, const WebView::Config& config, EventProxy& proxy)
{
#if defined(_WIN32)
    return create_backend_win(native_parent, config, proxy);
#elif defined(__APPLE__)
    return create_backend_mac(native_parent, config, proxy);
#else
    return create_backend_gtk(native_parent, config, proxy);
#endif
}

} // namespace orca::wv::detail

namespace orca::wv {

struct WebView::Impl : public detail::EventProxy
{
    Config   config;
    Events   events;
    WebView::State state = WebView::State::Creating;

    // Init scripts added while Creating are flushed to the backend once Ready.
    std::vector<std::string> pending_init_scripts;
    std::unique_ptr<detail::Backend> backend;

    bool ready() const { return state == WebView::State::Ready && backend != nullptr; }

    void flush_pending_init_scripts()
    {
        if (backend == nullptr)
            return;

        for (const auto& js : pending_init_scripts)
            backend->add_init_script(js);
        pending_init_scripts.clear();
    }

    // ---- detail::EventProxy (UI thread) ----

    void report_created() override
    {
        state = WebView::State::Ready;
        flush_pending_init_scripts();
        if (events.on_created)
            events.on_created();
    }

    void report_error(const Status& status) override
    {
        state = WebView::State::Failed;
        if (events.on_error)
            events.on_error(status);
    }

    void report_message(std::string message) override
    {
        if (events.on_message)
            events.on_message(message);
    }

    void report_document_loaded(const NavigationState& nav) override
    {
        if (events.on_document_loaded)
            events.on_document_loaded(nav);
    }

    void report_navigation_failed(const NavigationState& nav, const Status& status) override
    {
        if (events.on_navigation_failed)
            events.on_navigation_failed(nav, status);
    }

    void report_title(std::string title) override
    {
        if (events.on_title_changed)
            events.on_title_changed(title);
    }

    void report_url_changed(std::string url) override
    {
        if (events.on_url_changed)
            events.on_url_changed(url);
    }

    bool query_navigation(const NavigationState& nav) override
    {
        if (events.on_navigation)
            return events.on_navigation(nav);
        return true;
    }

    OpenDecision query_new_window(const NewWindowRequest& request) override
    {
        if (events.on_new_window)
            return events.on_new_window(request);
        return OpenDecision::Deny;
    }
};

WebView::WebView(std::unique_ptr<Impl> impl)
    : m_impl(std::move(impl))
{
}

WebView::~WebView()
{
    if (m_impl != nullptr && m_impl->state != State::Closed)
        close();
}

std::shared_ptr<WebView> WebView::create(void* native_parent, const Config& config, Status& status)
{
    status = Status::make(Errc::Ok);

    if (native_parent == nullptr)
    {
        status = Status::make(Errc::InvalidArgument, "native parent is null");
        return nullptr;
    }

    for (const auto& scheme : config.custom_schemes)
    {
        if (scheme.scheme.empty() || scheme.root_folder.empty())
        {
            status = Status::make(Errc::InvalidArgument, "custom scheme needs both scheme and root_folder");
            return nullptr;
        }
        if (scheme.scheme.find("://") != std::string::npos || scheme.scheme.find('/') != std::string::npos)
        {
            status = Status::make(Errc::InvalidArgument, "custom scheme name must not contain ':' or '/'");
            return nullptr;
        }
    }

    auto impl = std::make_unique<Impl>();
    impl->config = config;

    // Pending init scripts from the config are registered before any page
    // script of the first navigation, so queue them before creation completes.
    impl->pending_init_scripts = config.extra_init_scripts;

    auto view = std::shared_ptr<WebView>(new WebView(std::move(impl)));

    std::unique_ptr<detail::Backend> backend(detail::make_backend(native_parent, view->m_impl->config, *view->m_impl));
    if (backend == nullptr)
    {
        status = Status::make(Errc::NotSupported, "platform backend not available in this build");
        view->m_impl->state = State::Failed;
        return nullptr;
    }

    view->m_impl->backend = std::move(backend);
    Status start_status = view->m_impl->backend->start();
    if (!start_status.ok())
    {
        // Async failures surface through on_error; this is the sync prefix only.
        status = start_status;
        return nullptr;
    }

    return view;
}

WebView::State WebView::state() const
{
    return m_impl->state;
}

WebView::Events& WebView::events()
{
    return m_impl->events;
}

Status WebView::close()
{
    if (m_impl->state == State::Closed)
        return Status::make(Errc::Ok);

    m_impl->state = State::Closed;
    if (m_impl->backend != nullptr)
    {
        Status status = m_impl->backend->close();
        m_impl->backend.reset();
        return status;
    }
    return Status::make(Errc::Ok);
}

Status WebView::set_bounds(int x, int y, int width, int height)
{
    if (!m_impl->ready())
        return Status::make(Errc::InvalidState, "webview is not ready");
    return m_impl->backend->set_bounds(x, y, width, height);
}

Status WebView::set_visible(bool visible)
{
    if (!m_impl->ready())
        return Status::make(Errc::InvalidState, "webview is not ready");
    return m_impl->backend->set_visible(visible);
}

void* WebView::native_handle() const
{
    if (m_impl->backend == nullptr)
        return nullptr;
    return m_impl->backend->native_handle();
}

Status WebView::navigate(const std::string& url)
{
    if (url.empty())
        return Status::make(Errc::InvalidArgument, "url is empty");
    if (!m_impl->ready())
        return Status::make(Errc::InvalidState, "webview is not ready");
    return m_impl->backend->navigate(url);
}

Status WebView::load_html(const std::string& html, const std::string& base_url)
{
    if (!m_impl->ready())
        return Status::make(Errc::InvalidState, "webview is not ready");
    return m_impl->backend->load_html(html, base_url);
}

Status WebView::reload()
{
    if (!m_impl->ready())
        return Status::make(Errc::InvalidState, "webview is not ready");
    return m_impl->backend->reload();
}

Status WebView::go_back()
{
    if (!m_impl->ready())
        return Status::make(Errc::InvalidState, "webview is not ready");
    return m_impl->backend->go_back();
}

Status WebView::go_forward()
{
    if (!m_impl->ready())
        return Status::make(Errc::InvalidState, "webview is not ready");
    return m_impl->backend->go_forward();
}

std::string WebView::url() const
{
    if (!m_impl->ready())
        return std::string();
    return m_impl->backend->url();
}

Status WebView::add_init_script(const std::string& js)
{
    if (js.empty())
        return Status::make(Errc::InvalidArgument, "script is empty");

    if (m_impl->ready())
        return m_impl->backend->add_init_script(js);

    // Creating / Failed / Closed: queue only while creation is still in flight.
    if (m_impl->state != State::Creating)
        return Status::make(Errc::InvalidState, "webview is not accepting init scripts");

    m_impl->pending_init_scripts.push_back(js);
    return Status::make(Errc::Ok);
}

Status WebView::run_script(const std::string& js,
                           std::function<void(bool ok, const std::string& json_or_error)> cb)
{
    if (!m_impl->ready())
        return Status::make(Errc::InvalidState, "webview is not ready");
    return m_impl->backend->run_script(js, cb);
}

Status WebView::post_message_json(const std::string& json_text)
{
    if (!m_impl->ready())
        return Status::make(Errc::InvalidState, "webview is not ready");
    return m_impl->backend->post_message_json(json_text);
}

bool WebView::runtime_available()
{
    return !runtime_version().empty();
}

std::string WebView::runtime_version()
{
    return detail::platform_runtime_version();
}

Status open_in_system_browser(const std::string& url)
{
    if (url.empty())
        return Status::make(Errc::InvalidArgument, "url is empty");

#if defined(_WIN32)
    std::wstring wide = detail::utf8_to_wide(url);
    HINSTANCE result = ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32)
        return Status::make(Errc::PlatformFailure, "ShellExecuteW failed", static_cast<int64_t>(reinterpret_cast<INT_PTR>(result)));
    return Status::make(Errc::Ok);
#elif defined(__APPLE__)
    // backend_mac.mm provides the NSWorkspace launch; the plain-C fallback
    // below only serves builds without the backend linked yet.
    if (std::system(("open " + detail::shell_quote(url)).c_str()) != 0)
        return Status::make(Errc::PlatformFailure, "open failed");
    return Status::make(Errc::Ok);
#else
    pid_t pid = fork();
    if (pid < 0)
        return Status::make(Errc::PlatformFailure, "fork failed");
    if (pid == 0)
    {
        execlp("xdg-open", "xdg-open", url.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
    return Status::make(Errc::Ok);
#endif
}

} // namespace orca::wv
