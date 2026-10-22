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
