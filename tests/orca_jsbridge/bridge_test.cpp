/*
 * Headless Bridge tests. All traffic goes through attach_channel with a mock
 * Channel that records posted frames and injected init scripts; no real
 * webview is involved. Handlers complete synchronously on the test thread,
 * so no dispatcher is configured (Responder executes directly).
 *
 * Permission semantics assumed here (BridgeConfig without a policy function):
 *   permission == ""  -> allowed (origin check only)
 *   permission != ""  -> PermissionDenied
 */

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "orca/jsbridge/bridge.hpp"
#include "orca/jsbridge/channel.hpp"
#include "orca/jsbridge/protocol.hpp"

using namespace orca;  // wv:: / jb::
using jb::ApiDesc;
using jb::ApiOutcome;
using jb::Bridge;
using jb::BridgeConfig;
using jb::Frame;
using jb::Json;
using jb::Responder;
using jb::RpcErrc;
using jb::SessionInfo;

namespace
{

// Outbound record shared with the mock channel; owned by the test so the log
// survives channel destruction on detach.
struct ChannelLog
{
    std::vector<std::string> frames;
    std::vector<std::string> init_scripts;
};

class MockChannel : public jb::Channel
{
public:
    explicit MockChannel(std::shared_ptr<ChannelLog> log) : m_log(std::move(log)) {}

    wv::Status post_message_json(const std::string& json_text) override
    {
        m_log->frames.push_back(json_text);
        return wv::Status();
    }

    wv::Status add_init_script(const std::string& js) override
    {
        m_log->init_scripts.push_back(js);
        return wv::Status();
    }

private:
    std::shared_ptr<ChannelLog> m_log;
};

// Decode every frame the channel received; undecodable entries are skipped.
std::vector<Frame> posted_frames(const ChannelLog& log)
{
    std::vector<Frame> out;
    for (const std::string& text : log.frames)
    {
        Frame   frame;
        RpcErrc err = RpcErrc::InternalError;
        if (jb::decode_frame(text, frame, err))
            out.push_back(frame);
    }
    return out;
}

const Frame* find_result(const std::vector<Frame>& frames, const std::string& id)
{
    for (const Frame& f : frames)
    {
        if (f.type == "result" && f.id == id)
            return &f;
    }
    return nullptr;
}

// Bridge plus attached mock channel with default-ish config.
struct BridgeHarness
{
    Bridge                     bridge;
    std::shared_ptr<ChannelLog> log = std::make_shared<ChannelLog>();
    wv::Status                 attach_status;

    explicit BridgeHarness(BridgeConfig cfg = BridgeConfig{})
    {
        SessionInfo session;
        session.session_id = "unit-test";
        session.context     = Json{{"kind", "unit-test"}};
        // No allowed_origins / policy: these suites exercise the API layer,
        // not origin enforcement.
        attach_status = bridge.attach_channel(std::make_unique<MockChannel>(log), cfg, session);
    }
};

ApiDesc sync_desc(Json (*handler)(const Json&))
{
    ApiDesc desc;
    desc.sync = [handler](const Json& params) { return ApiOutcome::ok_value(handler(params)); };
    return desc;
}

} // namespace

TEST_CASE("attach_channel injects the SDK init script", "[orca_jsbridge][bridge]")
{
    BridgeHarness h;
    REQUIRE(h.attach_status.ok());
    REQUIRE(h.bridge.attached());
    REQUIRE(h.log->init_scripts.size() == 1);
    CHECK(h.log->init_scripts.front() == std::string(jb::sdk_init_script()));
}

TEST_CASE("synchronous API echoes params back as an ok result", "[orca_jsbridge][bridge]")
{
    BridgeHarness h;
    REQUIRE(h.bridge.register_api("demo.echo", sync_desc([](const Json& params) { return params; })).ok());

    const Json params = Json{{"text", "hello"}, {"n", 42}};
    h.bridge.feed_message(jb::encode_call("c1", "demo.echo", params));

    const auto        frames = posted_frames(*h.log);
    const Frame*      result = find_result(frames, "c1");
    REQUIRE(result != nullptr);
    CHECK(result->ok);
    CHECK(result->result == params);
    CHECK(h.bridge.pending_count() == 0);
}

TEST_CASE("unknown method answers MethodNotFound", "[orca_jsbridge][bridge]")
{
    BridgeHarness h;

    h.bridge.feed_message(jb::encode_call("c1", "demo.missing", Json::object()));

    const auto   frames = posted_frames(*h.log);
    const Frame* result = find_result(frames, "c1");
    REQUIRE(result != nullptr);
    CHECK(!result->ok);
    CHECK(result->error_code == RpcErrc::MethodNotFound);
}

TEST_CASE("_orca.hello answers with protocol metadata", "[orca_jsbridge][bridge]")
{
    BridgeHarness h;
    REQUIRE(h.bridge.register_api("demo.echo", sync_desc([](const Json&) { return Json::object(); })).ok());

    h.bridge.feed_message(jb::encode_call("h1", "_orca.hello", Json::object()));

    const auto   frames = posted_frames(*h.log);
    const Frame* result = find_result(frames, "h1");
    REQUIRE(result != nullptr);
    REQUIRE(result->ok);
    CHECK(result->result.contains("protocol"));
    CHECK(result->result.contains("apis"));
    CHECK(result->result.contains("capabilities"));
    // protocol is the numeric wire version, matching the "orca":1 frame header.
    CHECK(result->result["protocol"].is_number_integer());
    CHECK(result->result["protocol"] == 1);
    CHECK(result->result["apis"].is_array());
    CHECK(result->result["capabilities"].is_array());
}

TEST_CASE("async responder completes pending calls", "[orca_jsbridge][bridge]")
{
    BridgeHarness              h;
    std::vector<Responder>     held;

    ApiDesc hang;
    hang.async = [&held](const Json& params, Responder responder) { held.push_back(responder); };
    REQUIRE(h.bridge.register_api("demo.hang", hang).ok());

    h.bridge.feed_message(jb::encode_call("c1", "demo.hang", Json::object()));
    CHECK(h.bridge.pending_count() == 1);
    h.bridge.feed_message(jb::encode_call("c2", "demo.hang", Json::object()));
    CHECK(h.bridge.pending_count() == 2);
    REQUIRE(held.size() == 2);

    // Answer the first call; the second stays pending.
    held[0].send_result(Json{{"tag", "c1"}});
    CHECK(h.bridge.pending_count() == 1);

    const auto   frames = posted_frames(*h.log);
    const Frame* result = find_result(frames, "c1");
    REQUIRE(result != nullptr);
    CHECK(result->ok);
    CHECK(result->result == Json{{"tag", "c1"}});

    // Answering twice is a caller contract violation we do not exercise;
    // finishing the remaining call must reclaim the last pending slot.
    held[1].send_result(Json{{"tag", "c2"}});
    CHECK(h.bridge.pending_count() == 0);
}

TEST_CASE("permission-tagged APIs are denied without a policy; empty tag passes", "[orca_jsbridge][bridge]")
{
    BridgeHarness h;

    ApiDesc locked;
    locked.permission = "demo.special";
    locked.sync       = [](const Json&) { return ApiOutcome::ok_value(Json{{"got_in", true}}); };
    REQUIRE(h.bridge.register_api("demo.locked", locked).ok());

    ApiDesc open;
    open.sync = [](const Json& params) { return ApiOutcome::ok_value(params); };
    REQUIRE(h.bridge.register_api("demo.open", open).ok());

    h.bridge.feed_message(jb::encode_call("c1", "demo.locked", Json::object()));
    h.bridge.feed_message(jb::encode_call("c2", "demo.open", Json::object()));

    const auto frames = posted_frames(*h.log);

    const Frame* denied = find_result(frames, "c1");
    REQUIRE(denied != nullptr);
    CHECK(!denied->ok);
    CHECK(denied->error_code == RpcErrc::PermissionDenied);

    const Frame* allowed = find_result(frames, "c2");
    REQUIRE(allowed != nullptr);
    CHECK(allowed->ok);
}

TEST_CASE("pending-call flood guard trips at max_pending_calls", "[orca_jsbridge][bridge]")
{
    BridgeConfig          cfg;
    cfg.max_pending_calls = 2;
    BridgeHarness         h(cfg);

    std::vector<Responder> held;
    ApiDesc                hang;
    hang.async = [&held](const Json&, Responder responder) { held.push_back(responder); };
    REQUIRE(h.bridge.register_api("demo.hang", hang).ok());

    h.bridge.feed_message(jb::encode_call("c1", "demo.hang", Json::object()));
    h.bridge.feed_message(jb::encode_call("c2", "demo.hang", Json::object()));
    CHECK(h.bridge.pending_count() == 2);

    // Header contract: exceeding the limit answers SessionClosed (and detaches).
    h.bridge.feed_message(jb::encode_call("c3", "demo.hang", Json::object()));

    const auto   frames = posted_frames(*h.log);
    const Frame* result = find_result(frames, "c3");
    REQUIRE(result != nullptr);
    CHECK(!result->ok);
    CHECK(result->error_code == RpcErrc::SessionClosed);

    // Whether the session detaches fully is an implementation detail; assert
    // only that a further call never produces a successful result.
    h.bridge.feed_message(jb::encode_call("c4", "demo.hang", Json::object()));
    const auto        frames2 = posted_frames(*h.log);
    const Frame*      result2 = find_result(frames2, "c4");
    if (result2 != nullptr)
        CHECK(!result2->ok);
}

TEST_CASE("oversized frames are rejected with PayloadTooLarge", "[orca_jsbridge][bridge]")
{
    BridgeConfig           cfg;
    cfg.max_payload_bytes = 100;
    BridgeHarness          h(cfg);

    REQUIRE(h.bridge.register_api("demo.echo", sync_desc([](const Json& params) { return params; })).ok());

    const Json    big = Json{{"pad", std::string(300, 'x')}};
    const std::string wire = jb::encode_call("c1", "demo.echo", big);
    REQUIRE(wire.size() > cfg.max_payload_bytes);
    h.bridge.feed_message(wire);

    // The rejection may be emitted before the id is even decoded, so accept
    // any error frame; when the id survived it must be PayloadTooLarge.
    const auto frames = posted_frames(*h.log);
    REQUIRE(!frames.empty());
    bool saw_error = false;
    for (const Frame& f : frames)
    {
        if (f.type == "result" && !f.ok)
        {
            saw_error = true;
            if (f.id == "c1")
                CHECK(f.error_code == RpcErrc::PayloadTooLarge);
        }
    }
    CHECK(saw_error);
}

TEST_CASE("emit requires an attached channel", "[orca_jsbridge][bridge]")
{
    Bridge bare;
    const wv::Status st = bare.emit("demo.tick", Json{{"n", 1}});
    CHECK(!st.ok());

    BridgeHarness h;
    const wv::Status ok_st = h.bridge.emit("demo.tick", Json{{"n", 1}});
    CHECK(ok_st.ok());
    // Whether the event is pushed before document load is left open by the
    // header ("only meaningful after document load"); when it is pushed the
    // frame must be a well-formed event frame.
    for (const Frame& f : posted_frames(*h.log))
    {
        if (f.type == "event")
        {
            CHECK(f.event == "demo.tick");
            CHECK(f.params == Json{{"n", 1}});
        }
    }
}

TEST_CASE("detach stops all traffic and feeding stays harmless", "[orca_jsbridge][bridge]")
{
    BridgeHarness h;
    REQUIRE(h.bridge.register_api("demo.echo", sync_desc([](const Json& params) { return params; })).ok());
    const size_t frames_before = h.log->frames.size();

    REQUIRE(h.bridge.detach().ok());
    CHECK(!h.bridge.attached());

    h.bridge.feed_message(jb::encode_call("c1", "demo.echo", Json::object()));
    CHECK(h.log->frames.size() == frames_before);

    // Second detach is a no-op, not a crash.
    CHECK(h.bridge.detach().ok());
}

TEST_CASE("api_list reflects registrations and unregister removes them", "[orca_jsbridge][bridge]")
{
    BridgeHarness h;

    ApiDesc plain;
    plain.sync = [](const Json& params) { return ApiOutcome::ok_value(params); };
    REQUIRE(h.bridge.register_api("demo.echo", plain).ok());

    ApiDesc tagged;
    tagged.permission = "demo.special";
    tagged.sync       = [](const Json&) { return ApiOutcome::ok_value(Json::object()); };
    REQUIRE(h.bridge.register_api("demo.locked", tagged).ok());

    auto has = [](const std::vector<std::pair<std::string, std::string>>& apis, const std::string& method) {
        for (const auto& entry : apis)
        {
            if (entry.first == method)
                return true;
        }
        return false;
    };

    auto apis = h.bridge.api_list();
    CHECK(has(apis, "demo.echo"));
    CHECK(has(apis, "demo.locked"));
    for (const auto& entry : apis)
    {
        if (entry.first == "demo.echo")
            CHECK(entry.second.empty());
        if (entry.first == "demo.locked")
            CHECK(entry.second == "demo.special");
    }

    REQUIRE(h.bridge.unregister_api("demo.echo").ok());
    CHECK(!has(h.bridge.api_list(), "demo.echo"));

    h.bridge.feed_message(jb::encode_call("c1", "demo.echo", Json::object()));
    const auto   frames = posted_frames(*h.log);
    const Frame* result = find_result(frames, "c1");
    REQUIRE(result != nullptr);
    CHECK(!result->ok);
    CHECK(result->error_code == RpcErrc::MethodNotFound);
}

TEST_CASE("register_api rejects malformed and reserved method names", "[orca_jsbridge][bridge]")
{
    BridgeHarness h;

    ApiDesc desc;
    desc.sync = [](const Json& params) { return ApiOutcome::ok_value(params); };

    CHECK(h.bridge.register_api("Demo.echo", desc).code == wv::Errc::InvalidArgument);
    CHECK(h.bridge.register_api("demoecho", desc).code == wv::Errc::InvalidArgument);
    CHECK(h.bridge.register_api("demo.echo.x", desc).code == wv::Errc::InvalidArgument);
    CHECK(h.bridge.register_api("", desc).code == wv::Errc::InvalidArgument);
    // "_orca.*" is reserved for the built-in handshake.
    CHECK(h.bridge.register_api("_orca.custom", desc).code == wv::Errc::InvalidArgument);
}

TEST_CASE("feeding an unattached bridge is harmless", "[orca_jsbridge][bridge]")
{
    Bridge bare;
    bare.feed_message(jb::encode_call("c1", "demo.echo", Json::object()));
    CHECK(bare.pending_count() == 0);
}
