# 任务指令（REVIEWER → EXECUTOR）

每轮一节。EXECUTOR 按 Round 顺序做；commit 规则见 PROTOCOL.md。

---

## Round 1 — 打通插件中心端到端主链路（demo 最小闭环）

**现状**：PluginManager / PluginCenterPage / MainFrame 三个 plugin tab 函数全是 stub；
GUI_App 没挂 PluginManager；Notebook 的 closable 参数被忽略。

**目标闭环**：打开插件中心 tab → install_sample → 列表显示 demo_plugin → open →
动态 tab 出现且 SDK 握手成功 → 关闭 tab → 移除插件。

按依赖顺序完成，每块一次 commit：

### 1.1 PluginManager 实装（核心）
- 构造函数：`m_plugins_root = <user_local_data>/plugins`，确保目录存在。
- 把 `WebViewPageBase.cpp` 匿名 namespace 的 `user_local_data_dir()` 提为共享工具函数
  （PluginHost 的 browser_data_dir 必须与它字节一致：Windows 下 WebView2 每进程只允许
  一个 user data folder）。
- `ensure_host()`：懒创建 PluginHost；HostEnv 填 host_version=SLIC3R_VERSION、
  dispatcher=wxCallAfter 封装、permission_store_path=`<data_dir>/plugin/permissions.json`；
  并注册 host 级 API：`orca.host.info`（版本/平台/locale）、`orca.filament.list`（当前项目耗材表）。
- `register_center_apis(bridge)`：逐个实现 PluginManager.hpp 注释里承诺的 9 个 API。
- `open_plugin`：PluginHost::load → 有未授权权限 tag 时弹 wxMessageDialog 确认 →
  允许则写入 PermissionStore 再重载；返回 PluginTabPage。
- `installed_ids()`：扫描 root 下含 plugin.json 的子目录。
- 错误路径全部用 Status/返回值，禁止 try-catch。

### 1.2 GUI_App 挂载
- GUI_App 增加 PluginManager 成员和 `plugin_manager()` 访问器；退出路径先关掉所有插件。

### 1.3 PluginCenterPage::setup_page 实装
- create_view + attach_bridge（SessionInfo.session_id="plugin-center"，origin 与自定义
  scheme 匹配进 allowed_origins）+ register_center_apis + on_view_created 里导航。
- 选用的 scheme/URL 方案在代码注释里写明理由。
- reload_page：带 locale/dark_mode 查询参数重新导航。

### 1.4 MainFrame 三个 stub 实装
- tpPluginCenter 挂载到 tabpanel（参考 m_project / m_calibration 的创建模式，含侧栏按钮和图标）。
- open_plugin_tab：幂等（已开 → select）；用 FindPage 寻址，禁止裸索引。
- close_plugin_tab / close_all_plugin_tabs：保证 m_plugin_tabs 与 session 的映射一致性。

### 1.5 Notebook closable 实装
- `ButtonsListCtrl::InsertPage` 目前忽略 closable 参数：给 closable 的 tab 按钮加
  右键/中键关闭，发 wxCUSTOMEVT_NOTEBOOK_PAGE_CLOSE（携带 page index），由 MainFrame
  接住后走 close_plugin_tab。

### 1.6 plugin_center/index.html 完整化
- 从 48 行占位扩成完整 demo 页：插件列表（id/名称/版本/运行状态）、install_sample、
  导入 zip/文件夹、打开、关闭、移除按钮；全部走 orca SDK，API 名与 C++ 侧一一对应。

### 1.7 验证
- MSVC 构建通过，无新警告。
- 手动冒烟走一遍目标闭环，把结果（成功/卡点）写进 FEEDBACK.md 并 commit。

---

## Round 2+ — 待 Round 1 review 后发布（预告：权限提示 web 化、插件生命周期事件推送、
SDK 测试页回归、可迁移性文档）
