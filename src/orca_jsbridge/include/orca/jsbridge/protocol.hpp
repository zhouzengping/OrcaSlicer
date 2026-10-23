#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

#include "orca/jsbridge/export.h"

namespace orca::jb {

using Json = nlohmann::json;

/*
 * Wire error codes: JSON-RPC 2.0 compatible core plus orca extensions.
 */
enum class RpcErrc : int32_t
{
    ParseError        = -32700,
    InvalidRequest    = -32600,
    MethodNotFound    = -32601,
    InvalidParams     = -32602,
    InternalError     = -32603,
    // Orca extensions (reserved range below -32000)
    PermissionDenied  = -32000,
    Timeout           = -32001,
    NotAttached       = -32002,
    OriginNotAllowed  = -32003,
    HostTooOld        = -32004,
    PayloadTooLarge   = -32005,
    SessionClosed     = -32006,
};

/*
 * Decoded representation of one wire frame (protocol v1):
 *   call   {"orca":1,"type":"call","id":"c17","method":"demo.echo","params":{}}
 *   result {"orca":1,"type":"result","id":"c17","ok":true,"result":{}}
 *   event  {"orca":1,"type":"event","event":"demo.tick","data":{}}
 */
struct Frame
{
    std::string type;    // "call" | "result" | "event"
    std::string id;      // "c17" style; empty for events
    std::string method;  // call only, dot-separated namespace path, 2-8 segments
    std::string event;   // event only
    Json         params;         // call only
    bool         ok = false;     // result only
    Json         result;         // result, ok == true
    RpcErrc      error_code = RpcErrc::InternalError;   // result, ok == false
    std::string  error_message;
    Json         error_data;
};

/*
 * Exception-free codec. decode_frame returns false and fills err on any
 * protocol violation (non-JSON, non-object, missing fields, bad method name).
 */
ORCA_JB_API bool        decode_frame(const std::string& text, Frame& out, RpcErrc& err);
ORCA_JB_API std::string encode_call(const std::string& id, const std::string& method, const Json& params);
ORCA_JB_API std::string encode_result_ok(const std::string& id, const Json& result);
ORCA_JB_API std::string encode_result_err(const std::string& id, RpcErrc code, const std::string& message, const Json& data = Json());
ORCA_JB_API std::string encode_event(const std::string& event, const Json& data);

// Front-end SDK embedded into this DLL (generated from sdk/orca_sdk.js at build time).
ORCA_JB_API const char* sdk_init_script();
ORCA_JB_API const char* sdk_version();       // e.g. "1.0.0"
ORCA_JB_API const char* protocol_version();  // "1"

} // namespace orca::jb
