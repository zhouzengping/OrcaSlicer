#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "orca/plugin/export.h"
#include "orca/webview/types.hpp"

namespace orca::plugin {

using Json = nlohmann::json;

/*
 * Value-or-error wrapper, exception-free like wv::Status.
 * operator bool() reports success; on failure status carries the reason.
 */
template <typename T>
struct Result
{
    T          value{};
    wv::Status status;

    explicit operator bool() const { return status.ok(); }
};

/*
 * Parsed plugin.json of a locally installed plugin.
 * Flow: the marketplace (later phase) unpacks a plugin archive into
 * <plugins_root>/<plugin_id>/ and the host loads it from that manifest path.
 */
struct ORCA_PLUGIN_API PluginManifest
{
    int         manifest_version = 1;
    std::string id;                // "com.snapmaker.demo", ^[a-z0-9][a-z0-9.-]{2,63}$
    std::string name;              // 1..128 UTF-8 chars
    std::string version;           // semver MAJOR.MINOR.PATCH
    std::string entry = "index.html";   // relative path; no "..", no leading "/", no "\"
    std::vector<std::string> permissions;   // lowercase dotted tags, unique
    std::string min_host_version;  // optional semver
    std::string description;       // optional
    std::string icon;              // optional relative path

    /*
     * Read and parse a manifest file.
     * Fails with InvalidArgument on missing file, bad JSON or schema violation.
     */
    static Result<PluginManifest> load_from_file(const std::string& json_path);

    /*
     * Validate an already-parsed JSON object against the schema (manifest
     * version 1 only; unknown versions are rejected).
     */
    static Result<PluginManifest> parse(const Json& json);

    /*
     * Check entry/icon exist below plugin_root and no path escapes it.
     */
    wv::Status validate_against_dir(const std::string& plugin_root) const;
};

} // namespace orca::plugin
