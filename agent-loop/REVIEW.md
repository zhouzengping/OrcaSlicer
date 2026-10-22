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

---

## Round 1 / review 3 — aa448282e2 "1.1 implement PluginManager core"

对照 DIRECTIVES 1.1 逐项核对 + 三库 API 对接验证（plugin_host.cpp / permission_store.cpp /
bridge.cpp / backend_win.cpp 均已读）。9 个 API 全部注册且名字与 PluginManager.hpp 冻结
契约一致，数据真实、无 try-catch、无 TODO stub；但有 3 个 MUST FIX，其中两个会直接
破坏 1.7 目标闭环（权限授予无效 / 插件 tab 白屏）。

- [MUST FIX] **权限授予对会话策略不可见（权限流整体失效）**
  `PluginManager::prompt_plugin_permissions`（PluginManager.cpp:587-589）自建
  `PermissionStore store(m_permission_store_path)`，load→grant→save 只落盘；
  而 session 策略捕获的是 PluginHost 构造时 load 过一次、之后从不重读的**内存表**
  （plugin_host.cpp:195 只在 ctor 调 store.load()；policy lambda 持 store 拷贝，
  共享的是 host 那份 Impl，见 permission_store.cpp:73-77 每个 store 实例独立 Impl）。
  结果：用户在 wxMessageDialog 点了 Allow → 文件里是 granted → 会话内该 tag 仍
  Unknown → policy Deny（plugin_host.cpp:318-333）。DIRECTIVES 1.1 明确要求的
  "允许则写入 PermissionStore" 流程在运行时是空转，重启应用才生效。
  WebViewDevDialog::prompt_plugin_permissions（WebViewDevDialog.cpp:243 起，存量
  未实测路径）是同一颗雷。
  修法（二选一，(a) 更小）：(a) 给 PluginHost 加
  `orca::plugin::PermissionStore& permission_store() const`（plugin_host.hpp 暴露
  impl.store），PluginManager 与 DevDialog 都改为通过 host 的同一实例做
  query/grant/save——grant 直接进策略共享的内存表，save 负责持久化；
  (b) PluginHost::load 每次开头重读 `impl.store.load()`。

- [MUST FIX] **adopt 的会话 webview 永远拿不到初始 bounds（插件 tab 白屏直到手动 resize）**
  Windows 后端 controller 创建完成时 `put_Bounds(RECT{0,0,0,0})` 后立刻
  report_created（backend_win.cpp:426-427）；adopted 路径的 on_created 槽被
  PluginHost::load 独占且只做 navigate（plugin_host.cpp:371-379），不调 bounds；
  `adopt_session → adopt_view → apply_bounds()` 执行时 view 还在 Creating →
  直接 return（WebViewPageBase.cpp:219-221）；tab 挂进 notebook 触发的 wxEVT_SIZE
  也几乎必然早于异步 controller Ready，同样 no-op。之后无人再补 bounds →
  插件内容 0×0 不可见，"open → 动态 tab 出现" 目标闭环在视觉上直接失败。
  修法：`PluginTabPage::adopt_session`（PluginTabPage.cpp:19）adopt_view 之前包一层
  现有槽：`auto prev = m_view 为空时先取 session->webview()->events().on_created;
  webview->events().on_created = [weak_this, prev]{ apply_bounds(); if (prev) prev(); }`
  ——controller 完成回调在 UI 线程排队，当前同步替换必先生效，安全。
  （WebViewDevDialog 的 plugin session 路径同病：apply_view_bounds 只管 m_view，
  顺手一并修。）

- [MUST FIX] **5 个 API 违反冻结契约的 sync/async 约定，全部注册成了 sync**
  PluginManager.hpp:26-34 承诺 import/remove/open/close/install_sample 为 async，
  实现里全是 `.sync`（PluginManager.cpp:390/406/429/446/463）；commit message 自称
  "per the frozen wire contract"。这不是口味问题：sync handler 在 webview 消息回调内
  内联执行（bridge.cpp:451-459 "Sync handlers run inline on the UI thread"），于是
  import/install_sample 的 64MB 级 IO 冻结 UI；plugin.open 在别人家的
  WebMessageReceived 回调里跑模态对话框（嵌套 pump）+ 创建窗口 + 创建 WebView2
  controller；plugin.close/remove 在回调内销毁另一个 webview 的 controller 和 tab
  窗口——正是 async+Responder 设计要避开的重入面。
  修法：五个 API 改 `desc.async = [this](params, responder)`，函数体首行
  `wxTheApp->CallAfter([...]{ ... responder.send_result/send_error ... })` 把工作
  挪出消息回调（Responder 自带 cfg.dispatcher 回 UI 线程，attach_bridge 已配好）。
  JS 侧 wire 格式不变，无前端影响。

- [SHOULD FIX] **review 2 的 SHOULD FIX 未按约定并入本 commit**：三处修改
  （~WebViewRef 补 unregister_wx_sink、SSWCP.cpp:8141 注释改写、FEEDBACK.md 更正
  "Monitor 页不含 webview" 表述）内容核对无误，但目前只躺在工作区未提交
  （git status: WebView.cpp / SSWCP.cpp / FEEDBACK.md 三个未提交修改）。
  本 commit 没带上就作罢，但必须随下一个 commit（1.2）落地，不得再顺延或丢失。

- [SHOULD FIX] **import 覆盖安装不检查运行中实例**：import_plugin_from 对已存在的
  `dest` 直接 remove_all + rename（PluginManager.cpp:698-706）。若同 id 插件正在
  运行，Windows 下 WebView2 虚拟主机仍握着插件目录文件 → remove_all 半途失败，
  留下半删除残骸。建议与 plugin.remove 对齐：`is_running(id)` 时先
  `close_plugin_tab` 再删（或至少在 error 里写明需先关闭）。

- [SHOULD FIX] **拒绝路径不"干净失败"且不记忆拒绝**：用户在权限对话框点 No 后
  插件照常打开，只是其 tagged API 运行时被 Deny（页面只能看到 PermissionDenied）；
  且 deny 不落盘（store 只在 grant 后 save），下次 open 对同一 tag 再弹一遍。
  建议：任一 tag 被拒即返回 nullptr 干净失败（或至少把 denied 状态
  `store.revoke` 持久化），并考虑向页面明示拒绝原因。

- [SHOULD FIX] **权限链演示不出来**：两个 host 级 API（orca.filament.list /
  orca.host.info）ApiDesc.permission 均为空（origin-only，任何插件免授权可读项目
  耗数据），且 sample_plugin/filament_demo/plugin.json `permissions: []`——
  整个 prompt/PermissionStore 机制在 demo 里零触发。建议给
  `orca.filament.list` 配 `permission = "filament.read"`（或类似 tag）并让 sample
  manifest 声明之，1.7 冒烟才能真正走到权限弹窗→授权→API 通的链路。

- [OK] 构造函数：m_plugins_root=`<user_local_data>/plugins` + 目录创建，同时确保
  permission store 父目录存在（PluginManager.cpp:283-298），双重 ec 检查。
- [OK] `user_local_data_dir()` 提共享为 `Slic3r::GUI::webview_user_data_dir()`
  （WebViewPageBase.hpp:19 声明）：函数体逐字节未动，create_view 默认值、
  WebViewDevDialog、PluginManager::ensure_host 三处消费同一函数，与
  wv::WebView::Config::browser_data_dir 文档钉死的
  `wxStandardPaths::GetUserLocalDataDir().ToUTF8()` 一致——"字节一致"约束由
  单一来源保证。WebViewDevDialog 去重干净。
- [OK] ensure_host() HostEnv 四项全对（host_version=SLIC3R_VERSION、
  dispatcher=wxTheApp->CallAfter、browser_data_dir=共享目录、
  permission_store_path），并在 host 上注册两个 host 级 API（会克隆进每个
  session bridge，plugin_host.cpp:336-345）。
- [OK] orca.host.info 数据真实：版本宏/平台宏/locale 取
  current_language_code_safe() 并 `zh_CN`→`zh-CN` 归一化为 BCP 47；orca.filament.list
  来自 preset_bundle 真实项目数据（filament_presets 槽位、filament_colour /
  multi_colors / colour_mode、preset label/type/vendor/filament_id，全部核对存在，
  含无 preset 槽位的回退），非空壳。
- [OK] import zip 加固到位：两遍校验（条目数 512 / 单条 16MB / 总量 64MB 上限、
  zip-slip 拦截前导 `/`、`\\`、`..`、`:`）、内存解压绕开 miniz 窄字符路径、
  scratch 置于 plugins_root 下保证 rename 同卷、所有失败路径清 scratch、
  dest 目录名来自 schema 校验过的 id（`^[a-z0-9][a-z0-9.-]{2,63}$`，无路径分隔符）。
- [OK] plugin.list：坏目录跳过并记日志不崩；结果按 id 稳定排序；running 状态来自
  host->loaded_ids()。installed_ids() 按 DIRECTIVES 字面（存在性扫描）实现，
  ec 全防护、无 plugin.json 子目录跳过、不挂不崩。
- [OK] plugin.pick_import 为 sync 原生文件对话框（契约如此）：dir/zip 双分支、
  取消返回 `{"path":""}` 干净。
- [OK] 错误路径全 Status/返回值，全文件零 try-catch、零 TODO；register_api_checked
  把注册失败记日志而非吞掉；风格与 WebViewPageBase.cpp 一致（匿名 ns 助手、
  BOOST_LOG_TRIVIAL、boost::fs + error_code）。
- [OK] open_plugin 失败回收正确：adopt_session 失败（实际不可达）时 host->unload
  收回 session；MainFrame 三个 stub 签名与 handler 调用吻合（1.4 实装后自动接通）。

### Verdict: NEEDS CHANGES
### Next: 先修三个 MUST FIX（建议顺序：2 bounds 白屏 → 1 权限不可见 → 3 sync/async，
前两个是 1.7 闭环的硬阻断；三个都是小 diff，其中 1 需要 plugin_host.hpp 加一个
访问器，属预期内的三库小改）。修完一个小 commit，然后继续 DIRECTIVES 1.2 GUI_App
挂载。1.2/1.3 验证点：(a) PluginCenterPage 的 register_center_apis 必须挂在
GUI_App 持有的那个 PluginManager 实例上（plugin.list 的 running 状态和
open/close 才与 MainFrame 一致）；(b) 退出路径 close_all_plugin_tabs 必须在
tab 窗口销毁**之前**触发 host unload（close_plugin 注释已写明依赖存活的 HWND）；
(c) 工作区那三份未提交的 review-2 SHOULD FIX 修改随 1.2 commit 一并落地。
1.7 冒烟时按 review 2 的 Next 验证 PrinterWebView sw_* 回包，并新增验证：
权限弹窗出现 → Allow → tagged API 调用成功（验 MUST FIX 1）；插件 tab 打开即
可见内容、无需 resize（验 MUST FIX 2）。
