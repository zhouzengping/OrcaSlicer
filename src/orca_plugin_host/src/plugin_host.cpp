#include "orca/plugin/plugin_host.hpp"

#include "orca/plugin/manifest.hpp"
#include "orca/plugin/permission_store.hpp"
#include "semver.h"

#include <algorithm>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
#endif

namespace orca::plugin {

namespace fs = std::filesystem;

namespace {

// Public APIs pass UTF-8 paths; std::filesystem narrows through the active
// code page on Windows, so convert explicitly there.
fs::path utf8_to_path(const std::string& text)
{
#if defined(_WIN32)
    if (!text.empty())
    {
        const int wide_length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
        if (wide_length > 0)
        {
            std::wstring wide(static_cast<size_t>(wide_length), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), &wide[0], wide_length);
            return fs::path(std::move(wide));
        }
    }
    return fs::path(text);   // not valid UTF-8: fall back to the native narrow encoding
#else
    return fs::path(text);
#endif
}

std::string path_to_utf8(const fs::path& path)
{
#if defined(_WIN32)
    const std::wstring wide = path.wstring();
    if (!wide.empty())
    {
        const int utf8_length = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
        if (utf8_length > 0)
        {
            std::string utf8(static_cast<size_t>(utf8_length), '\0');
            WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), &utf8[0], utf8_length, nullptr, nullptr);
            return utf8;
        }
    }
    return std::string();
#else
    return path.string();
#endif
}

wv::Status invalid_argument(std::string message)
{
    return wv::Status::make(wv::Errc::InvalidArgument, std::move(message));
}

// Same rule as the bridge registry: ^[a-z0-9_]+\.[a-z0-9_]+$.
bool is_valid_api_method(const std::string& method)
{
    const size_t dot = method.find('.');
    if (dot == std::string::npos)
    {
        return false;
    }
    if (method.find('.', dot + 1) != std::string::npos)
    {
        return false;
    }

    auto segment_ok = [&method](size_t from, size_t to)
    {
        if (from >= to)
        {
            return false;
        }
        for (size_t i = from; i < to; ++i)
        {
            const char c = method[i];
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'))
            {
                return false;
            }
        }
        return true;
    };
    return segment_ok(0, dot) && segment_ok(dot + 1, method.size());
}

// WebView2 profile partition per plugin. Manifest ids are lowercase
// [a-z0-9.-]; '.' is the only character needing replacement.
std::string profile_name_for(const std::string& plugin_id)
{
    std::string profile = plugin_id;
    std::replace(profile.begin(), profile.end(), '.', '_');
    return "plugin_" + profile;
}

} // namespace

struct PluginSession::Impl
{
    std::string                  id;
    std::string                  base_url;
    std::shared_ptr<wv::WebView> webview;
    std::shared_ptr<jb::Bridge>  bridge;
};

PluginSession::PluginSession(std::shared_ptr<Impl> impl)
    : m_impl(std::move(impl))
{
}

const std::string& PluginSession::id() const
{
    return m_impl->id;
}

const std::string& PluginSession::base_url() const
{
    return m_impl->base_url;
}

std::shared_ptr<wv::WebView> PluginSession::webview() const
{
    return m_impl->webview;
}

std::shared_ptr<jb::Bridge> PluginSession::bridge() const
{
    return m_impl->bridge;
}

wv::Status PluginSession::close()
{
    // Bridge detach failure must not block webview teardown.
    wv::Status status = wv::Status::make(wv::Errc::Ok);
    if (m_impl->bridge != nullptr)
    {
        (void) m_impl->bridge->detach();
    }
    if (m_impl->webview != nullptr)
    {
        status = m_impl->webview->close();
    }
    return status;
}

struct PluginHost::Impl
{
    HostEnv                                          env;
    std::map<std::string, jb::ApiDesc>               host_api;
    PermissionStore                                  store;
    wv::Status                                       store_load_status;   // recorded, never fatal
    std::map<std::string, std::shared_ptr<PluginSession>> sessions;            // ordered by plugin id

    explicit Impl(HostEnv environment)
        : env(std::move(environment))
        , store(env.permission_store_path)
    {
    }

    ~Impl()
    {
        // Close every live session while we still own them. This runs on
        // whatever thread destroys the host; staying on the UI thread is the
        // embedding application's responsibility.
        for (const auto& item : sessions)
        {
            (void) item.second->close();
        }
    }
};

PluginHost::PluginHost(HostEnv env)
    : m_impl(std::make_shared<Impl>(std::move(env)))
{
    // A failed store load is only recorded, never fatal: PermissionStore::load
    // self-heals corrupt files and reports Ok for a missing one.
    m_impl->store_load_status = m_impl->store.load();
}

PermissionStore& PluginHost::permission_store() const
{
    return m_impl->store;
}

wv::Status PluginHost::register_api(const std::string& method, const jb::ApiDesc& desc)
{
    Impl& impl = *m_impl;

    if (!is_valid_api_method(method))
    {
        return invalid_argument("method must match ^[a-z0-9_]+\\.[a-z0-9_]+$: " + method);
    }
    if (method.rfind("_orca.", 0) == 0)
    {
        return invalid_argument("method is reserved: " + method);
    }

    const bool has_sync  = static_cast<bool>(desc.sync);
    const bool has_async = static_cast<bool>(desc.async);
    if (has_sync == has_async)
    {
        return invalid_argument("api '" + method + "' needs exactly one of sync/async handler");
    }

    // Re-registering overwrites the previous descriptor.
    impl.host_api[method] = desc;

    // Late-registered APIs go live for existing sessions immediately;
    // individual failures are ignored, the session keeps its current surface.
    for (const auto& item : impl.sessions)
    {
        if (item.second->bridge() != nullptr)
        {
            (void) item.second->bridge()->register_api(method, desc);
        }
    }
    return wv::Status::make(wv::Errc::Ok);
}

std::shared_ptr<PluginSession> PluginHost::load(const std::string& manifest_path, void* native_parent, wv::Status& st)
{
    st = wv::Status::make(wv::Errc::Ok);
    Impl& impl = *m_impl;

    Result<PluginManifest> manifest = PluginManifest::load_from_file(manifest_path);
    if (!manifest)
    {
        st = invalid_argument("failed to load manifest '" + manifest_path + "': " + manifest.status.message);
        return nullptr;
    }

    // The plugin lives next to its manifest.
    const fs::path    plugin_root    = utf8_to_path(manifest_path).parent_path();
    const std::string plugin_root_u8 = path_to_utf8(plugin_root);
    wv::Status        dir_status     = manifest.value.validate_against_dir(plugin_root_u8);
    if (!dir_status.ok())
    {
        st = dir_status;
        return nullptr;
    }

    if (!manifest.value.min_host_version.empty())
    {
        Semver host_version;
        if (!parse_semver(impl.env.host_version, host_version))
        {
            st = wv::Status::make(wv::Errc::InvalidState, "host_version is not valid semver: '" + impl.env.host_version + "'");
            return nullptr;
        }
        Semver min_host_version;   // schema-checked by the manifest parser already
        parse_semver(manifest.value.min_host_version, min_host_version);
        if (compare_semver(host_version, min_host_version) < 0)
        {
            // wv::Errc has no dedicated business code (HostTooOld exists only
            // as a wire-side RpcErrc), so InvalidState plus an explicit message.
            st = wv::Status::make(wv::Errc::InvalidState,
                                  "host version " + impl.env.host_version + " is older than required " + manifest.value.min_host_version);
            return nullptr;
        }
    }

    const std::string plugin_id = manifest.value.id;

    // Reload semantics: loading an installed id again replaces the old session.
    auto existing = impl.sessions.find(plugin_id);
    if (existing != impl.sessions.end())
    {
        (void) unload(plugin_id);
    }

    wv::WebView::Config view_config;
    view_config.browser_data_dir = impl.env.browser_data_dir;
    view_config.additional_browser_arguments = impl.env.additional_browser_arguments;
    view_config.profile_name     = profile_name_for(plugin_id);
    view_config.incognito        = false;
    view_config.devtools         = true;   // TODO(marketplace phase): tighten for release builds
    view_config.color_scheme     = wv::ColorScheme::Auto;
    view_config.custom_schemes.push_back(wv::CustomScheme{"orca-plugin", plugin_root_u8});
    // user_agent stays empty (engine default); extra_init_scripts stay empty:
    // the jsbridge SDK is injected by Bridge::attach.

    wv::Status                   create_status;
    std::shared_ptr<wv::WebView> view = wv::WebView::create(native_parent, view_config, create_status);
    if (view == nullptr)
    {
        st = create_status;
        return nullptr;
    }

    auto bridge = std::make_shared<jb::Bridge>();

    jb::BridgeConfig bridge_config;
    // Both platform origins are listed: Windows serves the plugin dir through
    // the "https://orca-plugin.orca" virtual host, mac/linux through the
    // "orca-plugin://<id>" custom scheme. Matching is exact, so at most one
    // ever applies, and both point at this plugin's content only.
    bridge_config.allowed_origins = {"https://orca-plugin.orca", "orca-plugin://" + plugin_id};
    bridge_config.api_timeout_ms  = 30000;
    bridge_config.dispatcher      = impl.env.dispatcher;
    // Permission chain (bridge side): origin gate -> empty tag Allow (safe
    // read-only APIs) -> this policy: Allow only when the manifest declares
    // the tag AND the user granted it. The store handle is copied by value,
    // keeping the shared grant table alive even if the host dies first.
    PermissionStore          store_copy  = impl.store;
    std::vector<std::string> declared    = manifest.value.permissions;
    bridge_config.policy = [plugin_id, declared, store_copy](
                              const jb::SessionInfo& session, const std::string& method, const std::string& tag) -> jb::PermissionDecision
    {
        (void) session;
        (void) method;
        if (tag.empty())
        {
            return jb::PermissionDecision::Allow;
        }
        const bool is_declared = std::find(declared.begin(), declared.end(), tag) != declared.end();
        if (is_declared && store_copy.query(plugin_id, tag) == PermissionState::Granted)
        {
            return jb::PermissionDecision::Allow;
        }
        return jb::PermissionDecision::Deny;
    };

    // Clone the host API surface into this session before attach.
    for (const auto& api : impl.host_api)
    {
        wv::Status register_status = bridge->register_api(api.first, api.second);
        if (!register_status.ok())
        {
            (void) view->close();
            st = register_status;
            return nullptr;
        }
    }

    jb::SessionInfo session_info;
    session_info.session_id = plugin_id;
    session_info.origin     = "";   // tracked by the bridge from webview url changes
    session_info.context    = Json{{"plugin_id", plugin_id}, {"plugin_version", manifest.value.version}};

    wv::Status attach_status = bridge->attach(view, bridge_config, session_info);
    if (!attach_status.ok())
    {
        (void) view->close();
        st = attach_status;
        return nullptr;
    }

    // Creation is asynchronous; navigate once the view reports Ready. The
    // bridge owns on_message/on_url_changed and leaves on_created to the host.
    std::string entry_url;
#if defined(_WIN32)
    // WebView2 virtual host mapping: scheme name -> host "orca-plugin.orca".
    entry_url = "https://orca-plugin.orca/" + manifest.value.entry;
#else
    // macOS/Linux custom scheme: orca-plugin://<plugin_id>/<entry>.
    entry_url = "orca-plugin://" + plugin_id + "/" + manifest.value.entry;
#endif
    std::weak_ptr<wv::WebView> view_watch = view;
    view->events().on_created             = [view_watch, entry_url]()
    {
        std::shared_ptr<wv::WebView> live = view_watch.lock();
        if (live == nullptr)
        {
            return;
        }
        (void) live->navigate(entry_url);   // failures surface via on_error / navigation events
    };

    // base_url stays platform-neutral: callers never see the Windows mapping.
    auto session_impl      = std::make_shared<PluginSession::Impl>();
    session_impl->id       = plugin_id;
    session_impl->base_url = "orca-plugin://" + plugin_id + "/";
    session_impl->webview  = view;
    session_impl->bridge   = bridge;

    std::shared_ptr<PluginSession> session(new PluginSession(session_impl));
    impl.sessions[plugin_id] = session;
    return session;
}

wv::Status PluginHost::unload(const std::string& plugin_id)
{
    Impl& impl = *m_impl;

    auto session = impl.sessions.find(plugin_id);
    if (session == impl.sessions.end())
    {
        return invalid_argument("plugin is not loaded: " + plugin_id);
    }

    wv::Status status = session->second->close();
    impl.sessions.erase(session);
    return status;
}

std::vector<std::string> PluginHost::loaded_ids() const
{
    std::vector<std::string> ids;
    ids.reserve(m_impl->sessions.size());
    for (const auto& item : m_impl->sessions)
    {
        ids.push_back(item.first);   // map iteration -> sorted by plugin id
    }
    return ids;
}

} // namespace orca::plugin
