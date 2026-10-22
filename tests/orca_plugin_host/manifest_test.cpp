/*
 * PluginManifest schema tests: file loading, JSON parsing and entry
 * validation against a real directory tree.
 */

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include "orca/plugin/manifest.hpp"

using orca::plugin::PluginManifest;
using Json = orca::plugin::Json;

namespace
{

// Unique per-process, per-instance scratch directory; cleaned up on scope exit.
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
        m_path = base / ("orca_plugin_manifest_test_" + std::to_string(tick) + "_" + std::to_string(counter++));
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

std::filesystem::path manifest_file(const std::string& name)
{
    return std::filesystem::path(std::string(TEST_DATA_DIR)) / "orca_plugin" / "manifests" / name;
}

void write_file(const std::filesystem::path& path, const std::string& content)
{
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary);
    out << content;
}

} // namespace

TEST_CASE("load_from_file accepts the minimal manifest", "[orca_plugin_host][manifest]")
{
    const auto result = PluginManifest::load_from_file(manifest_file("valid_minimal.json").string());
    REQUIRE(result.status.ok());
    CHECK(result.value.manifest_version == 1);
    CHECK(result.value.id == "com.orca.minimal");
    CHECK(result.value.name == "Minimal Plugin");
    CHECK(result.value.version == "1.0.0");
    CHECK(result.value.entry == "index.html");   // documented default
    CHECK(result.value.permissions.empty());
    CHECK(result.value.min_host_version.empty());
    CHECK(result.value.description.empty());
    CHECK(result.value.icon.empty());
}

TEST_CASE("load_from_file accepts the fully populated manifest", "[orca_plugin_host][manifest]")
{
    const auto result = PluginManifest::load_from_file(manifest_file("valid_full.json").string());
    REQUIRE(result.status.ok());
    CHECK(result.value.manifest_version == 1);
    CHECK(result.value.id == "com.orca.full");
    CHECK(result.value.name == "Full Plugin");
    CHECK(result.value.version == "2.3.4");
    CHECK(result.value.entry == "ui/main.html");
    CHECK(result.value.permissions == std::vector<std::string>{"demo.special", "device.read"});
    CHECK(result.value.min_host_version == "1.0.0");
    CHECK(result.value.description == "Exercise every manifest field");
    CHECK(result.value.icon == "icon.png");
}

TEST_CASE("load_from_file rejects schema violations", "[orca_plugin_host][manifest]")
{
    const char* cases[] = {"bad_id.json", "traversal_entry.json", "bad_version.json", "unknown_manifest_version.json"};
    for (const char* name : cases)
    {
        const auto result = PluginManifest::load_from_file(manifest_file(name).string());
        INFO("manifest file: " << name);
        CHECK(!result.status.ok());
        CHECK(result.status.code == orca::wv::Errc::InvalidArgument);
        CHECK(!result.status.message.empty());
    }
}

TEST_CASE("load_from_file reports a missing file", "[orca_plugin_host][manifest]")
{
    const auto result = PluginManifest::load_from_file(manifest_file("does_not_exist.json").string());
    CHECK(!result.status.ok());
    CHECK(result.status.code == orca::wv::Errc::InvalidArgument);
    CHECK(!result.status.message.empty());
}

TEST_CASE("parse ignores unknown fields", "[orca_plugin_host][manifest]")
{
    const auto parsed = Json::parse(R"({
        "orca_manifest": 1,
        "id": "com.orca.parse",
        "name": "Parse Test",
        "version": "3.2.1",
        "future_field": {"anything": [1, 2, 3]}
    })",
        nullptr, false);
    REQUIRE(!parsed.is_discarded());

    const auto result = PluginManifest::parse(parsed);
    REQUIRE(result.status.ok());
    CHECK(result.value.id == "com.orca.parse");
    CHECK(result.value.version == "3.2.1");
}

TEST_CASE("parse rejects duplicate permissions", "[orca_plugin_host][manifest]")
{
    const auto parsed = Json::parse(R"({
        "orca_manifest": 1,
        "id": "com.orca.dup",
        "name": "Duplicate Permissions",
        "version": "1.0.0",
        "permissions": ["demo.special", "demo.special"]
    })",
        nullptr, false);
    REQUIRE(!parsed.is_discarded());

    const auto result = PluginManifest::parse(parsed);
    CHECK(!result.status.ok());
    CHECK(result.status.code == orca::wv::Errc::InvalidArgument);
    CHECK(!result.status.message.empty());
}

TEST_CASE("validate_against_dir checks entry existence and escapes", "[orca_plugin_host][manifest]")
{
    TempDir root;
    REQUIRE(!root.path().empty());

    write_file(root.path() / "index.html", "<html></html>");
    std::error_code ec;
    std::filesystem::create_directories(root.path() / "ui", ec);
    write_file(root.path() / "ui" / "main.html", "<html></html>");

    PluginManifest manifest;
    manifest.manifest_version = 1;
    manifest.id               = "com.orca.dir";
    manifest.name             = "Dir Test";
    manifest.version          = "1.0.0";

    SECTION("default entry present")
    {
        manifest.entry = "index.html";
        CHECK(manifest.validate_against_dir(root.path().string()).ok());
    }

    SECTION("nested entry present")
    {
        manifest.entry = "ui/main.html";
        CHECK(manifest.validate_against_dir(root.path().string()).ok());
    }

    SECTION("entry missing")
    {
        manifest.entry = "nope.html";
        const auto st  = manifest.validate_against_dir(root.path().string());
        CHECK(!st.ok());
        CHECK(!st.message.empty());
    }

    SECTION("traversal entry rejected")
    {
        // Schema validation already rejects ".." in load_from_file/parse; the
        // directory check is defense in depth and must hold for a struct that
        // bypassed parsing (fields are public), whether or not anything exists
        // at the escaped path.
        manifest.entry = "../outside.html";
        CHECK(!manifest.validate_against_dir(root.path().string()).ok());
    }
}
