#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "orca/jsbridge/bridge.hpp"
#include "orca/plugin/export.h"
#include "orca/webview/types.hpp"
#include "orca/webview/webview.hpp"

namespace orca::plugin {

/*
 * Host-side configuration injected by the embedding application.
 */
struct HostEnv
{
    std::string host_version;   // e.g. SLIC3R_VERSION, compared against min_host_version

    // UI-thread marshal, e.g. [](std::function<void()> fn){ wxCallAfter(fn); }.
    std::function<void(std::function<void()>)> dispatcher;

    // WINDOWS ONLY: the SHARED WebView2 user data folder (must equal the one
    // used by wxWebViewEdge, see wv::WebView::Config::browser_data_dir).
    std::string browser_data_dir;

    // e.g. <data_dir>/plugin/permissions.json; parent dir must exist.
    std::string permission_store_path;
};

/*
 * One loaded plugin: an isolated webview plus its bridge session.
 */
class ORCA_PLUGIN_API PluginSession
{
public:
    const std::string& id() const;
    const std::string& base_url() const;   // e.g. "orca-plugin://com.snapmaker.demo/"

    std::shared_ptr<wv::WebView> webview() const;
    std::shared_ptr<jb::Bridge>  bridge() const;

    // Detach bridge and close webview; normally invoked by PluginHost::unload.
    wv::Status close();

private:
    friend class PluginHost;
    struct Impl;
    explicit PluginSession(std::shared_ptr<Impl> impl);
    std::shared_ptr<Impl> m_impl;
};

/*
 * Loads local plugins and assembles their sandboxed sessions:
 * parse+validate manifest -> min_host_version check -> permission policy from
 * the PermissionStore -> isolated webview (custom scheme "orca-plugin" scoped
 * to the plugin dir; Windows: per-plugin profile partition) -> bridge attach
 * -> navigate(entry).
 */
class ORCA_PLUGIN_API PluginHost
{
public:
    explicit PluginHost(HostEnv env);

    /*
     * Host API surface offered to plugins; registered once here and cloned
     * into every session bridge at load. "_orca.*" methods are reserved.
     */
    wv::Status register_api(const std::string& method, const jb::ApiDesc& desc);

    /*
     * manifest_path: "<root>/<plugin_id>/plugin.json".
     * Returns nullptr and fills st on any failure; on success the session
     * becomes Ready asynchronously (watch webview()->events()).
     */
    std::shared_ptr<PluginSession> load(const std::string& manifest_path, void* native_parent, wv::Status& st);

    wv::Status            unload(const std::string& plugin_id);
    std::vector<std::string> loaded_ids() const;

private:
    struct Impl;
    std::shared_ptr<Impl> m_impl;
};

} // namespace orca::plugin
