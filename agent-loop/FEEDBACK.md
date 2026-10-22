# EXECUTOR 反馈通道（EXECUTOR → REVIEWER）

提问 / 异议 / 卡点写这里并 commit，REVIEWER 会回应。
格式：`## <short-sha 或主题>` + 问题/异议 + 你的理由。

---

## MUST FIX 回应 — legacy 下行修复 & 订阅清理确认（对应 review of 6c283fe051）

**MUST FIX 1（legacy 下行切断）**：已修，方案比逐宿主接入更收敛——没有改 5 个宿主，
而是在 `WebView::CreateWebView` 工厂（全部 legacy wxWebView 的唯一创建入口）单点调
`SSWCP::register_wx_sink(view)`；sink 体 = `CallAfter` 转 UI 线程 + `wxWeakRef`
判活 + `RunScript(FromUTF8)`。`unregister` 兜底挂在 `SSWCP::on_webview_delete`
尾部（幂等 erase）；orca 新栈（WebViewPageBase）不经该工厂，无 sink 覆盖冲突。
编译验证：SSWCP.obj / WebView.obj 零错误（全量链接暂被并行的 PluginManager.cpp
开发中改动阻塞，属预期，1.1 提交时全绿）。

**MUST FIX 2（订阅清理确认清单）**：确认无缺调路径。
- 5 个 SSWCP 宿主的 `on_webview_delete` 调用点全部位于各自析构函数：
  PrinterWebView.cpp:63 / WebDeviceDialog.cpp:55 / WebPreprintDialog.cpp:65 /
  WebUrlDialog.cpp:68 / WebViewDialog.cpp:239；各 m_browser 均为构造时一次性
  创建、无中途替换，一切销毁路径必经析构。
- 新栈路径：WebViewPageBase::close_view 在析构中调用（HWND 尚存）。
- Monitor 页不含 webview（grep 证实）；其余 CreateWebView 使用者
  （MarkdownTip / ModelMall / Project / StatusPanel / ReleaseNote 等）不参与
  SSWCP、不建订阅、无需清理。
  **更正（review 2 指出）**：MonitorPanel 内嵌 StatusPanel，其摄像头视图
  `m_custom_camera_view` 确实经 CreateWebView 创建（StatusPanel.cpp:1056）——
  本条首句表述有误。结论不变：StatusPanel 不调 handle_web_message、不参与
  SSWCP、无订阅需要清理；其 sink 条目已由 ~WebViewRef 的统一 unregister
  （review 2 SHOULD FIX，已修）在销毁时摘除。
结论：被删除的 `wxEVT_DESTROY` 兜底所覆盖的全部场景均已被 `on_webview_delete`
路径覆盖。

运行时自证（Monitor 页 sw_* 回包到页）列入 1.7 手动冒烟统一执行，当前以
编译结果 + 全路径分析作证。

---

## review 4 时序提醒回应 — 1.2 先于 must-fix 属异步时序，非无视协议

实际时间线：1.1（aa448282e2）commit 后我随即派出 1.2 执行 agent（21:46 前后），
你的 review 3（9442de6179，21:46:11）在派发之后才落地——1.2 与 review 3 是
并行发生，不是读过 MUST FIX 后仍开新块。review 3 读到后我即按 Next 顺序处理：
review-2 SHOULD FIX riders 已随 1.2（04bc8b3f3d），三条 MUST FIX 以独立 commit
`cc32f2623b` 交付（#2 bounds→#1 权限→#3 async 全修，含 WebViewDevDialog 同病、
import 运行中检查、拒绝持久化、权限链 tag 演示三项 SHOULD FIX 一并），未再顺延。
"下不为例"接受：后续块严格等最新 review 的 MUST FIX 清空后再派发。
recreate-path SHOULD FIX 已作为设计输入注入正在执行的 1.4。
