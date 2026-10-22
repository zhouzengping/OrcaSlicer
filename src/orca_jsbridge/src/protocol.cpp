#include "orca/jsbridge/protocol.hpp"

#include "sdk_script.hpp"

#include <cstddef>
#include <utility>

namespace orca::jb {

namespace
{

constexpr std::size_t g_max_id_bytes = 64;

/*
 * Strict field readers. nlohmann value()/at() throw on type mismatch and this
 * codec is exception-free, so every access goes through contains() + is_*().
 */
bool read_string(const Json& obj, const char* key, std::string& out)
{
    if (!obj.is_object() || !obj.contains(key) || !obj[key].is_string())
        return false;

    out = obj[key].get<std::string>();
    return true;
}

bool valid_id(const Json& obj, std::string& id)
{
    return read_string(obj, "id", id) && !id.empty() && id.size() <= g_max_id_bytes;
}

// [a-z0-9_]+ between [from, to)
bool valid_segment(const std::string& name, std::size_t from, std::size_t to)
{
    if (from >= to)
        return false;

    for (std::size_t i = from; i < to; ++i)
    {
        const char c = name[i];
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        if (!ok)
            return false;
    }
    return true;
}

} // namespace

namespace detail {

bool valid_method_name(const std::string& name)
{
    // Exactly one dot; both segments non-empty [a-z0-9_]*.
    const std::string::size_type dot = name.find('.');
    if (dot == std::string::npos)
        return false;

    if (name.find('.', dot + 1) != std::string::npos)
        return false;

    return valid_segment(name, 0, dot) && valid_segment(name, dot + 1, name.size());
}

} // namespace detail

bool decode_frame(const std::string& text, Frame& out, RpcErrc& err)
{
    err = RpcErrc::ParseError;

    const Json parsed = Json::parse(text, nullptr, false);
    if (parsed.is_discarded())
        return false;

    if (!parsed.is_object())
    {
        err = RpcErrc::InvalidRequest;
        return false;
    }

    if (!parsed.contains("orca") || !parsed["orca"].is_number_integer() || parsed["orca"].get<int>() != 1)
    {
        err = RpcErrc::InvalidRequest;
        return false;
    }

    std::string type;
    if (!read_string(parsed, "type", type) || (type != "call" && type != "result" && type != "event"))
    {
        err = RpcErrc::InvalidRequest;
        return false;
    }

    Frame frame;
    frame.type = type;

    if (type == "call")
    {
        if (!valid_id(parsed, frame.id))
        {
            err = RpcErrc::InvalidRequest;
            return false;
        }

        if (!read_string(parsed, "method", frame.method) || !detail::valid_method_name(frame.method))
        {
            err = RpcErrc::InvalidRequest;
            return false;
        }

        // params must be an object; null / missing normalize to an empty object.
        if (!parsed.contains("params") || parsed["params"].is_null())
        {
            frame.params = Json::object();
        }
        else if (parsed["params"].is_object())
        {
            frame.params = parsed["params"];
        }
        else
        {
            // params present but not an object: a params problem per JSON-RPC.
            err = RpcErrc::InvalidParams;
            return false;
        }
    }
    else if (type == "result")
    {
        if (!valid_id(parsed, frame.id))
        {
            err = RpcErrc::InvalidRequest;
            return false;
        }

        if (!parsed.contains("ok") || !parsed["ok"].is_boolean())
        {
            err = RpcErrc::InvalidRequest;
            return false;
        }

        frame.ok = parsed["ok"].get<bool>();

        if (frame.ok)
        {
            if (parsed.contains("result") && !parsed["result"].is_null())
                frame.result = parsed["result"];
        }
        else
        {
            if (!parsed.contains("error") || !parsed["error"].is_object())
            {
                err = RpcErrc::InvalidRequest;
                return false;
            }

            const Json& error = parsed["error"];
            if (!error.contains("code") || !error["code"].is_number_integer())
            {
                err = RpcErrc::InvalidRequest;
                return false;
            }

            frame.error_code = static_cast<RpcErrc>(error["code"].get<int32_t>());
            read_string(error, "message", frame.error_message);

            if (error.contains("data") && !error["data"].is_null())
                frame.error_data = error["data"];
        }
    }
    else
    {
        // Event frames reuse Frame::params for the payload (the struct has no
        // dedicated data field).
        if (!read_string(parsed, "event", frame.event) || frame.event.empty())
        {
            err = RpcErrc::InvalidRequest;
            return false;
        }

        if (parsed.contains("data") && !parsed["data"].is_null())
            frame.params = parsed["data"];
    }

    out = std::move(frame);
    return true;
}

std::string encode_call(const std::string& id, const std::string& method, const Json& params)
{
    Json frame = Json::object();
    frame["orca"]   = 1;
    frame["type"]   = "call";
    frame["id"]     = id;
    frame["method"] = method;
    frame["params"] = params.is_null() ? Json::object() : params;
    return frame.dump();
}

std::string encode_result_ok(const std::string& id, const Json& result)
{
    Json frame = Json::object();
    frame["orca"]   = 1;
    frame["type"]   = "result";
    frame["id"]     = id;
    frame["ok"]     = true;
    frame["result"] = result;
    return frame.dump();
}

std::string encode_result_err(const std::string& id, RpcErrc code, const std::string& message, const Json& data)
{
    Json error = Json::object();
    error["code"]    = static_cast<int32_t>(code);
    error["message"] = message;
    if (!data.is_null())
        error["data"] = data;

    Json frame = Json::object();
    frame["orca"]  = 1;
    frame["type"]  = "result";
    frame["id"]    = id;
    frame["ok"]    = false;
    frame["error"] = std::move(error);
    return frame.dump();
}

std::string encode_event(const std::string& event, const Json& data)
{
    Json frame = Json::object();
    frame["orca"]  = 1;
    frame["type"]  = "event";
    frame["event"] = event;
    frame["data"]  = data.is_null() ? Json::object() : data;
    return frame.dump();
}

const char* sdk_init_script()
{
    return detail::kSdkInitScript;
}

const char* sdk_version()
{
    return "1.0.0";
}

const char* protocol_version()
{
    return "1";
}

} // namespace orca::jb
