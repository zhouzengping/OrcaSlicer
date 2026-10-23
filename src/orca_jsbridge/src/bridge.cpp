#include "orca/jsbridge/bridge.hpp"

#include "sdk_script.hpp"

#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace orca::jb {

namespace
{

constexpr const char* g_hello_method      = "_orca.hello";
constexpr const char* g_reserved_prefix   = "_orca.";
constexpr const char* g_host_version_key  = "host_version";

// scheme://host[:port] of an absolute url, path stripped; empty when the url
// carries no scheme separator.
std::string extract_origin(const std::string& url)
{
    const std::string::size_type sep = url.find("://");
    if (sep == std::string::npos || sep == 0)
        return std::string();

    const std::string::size_type end = url.find_first_of("/?#", sep + 3);
    if (end == std::string::npos)
        return url;

    return url.substr(0, end);
}

/*
 * Bridge state in a shared_ptr so Responder contexts can reference it weakly:
 * an async handler answering after the Bridge is destroyed locks a dead weak
 * pointer and turns into a no-op instead of touching freed state.
 */
class BridgeCore
{
public:
    wv::Status post(const std::string& json_text)
    {
        if (!attached || channel == nullptr)
            return wv::Status::make(wv::Errc::InvalidState, "bridge is not attached");

        return channel->post_message_json(json_text);
    }

    // Runs on the UI thread (directly or after dispatcher marshaling).
    void complete(const std::string& id, std::chrono::steady_clock::time_point deadline, const std::string& encoded)
    {
        if (!attached)
            return;

        auto it = pending.find(id);
        if (it == pending.end())
            return;   // answered, swept or detached; also makes double-send a no-op

        // Deadline the lazy sweep has not reclaimed yet.
        if (cfg.api_timeout_ms != 0 && std::chrono::steady_clock::now() > deadline)
        {
            pending.erase(it);
            return;
        }

        pending.erase(it);
        post(encoded);
    }

    void sweep()
    {
        if (cfg.api_timeout_ms == 0 || pending.empty())
            return;

        const auto now = std::chrono::steady_clock::now();
        for (auto it = pending.begin(); it != pending.end();)
        {
            if (now > it->second)
                it = pending.erase(it);
            else
                ++it;
        }
    }

    bool origin_allowed() const
    {
        if (cfg.allowed_origins.empty())
            return true;

        for (const std::string& allowed : cfg.allowed_origins)
        {
            if (allowed == session.origin)
                return true;
        }
        return false;
    }

    Json hello_result() const
    {
        Json result = Json::object();
        result["protocol"]     = 1;
        result["sdk_version"]  = sdk_version();
        result["host_version"] = host_version();
        result["capabilities"] = Json::array({"json", "timeout"});

        Json api_array = Json::array();
        for (const auto& entry : apis)
        {
            Json api = Json::object();
            api["method"]     = entry.first;
            api["permission"] = entry.second.permission;
            api_array.push_back(std::move(api));
        }
        result["apis"] = std::move(api_array);
        return result;
    }

    // ---- state (UI thread; async Responder entry marshals back first) ----

    std::unique_ptr<Channel> channel;
    BridgeConfig             cfg;
    SessionInfo              session;
    std::map<std::string, ApiDesc> apis;   // ordered: deterministic hello api list
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> pending;
    bool attached = false;

private:
    // BridgeConfig has no host-version field by contract; hosts surface their
    // version through the free-form session context.
    std::string host_version() const
    {
        if (session.context.is_object() && session.context.contains(g_host_version_key) && session.context[g_host_version_key].is_string())
            return session.context[g_host_version_key].get<std::string>();

        return std::string();
    }
};

void hard_detach(BridgeCore& core)
{
    core.channel.reset();
    core.pending.clear();
    core.attached = false;
}

} // namespace

ApiOutcome ApiOutcome::ok_value(Json value)
{
    ApiOutcome outcome;
    outcome.ok    = true;
    outcome.value = std::move(value);
    return outcome;
}

ApiOutcome ApiOutcome::error(RpcErrc code, std::string message)
{
    ApiOutcome outcome;
    outcome.ok      = false;
    outcome.code    = code;
    outcome.message = std::move(message);
    return outcome;
}

struct Responder::Ctx
{
    std::string id;
    std::chrono::steady_clock::time_point deadline;
    std::weak_ptr<BridgeCore> core;
};

Responder::Responder(std::shared_ptr<Ctx> ctx)
    : m_ctx(std::move(ctx))
{
}

void Responder::send_result(Json result)
{
    if (m_ctx == nullptr)
        return;

    std::shared_ptr<BridgeCore> core = m_ctx->core.lock();
    if (core == nullptr)
        return;   // bridge destroyed or detached

    // Holding the core alive across the dispatch closes the gap between
    // enqueue and run; complete() still checks attached / pending.
    auto task = [core, ctx = m_ctx, result = std::move(result)]()
    {
        core->complete(ctx->id, ctx->deadline, encode_result_ok(ctx->id, result));
    };

    if (core->cfg.dispatcher)
        core->cfg.dispatcher(std::move(task));
    else
        task();   // host contract: caller is already on the UI thread
}

void Responder::send_error(RpcErrc code, const std::string& message, Json data)
{
    if (m_ctx == nullptr)
        return;

    std::shared_ptr<BridgeCore> core = m_ctx->core.lock();
    if (core == nullptr)
        return;

    auto task = [core, ctx = m_ctx, code, message, data = std::move(data)]()
    {
        core->complete(ctx->id, ctx->deadline, encode_result_err(ctx->id, code, message, data));
    };

    if (core->cfg.dispatcher)
        core->cfg.dispatcher(std::move(task));
    else
        task();
}

struct Bridge::Impl
{
    std::shared_ptr<BridgeCore> core = std::make_shared<BridgeCore>();
    std::weak_ptr<wv::WebView> view;   // hooked view, released on detach
};

Bridge::Bridge()
    : m_impl(std::make_unique<Impl>())
{
}

Bridge::~Bridge()
{
    // Releases the view event hooks while the view may still be alive.
    detach();
}

wv::Status Bridge::attach(const std::shared_ptr<wv::WebView>& view, const BridgeConfig& cfg, SessionInfo session)
{
    if (view == nullptr)
        return wv::Status::make(wv::Errc::InvalidArgument, "webview is null");

    std::unique_ptr<Channel> channel = make_webview_channel(view);
    if (channel == nullptr)
        return wv::Status::make(wv::Errc::InvalidState, "webview channel is unavailable");

    wv::Status status = attach_channel(std::move(channel), cfg, std::move(session));
    if (!status.ok())
        return status;

    // Document-start injection: queues while the view is Creating and passes
    // straight through once Ready.
    status = m_impl->core->channel->add_init_script(sdk_init_script());
    if (!status.ok())
    {
        // Event hooks are not installed yet; roll back without touching them.
        m_impl->view.reset();
        hard_detach(*m_impl->core);
        return status;
    }

    m_impl->view = view;

    // A WebView exposes exactly one on_message slot: the bridge owns the
    // view's message channel and replaces whatever handler was set before.
    // Captures the Bridge raw; the destructor detaches and clears the hooks
    // while the view is still alive, so the slot never dangles.
    view->events().on_message = [this](const std::string& message)
    {
        feed_message(message);
    };

    view->events().on_url_changed = [core = std::weak_ptr<BridgeCore>(m_impl->core)](const std::string& url)
    {
        if (std::shared_ptr<BridgeCore> live = core.lock(); live != nullptr)
            live->session.origin = extract_origin(url);
    };

    return wv::Status::make(wv::Errc::Ok);
}

wv::Status Bridge::attach_channel(std::unique_ptr<Channel> channel, const BridgeConfig& cfg, SessionInfo session)
{
    if (channel == nullptr)
        return wv::Status::make(wv::Errc::InvalidArgument, "channel is null");

    if (m_impl->core->attached)
        return wv::Status::make(wv::Errc::InvalidState, "bridge is already attached");

    BridgeCore& core = *m_impl->core;
    core.channel  = std::move(channel);
    core.cfg      = cfg;
    core.session  = std::move(session);
    core.pending.clear();

    // Same contract as attach(): the SDK rides the channel as an init script
    // so it lands at document-start of every navigation.
    if (!core.channel->add_init_script(sdk_init_script()).ok())
    {
        core.channel.reset();
        core.pending.clear();
        return wv::Status::make(wv::Errc::InvalidState, "channel rejected the SDK init script");
    }

    core.attached = true;
    return wv::Status::make(wv::Errc::Ok);
}

wv::Status Bridge::detach()
{
    BridgeCore& core = *m_impl->core;
    if (!core.attached)
        return wv::Status::make(wv::Errc::Ok);

    if (std::shared_ptr<wv::WebView> view = m_impl->view.lock(); view != nullptr)
    {
        // Release the event slots the bridge owns; std::function slots accept null.
        view->events().on_message    = nullptr;
        view->events().on_url_changed = nullptr;
    }

    m_impl->view.reset();
    hard_detach(core);
    return wv::Status::make(wv::Errc::Ok);
}

bool Bridge::attached() const
{
    return m_impl->core->attached;
}

wv::Status Bridge::register_api(const std::string& method, const ApiDesc& desc)
{
    if (!detail::valid_method_name(method))
        return wv::Status::make(wv::Errc::InvalidArgument, "method must match ^[a-zA-Z0-9_]+(\\.[a-zA-Z0-9_]+){1,7}$");

    if (method.rfind(g_reserved_prefix, 0) == 0)
        return wv::Status::make(wv::Errc::InvalidArgument, "\"_orca.*\" methods are reserved");

    const bool has_sync  = static_cast<bool>(desc.sync);
    const bool has_async = static_cast<bool>(desc.async);
    if (has_sync == has_async)
        return wv::Status::make(wv::Errc::InvalidArgument, "exactly one of sync/async must be set");

    m_impl->core->apis[method] = desc;
    return wv::Status::make(wv::Errc::Ok);
}

wv::Status Bridge::unregister_api(const std::string& method)
{
    m_impl->core->apis.erase(method);
    return wv::Status::make(wv::Errc::Ok);
}

std::vector<std::pair<std::string, std::string>> Bridge::api_list() const
{
    std::vector<std::pair<std::string, std::string>> list;
    list.reserve(m_impl->core->apis.size());

    for (const auto& entry : m_impl->core->apis)
        list.emplace_back(entry.first, entry.second.permission);

    return list;
}

wv::Status Bridge::emit(const std::string& event, const Json& data)
{
    return m_impl->core->post(encode_event(event, data));
}

const SessionInfo& Bridge::session() const
{
    return m_impl->core->session;
}

void Bridge::feed_message(const std::string& raw)
{
    BridgeCore& core = *m_impl->core;
    if (!core.attached)
        return;

    if (raw.size() > core.cfg.max_payload_bytes)
    {
        // Oversized but well-formed calls still get their id back; anything
        // else cannot be correlated and is dropped.
        Frame frame;
        RpcErrc err = RpcErrc::ParseError;
        if (decode_frame(raw, frame, err) && frame.type == "call")
            core.post(encode_result_err(frame.id, RpcErrc::PayloadTooLarge, "frame exceeds max_payload_bytes"));
        return;
    }

    Frame frame;
    RpcErrc err = RpcErrc::ParseError;
    if (!decode_frame(raw, frame, err))
        return;   // no recoverable id -> drop silently

    core.sweep();

    if (frame.type != "call")
        return;   // pages never send result/event frames to the host

    // Applies to _orca.hello too: the SDK injects before the first navigation,
    // so the origin is already known when the first frame lands.
    if (!core.origin_allowed())
    {
        core.post(encode_result_err(frame.id, RpcErrc::OriginNotAllowed, "origin is not allowed"));
        return;
    }

    if (core.pending.size() >= core.cfg.max_pending_calls)
    {
        // Flood guard: answer, then drop the session.
        core.post(encode_result_err(frame.id, RpcErrc::SessionClosed, "too many pending calls"));
        detach();
        return;
    }

    if (frame.method == g_hello_method)
    {
        core.post(encode_result_ok(frame.id, core.hello_result()));
        return;
    }

    auto it = core.apis.find(frame.method);
    if (it == core.apis.end())
    {
        core.post(encode_result_err(frame.id, RpcErrc::MethodNotFound, "unknown method: " + frame.method));
        return;
    }

    const ApiDesc& desc = it->second;

    // Permission chain: "" tags are always allowed; tagged APIs need an
    // allowing policy (deny by default when no policy is configured).
    if (!desc.permission.empty())
    {
        PermissionDecision decision = PermissionDecision::Deny;
        if (core.cfg.policy)
            decision = core.cfg.policy(core.session, frame.method, desc.permission);

        if (decision != PermissionDecision::Allow)
        {
            core.post(encode_result_err(frame.id, RpcErrc::PermissionDenied, "permission denied: " + desc.permission));
            return;
        }
    }

    if (desc.sync)
    {
        // Sync handlers run inline on the UI thread.
        ApiOutcome outcome = desc.sync(frame.params);
        if (outcome.ok)
            core.post(encode_result_ok(frame.id, outcome.value));
        else
            core.post(encode_result_err(frame.id, outcome.code, outcome.message.empty() ? "api call failed" : outcome.message));
        return;
    }

    if (desc.async)
    {
        const auto deadline = core.cfg.api_timeout_ms == 0
            ? std::chrono::steady_clock::time_point::max()
            : std::chrono::steady_clock::now() + std::chrono::milliseconds(core.cfg.api_timeout_ms);

        core.pending[frame.id] = deadline;

        auto ctx = std::make_shared<Responder::Ctx>();
        ctx->id       = frame.id;
        ctx->deadline = deadline;
        ctx->core     = m_impl->core;

        // The handler may answer synchronously; complete() settles the pending
        // entry before posting, so reentrancy is safe. Exactly one send is the
        // handler's contract; a second send finds no pending entry and no-ops.
        desc.async(frame.params, Responder(std::move(ctx)));
    }
}

size_t Bridge::pending_count() const
{
    return m_impl->core->pending.size();
}

void Bridge::sweep_deadlines()
{
    m_impl->core->sweep();
}

} // namespace orca::jb
