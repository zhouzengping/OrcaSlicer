#include "orca/plugin/manifest.hpp"

#include "semver.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
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

wv::Status invalid_argument(std::string message)
{
    return wv::Status::make(wv::Errc::InvalidArgument, std::move(message));
}

// ^[a-z0-9][a-z0-9.-]{2,63}$: total length 3..64, lowercase letter or digit
// first, then only [a-z0-9.-].
bool is_valid_plugin_id(const std::string& id)
{
    if (id.size() < 3 || id.size() > 64)
    {
        return false;
    }

    const char first = id[0];
    if (!((first >= 'a' && first <= 'z') || (first >= '0' && first <= '9')))
    {
        return false;
    }

    for (size_t i = 1; i < id.size(); ++i)
    {
        const char c = id[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-'))
        {
            return false;
        }
    }
    return true;
}

// Permission tags: two or more non-empty [a-z0-9_]+ segments, e.g. "device.read".
bool is_valid_permission_tag(const std::string& tag)
{
    size_t segment_length = 0;
    size_t segment_count  = 0;
    for (const char c : tag)
    {
        if (c == '.')
        {
            if (segment_length == 0)
            {
                return false;   // empty segment, e.g. ".a" or "a..b"
            }
            segment_length = 0;
            ++segment_count;
            continue;
        }
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'))
        {
            return false;
        }
        ++segment_length;
    }

    if (segment_length == 0)
    {
        return false;   // empty tag or trailing dot
    }
    ++segment_count;
    return segment_count >= 2;
}

// Relative in-tree path rules shared by entry and icon: no "..", no leading
// '/', no backslashes.
wv::Status check_relative_path(const char* field, const std::string& value)
{
    if (value.empty())
    {
        return invalid_argument(std::string(field) + " must not be empty");
    }
    if (value.find("..") != std::string::npos)
    {
        return invalid_argument(std::string(field) + " must not contain '..'");
    }
    if (value[0] == '/')
    {
        return invalid_argument(std::string(field) + " must be a relative path");
    }
    if (value.find('\\') != std::string::npos)
    {
        return invalid_argument(std::string(field) + " must not contain backslashes");
    }
    return wv::Status::make(wv::Errc::Ok);
}

// True when child is strictly below root (component-prefix match on two
// canonical paths; a path equal to the root itself does not count).
bool is_under_root(const fs::path& child, const fs::path& root)
{
    auto child_part = child.begin();
    auto root_part  = root.begin();
    for (; root_part != root.end(); ++root_part, ++child_part)
    {
        if (child_part == child.end() || *child_part != *root_part)
        {
            return false;
        }
    }
    return child_part != child.end();
}

// rel must resolve (after symlink/.. resolution via canonical) to an existing
// regular file strictly below root_canonical.
wv::Status check_file_under_root(const fs::path& root_canonical, const std::string& rel, const char* field)
{
    std::error_code ec;
    const fs::path resolved = fs::canonical(root_canonical / utf8_to_path(rel), ec);
    if (ec)
    {
        return invalid_argument(std::string(field) + " not found");
    }
    if (!is_under_root(resolved, root_canonical))
    {
        return invalid_argument(std::string(field) + " escapes the plugin root");
    }
    if (!fs::is_regular_file(resolved, ec))
    {
        return invalid_argument(std::string(field) + " is not a regular file");
    }
    return wv::Status::make(wv::Errc::Ok);
}

} // namespace

Result<PluginManifest> PluginManifest::load_from_file(const std::string& json_path)
{
    Result<PluginManifest> result;

    std::ifstream file(utf8_to_path(json_path), std::ios::binary);
    if (!file)
    {
        result.status = invalid_argument("manifest file not readable");
        return result;
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    if (file.bad())
    {
        result.status = invalid_argument("manifest file not readable");
        return result;
    }

    // Exception-free parse: violations surface as a discarded value.
    Json parsed = Json::parse(buffer.str(), nullptr, false);
    if (parsed.is_discarded())
    {
        result.status = invalid_argument("manifest is not valid JSON");
        return result;
    }

    return PluginManifest::parse(parsed);
}

Result<PluginManifest> PluginManifest::parse(const Json& json)
{
    Result<PluginManifest> result;
    PluginManifest         manifest;

    auto fail = [&result](std::string message) -> Result<PluginManifest>
    {
        result.status = invalid_argument(std::move(message));
        return result;
    };

    if (!json.is_object())
    {
        return fail("manifest must be a JSON object");
    }

    const auto manifest_version = json.find("orca_manifest");
    if (manifest_version == json.end())
    {
        return fail("missing orca_manifest version");
    }
    if (!manifest_version->is_number_integer() || manifest_version->get<int64_t>() != 1)
    {
        return fail("unsupported manifest version");
    }

    const auto id = json.find("id");
    if (id == json.end() || !id->is_string())
    {
        return fail("id must be a string");
    }
    manifest.id = id->get<std::string>();
    if (!is_valid_plugin_id(manifest.id))
    {
        return fail("id must match ^[a-z0-9][a-z0-9.-]{2,63}$");
    }

    const auto name = json.find("name");
    if (name == json.end() || !name->is_string())
    {
        return fail("name must be a string");
    }
    manifest.name = name->get<std::string>();
    if (manifest.name.empty())
    {
        return fail("name must not be empty");
    }
    // 512 UTF-8 bytes ~= 128 CJK glyphs (3 bytes each): a deterministic byte
    // cap standing in for the documented "1..128 characters".
    if (manifest.name.size() > 512)
    {
        return fail("name must not exceed 512 UTF-8 bytes");
    }

    const auto version = json.find("version");
    if (version == json.end() || !version->is_string())
    {
        return fail("version must be a string");
    }
    manifest.version = version->get<std::string>();
    {
        Semver parsed_version;
        if (!parse_semver(manifest.version, parsed_version))
        {
            return fail("version must be MAJOR.MINOR.PATCH");
        }
    }

    const auto entry = json.find("entry");
    if (entry != json.end())
    {
        if (!entry->is_string())
        {
            return fail("entry must be a string");
        }
        manifest.entry = entry->get<std::string>();
        wv::Status entry_status = check_relative_path("entry", manifest.entry);
        if (!entry_status.ok())
        {
            return fail(entry_status.message);
        }
    }

    const auto permissions = json.find("permissions");
    if (permissions != json.end())
    {
        if (!permissions->is_array())
        {
            return fail("permissions must be an array of strings");
        }
        for (const auto& permission : *permissions)
        {
            if (!permission.is_string())
            {
                return fail("permissions must be an array of strings");
            }
            const std::string tag = permission.get<std::string>();
            if (!is_valid_permission_tag(tag))
            {
                return fail("permission '" + tag + "' must be a lowercase dotted tag, e.g. device.read");
            }
            if (std::find(manifest.permissions.begin(), manifest.permissions.end(), tag) != manifest.permissions.end())
            {
                return fail("duplicate permission '" + tag + "'");
            }
            manifest.permissions.push_back(tag);
        }
    }

    const auto min_host_version = json.find("min_host_version");
    if (min_host_version != json.end())
    {
        if (!min_host_version->is_string())
        {
            return fail("min_host_version must be a string");
        }
        manifest.min_host_version = min_host_version->get<std::string>();
        Semver parsed_min_host;
        if (!parse_semver(manifest.min_host_version, parsed_min_host))
        {
            return fail("min_host_version must be MAJOR.MINOR.PATCH");
        }
    }

    const auto description = json.find("description");
    if (description != json.end())
    {
        if (!description->is_string())
        {
            return fail("description must be a string");
        }
        manifest.description = description->get<std::string>();
    }

    const auto icon = json.find("icon");
    if (icon != json.end())
    {
        if (!icon->is_string())
        {
            return fail("icon must be a string");
        }
        manifest.icon = icon->get<std::string>();
        if (!manifest.icon.empty())
        {
            // An explicitly empty icon is treated as "no icon".
            wv::Status icon_status = check_relative_path("icon", manifest.icon);
            if (!icon_status.ok())
            {
                return fail(icon_status.message);
            }
        }
    }

    // Unknown fields are ignored on purpose (forward compatibility).

    result.value = std::move(manifest);
    return result;
}

wv::Status PluginManifest::validate_against_dir(const std::string& plugin_root) const
{
    // All filesystem access goes through error_code overloads; nothing throws.
    std::error_code ec;
    const fs::path root_canonical = fs::canonical(utf8_to_path(plugin_root), ec);
    if (ec)
    {
        return invalid_argument("plugin root not found");
    }

    wv::Status entry_status = check_file_under_root(root_canonical, entry, "entry");
    if (!entry_status.ok())
    {
        return entry_status;
    }

    if (!icon.empty())
    {
        wv::Status icon_status = check_file_under_root(root_canonical, icon, "icon");
        if (!icon_status.ok())
        {
            return icon_status;
        }
    }

    return wv::Status::make(wv::Errc::Ok);
}

} // namespace orca::plugin
