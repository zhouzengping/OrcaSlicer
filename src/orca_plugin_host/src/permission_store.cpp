#include "orca/plugin/permission_store.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <system_error>
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

using GrantTable = std::map<std::string, std::map<std::string, PermissionState>>;

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

wv::Status platform_failure(std::string message)
{
    return wv::Status::make(wv::Errc::PlatformFailure, std::move(message));
}

} // namespace

struct PermissionStore::Impl
{
    std::string store_path;

    // plugin_id -> permission tag -> state; map order keeps saves deterministic.
    GrantTable grants;

    // Mutators mark it dirty, save()/load() clear it. Persistence stays
    // caller-driven: mutators never write the file on their own.
    bool dirty = false;
};

PermissionStore::PermissionStore(std::string store_path)
    : m_impl(std::make_shared<Impl>())
{
    m_impl->store_path = std::move(store_path);
}

wv::Status PermissionStore::load()
{
    Impl& impl = *m_impl;
    impl.grants.clear();
    impl.dirty = false;

    const fs::path path = utf8_to_path(impl.store_path);

    std::error_code exists_ec;
    if (!fs::exists(path, exists_ec))
    {
        return wv::Status::make(wv::Errc::Ok);   // no store file yet -> empty store
    }

    // Corrupt-store self-heal: park the broken file next to the store and
    // restart empty. Renaming may fail (locked file); that is ignored, this
    // is never a caller-visible error.
    auto corrupt = [&impl, &path]() -> wv::Status
    {
        fs::path bad = path;
        bad += ".bad";
        std::error_code rename_ec;
        fs::rename(path, bad, rename_ec);
        impl.grants.clear();
        return wv::Status::make(wv::Errc::Ok);
    };

    std::string text;
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
        {
            return corrupt();
        }
        std::ostringstream buffer;
        buffer << file.rdbuf();
        if (file.bad())
        {
            return corrupt();
        }
        text = buffer.str();
    }

    // Exception-free parse: violations surface as a discarded value.
    nlohmann::json parsed = nlohmann::json::parse(text, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object())
    {
        return corrupt();
    }

    const auto version = parsed.find("version");
    if (version == parsed.end() || !version->is_number_integer() || version->get<int64_t>() != 1)
    {
        return corrupt();
    }

    const auto grants = parsed.find("grants");
    if (grants != parsed.end())
    {
        if (!grants->is_object())
        {
            return corrupt();
        }
        for (auto plugin = grants->begin(); plugin != grants->end(); ++plugin)
        {
            if (!plugin->is_object())
            {
                continue;   // malformed plugin entry: skip
            }
            for (auto permission = plugin->begin(); permission != plugin->end(); ++permission)
            {
                const auto state = permission->find("state");
                if (!permission->is_object() || state == permission->end() || !state->is_string())
                {
                    continue;   // malformed permission entry: skip
                }
                const std::string& value = state->get_ref<const std::string&>();
                if (value == "granted")
                {
                    impl.grants[plugin.key()][permission.key()] = PermissionState::Granted;
                }
                else if (value == "denied")
                {
                    impl.grants[plugin.key()][permission.key()] = PermissionState::Denied;
                }
                // any other state string -> Unknown -> skipped
            }
        }
    }

    return wv::Status::make(wv::Errc::Ok);
}

wv::Status PermissionStore::save()
{
    Impl& impl = *m_impl;

    nlohmann::json document;
    document["version"] = 1;

    nlohmann::json grants = nlohmann::json::object();
    for (const auto& plugin : impl.grants)
    {
        nlohmann::json entry    = nlohmann::json::object();
        bool           written  = false;
        for (const auto& permission : plugin.second)
        {
            // Only durable decisions are persisted; Unknown stays implicit.
            if (permission.second == PermissionState::Granted)
            {
                entry[permission.first] = nlohmann::json{{"state", "granted"}};
                written                 = true;
            }
            else if (permission.second == PermissionState::Denied)
            {
                entry[permission.first] = nlohmann::json{{"state", "denied"}};
                written                 = true;
            }
        }
        if (written)
        {
            grants[plugin.first] = std::move(entry);
        }
    }
    document["grants"] = std::move(grants);

    const std::string text = document.dump(2);

    const fs::path path = utf8_to_path(impl.store_path);
    fs::path       tmp  = path;
    tmp += ".tmp";

    {
        std::ofstream file(tmp, std::ios::binary | std::ios::trunc);
        if (!file)
        {
            return platform_failure("cannot open permission store temp file for writing");
        }
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        file.flush();
        if (!file)
        {
            return platform_failure("failed writing permission store temp file");
        }
    }

    // Atomic publish. MSVC's rename already replaces an existing target; the
    // remove-then-retry fallback covers backends that refuse overwriting.
    std::error_code rename_ec;
    fs::rename(tmp, path, rename_ec);
    if (rename_ec)
    {
        std::error_code remove_ec;
        fs::remove(path, remove_ec);
        fs::rename(tmp, path, rename_ec);
        if (rename_ec)
        {
            return platform_failure("cannot publish permission store: " + rename_ec.message());
        }
    }

    impl.dirty = false;
    return wv::Status::make(wv::Errc::Ok);
}

PermissionState PermissionStore::query(const std::string& plugin_id, const std::string& permission) const
{
    const Impl& impl   = *m_impl;
    const auto  plugin = impl.grants.find(plugin_id);
    if (plugin == impl.grants.end())
    {
        return PermissionState::Unknown;
    }
    const auto state = plugin->second.find(permission);
    if (state == plugin->second.end())
    {
        return PermissionState::Unknown;
    }
    return state->second;
}

wv::Status PermissionStore::grant(const std::string& plugin_id, const std::string& permission)
{
    if (plugin_id.empty() || permission.empty())
    {
        return invalid_argument("plugin id and permission must not be empty");
    }
    Impl& impl = *m_impl;
    impl.grants[plugin_id][permission] = PermissionState::Granted;
    impl.dirty                         = true;
    return wv::Status::make(wv::Errc::Ok);
}

wv::Status PermissionStore::revoke(const std::string& plugin_id, const std::string& permission)
{
    if (plugin_id.empty() || permission.empty())
    {
        return invalid_argument("plugin id and permission must not be empty");
    }
    // Revoke records an explicit denial so the host stops re-asking;
    // revoke_all is the "forget everything about this plugin" operation.
    Impl& impl = *m_impl;
    impl.grants[plugin_id][permission] = PermissionState::Denied;
    impl.dirty                         = true;
    return wv::Status::make(wv::Errc::Ok);
}

wv::Status PermissionStore::revoke_all(const std::string& plugin_id)
{
    if (plugin_id.empty())
    {
        return invalid_argument("plugin id must not be empty");
    }
    Impl& impl = *m_impl;
    impl.grants.erase(plugin_id);
    impl.dirty = true;
    return wv::Status::make(wv::Errc::Ok);
}

std::vector<std::string> PermissionStore::granted_permissions(const std::string& plugin_id) const
{
    const Impl& impl = *m_impl;

    std::vector<std::string> granted;
    const auto               plugin = impl.grants.find(plugin_id);
    if (plugin == impl.grants.end())
    {
        return granted;
    }

    granted.reserve(plugin->second.size());
    for (const auto& permission : plugin->second)
    {
        if (permission.second == PermissionState::Granted)
        {
            granted.push_back(permission.first);   // map iteration -> sorted output
        }
    }
    return granted;
}

} // namespace orca::plugin
