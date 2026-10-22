/*
 * Status / Errc / Config contract tests for orca_webview.
 *
 * Only side-effect-free paths are exercised: creating a real webview requires
 * a UI thread with a message loop, which the test process does not have.
 */

#include <catch2/catch_test_macros.hpp>

#include <string>

#include "orca/webview/types.hpp"
#include "orca/webview/webview.hpp"

using orca::wv::ColorScheme;
using orca::wv::Errc;
using orca::wv::Status;
using orca::wv::WebView;

TEST_CASE("Errc numeric values are stable", "[orca_webview]")
{
    // The enum values are part of the wire contract between the three orca
    // libraries and must never drift.
    CHECK(static_cast<int32_t>(Errc::Ok) == 0);
    CHECK(static_cast<int32_t>(Errc::InvalidArgument) == 1000);
    CHECK(static_cast<int32_t>(Errc::NotSupported) == 1001);
    CHECK(static_cast<int32_t>(Errc::InvalidState) == 1002);
    CHECK(static_cast<int32_t>(Errc::OutOfMemory) == 1003);
    CHECK(static_cast<int32_t>(Errc::RuntimeMissing) == 1004);
    CHECK(static_cast<int32_t>(Errc::EnvironmentRejected) == 1005);
    CHECK(static_cast<int32_t>(Errc::ParentInvalid) == 1006);
    CHECK(static_cast<int32_t>(Errc::NavigationFailed) == 1007);
    CHECK(static_cast<int32_t>(Errc::NavigationCancelled) == 1008);
    CHECK(static_cast<int32_t>(Errc::ScriptEvalFailed) == 1009);
    CHECK(static_cast<int32_t>(Errc::MessageChannelClosed) == 1010);
    CHECK(static_cast<int32_t>(Errc::PlatformFailure) == 2000);
}

TEST_CASE("Status default is ok and make fills a failure", "[orca_webview]")
{
    Status st;
    CHECK(st.ok());
    CHECK(st.code == Errc::Ok);
    CHECK(st.message.empty());
    CHECK(st.native == 0);

    Status bad = Status::make(Errc::InvalidArgument, "bad argument", 42);
    CHECK(!bad.ok());
    CHECK(bad.code == Errc::InvalidArgument);
    CHECK(bad.message == "bad argument");
    CHECK(bad.native == 42);

    Status defaulted = Status::make(Errc::PlatformFailure);
    CHECK(!defaulted.ok());
    CHECK(defaulted.message.empty());
    CHECK(defaulted.native == 0);
}

TEST_CASE("WebView::Config defaults", "[orca_webview]")
{
    WebView::Config cfg;
    CHECK(cfg.user_agent.empty());
    CHECK(cfg.color_scheme == ColorScheme::Auto);
    CHECK(cfg.devtools);
    CHECK(cfg.context_menu);
    CHECK(cfg.browser_data_dir.empty());
    CHECK(cfg.profile_name.empty());
    CHECK(!cfg.incognito);
    CHECK(cfg.custom_schemes.empty());
    CHECK(cfg.extra_init_scripts.empty());
}

TEST_CASE("Runtime probe is callable and side-effect free", "[orca_webview]")
{
    // The return value depends on the machine (WebView2 installed or not);
    // the contract is only that the probes answer without crashing and that
    // the version is a plain string.
    (void) WebView::runtime_available();
    const std::string version = WebView::runtime_version();
    (void) version;
}

TEST_CASE("create rejects a null native parent", "[orca_webview]")
{
    // The null-parent guard must fire before any runtime probing so this is
    // deterministic even on machines without a webview runtime installed.
    WebView::Config  cfg;
    Status           st;
    auto             view = WebView::create(nullptr, cfg, st);
    CHECK(view == nullptr);
    CHECK(!st.ok());
    CHECK(st.code == Errc::InvalidArgument);
    CHECK(!st.message.empty());
}
