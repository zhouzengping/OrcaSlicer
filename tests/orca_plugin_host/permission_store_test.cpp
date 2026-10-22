/*
 * PermissionStore persistence tests against a real JSON file in a temp dir.
 */

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include <nlohmann/json.hpp>

#include "orca/plugin/permission_store.hpp"

using orca::plugin::PermissionState;
using orca::plugin::PermissionStore;
using Json = nlohmann::json;

namespace
{

class TempDir
{
public:
    TempDir()
    {
        std::error_code ec;
        static std::atomic<unsigned> counter{0};
        std::filesystem::path base = std::filesystem::temp_directory_path(ec);
        if (ec)
            return;
        const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
        m_path = base / ("orca_plugin_perm_test_" + std::to_string(tick) + "_" + std::to_string(counter++));
        std::filesystem::create_directories(m_path, ec);
    }

    ~TempDir()
    {
        std::error_code ec;
        std::filesystem::remove_all(m_path, ec);
    }

    const std::filesystem::path& path() const { return m_path; }

private:
    std::filesystem::path m_path;
};

std::string read_text(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void write_text(const std::filesystem::path& path, const std::string& text)
{
    std::ofstream out(path, std::ios::binary);
    out << text;
}

} // namespace

TEST_CASE("grants round trip through the backing file", "[orca_plugin_host][permission_store]")
{
    TempDir dir;
    REQUIRE(!dir.path().empty());
    const std::filesystem::path file = dir.path() / "permissions.json";

    PermissionStore first(file.string());
    CHECK(first.load().ok());   // missing file -> empty store, Ok
    CHECK(first.query("com.orca.demo", "demo.special") == PermissionState::Unknown);

    CHECK(first.grant("com.orca.demo", "demo.special").ok());
    CHECK(first.query("com.orca.demo", "demo.special") == PermissionState::Granted);
    CHECK(first.save().ok());

    // A fresh instance over the same file must observe the persisted grant.
    PermissionStore second(file.string());
    CHECK(second.load().ok());
    CHECK(second.query("com.orca.demo", "demo.special") == PermissionState::Granted);
    // Other plugins / permissions are untouched.
    CHECK(second.query("com.orca.demo", "device.read") == PermissionState::Unknown);
    CHECK(second.query("com.orca.other", "demo.special") == PermissionState::Unknown);
}

TEST_CASE("revoke removes one permission, revoke_all removes every grant", "[orca_plugin_host][permission_store]")
{
    TempDir dir;
    REQUIRE(!dir.path().empty());
    PermissionStore store((dir.path() / "permissions.json").string());
    REQUIRE(store.load().ok());

    store.grant("com.orca.demo", "a.first");
    store.grant("com.orca.demo", "b.second");
    store.grant("com.orca.demo", "c.third");
    store.grant("com.orca.other", "a.first");

    CHECK(store.revoke("com.orca.demo", "a.first").ok());
    // revoke() records an explicit denial (persisted, no re-prompt);
    // revoke_all() below is the forget-everything counterpart.
    CHECK(store.query("com.orca.demo", "a.first") == PermissionState::Denied);
    CHECK(store.query("com.orca.demo", "b.second") == PermissionState::Granted);
    // Other plugins keep their grants.
    CHECK(store.query("com.orca.other", "a.first") == PermissionState::Granted);

    CHECK(store.revoke_all("com.orca.demo").ok());
    CHECK(store.query("com.orca.demo", "b.second") == PermissionState::Unknown);
    CHECK(store.query("com.orca.demo", "c.third") == PermissionState::Unknown);
    CHECK(store.query("com.orca.other", "a.first") == PermissionState::Granted);
    CHECK(store.granted_permissions("com.orca.demo").empty());
}

TEST_CASE("granted_permissions returns a sorted list", "[orca_plugin_host][permission_store]")
{
    TempDir dir;
    REQUIRE(!dir.path().empty());
    PermissionStore store((dir.path() / "permissions.json").string());
    REQUIRE(store.load().ok());

    store.grant("com.orca.demo", "c.third");
    store.grant("com.orca.demo", "a.first");
    store.grant("com.orca.demo", "b.second");

    const std::vector<std::string> expected{"a.first", "b.second", "c.third"};
    CHECK(store.granted_permissions("com.orca.demo") == expected);
}

TEST_CASE("a corrupt store file self-heals to empty", "[orca_plugin_host][permission_store]")
{
    TempDir dir;
    REQUIRE(!dir.path().empty());
    const std::filesystem::path file = dir.path() / "permissions.json";
    write_text(file, "{ this is not valid json");

    // Loading garbage must succeed with an empty in-memory store...
    PermissionStore store(file.string());
    CHECK(store.load().ok());
    CHECK(store.query("com.orca.demo", "demo.special") == PermissionState::Unknown);
    CHECK(store.granted_permissions("com.orca.demo").empty());

    // ...and saving rewrites the file into valid JSON.
    CHECK(store.save().ok());
    const auto parsed = Json::parse(read_text(file), nullptr, false);
    CHECK(!parsed.is_discarded());

    // A new instance loads the healed file and still sees no grants.
    PermissionStore reloaded(file.string());
    CHECK(reloaded.load().ok());
    CHECK(reloaded.query("com.orca.demo", "demo.special") == PermissionState::Unknown);
}
