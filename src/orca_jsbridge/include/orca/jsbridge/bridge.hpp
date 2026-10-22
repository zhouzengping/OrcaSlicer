#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "orca/jsbridge/channel.hpp"
#include "orca/jsbridge/export.h"
#include "orca/jsbridge/protocol.hpp"
#include "orca/webview/types.hpp"

namespace orca::jb {

/*
 * Exception-free synchronous handler result.
 */
struct ORCA_JB_API ApiOutcome
{
    bool        ok = true;
    Json        value;
    RpcErrc     code = RpcErrc::InternalError;
    std::string message;

    static ApiOutcome ok_value(Json value);
    static ApiOutcome error(RpcErrc code, std::string message);
};

/*
 * Async responder for long-running handlers. Copyable; send_result /
 * send_error must be called exactly once. May be invoked from ANY thread:
 * internally marshals through BridgeConfig.dispatcher back to the UI thread
 * before writing to the channel.
 */
class ORCA_JB_API Responder
{
public:
    void send_result(Json result);
    void send_error(RpcErrc code, const std::string& message, Json data = Json());

private:
    friend class Bridge;

    struct Ctx;
    explicit Responder(std::shared_ptr<Ctx> ctx);
    std::shared_ptr<Ctx> m_ctx;   // request id + weak bridge ref + deadline
};

using SyncHandler  = std::function<ApiOutcome(const Json& params)>;
using AsyncHandler = std::function<void(const Json& params, Responder responder)>;

/*
 * API registration descriptor. Exactly one of sync / async must be set.
 * permission: "" = origin check only (safe, read-only APIs); otherwise a
 * permission tag such as "device.read" checked against the session policy.
 */
struct ApiDesc
{
    std::string permission;
    SyncHandler  sync;
    AsyncHandler async;
};

/*
 * Identity of the attached page, surfaced to the permission policy.
 */
struct SessionInfo
{
    std::string session_id;   // opaque host tag, e.g. plugin id or "dev-window"
    std::string origin;       // tracked from webview url changes
    Json        context;      // free-form host data
};

enum class PermissionDecision : uint8_t
{
    Allow,
    Deny,
};

using PermissionPolicy = std::function<PermissionDecision(
    const SessionInfo& session, const std::string& method, const std::string& permission_tag)>;

struct BridgeConfig
{
    std::vector<std::string> allowed_origins;   // exact match, e.g. "https://host[:port]" or "orca-plugin://id"

    // Gate for APIs whose ApiDesc.permission is non-empty. A null policy
    // denies every tagged API (deny by default); "" permission never
    // consults the policy.
    PermissionPolicy policy;

    uint32_t api_timeout_ms    = 30000;         // host-side deadline; 0 = unlimited
    size_t   max_payload_bytes = 4u * 1024 * 1024;
    uint32_t max_pending_calls = 64;            // flood guard; exceeding detaches with SessionClosed

    // Host UI-thread marshal, e.g. [](std::function<void()> fn){ wxCallAfter(fn); }.
    // Required when async handlers finish on worker threads; may be null when
    // every handler completes on the UI thread.
    std::function<void(std::function<void()>)> dispatcher;
};

/*
 * One bridge instance per webview session. Attaches the SDK, decodes call
 * frames, enforces origin + permission policy and pushes events.
 */
class ORCA_JB_API Bridge
{
public:
    Bridge();
    ~Bridge();
    Bridge(const Bridge&) = delete;
    Bridge& operator=(const Bridge&) = delete;

    /*
     * Attach to a webview: injects sdk_init_script() and hooks on_message /
     * on_url_changed. Call while the view is Creating so the SDK lands at
     * document-start of the first navigation.
     */
    wv::Status attach(const std::shared_ptr<wv::WebView>& view, const BridgeConfig& cfg, SessionInfo session);

    // Headless / test entry over any Channel; incoming frames are fed via feed_message().
    wv::Status attach_channel(std::unique_ptr<Channel> channel, const BridgeConfig& cfg, SessionInfo session);

    wv::Status detach();
    bool       attached() const;

    // method must match ^[a-z0-9_]+\.[a-z0-9_]+$; "_orca.*" is reserved.
    wv::Status register_api(const std::string& method, const ApiDesc& desc);
    wv::Status unregister_api(const std::string& method);

    // (method, permission) pairs, consumed by the hello handshake.
    std::vector<std::pair<std::string, std::string>> api_list() const;

    // Push an event frame to the page. Only meaningful after document load.
    wv::Status emit(const std::string& event, const Json& data);

    const SessionInfo& session() const;

    // ---- internal / test hooks ----
    void   feed_message(const std::string& raw);   // one wire frame from the page
    size_t pending_count() const;
    void   sweep_deadlines();                      // reclaim expired calls; called on activity

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace orca::jb
