#include "PluginManager.hpp"

#include "orca/plugin/permission_store.hpp"

#include "GUI.hpp"
#include "I18N.hpp"
#include "MainFrame.hpp"
#include "PluginTabPage.hpp"
#include "WebViewPageBase.hpp"
#include "slic3r/GUI/GUI_App.hpp"

#include "libslic3r/FilamentColorLibrary.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/miniz_extension.hpp"

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/log/trivial.hpp>

#include <wx/app.h>
#include <wx/dirdlg.h>
#include <wx/filedlg.h>
#include <wx/msgdlg.h>

#include <algorithm>
#include <ctime>
#include <functional>
#include <string>
#include <vector>

namespace Slic3r {
namespace GUI {

namespace
{

#if defined(_WIN32)
constexpr const char* g_platform = "windows";
#elif defined(__APPLE__)
constexpr const char* g_platform = "macos";
#else
constexpr const char* g_platform = "linux";
#endif

// Scratch directory used while staging an import; below the plugins root so
// the final rename stays on one volume.
constexpr const char* g_import_scratch_name = ".import-tmp";

// Zip hardening: reject archives outside these bounds before extracting.
constexpr mz_uint32 g_max_zip_entries     = 512;
constexpr mz_uint64 g_max_zip_entry_bytes = 16ull * 1024 * 1024;
constexpr mz_uint64 g_max_zip_total_bytes = 64ull * 1024 * 1024;

// Bridge and PluginHost both expose register_api; log instead of ignoring.
template <typename Target>
void register_api_checked(Target* target, const std::string& method, const orca::jb::ApiDesc& desc)
{
    const orca::wv::Status status = target->register_api(method, desc);
    if (!status.ok())
        BOOST_LOG_TRIVIAL(error) << "PluginManager: register " << method << " failed: " << status.message;
}

// Shared between the plugin center bridge and every plugin session host.
orca::jb::ApiDesc host_info_api()
{
    orca::jb::ApiDesc desc;
    desc.sync = [](const orca::jb::Json&)
    {
        // wx gives "zh_CN" style codes; the wire wants BCP 47 ("zh-CN").
        std::string locale = into_u8(wxGetApp().current_language_code_safe());
        std::replace(locale.begin(), locale.end(), '_', '-');
        return orca::jb::ApiOutcome::ok_value(orca::jb::Json{
            {"host_version", SLIC3R_VERSION},
            {"platform",     g_platform},
            {"locale",       locale}});
    };
    return desc;
}

/*
 * Filament slots of the current project (same source of truth as the design
 * filament list in Plater: slot name -> preset, colors from project_config).
 * `permission` stays empty on the first-party plugin center bridge (origin
 * check only - that bridge has no manifest, and a tagged API without an
 * allowing policy is denied by default), while the host surface cloned into
 * plugin sessions carries the tag the manifest must declare and the user
 * must grant.
 */
orca::jb::ApiDesc filament_list_api(const std::string& permission)
{
    orca::jb::ApiDesc desc;
    desc.permission = permission;
    desc.sync       = [](const orca::jb::Json&)
    {
        orca::jb::Json filaments = orca::jb::Json::array();

        const PresetBundle* bundle = wxGetApp().preset_bundle;
        if (bundle)
        {
            const std::vector<std::string>& slots = bundle->filament_presets;
            const auto* colors_opt       = bundle->project_config.option<ConfigOptionStrings>("filament_colour");
            const auto* multi_colors_opt = bundle->project_config.option<ConfigOptionStrings>("filament_multi_colors");
            const auto* color_mode_opt   = bundle->project_config.option<ConfigOptionInts>("filament_colour_mode");

            for (size_t i = 0; i < slots.size(); ++i)
            {
                const Preset* preset = bundle->filaments.find_preset(slots[i]);

                std::string type;
                std::string vendor;
                std::string filament_id;
                if (preset)
                {
                    const auto* type_opt = preset->config.option<ConfigOptionStrings>("filament_type");
                    if (type_opt && !type_opt->values.empty())
                        type = type_opt->values[0];
                    const auto* vendor_opt = preset->config.option<ConfigOptionStrings>("filament_vendor");
                    if (vendor_opt && !vendor_opt->values.empty())
                        vendor = vendor_opt->values[0];
                    filament_id = preset->filament_id;
                }
                if (type.empty())
                {
                    // No preset resolved: fall back to the project slot value.
                    const auto* type_opt = bundle->project_config.option<ConfigOptionStrings>("filament_type");
                    if (type_opt && i < type_opt->values.size())
                        type = type_opt->values[i];
                }

                int color_mode = 0;
                std::vector<std::string> colors;
                if (multi_colors_opt && i < multi_colors_opt->values.size() && !multi_colors_opt->values[i].empty())
                    colors = Slic3r::SplitFilamentMultiColors(multi_colors_opt->values[i]);
                else if (colors_opt && i < colors_opt->values.size())
                    colors = {colors_opt->values[i]};
                if (color_mode_opt && i < color_mode_opt->values.size())
                    color_mode = color_mode_opt->values[i];

                orca::jb::Json item;
                item["index"]       = i;
                item["name"]        = preset ? preset->label(false) : slots[i];
                item["preset_name"] = slots[i];
                item["type"]        = type;
                item["vendor"]      = vendor;
                item["filament_id"] = filament_id;
                item["color"]       = colors.empty() ? std::string() : colors.front();
                item["colors"]      = colors;
                item["color_mode"]  = color_mode;
                filaments.push_back(std::move(item));
            }
        }

        return orca::jb::ApiOutcome::ok_value(orca::jb::Json{
            {"count",     filaments.size()},
            {"filaments", std::move(filaments)}});
    };
    return desc;
}

// Recursive copy for the "dir" import path; symlinks and special files are
// skipped, regular files overwrite existing destinations.
bool copy_dir_recursive(const boost::filesystem::path& src, const boost::filesystem::path& dst, std::string& error)
{
    boost::system::error_code ec;
    boost::filesystem::create_directories(dst, ec);
    if (ec)
    {
        error = "cannot create directory " + dst.string() + ": " + ec.message();
        return false;
    }

    boost::system::error_code it_ec;
    for (const auto& entry : boost::filesystem::directory_iterator(src, it_ec))
    {
        const boost::filesystem::path& path = entry.path();
        boost::system::error_code      type_ec;
        if (boost::filesystem::is_symlink(path, type_ec))
            continue;
        if (boost::filesystem::is_directory(path, type_ec))
        {
            if (!copy_dir_recursive(path, dst / path.filename(), error))
                return false;
        }
        else if (boost::filesystem::is_regular_file(path, type_ec))
        {
            boost::filesystem::copy_file(path, dst / path.filename(), boost::filesystem::copy_option::overwrite_if_exists, type_ec);
            if (type_ec)
            {
                error = "cannot copy file " + path.string() + ": " + type_ec.message();
                return false;
            }
        }
        // Anything else (device nodes, sockets, ...) is skipped.
    }
    return true;
}

/*
 * Extract a plugin zip into dest. Entry names must be relative forward-slash
 * paths without "..", backslashes or drive letters; extraction goes through
 * memory so UTF-8 output paths never touch the narrow-char fopen path inside
 * miniz.
 */
orca::wv::Status extract_plugin_zip(const boost::filesystem::path& zip_path, const boost::filesystem::path& dest)
{
    mz_zip_archive zip;
    mz_zip_zero_struct(&zip);
    if (!Slic3r::open_zip_reader(&zip, zip_path.string()))
        return orca::wv::Status::make(orca::wv::Errc::InvalidArgument, "cannot open zip archive");

    // Every failure path must close the archive before returning.
    auto fail = [&zip](const std::string& message)
    {
        Slic3r::close_zip_reader(&zip);
        return orca::wv::Status::make(orca::wv::Errc::InvalidArgument, message);
    };

    const mz_uint num_entries = mz_zip_reader_get_num_files(&zip);
    if (num_entries == 0 || num_entries > g_max_zip_entries)
        return fail("zip entry count out of range");

    // Pass 1: validate every entry name and size before touching the disk.
    mz_zip_archive_file_stat stat;
    mz_uint64                total_bytes = 0;
    for (mz_uint i = 0; i < num_entries; ++i)
    {
        if (!mz_zip_reader_file_stat(&zip, i, &stat))
            return fail("zip entry stat failed");

        const std::string name(stat.m_filename);
        if (name.empty() || name.front() == '/' || name.find('\\') != std::string::npos ||
            name.find("..") != std::string::npos || name.find(':') != std::string::npos)
            return fail("unsafe entry name in zip: " + name);
        if (stat.m_uncomp_size > g_max_zip_entry_bytes)
            return fail("zip entry too large: " + name);

        total_bytes += stat.m_uncomp_size;
        if (total_bytes > g_max_zip_total_bytes)
            return fail("zip total extracted size exceeds limit");
    }

    // Pass 2: extract through memory.
    for (mz_uint i = 0; i < num_entries; ++i)
    {
        if (!mz_zip_reader_file_stat(&zip, i, &stat))
            return fail("zip entry stat failed");

        const std::string        name(stat.m_filename);
        const boost::filesystem::path out_path = dest / boost::filesystem::path(name);

        boost::system::error_code ec;
        if (stat.m_is_directory)
        {
            boost::filesystem::create_directories(out_path, ec);
            if (ec)
                return fail("cannot create directory for zip entry: " + name);
            continue;
        }

        boost::filesystem::create_directories(out_path.parent_path(), ec);
        if (ec)
            return fail("cannot create directory for zip entry: " + name);

        boost::filesystem::ofstream out(out_path, std::ios_base::out | std::ios_base::binary | std::ios_base::trunc);
        if (!out)
            return fail("zip extract failed: " + name);

        if (stat.m_uncomp_size > 0)
        {
            std::vector<char> buf(static_cast<size_t>(stat.m_uncomp_size));
            if (!mz_zip_reader_extract_to_mem(&zip, i, buf.data(), buf.size(), 0))
            {
                out.close();
                return fail("zip extract failed: " + name);
            }
            out.write(buf.data(), static_cast<std::streamsize>(buf.size()));
        }
        out.close();
        if (out.fail())
            return fail("zip extract failed: " + name);
    }

    Slic3r::close_zip_reader(&zip);
    return orca::wv::Status{};
}

} // namespace

PluginManager::PluginManager()
{
    const boost::filesystem::path base(webview_user_data_dir());
    const boost::filesystem::path plugins_root = base / "plugins";
    const boost::filesystem::path store_path   = base / "plugin" / "permissions.json";

    boost::system::error_code ec;
    boost::filesystem::create_directories(plugins_root, ec);
    if (ec)
        BOOST_LOG_TRIVIAL(error) << "PluginManager: cannot create plugins root " << plugins_root.string() << ": " << ec.message();
    boost::filesystem::create_directories(store_path.parent_path(), ec);
    if (ec)
        BOOST_LOG_TRIVIAL(error) << "PluginManager: cannot create permission store dir " << store_path.parent_path().string() << ": " << ec.message();

    m_plugins_root          = plugins_root.string();
    m_permission_store_path = store_path.string();
}

std::string PluginManager::plugins_root() const
{
    return m_plugins_root;
}

void PluginManager::register_center_apis(orca::jb::Bridge* bridge)
{
    if (!bridge)
        return;

    orca::jb::ApiDesc plugin_list;
    plugin_list.sync = [this](const orca::jb::Json&)
    {
        orca::jb::Json plugins = orca::jb::Json::array();
        boost::system::error_code ec;
        for (const auto& entry : boost::filesystem::directory_iterator(boost::filesystem::path(m_plugins_root), ec))
        {
            boost::system::error_code dir_ec;
            if (!boost::filesystem::is_directory(entry.path(), dir_ec))
                continue;

            // Broken installations are skipped, not fatal.
            const boost::filesystem::path manifest_path = entry.path() / "plugin.json";
            const auto manifest = orca::plugin::PluginManifest::load_from_file(manifest_path.string());
            if (!manifest)
            {
                BOOST_LOG_TRIVIAL(error) << "PluginManager: skipping broken plugin dir " << entry.path().string()
                                         << ": " << manifest.status.message;
                continue;
            }

            boost::system::error_code time_ec;
            std::string installed_at;
            const std::time_t mtime = boost::filesystem::last_write_time(manifest_path, time_ec);
            if (!time_ec)
                installed_at = boost::posix_time::to_iso_extended_string(boost::posix_time::from_time_t(mtime));

            orca::jb::Json item;
            item["id"]           = manifest.value.id;
            item["name"]         = manifest.value.name;
            item["version"]      = manifest.value.version;
            item["description"]  = manifest.value.description;
            item["permissions"]  = manifest.value.permissions;
            item["icon"]         = manifest.value.icon;
            item["installed_at"] = installed_at;
            item["running"]      = is_running(manifest.value.id);
            plugins.push_back(std::move(item));
        }

        // Directory order is arbitrary; keep the list stable across refreshes.
        std::sort(plugins.begin(), plugins.end(), [](const orca::jb::Json& a, const orca::jb::Json& b)
        {
            return a.at("id").get<std::string>() < b.at("id").get<std::string>();
        });

        return orca::jb::ApiOutcome::ok_value(orca::jb::Json{{"plugins", std::move(plugins)}});
    };
    register_api_checked(bridge, "plugin.list", plugin_list);

    orca::jb::ApiDesc plugin_pick_import;
    plugin_pick_import.sync = [](const orca::jb::Json& params)
    {
        const std::string kind = params.value("kind", "zip");
        wxWindow*         parent = wxGetApp().mainframe;

        std::string path;
        if (kind == "dir")
        {
            wxDirDialog dlg(parent, _L("Select a plugin folder"), wxEmptyString, wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
            if (dlg.ShowModal() == wxID_OK)
                path = into_u8(dlg.GetPath());
        }
        else
        {
            wxFileDialog dlg(parent, _L("Select a plugin archive"), wxEmptyString, wxEmptyString,
                "Plugin archive (*.zip)|*.zip", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
            if (dlg.ShowModal() == wxID_OK)
                path = into_u8(dlg.GetPath());
        }

        if (path.empty())
            return orca::jb::ApiOutcome::ok_value(orca::jb::Json{{"path", ""}});
        return orca::jb::ApiOutcome::ok_value(orca::jb::Json{
            {"path", path},
            {"kind", kind == "dir" ? "dir" : "zip"}});
    };
    register_api_checked(bridge, "plugin.pick_import", plugin_pick_import);

    orca::jb::ApiDesc plugin_import;
    // Async by contract: the import does 64MB-class IO; deferring keeps it
    // out of the webview message callback, where sync handlers run inline.
    plugin_import.async = [this](const orca::jb::Json& params, orca::jb::Responder responder)
    {
        // mutable: the Responder senders are non-const members of the copy.
        wxTheApp->CallAfter([this, params, responder]() mutable
        {
            const std::string path = params.value("path", "");
            const std::string kind = params.value("kind", "zip");
            if (path.empty())
            {
                responder.send_error(orca::jb::RpcErrc::InvalidParams, "path is required");
                return;
            }

            std::string plugin_id;
            std::string error;
            if (!import_plugin_from(path, kind, plugin_id, error))
                responder.send_result(orca::jb::Json{{"ok", false}, {"error", error}});
            else
                responder.send_result(orca::jb::Json{{"ok", true}, {"id", plugin_id}});
        });
    };
    register_api_checked(bridge, "plugin.import", plugin_import);

    orca::jb::ApiDesc plugin_remove;
    // Async by contract: removing a running plugin tears down another
    // webview's controller and tab window - never inline in a callback.
    plugin_remove.async = [this](const orca::jb::Json& params, orca::jb::Responder responder)
    {
        wxTheApp->CallAfter([this, params, responder]() mutable
        {
            const std::string plugin_id = params.value("id", "");
            if (plugin_id.empty())
            {
                responder.send_result(orca::jb::Json{{"ok", false}, {"error", "plugin id is required"}});
                return;
            }

            if (is_running(plugin_id))
            {
                // The tab teardown path unloads the session before we delete files.
                MainFrame* frame = wxGetApp().mainframe;
                if (!frame || !frame->close_plugin_tab(plugin_id))
                {
                    responder.send_result(orca::jb::Json{{"ok", false}, {"error", "close the running plugin first"}});
                    return;
                }
            }

            boost::system::error_code ec;
            boost::filesystem::remove_all(boost::filesystem::path(m_plugins_root) / plugin_id, ec);
            if (ec)
                responder.send_result(orca::jb::Json{{"ok", false}, {"error", ec.message()}});
            else
                responder.send_result(orca::jb::Json{{"ok", true}});
        });
    };
    register_api_checked(bridge, "plugin.remove", plugin_remove);

    orca::jb::ApiDesc plugin_open;
    // Async by contract: opening runs a modal permission prompt and creates a
    // window plus a webview controller.
    plugin_open.async = [](const orca::jb::Json& params, orca::jb::Responder responder)
    {
        wxTheApp->CallAfter([params, responder]() mutable
        {
            MainFrame* frame = wxGetApp().mainframe;
            if (!frame)
            {
                responder.send_error(orca::jb::RpcErrc::InternalError, "main frame not ready");
                return;
            }

            const std::string plugin_id = params.value("id", "");
            if (plugin_id.empty())
            {
                responder.send_error(orca::jb::RpcErrc::InvalidParams, "plugin id is required");
                return;
            }

            if (!frame->open_plugin_tab(plugin_id))
                responder.send_result(orca::jb::Json{{"ok", false}, {"error", "failed to open plugin"}});
            else
                responder.send_result(orca::jb::Json{{"ok", true}});
        });
    };
    register_api_checked(bridge, "plugin.open", plugin_open);

    orca::jb::ApiDesc plugin_close;
    // Async by contract: closing destroys a tab window and its webview
    // controller from inside someone else's message callback otherwise.
    plugin_close.async = [](const orca::jb::Json& params, orca::jb::Responder responder)
    {
        wxTheApp->CallAfter([params, responder]() mutable
        {
            MainFrame* frame = wxGetApp().mainframe;
            if (!frame)
            {
                responder.send_error(orca::jb::RpcErrc::InternalError, "main frame not ready");
                return;
            }

            const std::string plugin_id = params.value("id", "");
            if (plugin_id.empty())
            {
                responder.send_error(orca::jb::RpcErrc::InvalidParams, "plugin id is required");
                return;
            }

            if (!frame->close_plugin_tab(plugin_id))
                responder.send_result(orca::jb::Json{{"ok", false}, {"error", "failed to close plugin"}});
            else
                responder.send_result(orca::jb::Json{{"ok", true}});
        });
    };
    register_api_checked(bridge, "plugin.close", plugin_close);

    orca::jb::ApiDesc plugin_install_sample;
    // Async by contract: same IO weight as plugin.import.
    plugin_install_sample.async = [this](const orca::jb::Json&, orca::jb::Responder responder)
    {
        wxTheApp->CallAfter([this, responder]() mutable
        {
            const boost::filesystem::path sample =
                boost::filesystem::path(Slic3r::resources_dir()) / "web" / "sample_plugin" / "filament_demo";

            std::string plugin_id;
            std::string error;
            if (!import_plugin_from(sample.string(), "dir", plugin_id, error))
                responder.send_result(orca::jb::Json{{"ok", false}, {"error", error}});
            else
                responder.send_result(orca::jb::Json{{"ok", true}, {"id", plugin_id}});
        });
    };
    register_api_checked(bridge, "plugin.install_sample", plugin_install_sample);

    // Center bridge: origin-only filament list (see filament_list_api).
    register_api_checked(bridge, "orca.filament.list", filament_list_api(std::string()));
    register_api_checked(bridge, "orca.host.info", host_info_api());
}

orca::plugin::PluginHost* PluginManager::ensure_host()
{
    if (m_host)
        return m_host.get();

    orca::plugin::HostEnv env;
    env.host_version          = SLIC3R_VERSION;
    env.dispatcher            = [](std::function<void()> fn) { wxTheApp->CallAfter(fn); };
    env.browser_data_dir      = webview_user_data_dir();   // same shared WebView2 folder
    env.permission_store_path = m_permission_store_path;
    m_host                    = std::make_unique<orca::plugin::PluginHost>(env);

    // Host API surface cloned into every plugin session bridge (same handlers
    // the plugin center page sees). filament.list is tagged here: plugin
    // sessions go through the manifest-declared permission chain.
    register_api_checked(m_host.get(), "orca.host.info", host_info_api());
    register_api_checked(m_host.get(), "orca.filament.list", filament_list_api("project.filaments.read"));

    return m_host.get();
}

std::shared_ptr<PluginTabPage> PluginManager::open_plugin(const std::string& plugin_id, wxWindow* tab_parent)
{
    orca::plugin::PluginHost* host = ensure_host();
    if (!host || !tab_parent || plugin_id.empty())
        return nullptr;

    const boost::filesystem::path manifest_path =
        boost::filesystem::path(m_plugins_root) / plugin_id / "plugin.json";

    // Parse first: the permission prompt needs the plugin id and tags.
    const auto manifest = orca::plugin::PluginManifest::load_from_file(manifest_path.string());
    if (!manifest)
    {
        BOOST_LOG_TRIVIAL(error) << "PluginManager: manifest load failed for " << plugin_id << ": " << manifest.status.message;
        return nullptr;
    }

    if (!prompt_plugin_permissions(manifest.value, tab_parent))
    {
        // Clean deny: a plugin whose requested permission was refused (now or
        // on an earlier open - refusals are persisted) never opens.
        BOOST_LOG_TRIVIAL(info) << "PluginManager: permission refused, not opening " << plugin_id;
        return nullptr;
    }

    // The tab panel must exist before load: the session webview is parented
    // to its native handle, then bound back into the tab.
    auto tab = std::make_shared<PluginTabPage>(tab_parent, nullptr);

    orca::wv::Status status;
    std::shared_ptr<orca::plugin::PluginSession> session =
        host->load(manifest_path.string(), tab->native_host_handle(), status);
    if (!session)
    {
        BOOST_LOG_TRIVIAL(error) << "PluginManager: plugin load failed for " << plugin_id << ": " << status.message;
        return nullptr;
    }

    if (!tab->adopt_session(session))
    {
        // Unreachable in practice (the tab was just created empty); reclaim
        // the session so the host does not track a dangling plugin.
        host->unload(plugin_id);
        return nullptr;
    }

    return tab;
}

bool PluginManager::close_plugin(const std::string& plugin_id)
{
    if (!m_host)
        return false;

    // Must run while the tab window is still alive (valid native parent).
    const orca::wv::Status status = m_host->unload(plugin_id);
    if (!status.ok())
    {
        BOOST_LOG_TRIVIAL(error) << "PluginManager: plugin unload failed for " << plugin_id << ": " << status.message;
        return false;
    }
    return true;
}

bool PluginManager::is_running(const std::string& plugin_id) const
{
    if (!m_host)
        return false;

    const std::vector<std::string> loaded = m_host->loaded_ids();
    return std::find(loaded.begin(), loaded.end(), plugin_id) != loaded.end();
}

std::vector<std::string> PluginManager::installed_ids() const
{
    std::vector<std::string> ids;

    boost::system::error_code ec;
    for (const auto& entry : boost::filesystem::directory_iterator(boost::filesystem::path(m_plugins_root), ec))
    {
        boost::system::error_code dir_ec;
        if (!boost::filesystem::is_directory(entry.path(), dir_ec))
            continue;

        boost::system::error_code file_ec;
        if (boost::filesystem::exists(entry.path() / "plugin.json", file_ec))
            ids.push_back(entry.path().filename().string());
    }

    std::sort(ids.begin(), ids.end());
    return ids;
}

bool PluginManager::prompt_plugin_permissions(const orca::plugin::PluginManifest& manifest, wxWindow* parent)
{
    orca::plugin::PluginHost* host = ensure_host();
    if (!host)
        return false;

    // Must be the host's store instance: every session policy reads the
    // grant table it owns, so grants/denials take effect in live sessions
    // and save() persists them. A separate store object would only touch
    // disk and the loaded session would keep denying the tag.
    orca::plugin::PermissionStore& store = host->permission_store();

    bool changed = false;
    bool denied  = false;
    for (const std::string& tag : manifest.permissions)
    {
        const orca::plugin::PermissionState state = store.query(manifest.id, tag);
        if (state == orca::plugin::PermissionState::Denied)
        {
            // Persisted refusal from an earlier open: fail without re-asking.
            denied = true;
            continue;
        }
        if (state == orca::plugin::PermissionState::Granted)
            continue;

        wxMessageDialog dlg(parent,
            wxString::Format(_L("Plugin \"%s\" requests the permission:\n\n%s\n\nGrant it now?"),
                wxString::FromUTF8(manifest.name.c_str()), wxString::FromUTF8(tag.c_str())),
            _L("Plugin permission request"), wxYES_NO | wxICON_QUESTION);
        if (dlg.ShowModal() == wxID_YES)
        {
            store.grant(manifest.id, tag);
        }
        else
        {
            // Revoke records an explicit Denied so the next open does not
            // re-ask this tag.
            store.revoke(manifest.id, tag);
            denied = true;
        }
        changed = true;
    }
    if (changed)
        store.save();
    return !denied;
}

bool PluginManager::import_plugin_from(const std::string& path, const std::string& kind, std::string& plugin_id, std::string& error)
{
    const boost::filesystem::path src(path);

    boost::system::error_code ec;
    const bool src_ok = (kind == "dir") ? boost::filesystem::is_directory(src, ec)
                                        : boost::filesystem::is_regular_file(src, ec);
    if (!src_ok)
    {
        error = "source path is not a valid " + std::string(kind == "dir" ? "directory" : "zip archive");
        return false;
    }

    const boost::filesystem::path scratch = boost::filesystem::path(m_plugins_root) / g_import_scratch_name;
    boost::filesystem::remove_all(scratch, ec);

    bool unpacked = false;
    if (kind == "dir")
    {
        unpacked = copy_dir_recursive(src, scratch, error);
    }
    else
    {
        const orca::wv::Status status = extract_plugin_zip(src.string(), scratch);
        if (!status.ok())
            error = status.message;
        unpacked = status.ok();
    }
    if (!unpacked)
    {
        boost::filesystem::remove_all(scratch, ec);
        return false;
    }

    // The manifest sits either at the scratch root or below exactly one
    // wrapper directory (the usual zip layout).
    boost::filesystem::path base;
    if (boost::filesystem::exists(scratch / "plugin.json", ec))
    {
        base = scratch;
    }
    else
    {
        std::vector<boost::filesystem::path> sub_dirs;
        boost::system::error_code            it_ec;
        for (const auto& entry : boost::filesystem::directory_iterator(scratch, it_ec))
        {
            boost::system::error_code dir_ec;
            if (boost::filesystem::is_directory(entry.path(), dir_ec))
                sub_dirs.push_back(entry.path());
        }
        if (sub_dirs.size() == 1 && boost::filesystem::exists(sub_dirs.front() / "plugin.json", ec))
            base = sub_dirs.front();
    }
    if (base.empty())
    {
        error = "plugin.json not found";
        boost::filesystem::remove_all(scratch, ec);
        return false;
    }

    const auto manifest = orca::plugin::PluginManifest::load_from_file((base / "plugin.json").string());
    if (!manifest)
    {
        error = "invalid plugin.json: " + manifest.status.message;
        boost::filesystem::remove_all(scratch, ec);
        return false;
    }

    const orca::wv::Status validate_status = manifest.value.validate_against_dir(base.string());
    if (!validate_status.ok())
    {
        error = validate_status.message;
        boost::filesystem::remove_all(scratch, ec);
        return false;
    }

    // Overwrite-install: replace any existing installation of the same id.
    const boost::filesystem::path dest = boost::filesystem::path(m_plugins_root) / manifest.value.id;

    // A running plugin holds its files through the WebView2 virtual host;
    // deleting under it leaves a half-removed directory. Same guard as
    // plugin.remove: close the tab (which unloads the session) first.
    if (is_running(manifest.value.id))
    {
        MainFrame* frame = wxGetApp().mainframe;
        if (!frame || !frame->close_plugin_tab(manifest.value.id))
        {
            error = "close the running plugin before reinstalling it";
            boost::filesystem::remove_all(scratch, ec);
            return false;
        }
    }

    boost::filesystem::remove_all(dest, ec);
    boost::filesystem::rename(base, dest, ec);
    if (ec)
    {
        error = "cannot move plugin into place: " + ec.message();
        boost::filesystem::remove_all(scratch, ec);
        return false;
    }

    boost::filesystem::remove_all(scratch, ec);
    plugin_id = manifest.value.id;
    return true;
}

} // GUI
} // Slic3r
