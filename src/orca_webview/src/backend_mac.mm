// macOS WKWebView backend. Compiled with -fobjc-arc (see CMakeLists.txt).
// ObjC classes must live at global scope, hence the split layout below.

#include "backend_mac.h"

#include <AppKit/AppKit.h>
#include <WebKit/WebKit.h>
#include <dispatch/dispatch.h>

#include <memory>
#include <string>
#include <utility>

namespace orca::wv::detail {
namespace {

// Shared closed flag guarding bridge callbacks (main thread only).
struct MacSharedState
{
    bool closed = false;
};

class BackendMac;

std::string ns_to_utf8(NSString* value)
{
    if (value == nil)
        return std::string();
    const char* utf8 = value.UTF8String;
    return utf8 != nullptr ? std::string(utf8) : std::string();
}

// JSON text is a valid JS expression except for raw U+2028/U+2029, which JSON
// allows inside strings; escape them so evaluateJavaScript is safe everywhere.
std::string to_js_literal(const std::string& json_text)
{
    std::string literal;
    literal.reserve(json_text.size());
    for (size_t i = 0; i < json_text.size(); ++i)
    {
        unsigned char c     = static_cast<unsigned char>(json_text[i]);
        unsigned char next1 = (i + 1 < json_text.size()) ? static_cast<unsigned char>(json_text[i + 1]) : 0;
        unsigned char next2 = (i + 2 < json_text.size()) ? static_cast<unsigned char>(json_text[i + 2]) : 0;
        if (c == 0xE2 && next1 == 0x80 && (next2 == 0xA8 || next2 == 0xA9))
        {
            literal += (next2 == 0xA8) ? "\\u2028" : "\\u2029";
            i += 2;
        }
        else
            literal += json_text[i];
    }
    return literal;
}

NSString* mime_for_path(NSString* path)
{
    static NSDictionary<NSString*, NSString*>* table = @{
        @"html" : @"text/html",
        @"htm" : @"text/html",
        @"js" : @"application/javascript",
        @"css" : @"text/css",
        @"png" : @"image/png",
        @"svg" : @"image/svg+xml",
        @"json" : @"application/json",
        @"gif" : @"image/gif",
        @"ico" : @"image/x-icon",
        @"wasm" : @"application/wasm",
    };
    NSString* mime = table[path.pathExtension.lowercaseString];
    return mime != nil ? mime : @"application/octet-stream";
}

constexpr NSUInteger g_chunk_size = 1 << 20;

} // namespace
} // namespace orca::wv::detail

using OrcaMacBackend = orca::wv::detail::BackendMac;
using OrcaMacState   = orca::wv::detail::MacSharedState;

// ObjC bridge for script messages, navigation/UI delegates and custom scheme
// serving. Holds only weak ties to the backend; every entry point is guarded
// by the shared closed flag.
@interface OrcaMacBridge : NSObject <WKScriptMessageHandler, WKNavigationDelegate, WKUIDelegate, WKURLSchemeHandler>
{
@public
    OrcaMacBackend*                  backend;
    std::shared_ptr<OrcaMacState>    state;
}

@property (nonatomic, strong) NSDictionary<NSString*, NSString*>* schemeRoots;
@property (nonatomic, strong) NSMutableSet<id<WKURLSchemeTask>>*  activeTasks;

@end

namespace orca::wv::detail {
namespace {

class BackendMac final : public Backend
{
public:
    BackendMac(void* native_parent, const WebView::Config& config, EventProxy& proxy)
        : m_parent_view(static_cast<NSView*>(native_parent))
        , m_config(config)
        , m_proxy(proxy)
        , m_state(std::make_shared<MacSharedState>())
    {
    }

    ~BackendMac() override
    {
        close();
    }

    Status start() override
    {
        WKWebViewConfiguration* configuration = [[WKWebViewConfiguration alloc] init];

        if (!m_config.user_agent.empty())
            configuration.customUserAgent = [NSString stringWithUTF8String:m_config.user_agent.c_str()];
        if (m_config.incognito)
            configuration.websiteDataStore = [WKWebsiteDataStore nonPersistentStore];

        // developerExtrasEnabled is not declared by the WKPreferences headers;
        // the KVC key is the established way to toggle the inspector.
        [configuration.preferences setValue:@(m_config.devtools) forKey:@"developerExtrasEnabled"];

        m_bridge = [[OrcaMacBridge alloc] init];
        m_bridge->backend = this;
        m_bridge->state = m_state;

        m_content_controller = configuration.userContentController;
        [m_content_controller addScriptMessageHandler:m_bridge name:@"orca"];

        if (!m_config.custom_schemes.empty())
        {
            NSMutableDictionary* roots = [NSMutableDictionary dictionary];
            for (const CustomScheme& scheme : m_config.custom_schemes)
                roots[[NSString stringWithUTF8String:scheme.scheme.c_str()]] =
                    [NSString stringWithUTF8String:scheme.root_folder.c_str()];
            m_bridge.schemeRoots = roots;
            for (NSString* scheme in roots.allKeys)
                [configuration setURLSchemeHandler:m_bridge forURLScheme:scheme];
        }

        m_webview = [[WKWebView alloc] initWithFrame:NSZeroRect configuration:configuration];
        m_webview.navigationDelegate = m_bridge;
        m_webview.UIDelegate = m_bridge;

        [static_cast<NSView*>(m_parent_view) addSubview:m_webview];
        m_webview.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;

        // TODO: config.color_scheme cannot be forced per view; WKWebView follows
        // the system appearance for prefers-color-scheme.

        // Defer report_created until start() has returned: hosts wire
        // events().on_created only after WebView::create comes back, so a
        // synchronous report would always fire into an empty slot (Windows
        // already reports from the async controller completion). The block
        // keeps the shared state alive and only dereferences the raw proxy
        // pointer while the closed flag is still clear: close() flips it on
        // the same (main) thread before this backend, and with it the proxy's
        // owner, can be destroyed -- the same guard run_script uses.
        std::shared_ptr<MacSharedState> state = m_state;
        EventProxy*                     proxy = &m_proxy;
        dispatch_async(dispatch_get_main_queue(), ^{
            if (state->closed)
                return;
            proxy->report_created();
        });
        return Status::make(Errc::Ok);
    }

    Status navigate(const std::string& url) override
    {
        if (m_webview == nil)
            return Status::make(Errc::InvalidState, "webview is not ready");
        NSURL* ns_url = [NSURL URLWithString:[NSString stringWithUTF8String:url.c_str()]];
        if (ns_url == nil)
            return Status::make(Errc::InvalidArgument, "invalid url");
        [m_webview loadRequest:[NSURLRequest requestWithURL:ns_url]];
        return Status::make(Errc::Ok);
    }

    Status load_html(const std::string& html, const std::string& base_url) override
    {
        if (m_webview == nil)
            return Status::make(Errc::InvalidState, "webview is not ready");
        NSURL* base = base_url.empty() ? nil : [NSURL URLWithString:[NSString stringWithUTF8String:base_url.c_str()]];
        [m_webview loadHTMLString:[NSString stringWithUTF8String:html.c_str()] baseURL:base];
        return Status::make(Errc::Ok);
    }

    Status reload() override
    {
        if (m_webview == nil)
            return Status::make(Errc::InvalidState, "webview is not ready");
        [m_webview reload];
        return Status::make(Errc::Ok);
    }

    Status go_back() override
    {
        if (m_webview == nil)
            return Status::make(Errc::InvalidState, "webview is not ready");
        [m_webview goBack];
        return Status::make(Errc::Ok);
    }

    Status go_forward() override
    {
        if (m_webview == nil)
            return Status::make(Errc::InvalidState, "webview is not ready");
        [m_webview goForward];
        return Status::make(Errc::Ok);
    }

    std::string url() const override
    {
        if (m_webview == nil)
            return std::string();
        return ns_to_utf8(m_webview.URL.absoluteString);
    }

    Status set_bounds(int x, int y, int width, int height) override
    {
        if (m_webview == nil)
            return Status::make(Errc::InvalidState, "webview is not ready");
        // NSView coordinates; the host is responsible for any flip conversion.
        [m_webview setFrame:NSMakeRect(x, y, width, height)];
        return Status::make(Errc::Ok);
    }

    Status set_visible(bool visible) override
    {
        if (m_webview == nil)
            return Status::make(Errc::InvalidState, "webview is not ready");
        m_webview.hidden = !visible;
        return Status::make(Errc::Ok);
    }

    Status add_init_script(const std::string& js) override
    {
        if (m_content_controller == nil)
            return Status::make(Errc::InvalidState, "webview is not ready");
        WKUserScript* script = [[WKUserScript alloc] initWithSource:[NSString stringWithUTF8String:js.c_str()]
                                                      injectionTime:WKUserScriptInjectionTimeAtDocumentStart
                                                   forMainFrameOnly:NO];
        [m_content_controller addUserScript:script];
        return Status::make(Errc::Ok);
    }

    Status run_script(const std::string& js, ScriptDoneFn cb) override
    {
        if (m_webview == nil)
            return Status::make(Errc::InvalidState, "webview is not ready");

        NSString* script = [NSString stringWithUTF8String:js.c_str()];
        std::shared_ptr<MacSharedState> state = m_state;
        [m_webview evaluateJavaScript:script
                    completionHandler:^(id result, NSError* error)
        {
            if (!cb)
                return;
            if (state->closed)
            {
                cb(false, "");
                return;
            }
            if (error != nil)
                cb(false, ns_to_utf8(error.localizedDescription));
            else
                cb(true, result != nil ? ns_to_utf8([result description]) : std::string());
        }];
        return Status::make(Errc::Ok);
    }

    Status post_message_json(const std::string& json_text) override
    {
        if (m_webview == nil)
            return Status::make(Errc::InvalidState, "webview is not ready");
        // WKWebView has no native reverse message channel; deliver through the
        // page-side SDK instead.
        NSString* script = [NSString stringWithFormat:@"window.__orcaSdk && window.__orcaSdk._recv(%@);",
                                      [NSString stringWithUTF8String:to_js_literal(json_text).c_str()]];
        [m_webview evaluateJavaScript:script completionHandler:nil];
        return Status::make(Errc::Ok);
    }

    Status close() override
    {
        if (m_closed)
            return Status::make(Errc::Ok);

        m_closed = true;
        m_state->closed = true;

        if (m_webview != nil)
        {
            [m_content_controller removeScriptMessageHandlerForName:@"orca"];
            [m_content_controller removeAllUserScripts];
            m_webview.navigationDelegate = nil;
            m_webview.UIDelegate = nil;
            [m_webview removeFromSuperview];
            m_webview = nil;
        }
        if (m_bridge != nil)
        {
            m_bridge->backend = nullptr;
            m_bridge = nil;
        }
        m_content_controller = nil;
        return Status::make(Errc::Ok);
    }

    void* native_handle() const override
    {
        return (__bridge void*) m_webview;
    }

    // ---- bridge entry points (main thread, guarded by the closed flag) ----

    void on_message(std::string payload)
    {
        m_proxy.report_message(std::move(payload));
    }

    void on_url_committed(std::string url)
    {
        m_proxy.report_url_changed(std::move(url));
    }

    bool query_navigation(const NavigationState& navigation)
    {
        return m_proxy.query_navigation(navigation);
    }

    void on_new_window(std::string url)
    {
        NewWindowRequest request;
        request.url = url;
        if (m_proxy.query_new_window(request) == OpenDecision::OpenInWebView && !url.empty())
            navigate(url);
    }

private:
    NSView*                         m_parent_view = nil;
    WebView::Config                 m_config;
    EventProxy&                     m_proxy;
    std::shared_ptr<MacSharedState> m_state;

    WKWebView*               m_webview = nil;
    WKUserContentController* m_content_controller = nil;
    OrcaMacBridge*           m_bridge = nil;

    bool m_closed = false;
};

} // namespace
} // namespace orca::wv::detail

@implementation OrcaMacBridge

- (instancetype)init
{
    self = [super init];
    if (self != nil)
        _activeTasks = [NSMutableSet set];
    return self;
}

- (void)userContentController:(WKUserContentController*)userContentController
      didReceiveScriptMessage:(WKScriptMessage*)message
{
    if (state == nullptr || state->closed || backend == nullptr)
        return;

    std::string payload;
    if ([message.body isKindOfClass:[NSString class]])
        payload = orca::wv::detail::ns_to_utf8(static_cast<NSString*>(message.body));
    else
        payload = orca::wv::detail::ns_to_utf8([NSString stringWithFormat:@"%@", message.body]);
    backend->on_message(std::move(payload));
}

- (void)webView:(WKWebView*)webView
    decidePolicyForNavigationAction:(WKNavigationAction*)navigationAction
                    decisionHandler:(void (^)(WKNavigationActionPolicy))decisionHandler
{
    if (state == nullptr || state->closed || backend == nullptr)
    {
        decisionHandler(WKNavigationActionPolicyCancel);
        return;
    }

    orca::wv::NavigationState navigation;
    navigation.url = orca::wv::detail::ns_to_utf8(navigationAction.request.URL.absoluteString);
    navigation.is_main_frame = navigationAction.targetFrame.mainFrame;
    decisionHandler(backend->query_navigation(navigation) ? WKNavigationActionPolicyAllow
                                                          : WKNavigationActionPolicyCancel);
}

// Track committed urls so the bridge can gate APIs by origin (session.origin
// on the jsbridge side). didCommit fires before the new document's scripts
// run, so an SDK hello sent from page scripts always sees the current origin
// -- the analog of WebView2 SourceChanged / GTK notify::uri.
- (void)webView:(WKWebView*)webView didCommitNavigation:(WKNavigation*)navigation
{
    if (state == nullptr || state->closed || backend == nullptr)
        return;

    backend->on_url_committed(orca::wv::detail::ns_to_utf8(webView.URL.absoluteString));
}

- (WKWebView*)webView:(WKWebView*)webView
    createWebViewWithConfiguration:(WKWebViewConfiguration*)configuration
              forNavigationAction:(WKNavigationAction*)navigationAction
                      windowFeatures:(WKWindowFeatures*)windowFeatures
{
    if (state == nullptr || state->closed || backend == nullptr)
        return nil;
    // Never open a native popup; the host decides through on_new_window.
    backend->on_new_window(orca::wv::detail::ns_to_utf8(navigationAction.request.URL.absoluteString));
    return nil;
}

- (void)webView:(WKWebView*)webView startURLSchemeTask:(id<WKURLSchemeTask>)urlSchemeTask
{
    NSString* scheme = urlSchemeTask.request.URL.scheme.lowercaseString;
    NSString* root = self.schemeRoots[scheme];
    if (root == nil)
    {
        [urlSchemeTask didFailWithError:[NSError errorWithDomain:NSURLErrorDomain code:NSURLErrorUnsupportedURL userInfo:nil]];
        return;
    }

    NSString* sub_path = urlSchemeTask.request.URL.path;
    if (sub_path != nil && [sub_path containsString:@".."])
    {
        [urlSchemeTask didFailWithError:[NSError errorWithDomain:NSURLErrorDomain code:NSURLErrorBadURL userInfo:nil]];
        return;
    }

    NSString* relative = ([sub_path length] > 0) ? [sub_path substringFromIndex:1] : @"";
    NSString* full = [root stringByAppendingPathComponent:relative];

    NSDictionary* attributes = [[NSFileManager defaultManager] attributesOfItemAtPath:full error:nil];
    if (attributes == nil)
    {
        [urlSchemeTask didFailWithError:[NSError errorWithDomain:NSCocoaErrorDomain code:NSFileReadNoSuchFileError userInfo:nil]];
        return;
    }
    NSInteger length = [attributes fileSize];

    [self.activeTasks addObject:urlSchemeTask];
    NSURLResponse* response = [[NSURLResponse alloc] initWithURL:urlSchemeTask.request.URL
                                                        MIMEType:orca::wv::detail::mime_for_path(full)
                                           expectedContentLength:length
                                               textEncodingName:nil];
    [urlSchemeTask didReceiveResponse:response];

    // Chunked file streaming. The task is served synchronously so a stop can
    // never interleave, but the active-task set is checked regardless because
    // touching a stopped task crashes WebKit.
    NSFileHandle* handle = [NSFileHandle fileHandleForReadingAtPath:full];
    if (handle == nil)
    {
        [self.activeTasks removeObject:urlSchemeTask];
        [urlSchemeTask didFailWithError:[NSError errorWithDomain:NSCocoaErrorDomain code:NSFileReadNoSuchFileError userInfo:nil]];
        return;
    }
    while (true)
    {
        NSData* chunk = [handle readDataOfLength:orca::wv::detail::g_chunk_size];
        if ([chunk length] == 0)
            break;
        if (![self.activeTasks containsObject:urlSchemeTask])
        {
            [handle closeFile];
            return;
        }
        [urlSchemeTask didReceiveData:chunk];
    }
    [handle closeFile];

    if ([self.activeTasks containsObject:urlSchemeTask])
        [urlSchemeTask didFinishLoading];
    [self.activeTasks removeObject:urlSchemeTask];
}

- (void)webView:(WKWebView*)webView stopURLSchemeTask:(id<WKURLSchemeTask>)urlSchemeTask
{
    [self.activeTasks removeObject:urlSchemeTask];
}

@end

namespace orca::wv::detail {

Backend* create_backend_mac(void* native_parent, const WebView::Config& config, EventProxy& proxy)
{
    return new BackendMac(native_parent, config, proxy);
}

std::string platform_runtime_version()
{
    // WKWebView ships with the OS; the system build identifies the engine.
    return "macOS " + ns_to_utf8([[NSProcessInfo processInfo] operatingSystemVersionString]);
}

} // namespace orca::wv::detail
