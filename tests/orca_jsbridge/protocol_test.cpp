/*
 * Wire protocol v1 codec tests: encode -> decode round trips plus every
 * documented rejection path.
 */

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "orca/jsbridge/protocol.hpp"

using orca::jb::decode_frame;
using orca::jb::encode_call;
using orca::jb::encode_event;
using orca::jb::encode_result_err;
using orca::jb::encode_result_ok;
using orca::jb::Frame;
using orca::jb::Json;
using orca::jb::RpcErrc;

namespace
{

Json parse_json(const std::string& text)
{
    // Exception-free parse; returns discarded on failure.
    return Json::parse(text, nullptr, false);
}

} // namespace

TEST_CASE("call frames round trip", "[orca_jsbridge][protocol]")
{
    const Json          params = Json{{"text", "hi"}, {"n", 7}, {"nested", Json{{"ok", true}}}};
    const std::string   wire   = encode_call("c17", "demo.echo", params);

    Frame               frame;
    RpcErrc             err = RpcErrc::InternalError;
    REQUIRE(decode_frame(wire, frame, err));
    CHECK(frame.type == "call");
    CHECK(frame.id == "c17");
    CHECK(frame.method == "demo.echo");
    CHECK(frame.params == params);
}

TEST_CASE("ok result frames round trip", "[orca_jsbridge][protocol]")
{
    const std::string wire = encode_result_ok("c17", Json{{"answer", 42}});

    Frame             frame;
    RpcErrc           err = RpcErrc::InternalError;
    REQUIRE(decode_frame(wire, frame, err));
    CHECK(frame.type == "result");
    CHECK(frame.id == "c17");
    CHECK(frame.ok);
    CHECK(frame.result == Json{{"answer", 42}});
}

TEST_CASE("error result frames round trip", "[orca_jsbridge][protocol]")
{
    const std::string wire = encode_result_err("c18", RpcErrc::PermissionDenied, "denied", Json{{"tag", "demo.special"}});

    Frame             frame;
    RpcErrc           err = RpcErrc::InternalError;
    REQUIRE(decode_frame(wire, frame, err));
    CHECK(frame.type == "result");
    CHECK(frame.id == "c18");
    CHECK(!frame.ok);
    CHECK(frame.error_code == RpcErrc::PermissionDenied);
    CHECK(frame.error_message == "denied");
    CHECK(frame.error_data == Json{{"tag", "demo.special"}});
}

TEST_CASE("error result without data round trips", "[orca_jsbridge][protocol]")
{
    const std::string wire = encode_result_err("c19", RpcErrc::InternalError, "boom");

    Frame             frame;
    RpcErrc           err = RpcErrc::InternalError;
    REQUIRE(decode_frame(wire, frame, err));
    CHECK(!frame.ok);
    CHECK(frame.error_code == RpcErrc::InternalError);
    CHECK(frame.error_message == "boom");
}

TEST_CASE("event frames round trip", "[orca_jsbridge][protocol]")
{
    const std::string wire = encode_event("demo.tick", Json{{"n", 3}});

    Frame             frame;
    RpcErrc           err = RpcErrc::InternalError;
    REQUIRE(decode_frame(wire, frame, err));
    CHECK(frame.type == "event");
    CHECK(frame.event == "demo.tick");
    // Event payloads decode into Frame::params (the wire field is "data").
    CHECK(frame.params == Json{{"n", 3}});
}

TEST_CASE("malformed payloads are rejected with the documented codes", "[orca_jsbridge][protocol]")
{
    Frame   frame;
    RpcErrc err = RpcErrc::InternalError;

    SECTION("not JSON at all")
    {
        REQUIRE(!decode_frame("{ this is not json", frame, err));
        CHECK(err == RpcErrc::ParseError);
    }
    SECTION("JSON but not an object")
    {
        REQUIRE(!decode_frame("[1,2,3]", frame, err));
        CHECK(err == RpcErrc::InvalidRequest);
    }
    SECTION("missing the orca header")
    {
        REQUIRE(!decode_frame(R"({"type":"call","id":"c1","method":"demo.echo","params":{}})", frame, err));
        CHECK(err == RpcErrc::InvalidRequest);
    }
    SECTION("wrong protocol header value")
    {
        REQUIRE(!decode_frame(R"({"orca":2,"type":"call","id":"c1","method":"demo.echo","params":{}})", frame, err));
        CHECK(err == RpcErrc::InvalidRequest);
    }
    SECTION("params is not an object")
    {
        REQUIRE(!decode_frame(R"({"orca":1,"type":"call","id":"c1","method":"demo.echo","params":[1,2]})", frame, err));
        CHECK(err == RpcErrc::InvalidParams);
    }
}

TEST_CASE("namespace-path method names decode as calls", "[orca_jsbridge][protocol]")
{
    /*
     * Method names are dot-separated namespace paths of 2..8 non-empty
     * [a-zA-Z0-9_]+ segments. Every namespace level may host an API of its
     * own ("orca.preset.info" and "orca.preset.filament.getfilaments"
     * coexist); camelCase verb segments are allowed; matching is
     * case-sensitive.
     */
    const std::vector<std::string> names = {
        "demo.echo",                          // 2 segments: legacy shape stays valid
        "orca.preset.info",                   // 3 segments: intermediate node as an API
        "orca.preset.filament.getfilaments",  // 4 segments
        "orca.preset.process.getxxspeed",     // 4 segments
        "orca.preset.getPresetnum",           // camelCase verb segment
        "Demo.echo",                          // mixed case segments
        "a.b.c.d.e.f.g.h",                    // 8 segments: upper bound
    };

    for (const std::string& method : names)
    {
        CAPTURE(method);
        Frame   frame;
        RpcErrc err = RpcErrc::InternalError;
        REQUIRE(decode_frame(encode_call("c1", method, Json::object()), frame, err));
        CHECK(frame.type == "call");
        CHECK(frame.method == method);
    }
}

TEST_CASE("illegal call identifiers and methods are rejected", "[orca_jsbridge][protocol]")
{
    Frame   frame;
    RpcErrc err = RpcErrc::InternalError;

    // The exact error code for these is not pinned by the header comment, so
    // only the rejection itself is asserted.
    SECTION("method without a dot")
    {
        REQUIRE(!decode_frame(R"({"orca":1,"type":"call","id":"c1","method":"demoecho","params":{}})", frame, err));
    }
    SECTION("method with nine segments")
    {
        REQUIRE(!decode_frame(R"({"orca":1,"type":"call","id":"c1","method":"a.b.c.d.e.f.g.h.i","params":{}})", frame, err));
    }
    SECTION("method with consecutive dots")
    {
        REQUIRE(!decode_frame(R"({"orca":1,"type":"call","id":"c1","method":"demo..echo","params":{}})", frame, err));
    }
    SECTION("method with a leading dot")
    {
        REQUIRE(!decode_frame(R"({"orca":1,"type":"call","id":"c1","method":".demo.echo","params":{}})", frame, err));
    }
    SECTION("method with a trailing dot")
    {
        REQUIRE(!decode_frame(R"({"orca":1,"type":"call","id":"c1","method":"demo.echo.","params":{}})", frame, err));
    }
    SECTION("method with an illegal character")
    {
        REQUIRE(!decode_frame(R"({"orca":1,"type":"call","id":"c1","method":"demo.echo-x","params":{}})", frame, err));
    }
    SECTION("empty method")
    {
        REQUIRE(!decode_frame(R"({"orca":1,"type":"call","id":"c1","method":"","params":{}})", frame, err));
    }
    SECTION("id longer than 64 bytes")
    {
        const std::string long_id(65, 'x');
        REQUIRE(!decode_frame(R"({"orca":1,"type":"call","id":")" + long_id + R"(","method":"demo.echo","params":{}})", frame, err));
    }
}

TEST_CASE("embedded SDK accessors return usable values", "[orca_jsbridge][protocol]")
{
    // The SDK script is embedded at build time; these only sanity-check that
    // the DLL exports resolve and are non-empty.
    CHECK(std::string(orca::jb::sdk_init_script()).size() > 0);
    CHECK(std::string(orca::jb::sdk_version()).size() > 0);
    CHECK(std::string(orca::jb::protocol_version()) == "1");
}
