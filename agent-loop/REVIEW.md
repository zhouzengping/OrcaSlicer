# Review 记录（REVIEWER → EXECUTOR）

按时间追加。EXECUTOR 开新块前先读最新一节。

---

## Round 1 / review 1 — 6c283fe051 "groundwork: webview host base + plugin center skeletons"

- [MUST FIX] **legacy wxWebView 下行通道被静默切断（回归，最高优先级）**
  `SSWCP.cpp` `send_to_js()` 与 `send_message_auto()` 改为只走 `dispatch_to_sink()`，
  但 sink 注册表里只有 `WebViewPageBase`（WebViewPageBase.cpp:107）注册过。
  PrinterWebView / WebDeviceDialog / WebPreprintDialog / WebUrlDialog / WebViewDialog /
  MonitorPanel / WebViewPanel 等 legacy 页面从未注册 sink → 它们的 SSWCP 回包
  全部被 `dispatch_to_sink` 返回 false 后静默丢弃。
  修法：在 SSWCP 提供
  `static void register_wx_sink(wxWebView* view)` /
  `static void unregister_wx_sink(wxWebView* view)`
  helper（内部:CallAfter marshal + `view->GetRefData()` 存活检查 + `WebView::RunScript`），
  并接入上述每个 legacy 页面的构造/析构（或集中在一个公共基类/工厂处挂摘）。
  这是 1.1 之前必须先修的块，commit 为 `[plugin-demo] fix legacy wxwebview downlink`。
  修完自证：在 Monitor 页触发任意 sw_* 回包能到页面（日志或断点）。

- [MUST FIX] **timelapse 订阅清理兜底被删**
  `sw_SubscribeDownloadState()` 原来的 `wxEVT_DESTROY` 自动清理被移除，注释改为依赖
  `SSWCP::on_webview_delete()`。目前 grep 只见 5 个 Dialog 调用它。请逐一确认 MonitorPanel
  与主窗口常驻 webview 的销毁路径也调用 `on_webview_delete`；任何缺调的路径补上，
  或恢复一段等价的兜底。结论（确认清单）写进 FEEDBACK.md。

- [OK] WebViewHandle(void*) 机械替换完整，静态 map 同步更新。
- [OK] sink registry 线程安全：锁内拷贝 std::function、锁外调用，无死锁风险。
- [OK] WebViewPageBase 生命周期（析构在原生 HWND 存活时 close_view；adopt 路径不越权）。
- [OK] plugin_center 前端（app.js 479 行）+ sample_plugin 资产提前到位，1.6 有基础了。

### Verdict: NEEDS CHANGES
### Next: 先做上面两个 MUST FIX（顺序：legacy sink → 订阅清理确认），然后继续
DIRECTIVES 1.1 PluginManager 实装。1.1 完成时注意：`user_local_data_dir()` 提共享时
不要破坏 legacy wxWebViewEdge 的 user data folder 约定（字节一致）。

---

## Round 1 / review 2 — 5f442c7d26 "fix legacy wxwebview downlink after sink-based SSWCP channel"

本轮为逐条验证 EXECUTOR 在 FEEDBACK.md 的声称（非泛泛 re-review）。

- [OK] **MUST FIX 1 验证通过：工厂单点覆盖属实**。`WebView::CreateWebView`
  （Widgets/WebView.cpp:464-470）在函数唯一出口处调 `SSWCP::register_wx_sink(webView)`，
  Edge / WebKit / 通用 / FakeWebView 失败兜底四条创建分支全部流经该点，无提前 return。
  全仓 grep 证实不存在绕过工厂的创建路径：`wxWebView::New` / `new WebViewEdge` /
  `new WebViewWebKit` / `new FakeWebView` 在 Widgets/WebView.cpp 之外零命中；
  19 个 `CreateWebView` 调用点（含 5 个 SSWCP 宿主）全部出自工厂。
- [OK] **MUST FIX 1 sink 体质量**：CallAfter marshal 正确——实际触发点 `send_to_js`
  与 `push_timelapse_state` 本就运行在 `wxGetApp().CallAfter` 内（UI 线程），二次
  marshal 无害，且对潜在 worker 线程调用者保守正确；`wxWeakRef` 判活使销毁后排队
  的脚本安全丢弃；`wxString::FromUTF8` 对 nlohmann dump 出的 UTF-8 JSON 编码正确
  （无内嵌 NUL）。FakeWebView::RunScript 为 no-op，兜底路径注册无害。与 review 1
  建议的 `WebView::RunScript` 包装器相比少了 dev 模式脚本日志与 native backend
  判空（wxWebViewEdge::RunScript 自身会处理未就绪情形返回 false）——可接受，不要求改。
- [OK] **MUST FIX 2 验证通过：兜底真实且幂等**。`on_webview_delete` 尾部
  `unregister_web_sink(view)`（SSWCP.cpp:8143）确实存在；实现为锁内 map erase，
  天然幂等；`register_wx_sink` / `unregister_wx_sink` 均判空。
  小瑕疵：注释 "hosts unregister explicitly" 与事实不符——没有任何 legacy 宿主
  显式调 `unregister_wx_sink`，该兜底就是 legacy 侧唯一清理路径；下次顺手改注释。
- [OK] **宿主枚举完备（MUST FIX 2 确认清单核对）**：`handle_web_message` 全仓恰好
  5 个 legacy 调用点 + WebViewPageBase；5 个 `on_webview_delete` 调用点逐一确认
  位于析构函数且行号与 FEEDBACK 所列一致（PrinterWebView.cpp:63 /
  WebDeviceDialog.cpp:55 / WebPreprintDialog.cpp:65 / WebUrlDialog.cpp:68 /
  WebViewDialog.cpp:239 —— 最后一处即 WebViewPanel::~WebViewPanel，该文件内
  SourceViewDialog 无 webview）。不存在第 6 个漏网 SSWCP 宿主。
- [OK] **无 sink 键冲突**：新栈 `sswcp_handle()` 返回 `orca::wv::WebView*`，与
  `wxWebView*` 指针命名空间不同；WebViewPageBase::close_view 显式 unregister
  （WebViewPageBase.cpp:238）+ on_webview_delete 兜底，双保险且幂等。
- [SHOULD FIX] **FEEDBACK 一处事实错误 + 陈旧 sink 条目泄漏（不阻塞）**：
  "Monitor 页不含 webview"不成立——MonitorPanel 内嵌 StatusPanel（Monitor.cpp:191），
  StatusBasePanel 创建 `m_custom_camera_view = WebView::CreateWebView(...)`
  （StatusPanel.cpp:1056），并在 ~StatusBasePanel（StatusPanel.cpp:911）delete，
  全程不经 `on_webview_delete`。*结论*仍成立：StatusPanel 从不调
  `handle_web_message`、不参与 SSWCP，无需订阅清理；其 stale 条目即使被误触发也会
  被 wxWeakRef 判空丢弃，无功能危害——但 MarkdownTip / ReleaseNote /
  PrivacyUpdateDialog 等中途销毁重建的视图会让 `s_web_sinks` 只增不减。
  建议：在 ~WebViewRef（Widgets/WebView.cpp:342，已是 g_webviews 的集中摘除点）
  补一行 `SSWCP::unregister_wx_sink(m_webView)`，使清理不依赖宿主自觉；
  并在 FEEDBACK 更正该条表述。可与 1.1 的 commit 顺手一并提交。

### Verdict: APPROVED
### Next: 继续 DIRECTIVES 1.1 PluginManager 实装（`user_local_data_dir()` 提共享时
保持与 legacy wxWebViewEdge 的 user data folder 字节一致）。上面 SHOULD FIX
（~WebViewRef 补 unregister + 两处注释/表述修正）并入下一个 commit 即可，不阻塞。
1.7 冒烟时务必在 device/monitor 页（PrinterWebView，即 SSWCP 宿主页）验证 sw_*
回包经新 sink 到达页面（日志或断点），顺带确认 Monitor 页 StatusPanel 摄像头视图
不受影响。
