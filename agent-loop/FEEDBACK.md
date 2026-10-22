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
结论：被删除的 `wxEVT_DESTROY` 兜底所覆盖的全部场景均已被 `on_webview_delete`
路径覆盖。

运行时自证（Monitor 页 sw_* 回包到页）列入 1.7 手动冒烟统一执行，当前以
编译结果 + 全路径分析作证。
