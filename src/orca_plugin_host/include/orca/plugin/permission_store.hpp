#pragma once

#include <memory>
#include <string>
#include <vector>

#include "orca/plugin/export.h"
#include "orca/webview/types.hpp"

namespace orca::plugin {

enum class PermissionState : uint8_t
{
    Unknown,
    Granted,
    Denied,
};

/*
 * Persistent per-plugin permission grants, backed by one JSON file.
 * File format (version 1):
 * {
 *   "version": 1,
 *   "grants": {
 *     "com.snapmaker.demo": {
 *       "demo.special": { "state": "granted", "granted_at": "2026-10-22T08:00:00Z" }
 *     }
 *   }
 * }
 * Writes are atomic (tmp file + rename); a corrupt file is backed up as
 * "<path>.bad" and the store restarts empty.
 */
class ORCA_PLUGIN_API PermissionStore
{
public:
    /*
     * store_path: absolute path of the JSON file; the parent dir must exist.
     */
    explicit PermissionStore(std::string store_path);

    wv::Status load();   // missing file -> empty store, Ok
    wv::Status save();

    PermissionState query(const std::string& plugin_id, const std::string& permission) const;

    wv::Status grant(const std::string& plugin_id, const std::string& permission);
    wv::Status revoke(const std::string& plugin_id, const std::string& permission);
    wv::Status revoke_all(const std::string& plugin_id);

    std::vector<std::string> granted_permissions(const std::string& plugin_id) const;

private:
    struct Impl;
    std::shared_ptr<Impl> m_impl;
};

} // namespace orca::plugin
