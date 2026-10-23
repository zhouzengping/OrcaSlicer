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

---

## Round 1 / review 4 — 04bc8b3f3d "1.2 mount PluginManager on GUI_App"

本 commit 两部分：1.2 本体（GUI_App 挂载）+ 兑现 review-2 SHOULD FIX 三件套。
逐项核对完毕（GUI_App.cpp/hpp、WebView.cpp、SSWCP.cpp、FEEDBACK.md diff 全读，
MainFrame 三函数、PluginManager.cpp、PluginTabPage.cpp 现状核实）。

**A. review-2 SHOULD FIX 兑现验证 — 三项全部落地：**

- [OK] **~WebViewRef 补 unregister_wx_sink**（Widgets/WebView.cpp:347-349）：
  位于 g_webviews 集中摘除点，`SSWCP::unregister_wx_sink` 判空 + 幂等 map erase
  （SSWCP.cpp:8207-8212），对从未注册 sink 的视图调用也无害；SSWCP.hpp:727 有
  声明，WebView.cpp 已 include SSWCP.hpp。stale sink 条目不再依赖宿主自觉。
- [OK] **SSWCP.cpp:8141-8143 注释修正**：新表述（orca 栈宿主显式 unregister、
  legacy 全走 ~WebViewRef、此处是兜底、幂等）与代码事实一致。
- [OK] **FEEDBACK.md Monitor 表述更正**：更正内容与 review 2 指出的事实一致
  （MonitorPanel 内嵌 StatusPanel 的摄像头视图，StatusPanel.cpp:1056），并如实
  注明"其 sink 条目由 ~WebViewRef 统一摘除"。

**B. 1.2 本体质量（DIRECTIVES 1.2）：**

- [OK] **挂载方式**：GUI_App.hpp 前向声明 + `std::unique_ptr<PluginManager>
  m_plugin_manager{nullptr}` + 懒创建访问器 `plugin_manager()`（GUI_App.hpp:308/
  804，GUI_App.cpp:1287-1291），与 GUI_App 现有私有单例成员风格一致
  （m_printhost_job_queue / other_instance_message_handler 同族；~GUI_App 声明于
  hpp、定义于 cpp，unique_ptr 不完整类型无问题）。懒创建使 PluginManager 构造
  （仅 boost::fs 建目录，review 3 已核）不发生在 GUI_App 构造期——无过早调用
  wx 设施。小 nit：commit message 称 lazy "same pattern as m_printhost_job_queue"，
  实际后者是 mainframe init 时 eager reset(new)——措辞不精确，无代码影响。
- [OK] **退出顺序正确**：GUI_App::shutdown 唯一调用点是 MainFrame::shutdown 尾部
  （MainFrame.cpp:1084），teardown 块位于 `m_is_recreating_gui` 早退之后
  （GUI_App.cpp:1337-1346）：先 `mainframe->close_all_plugin_tabs()`（此刻
  MainFrame 尚未销毁、仅隐藏，满足 review-3 Next(b) "host unload 先于 tab 窗口
  销毁"）再 `m_plugin_manager.reset()`；manager 与 mainframe 双判空；shutdown
  只跑一次。close_all_plugin_tabs 目前是 no-op stub（MainFrame.cpp:3648，1.4
  范围），commit 注释如实说明 PluginHost dtor 是当前兜底——可接受的中间态。
- [SHOULD FIX] **recreate 路径完全跳过插件 teardown**：语言切换等
  recreate_GUI 场景下 `m_is_recreating_gui` 早退使 teardown 块不执行，而
  manager 跨 recreate 存活。1.4 落地后 `old_main_frame->Destroy()` 会带着仍
  loaded 的 session 销毁插件 tab（GUI_App.cpp:3969），tab↔session 映射丢失、
  session 指向已关闭视图。1.4 时需定夺：把 teardown 移到早退之前（两条路径各
  跑一次）或显式关-重建插件 tab。当前无插件 tab，不阻塞。
- [OK] 通用质量：本 diff 零 try-catch、零新 stub、注释风格一致、构建自证
  （GUI_App.obj + libslic3r_gui.lib + Snapmaker_Orca.dll 全绿）。

**C. review 3 三个 MUST FIX 状态盘点 —— 全部未动（HEAD + 工作区双重核实）：**

- [MUST FIX] **（review 3 #1，未修，逐字原样）权限授予对会话策略不可见**：
  `PluginManager::prompt_plugin_permissions` 仍自建
  `orca::plugin::PermissionStore store(m_permission_store_path)`（PluginManager.cpp:
  587-589），grant 只落盘不进策略共享的内存表；plugin_host.hpp 未加
  permission_store() 访问器；WebViewDevDialog.cpp:243 同病未动。
- [MUST FIX] **（review 3 #2，未修，逐字原样）adopt 的会话 webview 拿不到初始
  bounds**：PluginTabPage 构造与 adopt_session 仍只 `adopt_view(...)` 无
  on_created 补 apply_bounds（PluginTabPage.cpp:12-31）；backend_win.cpp /
  WebViewPageBase.cpp 未改。
- [MUST FIX] **（review 3 #3，未修，逐字原样）5 个 API 仍全部 sync**：
  plugin.import / plugin.remove / plugin.open / plugin.close / plugin.install_sample
  依旧 `.sync`（PluginManager.cpp:390/406/429/446/463），未见 desc.async +
  Responder。
- 工作区 clean（git status 无未提交改动），`git diff aa448282e2..04bc8b3f3d --
  src/` 仅 GUI_App/SSWCP/WebView 三处 → 三条 MUST FIX **完全未动、也无人正在修**。
  **时序提醒**：review 3（9442de6179，21:46:11 NEEDS CHANGES）早于本 commit
  （21:54:06），即 EXECUTOR 在 MUST FIX 未处理的情况下开了新块 1.2——协议第 5 条
  （开新块前先读 REVIEW.md 最新节、MUST FIX 优先于 DIRECTIVES 新任务）与
  review 3 Next 的显式顺序（"修完一个小 commit，然后继续 1.2"）这次没有被执行。
  本 commit 顺带兑现了 review-3 SHOULD FIX #1（riders 落地），这是对的；但
  MUST FIX 优先级高于 riders，下不为例。

### Verdict: APPROVED（仅就本 commit 自身内容：1.2 挂载正确、三件 rider 全兑现；
### review 3 的 NEEDS CHANGES 对 aa448282e2 继续有效，不因本 commit 消除）
### Next: **暂停 1.3。** 下一个 commit 必须是单独的
`[plugin-demo] fix review-3 must-fixes`，修完上面 C 节三条（建议顺序：#2 bounds
白屏 → #1 权限不可见 → #3 sync/async；#1 按 review 3 建议 (a) 给 plugin_host.hpp
加 `permission_store()` 访问器，PluginManager 与 WebViewDevDialog 同步改；#2 顺手
修 WebViewDevDialog 的 plugin session 路径；#3 五个 API 改 desc.async + 函数体首行
CallAfter 挪出消息回调）。修完该 commit 后才继续 DIRECTIVES 1.3，不得再把 MUST FIX
顺延到后续块。B 节的 recreate-path SHOULD FIX 记入 1.4 的设计输入。

---

## Round 1 / review 5 — cc32f2623b "fix review-3 must-fix: visible grants, adopted bounds, async contract"

专项验收轮（review 4 下令、暂停 1.3 的那个修复 commit）。逐条对照 review 3 原始
定义验收，三库联动路径全部实读核实（plugin_host.cpp / permission_store.cpp /
webview.cpp report_created / backend_win.cpp 完成回调 / bridge.cpp Responder 与
权限链）。**三条 MUST FIX 全部验收通过。**

- [OK] **MUST FIX 1（权限授予可见性）验收通过**。修法为 review 3 建议 (a)：
  plugin_host.hpp:89 / plugin_host.cpp:198 新增 `permission_store()` 返回
  `m_impl->store`；PluginManager::prompt_plugin_permissions（PluginManager.cpp:
  654-699，经 ensure_host 取 host 实例）与 WebViewDevDialog::prompt_plugin_
  permissions（WebViewDevDialog.cpp:267 起）都改为走 host 的同一实例。
  写后读同源成立：session policy 捕获的 `store_copy = impl.store`
  （plugin_host.cpp:321）虽是 PermissionStore 值拷贝，但 PermissionStore 持
  `shared_ptr<Impl>`（permission_store.hpp:81），拷贝共享同一张 grant 表 →
  grant/revoke 经访问器直达策略查询的内存表，save() 持久化、host ctor 的 load
  负责重启回读；ensure_host 懒单例保证 prompt 与 load 用同一个 host。
  附带验收（review 3 SHOULD FIX "deny 仍 open" 一并修掉）：No → store.revoke
  持久化 Denied + open_plugin 返回 nullptr 干净失败（PluginManager.cpp:578-583），
  下次 open 对已拒 tag 不再弹窗直接拒——不再"带着被拒权限打开插件"。

- [OK] **MUST FIX 2（adopted bounds）验收通过**。PluginTabPage::adopt_session
  （PluginTabPage.cpp:33-46）在 adopt_view 前同步包一层 on_created：
  weak_from_this 守卫（类新增 enable_shared_from_this，头注释钉死 shared_ptr
  所有权约定）→ apply_bounds() → prev_created()，navigate 语义保留且排在
  bounds 之后（首帧即有正确尺寸）。时序可靠：webview.cpp:62-67 report_created
  先置 State::Ready 再调 on_created，wrap 里的 apply_bounds 不会撞 no-op 守卫；
  controller 完成回调是 UI 线程排队事件，host->load 返回后的同步替换必先生效
  （代码注释与此分析一致）；之后 wxEVT_SIZE → on_size → apply_bounds 持续接管。
  apply_bounds 移 protected（WebViewPageBase.hpp:85）后全仓唯一外部消费者就是
  本 wrap，无破坏。WebViewDevDialog 同病已修：apply_plugin_view_bounds
  （WebViewDevDialog.cpp:211-226）+ on_load_plugin 同款 wrap（:343-348）+
  on_size 双视图（:376-382）。DevDialog wrap 的 raw-this 捕获与该文件既有事件
  槽（:102 on_created 等）风格一致，且 on_close 先 unload/close 视图再 Destroy、
  backend 的 ControllerCompletedHandler 对 closed 状态直接丢弃回调
  （backend_win.cpp:389-396），wrap 不会在对话框死后触发。

- [OK] **MUST FIX 3（sync→async 契约）验收通过**。五个 API（plugin.import /
  remove / open / close / install_sample，PluginManager.cpp:398-540）全部
  desc.async + 函数体首行 wxTheApp->CallAfter，消息回调内只做入队。Responder
  恰好一次：逐路径核对，五个 handler 的早退/失败/成功每个分支恰好一个 send_*，
  且 bridge 侧有集中兜底——complete() 对已应答/已扫掉的 pending no-op
  （bridge.cpp:59-61）、bridge 已死 no-op、超时 sweep 兜底，即使双发也不破坏
  协议。错误码回传不变：InvalidParams/InternalError 仍走 error 帧，域失败仍走
  ok:false payload，wire 语义与前端零改动。新竞态评估：连点/重入都经单一
  CallAfter 队列在 UI 线程串行，无交错；import 覆盖运行中插件补了先 close 再删
  的守卫（:785-798，与 plugin.remove 对齐，close 失败清 scratch 干净退出）；
  当前 MainFrame 三 stub 下 open/remove 对运行态只能干净失败（stub 返回 false →
  ok:false），1.4 接通后双击幂等由 open_plugin_tab 承担（DIRECTIVES 1.4 本就
  要求"已开 → select"）。

- [OK] **review 3 SHOULD FIX riders 全部落地且核对无误**：(1) import 运行中
  守卫（上述）；(2) 拒绝持久化 + 干净失败（上述）；(3) 权限链演示——
  orca.filament.list 双面拆分：center bridge 注册 origin-only 空标签
  （PluginManager.cpp:534-535；空标签永不 consult policy，bridge.cpp:436-449），
  host 面注册 "project.filaments.read"（:558-560，克隆进每个插件 session），
  sample manifest 声明该 tag → 1.7 冒烟能走全 prompt→grant→Allow 链。

- [OK] 通用：diff 零 try-catch、零新 stub（permission_store() 与
  apply_plugin_view_bounds 均为实装）；`git show --stat` 改动聚焦——10 个文件
  全部映射到三条 must-fix 与 review 3 SHOULD FIX riders，无夹带。

- [OK] 时序违规说明接受：ab19dc9c60 FEEDBACK 的时间线（1.2 agent 派发早于
  review 3 落地，非读过 MUST FIX 仍开新块）可信；cc32f2623b 按指令以独立
  commit 交付且未再顺延；"后续块等 MUST FIX 清空后再派发"的承诺记录在案，
  本条即清空信号。

- [SHOULD FIX] **拒绝锁死无逃生门**：revoke 持久化 Denied 后，同一 id 插件
  永远打不开（prompt 对 Denied 不再询问、直接拒），且 plugin.remove 只删目录
  不清权限表（permissions.json 按 plugin id 键控，删除重装后依旧 Denied）——
  冒烟时手滑点一次 No，就只能手改用户目录下的 permissions.json 才能恢复。
  建议随 1.6（或下一个顺手 commit）：plugin.remove 成功后调
  `host->permission_store().revoke_all(id)` + save()，用现有 UI 给用户留一条
  逃生门；有余力再考虑中心页"重置权限"入口。

- [SHOULD FIX]（小，1.4 顺手）PluginTabPage 带非空 session 的构造路径
  （PluginTabPage.cpp:12-18）没有 wrap on_created——当前是死路径（open_plugin
  走空构造 + adopt_session），但留着就是"tab 白屏"复发的引线。1.4 接线时让
  构造路径委托 adopt_session（或删掉带 session 的构造重载），保证唯一的
  session 绑定入口就是修过的那个。

- 备注（不要求改）：(a) import/install_sample 的 CallAfter 体捕获 this
  （PluginManager），退出路径 m_plugin_manager.reset() 之后若队列里还有未跑的
  lambda 理论上悬垂——与全仓 CallAfter 惯用模式同险，且 Responder 对已死
  bridge no-op，接受现状；(b) prompt 里 store.save() 返回值未检查（与旧代码
  行为一致）。

### Verdict: APPROVED — review 3 的三条 MUST FIX 全部验收通过，aa448282e2 的
### NEEDS CHANGES 就此解除
### Next: **解除 1.3 暂停**，恢复 DIRECTIVES 顺序。1.3 PluginCenterPage::setup_page：
create_view + attach_bridge（SessionInfo.session_id="plugin-center"、origin 与
自定义 scheme 匹配进 allowed_origins、scheme/URL 方案写注释说明理由）、
register_center_apis 必须挂在 GUI_App 持有的 plugin_manager() 实例上（plugin.list
的 running 状态与 open/close 才和 MainFrame 一致）；center bridge 的 BridgeConfig
记得配 dispatcher（async Responder 靠它回 UI 线程，WebViewPageBase::attach_bridge
有默认值但显式配置更稳）+ api_timeout_ms；reload_page 带 locale/dark_mode 查询
参数重新导航。**1.4 需要吸收的设计输入（重申）**：(1) review 4 B 节 recreate-path
——语言切换等 recreate_GUI 会带着仍 loaded 的 session 销毁插件 tab（GUI_App.cpp:3969
old_main_frame->Destroy()），teardown（close_all_plugin_tabs → host unload，必须在
tab 窗口销毁、HWND 尚活时执行）要么移到 m_is_recreating_gui 早退之前让两条路径各
跑一次，要么显式关-重建插件 tab，tab↔session 映射不能丢；(2) open_plugin_tab
幂等（已开 → select，FindPage 寻址禁止裸索引）同时兜住 async 化后的双击重入；
(3) 上面第二条 SHOULD FIX：PluginTabPage session 绑定入口统一到 adopt_session。
1.7 冒烟新增验证点（本轮修复的直接验收）：权限弹窗 → Allow → tagged
orca.filament.list 调用成功；点 No → 插件不打开、再次 open 不再弹窗；插件 tab
打开即有内容、无需 resize；DevDialog Load demo plugin 后插件视图立即可见。

---

## Round 1 / review 6 — 7eed4467b8 "1.3 implement PluginCenterPage::setup_page"

对照 DIRECTIVES 1.3 逐项核对。联动路径全部实读：bridge.cpp（extract_origin /
origin_allowed / attach 时序）、webview.cpp（pending_init_scripts / report_created）、
backend_win.cpp（apply_custom_schemes / SourceChanged）、backend_gtk.cpp、
backend_mac.mm、plugin_host.cpp（对照模板）、GUI_App::get_international_url、
resources/web/plugin_center/app.js。origin 字符串已按 extract_origin 的实现逐字符
模拟比对（见 OK 第 2 条）。Windows 主链路无 MUST FIX；另发现两条**先于本 commit
的框架级非 Windows 缺陷**（见 SHOULD FIX 1/2，与 plugin session 同病，不阻塞
Windows 冒烟）。

- [OK] **A. scheme/URL 方案与安全性**：自定义 scheme `plugin-center`，
  root=resources_dir()/web/plugin_center（PluginCenterPage.cpp:46-48，generic_string）。
  Windows 经 WebView2 虚拟主机映射成真实 https origin——backend_win.cpp:630 把
  host 定死为 `<scheme>.orca`，与入口 URL 的 host `plugin-center.orca` 一致；
  mac/linux 保持 `plugin-center://center/` 形态，与 plugin_host.cpp:368-374 的
  分平台模板完全同构。方案理由按约写在注释里（:15-25：静态随包内容、无网络
  往返、不与外部共享 cookie jar、origin 兼作 bridge 门禁的内容身份）。路径
  边界安全：三平台 handler 都被钉在 root 内——WebView2 虚拟主机映射本身只暴露
  映射目录（且 ALLOW 只影响该 webview 内的跨源子资源）；GTK handler 显式拒绝
  `..`（backend_gtk.cpp:73-77）；mac handler 同样拒绝（backend_mac.mm:405-409）。
  root 是只读随包资源目录，页面无法借 scheme 读任意本地文件。
- [OK] **B. origin 匹配逐字符验证通过（Windows 与 GTK）**：实际导航 URL 是
  `get_international_url` 拼出的 `https://plugin-center.orca/index.html?locale=
  zh-CN&dark_mode=0`；bridge 的 extract_origin（bridge.cpp:24-35，"://" 后取到
  第一个 /?# 前）得 `https://plugin-center.orca`——scheme https、无端口、无尾
  斜杠、host 全小写，与 allowed_origins[0] 26 字符全等（已用脚本按同算法模拟
  比对）。mac/linux 分支 `plugin-center://center/index.html?...` →
  `plugin-center://center`，与 allowed_origins[1] 22 字符全等。双 origin 并列
  与 plugin_host.cpp:314 的先例一致（精确匹配下至多一个生效）。hello 竞态不
  存在：SDK 不在 document-start 自动发帧，hello 由页面 init() 主动 ready()
  触发（orca_sdk.js:171-179），此刻 SourceChanged（Win）/notify::uri（GTK）
  早已把 session.origin 置好。
- [OK] **B. attach 时序**：Windows 下 create_view 返回时 controller 创建仍在
  异步进行（backend_win 环境→controller 两级 COM 回调），attach_bridge 落在
  Creating 态，SDK 进 pending_init_scripts；report_created（webview.cpp:62-68）
  先 State::Ready → flush init scripts → 再回调 on_created → on_view_created →
  navigate——首次导航必然带 SDK 且在 document-start，顺序由框架保证。session_id
  ="plugin-center"，context 带 kind 标识。register_center_apis 在 attach 成功后
  调用（:73），且挂在 wxGetApp().plugin_manager() 这个 GUI_App 持有的实例上
  （review-3/5 点 (a) 兑现）：plugin.list 的 running 与 open/close 和 MainFrame
  的 tab 状态同源。center bridge 九个 API 全部空 permission（origin-only），
  policy 留空无碍（空 tag 永不 consult policy）。dispatcher/api_timeout_ms 未
  显式配置——attach_bridge 默认填 wxTheApp->CallAfter（WebViewPageBase.cpp:
  140-146，与 plugin_host env.dispatcher 同体），BridgeConfig 默认 30000ms，
  与 review 5 建议值完全一致，功能等价，仅"显式更稳"的措辞未采纳，接受。
- [OK] **C. 导航与 reload_page**：on_view_created 覆写基类钩子（hpp:30 声明），
  导航经 get_international_url 拼 `?locale=<lang>-<region>&dark_mode=<0|1>`
  （GUI_App.cpp:4587-4611，语言/国家码与 dark_color_mode 每次调用现取，
  首载与重载同一路径）；reload_page 有 is_ready() 守卫。
- [SHOULD FIX] **C1. reload_page 目前无调用方**：语言切换主路径走 recreate_GUI
  （GUI_App.cpp:6110），页面整体重建覆盖了该场景；但向导/新手引导后的
  `m_fltviews.reload_all()`（GUI_App.cpp:4065 / 7405，locale/dark_mode 可能已
  变）只认三张 legacy map，不认识新栈的 PluginCenterPage → 该路径下插件中心
  的 locale/dark_mode 停留旧值。建议 1.4/1.6 接线时在 reload_all 同点位补
  `mainframe->m_plugin_center->reload_page()`（或等价入口），不阻塞。
- [OK] **D. 前端抽查（1.6 才全面审）**：app.js 调用的 8 个方法名与
  register_center_apis 注册名逐一相符（plugin.list/pick_import/import/remove/
  open/close/install_sample、orca.host.info）；响应字段核对一致——plugin.list
  消费 res.plugins[].{id,name,version,description,permissions,icon,running}
  （C++ 全部产出，PluginManager.cpp:346-354）；import/install_sample 消费
  {ok,id|error}、open/close/remove 消费 {ok,error}、pick_import 消费 {path,kind}、
  host.info 消费 host_version——全部对得上。页面读 `?locale=` 的参数名与
  get_international_url 拼的参数名一致（dark_mode 参数已下发、页面暂未消费，
  留 1.6）。
- [SHOULD FIX] **E1（框架债，非本 commit 引入）macOS origin 全程不被跟踪**：
  backend_mac.mm 全文件没有任何 report_url_changed 调用（navigation delegate
  只接了 decidePolicy），session.origin 恒为初始空串 → origin_allowed()
  （bridge.cpp:407）对非空 allowed_origins 必拒，连 _orca.hello 都吃
  OriginNotAllowed——插件中心页和所有 plugin session（plugin_host 同机制）在
  macOS 上 API 全灭。修法：WKNavigationDelegate 补
  `webView:didCommitNavigation:`（或 didStartProvisional）→ 读
  m_webview.URL.absoluteString → proxy.report_url_changed。与 E2 同属一个
  "非 Windows bring-up" 框架修复 commit，不阻塞 Round 1（1.7 冒烟明确是
  MSVC/Windows），但必须在跨平台宣称成立前修掉。
- [SHOULD FIX] **E2（框架债，同上）mac/gtk 的 report_created 在 start() 内同步
  发射，on_created 钩子必然丢失**：backend_mac.mm:154 / backend_gtk.cpp:172 在
  `Backend::start()` 里同步调 m_proxy.report_created()，而它发生在
  `WebView::create` 内部——宿主（WebViewPageBase::create_view:109-117、
  plugin_host.cpp:376、WebViewDevDialog:102）都只能在 create 返回**之后**才能给
  events().on_created 赋槽 → mac/linux 上 on_created 永远空跑，
  PluginCenterPage::on_view_created 不触发、页面永不导航（plugin tab 的
  navigate-only 槽同病）。Windows 之所以正常，是因为 controller 完成回调异步
  回 UI 线程。修法（框架侧任选其一）：backend 侧把 report_created 推迟到
  start() 返回之后（mac: dispatch_async 主队列；gtk: g_idle_add），或
  WebView::create 在调 start() 前允许预置事件槽。修掉之前本 commit 的
  `#else plugin-center://` 分支在 mac/linux 是死代码。与 E1 合并成一个框架
  修复 commit 处理，建议排在 1.6 前后，不阻塞 Windows 冒烟。
- [OK] **E. review 5 两条 SHOULD FIX 状态**：均未在本 commit 顺带处理——
  (1) plugin.remove 成功后 revoke_all+save 未做（PluginManager.cpp:423-456 无
  调用；PermissionStore::revoke_all 已存在，permission_store.hpp:48，随时可接）；
  (2) PluginTabPage 带 session 的构造重载在本 commit 时点仍在（已核
  7eed4467b8:PluginTabPage.cpp:6-17，无 bounds wrap 的死路径）。两条都属约定
  内的"随后续块"，不扣分；(2) 已被 HEAD 上的 1.4 commit（73e8a4d6ad）以
  "删掉带 session 构造"方式处理，下轮（review 7）验收。
- [OK] **F. 通用**：diff 零 try-catch、零 stub 残留（旧占位注释全部被实装替换）；
  `git show --stat` 仅 PluginCenterPage.cpp/hpp 两文件 78 行，范围聚焦；风格与
  WebViewPageBase/WebViewDevDialog 一致（匿名 ns + 理由注释）；编译证据由 1.4
  commit message 的 "PluginCenterPage compiled this round"（22:14）覆盖，1.7
  统一冒烟。小备注（不要求改）：Bridge::attach 实际把 SDK init script 加了两次
  （attach_channel 内 bridge.cpp:300 + attach 尾部 :255），靠 SDK 的
  `window.__orcaSdk` 幂等守卫无害，属框架既有小瑕疵，顺手时可去掉一处。

### Verdict: APPROVED（Windows 主链路；E1/E2 为先于本 commit 的框架债，
### 记入后续框架修复 commit，不阻塞 Round 1）
### Next: 继续 DIRECTIVES 顺序（HEAD 上 1.4 已 commit，下轮 review 73e8a4d6ad）。
1.4 review 验收清单：(a) recreate-path teardown 是否按 review 4/5 的方案落到
m_is_recreating_gui 早退之前、两条路径各跑一次且 HWND 尚活；(b) open_plugin_tab
幂等 + FindPage 寻址（禁裸索引）+ async 双击重入；(c) PluginTabPage session
绑定入口是否统一到 adopt_session（工作区已见带 session 构造被删，验收其死路径
消除）；(d) close_plugin_tab 的 RemovePage → close_plugin（unload）→ map erase
顺序与 bridge 侧 plugin.close/plugin.remove 重入的单向链。**遗留事项登记**：
(1) plugin.remove 成功后 `host->permission_store().revoke_all(id)` + save（review 5
逃生门）约定随 1.6 或顺手 commit 兑现；(2) reload_page 调用方接线（本 review
C1）；(3) 非Windows框架修复 commit：mac report_url_changed（E1）+ mac/gtk 延迟
report_created（E2）。1.7 冒烟按 review 5 清单 + 本轮新增：插件中心 tab 打开即
渲染列表（验 scheme/origin/bridge 全链），locale/dark_mode 参数在页面生效。

---

## Round 1 / review 7 — 73e8a4d6ad + 54ad632374 "1.4 tab system + 1.5 closable tabs"

两 commit 合并审查（同一子系统，1.5 含 review-5 riders）。联动路径全部实读：
MainFrame 三函数与 PAGE_CHANGED/PAGE_CLOSE 处理、GUI_App::shutdown 与 recreate_GUI
时序、Notebook/ButtonsListCtrl、PluginManager open/close/remove、PluginTabPage、
plugin_host.cpp unload、PluginCenterPage bridge 配置。review 5/6 给的 1.4 设计输入
四条（recreate teardown / 幂等+FindPage / adopt_session 统一 / 单向 close 链）
全部兑现且实现正确；新引入 1 个 MUST FIX（recreate 下的静态悬垂指针）。

- [OK] **A. tpPluginCenter 挂载**：与 m_project/m_calibration 同款创建模式
  （MainFrame.cpp:1352-1353，unconditional AddPage + tab_plugin_active 图标 +
  _L("Plugins") 文案），不进 show_device 换页集合——正确（show_device 只动
  monitor/printer_view/multi/calibration）。tpPluginCenter=9 枚举追加无冲突
  （MainFrame.hpp:225）。图标 tab_plugin_active.svg 与 tab_calibration_active
  同规格（20×20、#fff stroke、square cap），ScalableBitmap 按名解析无需注册表。
  小备注（不要求改）：show_device 里 "Calibration is always the last page"
  注释（MainFrame.cpp:1404）本就因插件中心追加而过时，顺手续正即可。
- [OK] **B. open_plugin_tab**（MainFrame.cpp:3696-3732）：幂等——map 命中 +
  FindPage 找到 → SetSelection；map 命中但页面不在 notebook 的 stale 兜底
  （erase 重建，防御性，实测无产生路径：外部无人 RemovePage 插件页）；
  AddPage 失败回收 session（close_plugin 后 shared_ptr 析构窗口，安全）；
  寻址全程 FindPage，无裸索引。async 双击重入：plugin.open 的 CallAfter FIFO
  串行 + handler 内部才读 wxGetApp().mainframe（PluginManager.cpp:482，recreate
  后取到新 frame），双击第二次走幂等 select。AddPage(bSelect=true) 同步触发
  PAGE_CHANGED 时页面已活，无重入。
- [OK] **C. close 单向链**（MainFrame.cpp:3734-3762）：RemovePage（PAGE_CHANGED
  在页面存活时发射）→ close_plugin（HWND 尚活时 host unload）→ map erase
  （shared_ptr 释放 → ~WebViewPageBase → close_view，adopted 视图只 reset 引用）。
  **半关状态不可能**：PluginHost::unload 无论 session->close() 成败都先 erase
  session（plugin_host.cpp:408-409）→ 不会"tab 删了 session 还 running"；
  close_plugin_tab 无条件 erase map → 不会"session 关了 map 还挂着"。bridge 侧
  plugin.close/plugin.remove（PluginManager.cpp:425-473）→ frame->close_plugin_tab
  单向调用，close 链不回调 MainFrame，无重入。plugin.remove 对运行中插件先
  close_plugin_tab 再删目录（:437-446），与 import 守卫对齐。
- [OK] **D. recreate teardown（review 4/5 设计输入核心项，验收通过）**：
  close_all_plugin_tabs 提升到 m_is_recreating_gui 早退**之前**
  （GUI_App.cpp:1335-1342），两条 shutdown 路径各跑一次；时序核实——recreate_GUI
  在 GUI_App.cpp:3945 调 mainframe->shutdown(true) → wxGetApp().shutdown →
  close_all_plugin_tabs，全部发生在 :3977 old_main_frame->Destroy() 之前，
  HWND 存活；真实退出路径（wxEVT_CLOSE_WINDOW → MainFrame.cpp:555 shutdown()
  → 同链）先关 tab 再 reset manager。recreate 保留 manager 的决策有据
  （WebView2 controller parentage 创建即固定，session 无法跨 frame 存活；注释
  写明）。语言切换行为明确：插件 tab 全部关闭、session 全部卸载，新 frame 的
  PluginCenterPage::setup_page 在幸存 manager 上重注册 center APIs（manager 不
  持 bridge 指针，register_center_apis 按页注册，无悬垂）。
- [OK] **E. session 入口统一（review 5 SHOULD FIX 兑现）**：带 session 的
  PluginTabPage 构造重载已删（54ad632374 diff，PluginTabPage.cpp:6-9 现为空构造），
  唯一调用方 open_plugin 改空构造 + adopt_session（PluginManager.cpp:605）；
  on_created bounds wrap 只存在于 adopt_session（PluginTabPage.cpp:27-33），
  死路径消除。全仓无其他构造调用点。
- [OK] **F. closable 事件链**：ButtonsListCtrl::InsertPage 落地 closable 参数
  （Notebook.cpp:204-220）——右键/中键 bind，**dispatch 时**按按钮指针查
  m_pageButtons 取当前索引（与 SEL_CHANGED 同款模式，插入/删除后索引自动正确）；
  wxPostEvent 到 Notebook → MainFrame 绑定（MainFrame.cpp:1277-1299）边界检查 →
  立即解析页面指针（此后不再用索引）→ dynamic_cast 过滤非插件页 → 按指针查
  map → close_plugin_tab。固定页 closable 全部默认 false，不受影响。绑定生命周期：
  每帧 init_tabpanel 一次、绑在 m_tabpanel 上，recreate 建新 tabpanel，旧绑定随
  旧窗口销毁，无泄漏、无双绑。m_pageButtons/m_pageClosable 仅 Insert/Remove 两处
  同步维护（另见 SHOULD FIX 2）。
- [OK] **G. review-5 riders 全部兑现**：(1) 逃生门——plugin.remove 目录删除成功
  后 `host->permission_store().revoke_all(plugin_id)` + save（PluginManager.cpp:
  456-469），失败仅记日志、remove 仍报成功（目录已删，语义正确）；revoke_all
  返回 wv::Status 与 hpp:48 契约一致；reinstall 重新弹权限，锁死解除。
  (2) 入口统一见 E。(3) center bridge 显式钉 dispatcher（CallAfter 回 UI 线程）
  与 api_timeout_ms=30000（PluginCenterPage.cpp:64-70），与默认值等价、抗未来
  默认漂移，落实 review 5 "显式更稳" 建议。
- [MUST FIX] **PAGE_CHANGED 处理器新增的函数级 static 指针跨 recreate 悬垂
  （UAF，新引入）** `MainFrame.cpp:1234` `static WebViewPageBase* s_prev_web_page`
  是 lambda 内 static：所有 MainFrame 实例的 handler 共享同一变量，recreate 后
  它仍指向旧 frame 已 Destroy 的 PluginCenterPage/PluginTabPage。当前编辑器模式
  **碰巧**不炸：recreate_GUI 在 :3971 select_tab(tp3DEditor)（新 tabpanel 发
  PAGE_CHANGED、panel 非 webview → else-if 分支把 static 清空）先于 :3977
  old_main_frame->Destroy()。这个保护依赖另一文件 40 行外的调用顺序巧合，无任何
  注释或断言；viewer 模式跳过 :3971、或将来有人调整 select_tab/Destroy 顺序，
  新 tabpanel 首次 PAGE_CHANGED 即对已销毁窗口调 notify_page_state()（虚基成员
  访问，无任何判活）→ UAF。修法（2 行）：把 prev-page 跟踪从 static 改为
  MainFrame 成员（如 `WebViewPageBase* m_prev_web_page{nullptr};`，handler 捕
  this 使用）——每帧独立生命周期，recreate 后新帧从 nullptr 起步，旧帧的指针随
  旧帧消亡；紧邻的 `static int prev_monitored_tab` 是 int（无解引用）属存量，
  不必同改。
- [SHOULD FIX] **m_tab_map 新增 {"PluginCenter", tpPluginCenter=9} 是打不中的
  裸索引**：消费链 SSWCP.cpp:1128-1129 → request_select_tab（MainFrame.cpp:
  3830-3835）→ SetSelection(pos)（MainFrame.cpp:425-428）直接按枚举值选页。
  实际运行时插件中心页在编辑器布局位于 6/7（multi-machine 有无各差一），
  本构建无 auxiliary/debugtool 页，索引 9 永远越界——release 下 wxCHECK 记错
  日志后 no-op，debug 下断言。存量九个条目同病（tpProject=5 在无 multi 布局
  同样偏移），非本 commit 引入；但新条目不应延续坏模式，且与 DIRECTIVES 1.4
  "寻址一律 FindPage 禁裸索引"精神相悖。修法：SSWCP 处理器对 PluginCenter 特判
  `mainframe->select_tab(mainframe->m_plugin_center)`（MainFrame.cpp:3674 的
  wxPanel* 重载内部即 FindPage），或在 m_tab_map 值侧引入"按 panel 解析"回调。
- [SHOULD FIX]（小）`m_pageClosable`（Notebook.hpp:44，Notebook.cpp:223/234
  维护）全仓只写不读——closable 行为实际由"绑定时才知道的按钮闭包"承载，向量
  是死状态。要么删掉（m_pageButtons 单列即可），要么在下个需要查询 closable 的
  使用点前别留悬念。随顺手 commit 处理即可。
- [OK] 通用：三个销毁路径（关 tab / bridge 关插件 / 退出 app）+ recreate 路径的
  session↔map 一致性走查全部通过（见 C/D）；两 commit 零 try-catch、零新 stub；
  `git show --stat` 各自聚焦（1.4 五文件 GUI_App/MainFrame/SSWCP/图标；1.5 七文件
  Notebook/PluginTabPage/riders，无夹带）；plugin.list 的 running 来自
  host->loaded_ids()，与 tab map 同源一致；工作区 clean，1.6（c7838d2e83）已
  commit 但仅动 web 资源，与本轮无涉，下轮专审；E1/E2（mac 框架债）未被触碰，
  维持登记；遗留登记项 (2) reload_page 调用方仍未接线（本就约定 1.6+，见 1.6
  review）。
- 备注（不要求改）：(a) PluginTabPage 的 notify_page_state 走 else 分支
  （page 无 bridge）→ page_state_notify_webview(oracle 句柄) 无订阅者 → no-op，
  即插件 tab 当前收不到 page.state 事件——1.6 的 active 刷新只覆盖 center 页，
  插件页如需感知 active/idle 再议（session bridge emit 即可）；(b) tab↔map 的
  shared_ptr 所有权依赖"MainFrame 销毁前必经 shutdown 清空 map"这一不变量
  （两条路径已验证成立），若将来出现第三条不经过 shutdown 的销毁路径会双删
  ——现状接受，引入新路径时记得带上。

### Verdict: NEEDS CHANGES（仅 1 条 MUST FIX：新引入的 static 悬垂指针；
### review 5/6 全部设计输入与 riders 验收通过，1.4/1.5 主体合格）
### Next: 单独一个 `[plugin-demo] fix review-7 must-fix` commit：s_prev_web_page
### 改 MainFrame 成员（保留 per-frame 语义，recreate 天然安全）。两条 SHOULD FIX
### （PluginCenter 特判走 select_tab(wxPanel*)、m_pageClosable 去留）可随该
### commit 或 1.6 rider 顺手带上，不阻塞。修完后按序推进：1.6（c7838d2e83 已
### commit，下轮专审：契约核对、双击/失败路径 UX、26 键中英表、dark_mode 三路径）
### 与 1.7 冒烟（MSVC 构建无新警告 + review 5/6 冒烟清单 + 本轮新增验证：开着
### 插件中心 tab 切语言 → 无崩溃、插件 tab 全关、新 frame 插件中心正常重载；
### 右键/中键关 tab + 中心页 Close 按钮两条关闭链各走一遍；remove 后 permissions.json
### 中该 id 条目清空、重装重开重新弹权限）。

---

## Round 1 / review 8 — c7838d2e83 "1.6 plugin center page completion"

对照 DIRECTIVES 1.6 逐项核对。联动路径全部实读：app.js/app.css/index.html
（中心页）与 sample_plugin（plugin.json/index.html）当前态、PluginManager.cpp
register_center_apis 全部 9 个注册体、open_plugin/prompt_plugin_permissions
权限链、orca_sdk.js 错误对象形状、protocol.hpp RpcErrc、bridge.cpp 权限拒绝帧、
WebViewPageBase::notify_page_state / MainFrame PAGE_CHANGED 的 page.state 发射链、
manifest.hpp/schema + validate_against_dir（icon 路径校验）。**无 MUST FIX；
权限流缺口（B 项）确认已闭合。**

- [OK] **B. 权限流真实触发（demo 完整性的关键缺口）— 已闭合，非本 commit 闭合
  而是 cc32f2623b（review 5 已验收），本轮复核确认仍在**：
  `sample_plugin/filament_demo/plugin.json` 的 permissions 为
  `["project.filaments.read"]`（当前 HEAD、7eed4467b8 时点、cc32f2623b diff 三处
  核实）。触发链完整：plugin.open（async CallAfter）→ open_plugin_tab →
  open_plugin（PluginManager.cpp:595）→ prompt_plugin_permissions（:671）对
  Unknown tag 弹 wxMessageDialog；Allow → 经 host 共享 store 的 grant 直达会话
  策略（review 5 MUST FIX 1 修法）；Deny → revoke 持久化 + 干净失败。host 面的
  orca.filament.list 带 "project.filaments.read" tag（:573），被拒时
  bridge.cpp:446 发 RpcErrc::PermissionDenied = -32000 错误帧（protocol.hpp:25）
  → SDK settle() 构造 `err.code = -32000` 的 Error reject（orca_sdk.js:77-83）
  → sample 页 `err.code === -32000` 专属提示，逐环对得上。1.7 冒烟可走全
  prompt → grant → Allow → API 通的链路。
- [OK] **A. 七项 UI 齐备且全部走 orca SDK，API 名逐一命中**：插件列表
  （id/name/version/running 徽章 + description/permissions chips，字段与
  plugin.list 产出 PluginManager.cpp:346-354 全对上）、install_sample（空态按钮）、
  导入 zip / 导入文件夹（header 双按钮 → pick_import {kind} → import
  {path,kind}）、打开/关闭（卡片 toggle 按 running 切换 plugin.open/close）、
  移除（两步武装确认删除）。响应形状逐个核对：list 消费 {plugins:[...]} 包裹；
  import/install_sample 消费 {ok,id|error}；open/close/remove 消费 {ok,error}；
  pick_import 消费 {path,kind}、空 path 静默取消——全部与 C++ 侧一致。1.6 未新增
  C++ 调用（--stat 仅 web 资源），与冻结契约一致。
- [OK] **C1. running 状态刷新时机（事件驱动，无轮询）**：动作成功即 refreshList；
  page.state active 重拉 + 300ms 去重。去重锚点设计的正确性核实：时钟记在
  "上次 active 处理" 而非 "上次刷新"，动作触发的 refreshList 不推进时钟 →
  open 后立刻中键关 tab、焦点弹回中心页的 active 不会被吞（注释所述场景成立）。
  中心页有 bridge → notify_page_state 走 emit 分支（WebViewPageBase.cpp:193），
  PAGE_CHANGED → active/inactive 链对中心页真实可达（MainFrame.cpp:1236 +
  PluginCenterPage wants_generic_page_state=true）。
- [OK] **C2. 进行中/错误/并发防护**：callAction 禁用触发按钮（finally 恢复，
  即使网格已重渲染、按钮已脱树也无害）；import 路径 importBusy + 双按钮禁用；
  错误可见性全覆盖——ok:false → toast res.error、reject/超时 → toast
  err.message、列表加载失败 → 带重试按钮的错误态、hello 3s 失败提前出错误态、
  loadHostInfo 静默属声明过的 best-effort。双击：按钮禁用 + open_plugin_tab
  幂等 select + CallAfter UI 线程串行，三层兜住。
- [OK] **D. locale/dark_mode 消费**：locale 读 `?locale=`，缺省 zh-CN、未知值
  回落 en-US、大小写不敏感精确键 + zh/zh-hans/en 别名；zh/en 两表 26/26 键
  逐个数过、lockstep（commit message 声称属实）。dark_mode：index.html `<head>`
  首绘前内联脚本把 `?dark_mode=0|1` 映射为 data-theme light/dark；app.css 三条
  路径齐备（显式 dark、显式 light 经 `:not([data-theme="light"])` 压过系统
  媒体查询、无参数时 prefers-color-scheme）——commit 声称 "re-verified across
  all three CSS paths" 属实。sample 页仅随系统偏好（注释声明 host 不带参数，
  与 plugin_host 导航模板一致）。
- [OK] **E1. SDK 使用与安全卫生**：错误路径全处理（见 C2），无未消费的 reject；
  page.state 订阅生命周期 = 页面生命周期 = webview 生命周期，无泄漏路径。
  XSS：动态内容全部 createElement/textContent；唯一 innerHTML 是 makeIcon 的
  静态 ICONS 表（有注释钉死）；icon 走 `<img src>`，manifest schema 强制相对
  路径（无 `..`、无反斜杠、非绝对）+ import 时 validate_against_dir 要求
  plugin root 下真实存在文件（PluginManager.cpp:788）→ 无注入面，
  `javascript:` URL 对 `<img>` 在 Chromium 本就惰性；sample 页 swatch 的
  style.background 赋值无法逃逸 CSS。干净。
- [SHOULD FIX] **E2. sample 插件的 page.state 订阅当前是死线（review 7 备注 (a)
  的直接后果，1.7 冒烟前应落地）**：PluginTabPage 自身无 bridge，
  notify_page_state 走 else 分支 → page_state_notify_webview（SSWCP 订阅者）
  → no-op；即 sample 页 "每次激活自动刷新" 的回调永不触发（手动刷新按钮可用，
  不算断链）。修法很小：PluginTabPage 增加 notify_page_state override，经
  `m_session->bridge()->emit("page.state", {state})` 转发（PluginSession 已暴露
  bridge() 访问器，plugin_host.hpp:44）。若决定不做，则删掉 sample 页的订阅与
  注释声明，别留一条 demo 里演示不出来的特性。
- [SHOULD FIX] **C3. 权限被拒的中心页反馈是未本地化的笼统英文 toast**：open 被
  拒时 plugin.open 返回 ok:false "failed to open plugin"（PluginManager.cpp:497），
  中文界面里弹出英文通用文案，用户刚在原生对话框点了 No 却得不到"因权限被拒
  而未打开"的明确反馈（sample 页的 -32000 提示此时不可达——插件根本没打开）。
  建议：plugin.open 失败原因区分（open_plugin 返回 nullptr 的两个路径：权限拒 /
  加载失败），中心页加对应本地化 toast 键。小改动，随 1.7 前的顺手 commit。
- [OK] **F. 通用**：与既有 app.js/app.css 风格一致（strict、函数声明、英文注释、
  BEM-ish 命名）；零 console.log/debugger 残留（三文件 grep 证实）；两步删除的
  4s 自动解除、运行中插件的额外说明行、武装态样式均实现且注释清楚；
  `git show --stat` 仅 3 个 web 资源文件，范围聚焦。工作区另有未提交的
  backend_gtk/backend_mac/GUI_App 改动（内容对上登记的 E1/E2 框架修复 +
  reload_page 调用方接线 GUI_App::reload_plugin_center_page），属另一条修复线
  的半成品，不计入本轮，下轮见 commit 后验收。
- 备注（不要求改）：(a) 卡片图标实际渲染不出——p.icon 是插件目录内相对路径，
  在中心页 origin 下解析必 404 → error 回退字母占位（优雅降级，sample 未声明
  icon，demo 无感；真要支持需 center 侧提供按 id 的图标端点或绝对虚拟主机 URL）；
  (b) open 成功 toast 弹在刚被切走的中心页上，大概率无人看见即过期——无害；
  (c) install_sample 仅空态可达——demo 闭环（装→开→关→删→回空态）不受影响，
  非空列表下重装 sample 需先删光，可不改；(d) review 6 C1（reload_page 调用方）
  本 commit 未接（web-only，合理），工作区 GUI_App 半成品疑似在做，落地后验收。

### Verdict: APPROVED（1.6 本体零 MUST FIX；两条 SHOULD FIX 均小、建议随
### 1.7 前的顺手 commit 落地）
### Next: 1.7 冒烟准备。**前置硬条件**：(1) review 7 的 MUST FIX（MainFrame.cpp:
### 1234 s_prev_web_page 函数级 static 改 MainFrame 成员）仍未修、也不在当前
### 工作区——必须先以独立 commit 落地；(2) 工作区在飞的 E1/E2 + reload_page
### 接线 commit 后由下轮验收。**1.7 冒烟清单（历轮汇总，执行时逐项记录进
### FEEDBACK.md）**：
### 1. MSVC 构建通过、无新警告（1.7 本体要求）。
### 2. legacy 回归（review 1/2）：device/monitor 页（PrinterWebView）sw_* 回包经
###    sink 到达页面；Monitor 页 StatusPanel 摄像头视图不受影响。
### 3. 权限链（review 5/8）：open → 弹权限框 → Allow → 插件页 orca.filament.list
###    出数据；点 No → 插件不打开、中心页出错误 toast、再次 open 不再弹窗直接拒；
###    remove 后 permissions.json 中该 id 条目清空，重装重开重新弹权限。
### 4. 插件 tab 白屏回归（review 5）：open 后 tab 首帧即有正确尺寸，无需手动
###    resize；DevDialog Load demo plugin 后插件视图立即可见。
### 5. 中心页全链（review 6/8）：插件中心 tab 打开即渲染列表（scheme/origin/bridge）；
###    locale/dark_mode 参数生效（zh/en × dark/light/系统三路径）；页脚 host 版本号。
### 6. UI 状态机（本轮）：两步删除（空闲/运行中两文案 + 4s 自动解除）；open/close
###    成功 toast + 列表 running 徽章即时刷新；中键关插件 tab 后切回中心页徽章
###    正确回落（page.state active 路径）；import zip 与文件夹各走一遍、取消静默；
###    双击 open 幂等；sample 页手动刷新可用（E2 落地后加验：切走再切回自动刷新）。
### 7. 生命周期（review 7）：开着插件中心 tab 切语言 → 无崩溃、插件 tab 全关、
###    新 frame 插件中心正常重载；右键/中键关 tab 与中心页 Close 按钮两条关闭链
###    各走一遍。
### 8. 若 E1/E2 已落地：mac/linux 至少编译过（跨平台宣称的最低门槛），Windows
###    行为无变化。

---

## Round 1 / review 9 — ce4a7d9eac + c04fed175a "E1/E2/C1 framework fixes + review-7 must-fix"

两 commit 一轮（框架修复 + must-fix 验收）。联动路径全部实读：backend_mac.mm /
backend_gtk.cpp 现状全文、webview.cpp create/report_created 链、bridge.cpp
on_url_changed→session.origin、backend_win.cpp SourceChanged 与 controller 完成回调
（确认未被触碰）、GUI_App Fltviews::reload_all 三个调用点、WebGuideDialog、
Notebook DoRemovePage 事件时序、MainFrame PAGE_CHANGED 处理器、SSWCP sw_SwitchTab、
PluginCenterPage::reload_page、orca_webview/CMakeLists.txt 平台门。**全部验收通过，
零 MUST FIX。**

- [OK] **A. E1（mac origin）验收通过**。WKNavigationDelegate 补
  `webView:didCommitNavigation:`（backend_mac.mm:407-413），读
  `webView.URL.absoluteString` → `on_url_committed` → `m_proxy.report_url_changed`
  （:328-331）；入口三重守卫（state 判空 / closed / backend 判空），与同文件
  didReceiveScriptMessage/decidePolicy 同款。时机语义与 Windows 对齐成立：
  didCommit 在新文档任何脚本（含 document-start init script 与页面 SDK hello）之前
  发射 → 页面发起 hello 时 session.origin 必已更新，等价于 Win SourceChanged /
  GTK notify::uri；下行走同一套追踪——bridge.cpp:275 的
  `view->events().on_url_changed` → `session.origin = extract_origin(url)` →
  origin_allowed 策略，插件会话与中心页（都经 attach）同时受益。custom scheme
  导航同样触发 didCommit，`plugin-center://center/...` 提取出
  `plugin-center://center` 与 allowed_origins[1] 精确匹配（review 6 已核字符串）。
- [OK] **B. E2（deferred created）验收通过，无新竞态**。mac：start() 尾部
  `dispatch_async(dispatch_get_main_queue(), ...)`（backend_mac.mm:156-170），block
  持 `shared_ptr<MacSharedState>` 保活、raw proxy 仅在 `!closed` 时解引用——close()
  在同一主线程先置 closed 再销毁 backend/Impl，run_script 的 completionHandler
  （:259-274）即同一守卫模式；gtk：`g_idle_add` 一次性 idle（backend_gtk.cpp:
  35-52, 191-198），unique_ptr 接管 payload（closed 早退路径也不泄漏），守卫同构。
  **顺序保证**：main queue / GLib main context 只在当前同步调用栈（WebView::create
  → 返回 → 宿主 attach_bridge + 挂 on_created） unwind 后才排水，宿主槽位必先于
  report_created 生效——这正是修的 bug。**Windows 路径零影响**：backend_win.cpp
  不在 diff 内，report_created 仍由异步 controller 完成回调发射，行为不变。
  语义顺带核实：backend.h 内部契约本就写着 "start() kicks off asynchronous
  creation and returns immediately; completion is reported through the
  EventProxy"——旧 mac/gtk 同步发射其实违反它，本修复是回归契约。
- [OK] **C. C1（reload wiring）验收通过**。`Fltviews::reload_all()` 尾部新增
  `app->reload_plugin_center_page()`（GUI_App.hpp:1009-1013）→ 判 mainframe 与
  m_plugin_center 非空后 `reload_page()`（GUI_App.cpp:4614-4622）。**覆盖三个
  调用点**：guide（GUI_App.cpp:4066）、wizard（:7416）、web-guide
  （WebGuideDialog.cpp:505，第三个是白捡的）。经 GUI_App 方法中转的理由成立
  （GUI_App.hpp 里 MainFrame 仅前向声明，不能解引用成员）；GUI_App.cpp 补
  PluginCenterPage.hpp include，编译绿。参数非硬编码：reload_page 经
  get_international_url 现取 locale 与 dark_mode（review 6 已核每次调用现读），
  is_ready() 守卫保留。
- [OK] **D. review-7 MUST FIX 验收通过**。`s_prev_web_page` 函数级 static 改
  MainFrame 成员 `m_prev_web_page{nullptr}`（MainFrame.hpp:411-414 类内初始化；
  MainFrame.cpp:1234-1250 逻辑与原 static 版逐行等价），per-frame 生命周期，
  recreate 后新帧从 nullptr 起步，不再依赖 select_tab/Destroy 跨文件顺序巧合；
  全仓 grep 无 s_prev_web_page 残留。**帧内悬挂也走查了一遍**：关闭选中插件 tab
  时 Notebook::DoRemovePage → DoSetSelectionAfterRemoval 同步发射
  PAGE_CHANGED（Notebook.hpp:372-382，wx 同步 HandleWindowEvent），此刻页面窗口
  仍存活（销毁在 close_plugin_tab 后续的 map erase），处理器先把 m_prev_web_page
  重置到新选中页/null 再放行——帧内无悬挂窗口。`prev_monitored_tab` int static
  属存量豁免（review 7 已裁），保留正确。
- [OK] **E. riders 验收（c04fed175a 载的是 review-7 两条 SHOULD FIX，均兑现）**：
  (1) sw_SwitchTab 对 "PluginCenter" 特判（SSWCP.cpp:1129-1137）→
  `select_tab(wxPanel*)` FindPage 寻址（MainFrame.cpp:3675-3692），frame/page
  缺失走 handle_general_fail 正常失败语义；SSWCP.cpp 补 PluginCenterPage.hpp
  include（upcast 需完整类型）。(2) m_pageClosable 全仓删净（grep 零残留），
  closable 行为由 InsertPage 的按钮绑定闭包继续承载（Notebook.cpp:204-220），
  1.5 行为无回归。**review-8 两条 SHOULD FIX（PluginTabPage page.state 死线、
  open 被拒 toast 区分+本地化）两 commit 均未包含 → 结转**（见 Next，不扣分）。
- [OK] **F. frozen 契约检查通过（关键项）**。`git show ce4a7d9eac --stat`：仅
  backend_gtk.cpp / backend_mac.mm（实现层）+ GUI_App 两文件；`git diff
  6fcc392e6c..c04fed175a -- src/orca_webview/` 全历史仅这两个 .mm/.cpp，
  **include/ 三库公共头零改动**；orca_jsbridge 自 6fcc392e6c 零改动；
  orca_plugin_host 仅 plugin_host.hpp +8（permission_store() 访问器，review 3
  预告、review 5 验收过的预期内小改，非本 commit）。行为变化不违约反而在履约：
  webview.hpp:54-55 "Wire them immediately after create() returns, while state
  is still Creating, to not miss on_created" 在 mac/gtk 上从假话变真话；
  on_url_changed 注释声明的 "origin tracking; consumed by jsbridge" 落地；
  backend 公共接口（backend.h 方法集）未动。样本检查 on_created 语义亦无矛盾
  （无任何头文件承诺 created 在 create() 内同步发射）。
- [SHOULD FIX] **m_tab_map 残留死条目**："PluginCenter" 仍在 SSWCP::m_tab_map
  （SSWCP.cpp:7778），但特判分支先于 map 查找拦截，该条目永远不会被走到——
  留着会诱导后人以为 map 路径可用（tpPluginCenter=9 裸索引越界）。随下一个
  commit 删掉这一行即可。
- [OK] 通用：两 diff 零 try-catch（sw_SwitchTab 的 try 属存量）；零新 stub；
  各自范围聚焦（框架修复 4 文件 / must-fix+riders 5 文件，无夹带）；工作区
  clean，无 1.7 半成品；Windows 构建自证（//m:1 绿，backend 两文件被
  CMakeLists 平台门挡在 Windows 构建外，src/orca_webview/CMakeLists.txt:13-39
  核实——"read-through verified" 的说法诚实）。备注（不要求改）：(a) mac E1 用
  didCommit 时刻的 `webView.URL`，属标准写法，但个别 macOS 版本有 URL 滞后于
  didCommit 的历史怪癖——mac 真机 bring-up 时验证一下，若滞后改缓存
  decidePolicy 的 navigationAction.request.URL；(b) mac/linux 侧一切结论同为
  read-through，编译验证只能等对应平台（正是冒烟清单第 8 条的最低门槛）。

### Verdict: APPROVED — review-6 E1/E2/C1 与 review-7 MUST FIX 全部验收通过；
### review-7 的 NEEDS CHANGES 就此解除，1.7 前置硬条件全部清空
### Next: **放行 1.7 冒烟**。按 review 8 汇总的 8 条清单逐项执行，结果（成功/
### 卡点）写进 FEEDBACK.md 并 commit；清单第 8 条的最低门槛即"mac/linux 至少
### 编译过"——本 commit 后 mac/linux 的 origin/created 行为真机验证仍空缺，
### 编译通过即可，运行时验证登记为 mac bring-up 项。**结转事项（建议冒烟前
### 用一个小 commit 落地，非阻塞）**：(1) review-8 E2：PluginTabPage 加
### notify_page_state override 经 `m_session->bridge()->emit("page.state", ...)`
### 转发（PluginSession 已暴露 bridge()），落地后冒烟第 6 条追加"切走再切回
### sample 页自动刷新"；(2) review-8 C3：plugin.open 失败原因区分（权限拒/
### 加载失败）+ 中心页本地化 toast 键；(3) 本轮 m_tab_map 死条目删除。

---

## Round 1 / review 10 — 406b30f378 "carry-over fixes"

专项验收轮：review-8 两条 SHOULD FIX（E2 page.state 死线 / C3 open 失败原因）+
review-9 一条（m_tab_map 死条目）。联动路径全部实读：MainFrame PAGE_CHANGED
处理器（virtual 派发点）、WebViewPageBase::notify_page_state 基类实现、
PluginSession::bridge()/close()（plugin_host.cpp:144-160，detach 不置空）、
Bridge::emit → BridgeCore::post（bridge.cpp:45-51, 368-371）、encode_event
（protocol.cpp:228-236）、SDK recv/dispatchEvent（orca_sdk.js:101-119）、
sample 页与中心页的 page.state 订阅、open_plugin 全部失败出口、
prompt_plugin_permissions 全文、MainFrame::open_plugin_tab/close_plugin_tab、
sw_SwitchTab 特判与 m_tab_map 唯一消费点。**三条全部验收通过，零 MUST FIX、
零 SHOULD FIX。**

- [OK] **A. page.state 死线修复（review-8 E2）验收通过**。PluginTabPage 增加
  notify_page_state override（PluginTabPage.cpp:44-55）经
  `m_session->bridge()->emit("page.state", {{"state", state}})` 转发——正是
  review-8 建议的修法。**派发正确性**：基类方法改 virtual
  （WebViewPageBase.hpp:46）是必要且正确的——MainFrame 处理器全部经
  `WebViewPageBase*` 调用（MainFrame.cpp:1239/1240/1244/1248，全仓无其他
  调用点），此前静态绑定永远进不了 override；改 virtual 对其余子类零影响
  （全仓唯一 override 就是 PluginTabPage，PluginCenterPage 走原基类实现，
  行为逐字节不变）。**wire 匹配逐环核实**：encode_event 产出
  `{orca:1,type:"event",event:"page.state",data:{state:...}}` → SDK recv 的
  `dispatchEvent(frame.event, frame.data)`（orca_sdk.js:115-116）→ sample 页
  `e.state === 'active'`（sample index.html:318-321）与中心页同款
  （app.js:550）——事件名与负载结构完全对上。**降级路径安全**：!m_session /
  bridge 为空时静默返回——两条都不可达（tab 进 notebook 前 session 必已
  adopt，AddPage 只在 open_plugin 成功后），且等价于基类 SSWCP fallback 对
  插件页的 no-op；bridge 已 detach 时 emit 经 BridgeCore::post 返回
  InvalidState 状态（bridge.cpp:47-48），不崩、不误导。**时序走查**：中键关
  插件 tab 时 RemovePage 同步触发 PAGE_CHANGED → notify("inactive") 此刻
  session 尚未 unload（close_plugin 在 RemovePage 之后，MainFrame.cpp:3752-
  3759），emit 安全；帧发向仍存活的 webview，随后 session 关闭，无 UAF。
  基类 virtual 化只动应用层 WebViewPageBase.hpp，三库公共头零改动（延续
  review-9 F 的冻结检查）。
- [OK] **B. open 失败原因区分 + 本地化（review-8 C3）验收通过**。
  **C++ 侧**：open_plugin 入口 clear、四个 nullptr 出口全部落因——manifest
  无效/host load 失败/adopt 失败 → "load_failed"（PluginManager.cpp:598/622/
  631），权限拒 → "permission_denied"（本次弹窗点 No）或 "already_denied"
  （持久化拒录，denial_was_persisted 出参在 prompt 内只在 store 命中
  Persisted-Denied 分支置位，:705-709，语义准确）；handler 在同一 UI 线程
  同步调用链上读 m_last_open_reason（open_plugin_tab → open_plugin 同帧完成），
  无竞态。"已运行"不算失败（幂等 select → ok:true），无需原因，与契约一致。
  wire 变化纯增量（"error" 保留英文原文，追加 "reason" 字段），中心页是唯一
  消费方且同 commit 更新。**前端**：REASON_TOAST_KEYS 把三个 reason 映射到
  两个新键（app.js:401-412），permission_denied/already_denied 共用
  toast_open_denied——合理，两个 denial 变体用户行动一致（删了重装），
  注释写明理由；未知 reason / 无 reason 回落 res.error → err_generic，
  其他动作（import/remove/close）文案不受影响（callAction 仅失败分支换用
  failureText，:434）。**本地化**：zh/en 两表各 28 键、逐键 lockstep
  （脚本计数验证，28/28 无单侧键）；中文文案如实描述恢复路径（"先删除再
  安装"与 remove → revoke_all 行为一致，review-7 G 已验）。英文笼统直出
  问题解决。
- [OK] **C. m_tab_map 死条目删除（review-9）验收通过，无回归**。"PluginCenter"
  条目删除后，sw_SwitchTab 的 `if (target_tab == "PluginCenter")` 特判
  （SSWCP.cpp:1129-1137）位于 `else if (m_tab_map.count(...))`（:1138）之前
  ——该字符串永远到不了 map 查找，删除被遮蔽条目对运行时行为零影响。
  m_tab_map 全仓唯一消费点就是 :1138-1139；全文件 grep "PluginCenter" 仅剩
  include（:6，特判里 upcast 需要完整类型）与特判本身，无其他引用。存量
  八个条目（legacy 字符串）未动。特判缺 frame/center 时经 handle_general_fail
  正常失败语义保留（review-9 已验，本 commit 未触碰该分支）。
- [OK] **D. 通用**：diff 零 try-catch、零新 stub；`git show --stat` 7 文件
  全部映射到三条结转事项 + 派发所必需的基类 virtual（应用层），无夹带；
  风格与各文件既有惯例一致（对齐赋值、理由注释、m_last_open_reason 成员
  注释完整）；工作区 clean（无 1.7 冒烟半成品混入），HEAD 即本 commit；
  构建自证（commit message "Build verified: //m:1 green, dll relinked"）。
- 备注（不要求改）：(a) m_last_open_reason 有两个理论上的报告盲区——
  open_plugin_tab 在调用 open_plugin **之前**失败（`!m_tabpanel`，此时读到
  上一次的旧 reason）与 AddPage 失败回收路径（open 成功后 reason=""，回落
  英文 "failed to open plugin"）；两条在当前调用链均不可达（handler 已前置
  拦截空 id；frame 存在则 tabpanel 必在），接受现状。(b) plugin_open handler
  本次开始捕获 this（PluginManager）——与 import/install_sample 既有模式
  同险（review-5 备注 (a) 已裁接受的 CallAfter-在-shutdown-后悬垂窗口），
  非新级别风险。(c) 混合拒绝（一个 tag 持久化拒 + 另一个本次点 No）报
  already_denied——前端两键共用一个 toast，用户可见行为无差，注释已声明
  该取舍。

### Verdict: APPROVED — review-8 E2/C3 与 review-9 死条目三条全部验收通过，
### Round 1 代码侧结转事项清零
### Next: **1.7 冒烟即刻执行**：按 review 8 汇总的 8 条清单逐项跑（MSVC 构建
### 无新警告；legacy sw_* 回包；权限链全走包括点 No 的中心页 toast 文案区分；
### 白屏回归；中心页全链 locale/dark_mode；UI 状态机；生命周期 recreate；mac/
### linux 编译门槛），外加本轮落地后新增的第 9 项：**切走再切回 sample 插件
### tab 自动刷新**（page.state active → sample 页 orca.filament.list 重拉），
### 以及中心页 open 被拒时中文界面出中文 toast（不再是英文直出）。结果
### （成功/卡点）逐项写进 FEEDBACK.md 并 commit。
---

## Round 1 / review 11 — f2bbe1e810 "fix plugin center blank page/crash: WebView2 env options"

先说明审查对象身份：f2bbe1e810 已被 EXECUTOR 自行 amend 为 94a334daab（仅补
`[plugin-demo]` 前缀；父提交同为 9a530b91ae，`git diff f2bbe1e810 94a334daab`
为空）——本方 commit、review 前、未 push，属纠正协议瑕疵而非改写历史，不扣分；
本节按内容审查，两者等同。本轮实读：完整 diff、backend_win_environment.cpp
全文、backend_win.cpp 环境/controller 创建链与 put_Bounds 路径、Widgets/
WebView.cpp 工厂与 WebViewEdge 子类、**deps 内实际构建的 wx 源码**（src/msw/
webview_edge.cpp、include/wx/webview.h、include/wx/msw/webview_edge.h）、WRL
WebView2EnvironmentOptions.h（nuget 1.0.1418.22，随 wx 构建）、全部 SetUserAgent
调用点、MainFrame 启动顺序（WebViewPanel :1303 先于 PluginCenterPage :1353）。
结论：修复的工程结构正确、创建点覆盖完整，但**中心前提与本树代码事实不符**
——本进程内 wx 环境的 AdditionalBrowserArguments 实际为空，新代码传入的
--user-agent 与 wx 的空参数恰好构成 commit 自己注释里声明为 UB 的 mismatch。

- [MUST FIX] **修复前提不成立：wx 侧环境参数在本树恒为空，本修复反而引入了
  它自己禁止的 mismatch**。证据链（逐环可复核）：
  (1) wx 环境参数的唯一来源是 `wxWebViewEdge::SetUserAgent`（deps wx
  src/msw/webview_edge.cpp:794-796）写 `m_impl->m_customUserAgent`，再由
  `wxWebViewEdgeImpl::Create`（同文件 :97-101）在 `!m_customUserAgent.empty()`
  时拼 `--user-agent="..."` 传入 options；
  (2) 但应用侧 `Widgets/WebView.cpp:112` 的 `WebViewEdge` 子类 **override 了
  virtual SetUserAgent**（基方法链：include/wx/webview.h:190 virtual →
  include/wx/msw/webview_edge.h:89 wxOVERRIDE），实现是 per-view
  `ICoreWebView2Settings2::put_UserAgent` + `pendingUserAgent` 暂存，**从不写
  m_customUserAgent**（m_impl 亦为 private，子类够不着）；
  (3) 全部调用点虚派发到子类：工厂 `WebView::CreateWebView`（:390/:419/:539）、
  WebViewDialog.cpp:871（`wxWebView* m_browser`）——无一到达 wx 基实现；
  (4) ⇒ 本进程每个 wx 环境的 AdditionalBrowserArguments = 空。**旧代码
  （nullptr options = 空参数）本来就与 wx 字节一致**，"orca 空参数 vs wx
  --user-agent" 的 mismatch 从未存在过；新代码传 `--user-agent="SM-Slicer/
  v..."` 后两侧反而不同——正是 webview.hpp 新注释（"must be byte-identical
  ... a mismatch is undefined behavior"）与 backend_win_environment.h 新注释
  禁止的情形。运行时"能跑"的解释：启动顺序固定（wx 环境随 home 页先建、
  浏览器进程已持有 folder），当前 WebView2 runtime 对同 folder 的后续环境
  attach-and-tolerate、不校验参数差异——即靠 UB 容忍跑通，不是靠匹配；
  commit 自述 "intermittently crashed" + 修复后一次验证通过，无法排除时序
  运气成分（见 SHOULD FIX 根因候选）。
  修法（推荐 (a)，单一 commit `[plugin-demo] fix review-11 must-fix`）：
  (a) 让参数真正字节一致——本树事实是空：`webview_browser_arguments()` 改返回
  空串（**函数、Config 字段、三处接线全部保留**，未来 wx 侧真启用时再填回）；
  同时 `acquire_shared_environment` 改为**无条件**构造 WRL options 对象（args
  为空时只跳过 put），使 orca 的请求与 wx 的形状完全一致（同一 WRL 类 ctor、
  同 TargetCompatibleBrowserVersion=107.0.1418.22、空 args）。改完必须重跑
  UIAutomation repro（launch → 点 Plugins tab + 走一个 legacy webview 页）；
  若空白页复现，把 create_view 失败链的 HRESULT（Status 已带 hr）打进日志
  并写进 FEEDBACK.md——那证明真实根因不在参数，按下面 SHOULD FIX 候选另行
  定位，不许再靠试参数碰运气；
  (b) 若 EXECUTOR 认定 wx 确实传了 --user-agent：先拿运行时证据（在
  wxWebViewEdgeImpl::Create 断点看 m_customUserAgent，或日志）写进
  FEEDBACK.md 再谈，静态代码不支持该结论；
  (c) "让 wx 侧真启用"路线需 patch wx/给子类加转发，风险远大于收益，不推荐。
- [SHOULD FIX] **真实根因未定位**：既然旧代码（空 vs 空）参数本就匹配，空白页/
  崩溃的真实成因需要登记并在 FEEDBACK.md 记录排查结论（随 must-fix commit
  一并）。三个候选，按可能性排序：
  1) 启动期并发创建：orca 环境创建（PluginCenterPage ctor，MainFrame.cpp:
     1353）可能与 wx 首个环境创建（:1303，异步 spawn 浏览器进程）仍在飞行中
     重叠；同 folder 并发且 options 不同时 loader 未必合并请求。这是 orca 栈
     首次在 app 内真实运行（1.7 冒烟），与"间歇性"症状吻合。可在
     acquire_shared_environment 加一条创建起止日志验证时序。
  2) fixed runtime 分叉：`WebView::CreateWebView` 检测 `$exe/edge_fixed` 时调
     `MSWSetBrowserExecutableDir`（Widgets/WebView.cpp:365-375），wx 环境改用
     固定版浏览器；orca `create_environment` 第一参恒 nullptr（Evergreen）。
     装了 fixed runtime 的机器上两侧浏览器二进制不同、同 folder 必坏——orca
     侧应镜像该检测（或至少日志显式提示两侧 runtime 来源）。
  3) 残留浏览器进程：上次崩溃留下的 msedgewebview2.exe 僵尸持有 folder，同样
     产生 "controller browser 侧不活 + put_Bounds AV" 的症状。
- [OK] **A. 三处创建点覆盖完整**（除参数串本身取值错误外）：WebViewPageBase::
  create_view 的 Windows-only 空值默认填充覆盖 PluginCenterPage（cfg 留空即得）；
  PluginHost::load 的 session webview 经 HostEnv.additional_browser_arguments
  线程化（plugin_host.cpp:291），两个 HostEnv 填充点（PluginManager.cpp:569、
  WebViewDevDialog.cpp:243）都填；WebViewDevDialog::create_webview（:87）直接
  填。全仓 grep 无第 4 个直接 create 点（PluginTabPage / DevDialog adopt 的是
  session 视图，非创建）；纯 orca 测试进程空参数有注释说明，自洽。
- [OK] **B. 冲突检测把 UB 转成干净失败**：已建（s_environment）/在建
  （s_pending）双阶段都比对 folder+args，mismatch → InvalidState 且错误消息
  含双方实参（可诊断）；同步失败（create_environment 直接 FAILED）与异步失败
  （callback errorCode）路径都复位 s_environment_creating 并 fail_pending，
  无卡死窗口、无泄漏。
- [OK] **WRL options 生命周期正确**：put_AdditionalBrowserArguments 经
  AutoCoMemString 深拷贝（WebView2EnvironmentOptions.h 的 STRING_PROPERTY 宏
  → m_p.Set(value) CoTaskMem 分配），临时 wstring 的 .c_str() 无悬垂；
  create_environment 后 options->Release() 配平（COM 引用由调用方在调用期间
  持有）。ctor 自带的 TargetCompatibleBrowserVersion=107.0.1418.22 与 wx 用
  同一 header 同值——这是 MUST FIX (a) "无条件构造 options 对象形状一致"的
  依据。
- [OK] **快照语义与字节忠实度（串的构造本身）**：static 单次快照 + 注释钉死
  "参数固定于环境创建时刻、后续 dark 切换不得漂移"；SLIC3R_VERSION 全仓唯一
  定义（src/common_func/common_func.hpp:8），WebView.cpp 与 WebViewPageBase.cpp
  取到同值；格式串与 wx 工厂（Widgets/WebView.cpp:390-392）逐字符相同、
  `--user-agent="<ua>"` 包裹方式与 wx 的 Format 一致。common_func include 是
  必需的（提供 SLIC3R_VERSION），非多余。错的不是串，是 wx 从不把它传进环境。
- [OK] **D. frozen 契约**：include/ 改动仅 webview.hpp Config 与 plugin_host.hpp
  HostEnv 两个**增量字段**（默认空、向后兼容、注释写明 Windows-only / 他平台
  忽略——backend_gtk.cpp / backend_mac.mm 确实不读该字段，grep 证实），与
  permission_store() 同一先例（review 3 预告、review 5 认可），且 commit
  message 显式自 flag 了该点；orca_jsbridge 零改动；backend 公共接口
  （backend.h）未动，签名变化只发生在 detail 内部文件之间。
- [OK] **F. 通用**：diff 零 try-catch；`git show --stat` 10 文件全部映射到本
  修复（Config 字段 / 环境创建 / 三处填充），无夹带；风格与既有文件一致。
  构建/ctest(37/37)/UIAutomation 自证引用 commit message，本轮无法独立复核
  ctest，按声明接受。

### Verdict: NEEDS CHANGES（仅 1 条 MUST FIX：参数匹配的前提与代码事实相反，
### 修复后两侧环境参数实际不一致，靠 runtime 容忍而非匹配在跑）
### Next: 按上述 (a) 修法单独一个 `[plugin-demo] fix review-11 must-fix` commit：
### webview_browser_arguments() 返回空 + 无条件构造 options 对象（字段与接线
### 全保留），**并重跑 UIAutomation repro 复验空白页确实不再出现**（这是本条
### 修法的验收硬条件；若复现，打 HRESULT 日志、按 SHOULD FIX 三候选定位真因，
### 结论写 FEEDBACK.md）。同 commit 顺带：环境创建起止/失败日志（候选 1 的最低
### 成本排查）。**另催（协议项）**：FEEDBACK.md 至今没有 1.7 冒烟结果——review 10
### Next 要求的 9 条清单逐项记录（成功/卡点）仍缺席，本 commit 消息里的一条
### UIAutomation 验证不构成交付；请在下一个 commit 前把现有进度（含本 bug 的
### 发现-修复-复验全过程）补写进 FEEDBACK.md。冒烟剩余项在 must-fix 验收后
### 继续。

---

## Round 1 / review 12 — 94a334daab（REVIEWER 撤回旧 E1、落地修正、崩溃真因实测）

本节取代本轮 REVIEWER 早前误写为第二条 "review 11" 的条目（其时位于本条
之前、立论前提错误、未 commit，已整条移除）。三件事：(1) 撤回我的错误结论；
(2) 独立确认 3c3cac2aaa 所载复核（f2bbe1e810 条）的中心前提并把修正代码
落地到工作区；(3) 用受控实验把崩溃真因钉死为该条 SHOULD FIX 候选 3（残留
浏览器进程持有共享 UDF），同时修正该条一处事实错误（"同一 header 同值"）。

**撤回声明**：我此前断言 "wx 工厂在每次 legacy webview 创建时用当时
dark_mode 现烤 --user-agent 传入环境参数"（旧 E1 快照时序洞）——前提错误。
独立复核确认 3c3cac2aaa 的证据链成立：Widgets/WebView.cpp:110 的
WebViewEdge 子类 override virtual SetUserAgent（wx/webview.h:190 virtual →
wx/msw/webview_edge.h:89 wxOVERRIDE），实现为 per-view
ICoreWebView2Settings2::put_UserAgent + pendingUserAgent 暂存，从不写
wxWebViewEdgeImpl 的 m_customUserAgent（private，子类不可达）；全部调用点
（WebView.cpp:390/419/539、WebViewDialog.cpp:871）虚派发到子类 ⇒ 本进程
wx 环境 AdditionalBrowserArguments 恒为空。旧代码（nullptr options）本就
与 wx 字节一致；dark_mode 切换不会造成环境参数分裂（UA 是 per-view 的）。
旧 E1 及其修复（进程级 UA 快照 + wx 工厂改读快照，动过 Widgets/
WebView.cpp）已全部回滚，作废。

- [MUST FIX → 已由本轮 REVIEWER 落地工作区，待主会话 commit] **修正 =
  webview_browser_arguments() 返回空串（f2bbe1e810 条修法 (a) 前半），
  但有意偏离其后半（"无条件构造 WRL options"）**。落地三文件：
  (1) WebViewPageBase.cpp：webview_browser_arguments() 返回空串，注释写明
  依据（子类 override ⇒ wx 环境参数恒空，字节一致即空）；函数、Config
  字段、三处接线全部保留，未来 wx 侧真启用时只需填这一个函数。仅为
  SLIC3R_VERSION 引入的 common_func.hpp include 一并移除。
  (2) WebViewPageBase.hpp：文档注释同步。
  (3) backend_win_environment.cpp：options 构造处注释重写（空 args ⇒
  保持 nullptr 的理由）；新增 trace_environment()（OutputDebugStringA），
  覆盖 create requested / completed / failed(async) / failed(sync) 四阶段
  ——即 f2bbe1e810 条 Next 要求的"环境创建起止/失败日志"，下次复现可用
  DebugView 直接取证。
  **偏离依据（并修正 f2bbe1e810 条一处事实错误）**：该条 [OK] 称 WRL
  ctor 的 TargetCompatibleBrowserVersion "与 wx 用同一 header 同值"——
  不成立。wx 编译用随其构建拉取的 nuget 头（dep_wxWidgets-build/packages/
  Microsoft.Web.WebView2.1.0.1418.22/build/native/include/
  WebView2EnvironmentOptions.h:12 = L"107.0.1418.22"）；orca 侧
  backend_win_environment.cpp 包含的是本仓 deps/WebView2/include/
  WebView2EnvironmentOptions.h:28 = L"138.0.3351.48"。按 (a) 无条件构造
  会得到 TCBV=138 vs wx 107 的新形状差异，且把运行时下限抬到 138（高于
  wx 的要求）。nullptr-when-empty 保持可观察参数（args 空、无 TCBV 约束）
  与 wx 等效且零下限变化。若仍要形状全等须显式
  put_TargetCompatibleBrowserVersion(L"107.0.1418.22")——把 wx 的 nuget
  版本硬编码进 orca，属劣化耦合；除非有证据表明 TCBV 参与同 UDF 环境
  合并判定，否则不推荐（本轮 3 次干净启动 0 崩溃，无此证据）。
  验证：orca_webview + libslic3r_gui + Snapmaker_Orca 重编译零错误，
  ctest -R ^orca_ 37/37 绿。
- [SHOULD FIX → 真因已实测钉死；缓解项移交后续] **崩溃根因 =
  f2bbe1e810 条候选 3：被杀 app 残留的 msedgewebview2 孤儿树持有共享
  UDF**。受控实验（全部跑在修正后构建上）：run1 干净 UDF → 通过（插件
  中心渲染，截图在案）；run1 结束被 Stop-Process -Force → msedgewebview2
  孤儿持有 UDF → run2 CRASH（dmp 11:56）、run3 CRASH（dmp 12:04，签名
  全同）；随后受控复验——12:05 的 6 个具名孤儿 PID（23548/33040/39108/
  53780/61608/62896）在场启动 run4 → CRASH（exit 0xC0000005）；杀掉孤儿
  后 run5/run6 均无崩溃（UIA 文本探针两次阴性——该探针从未成功过，渲染
  确认以 run1 截图变体为准，不影响崩溃判定）。run6 自己的 force-kill 留下
  的 2 个孤儿 ~2 分钟内自行退出——孤儿是否滞留正是间歇性的来源。两份
  dmp 分析完全一致：0xC0000005 于 orca_webview.dll+0xB425 =
  BackendWin::set_bounds（backend_win.cpp:242-248）的
  m_controller->put_Bounds 间接调用，vtable 槽（+0x30）值为
  0xFFFFFFFFFFFFFFFF、异常参数为读 0xFFFFFFFFFFFFFFFF ⇒ 间接调用目标
  是 -1：**controller 对象已被释放（use-after-free 特征，非空指针解引用）**，
  UI 线程（线程表首位）、view state=Ready（set_bounds 的 ready() 门已过）。
  函数定位依据：.pdata 区间 0xB2E0-0xB591 内引用串 "webview controller
  is not ready"（Errc 1002=0x3ea）与 "put_Bounds failed"（2000=0x7d0），
  与源码逐值对应。orca_webview 自身引用计数自洽（controller 引用一次性
  转移、close() 先置空再 Release，全 UI 线程无竞态）——对象是被外部
  （loader 侧随浏览器进程/环境死亡）释放的。机理链：app 被杀/崩溃 →
  msedgewebview2 孤儿持有 GetUserLocalDataDir（wx 与 orca 共享）→ 下次
  启动的环境/控制器死胎 → put_Bounds 经已释放 controller → AV。**两次
  崩溃均发生在修正后（空参数）构建上——env-args mismatch 不是崩溃成因**；
  原始用户"一次崩溃后白屏/崩溃循环"的症状与本机理自洽。方法论注意：
  冒烟/repro 脚本用 Force kill 恰好制造该前置条件，后续脚本要记录（优雅
  退出或杀后清理孤儿）。缓解方向（EXECUTOR 评估，非本轮）：利用已加的
  四阶段 trace + Status hr 做死胎检测（重试或转干净报错）；app 退出确保
  浏览器树随退；必要时文档化"强杀后需等 msedgewebview2 退出再重启"。
- [OK] 以下结论经本轮独立复核仍成立，自被撤回条目 carry-over（以本条
  为准）：(a) 环境缓存两阶段写入无竞态无脏值（pending 对先成对写入再置
  creating 位；creating 期排队按 pending 对比对；loader 缺失同步拒 /
  HRESULT 同步失败 / 异步 errorCode 失败三条路径都复位 creating 并
  fail_pending 清队；残留 pending 值失败后不可达，下次成对覆写）；
  (b) 同 UDF 创建点覆盖完整（WebViewPageBase::create_view 空值自动填、
  WebViewDevDialog:85 显式填、plugin_host.cpp:301 经 HostEnv，两个
  HostEnv 填充点同源；全仓无第 4 处直接 create；纯 orca 测试进程空
  args → nullptr options 与改前逐字节一致）；(c) 三库冻结接口加法合规
  （两字段默认空、文档注明 Windows-only、全仓无位置式聚合初始化、既有
  嵌入方不填即空）；(d) 跨平台面完好（#else 空串、#if defined(_WIN32)
  包裹、新字段 mac/gtk 后端不读、backend_win*.cpp 仍在 WIN32 门内）；
  (e) 空 args 回归面与 6fcc392e6c 以来逐字节一致；(f) 备注三项：
  WebView::RecreateAll 用 Mac 格式 UA 串再调 SetUserAgent 在 Windows 上
  为无害存量怪癖（基实现先赋值后拒、Create 后不再被读）；登录框
  m_sm_user_agent 是 HTTP 自定义头不进环境参数；put_ 忽略 HRESULT 若
  收紧应连同 backend_win.cpp:359-360 两处存量一起。

### Verdict: NEEDS CHANGES（与 3c3cac2aaa 所载结论一致：94a334daab 修复
### 前提与代码事实相反。修正代码已由本轮 REVIEWER 落地工作区并重编译验证
### ——空参数返回 + nullptr-when-empty（含对修法 (a) 后半的有据偏离）+
### 四阶段环境 trace；崩溃真因实测为残留浏览器进程持有共享 UDF，与
### env-args 无关，缓解项移交后续任务）
### Next: 1) 主会话以 `[plugin-demo] fix review-11/12 must-fix` 落地工作区
### 三文件（WebViewPageBase.hpp / WebViewPageBase.cpp /
### backend_win_environment.cpp），不要 amend 94a334daab。2) EXECUTOR：
### FEEDBACK.md 的 1.7 冒烟台账仍缺席（f2bbe1e810 条已催过一次）；本轮
### 6 次 repro 结果（含 2 份 dmp 与孤儿进程对照实验）应作为清单第 10 项
### 的证据入账，Force-kill 方法论注意一并写入。3) 新任务：残留浏览器进程
### 缓解（见 SHOULD FIX 条），并用四阶段 trace 做一次带 DebugView 的复现
### 取 hr 证据。4) TCBV 形状问题（构造 options vs nullptr）如要跟进，先拿
### 运行时证据再动，见偏离依据；默认维持 nullptr-when-empty。
