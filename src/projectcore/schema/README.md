# Preset Schema 使用与新增 Key 指南

`src/projectcore/schema/` 下的 YAML 由 `src/projectcore/schema/script/generate_schema.py`
从源码生成，**不要手改**——改源码后重跑脚本即可同步（后续接入 CI 做门禁）。

```bash
python src/projectcore/schema/script/generate_schema.py   # 输出统计 + warnings（应为 0）
python src/projectcore/schema/script/yaml_to_json.py      # 同目录生成 .json（格式对照/消费）
```

## 文件布局

| 文件 | 内容 | 来源 |
|---|---|---|
| `print.yaml` | 工艺预设（质量/强度/速度/支撑/材料/其他 tab） | `TabPrint::build` |
| `filament.yaml` | 耗材预设（含 Setting Overrides 页） | `TabFilament::build + add_filament_overrides_page` |
| `printer.yaml` | 打印机预设（含 Extruder %d 模板页、机器限制） | `TabPrinter::build_fff + build_kinematics_page + build_unregular_pages` |
| `hidden.yaml` | 不出现在预设 tab 的 key（单页平铺，tab/group 名为占位） | PrintConfigDef 减去上述 UI（已知盲区见文末） |
| `i18n/{print,filament,printer,hidden}.yaml` | 展示层：label/full_label/tooltip/sidetext/category/enum_labels 的原文 + 各语言翻译 | `PrintConfig.cpp` 的 L() 文本 + `localization/i18n/zh_CN/*.po` |

层级：`tabs[] → groups[] → keys[] → 属性`，四份文件格式统一。
**三层全部是数组，顺序即 UI 显示顺序**——数组在 YAML/JSON 规范中保证有序，
消费端按下标渲染；mapping 只用于 key 条目内的属性包（按名访问，无顺序需求）。
同一 key 在页间/组间移动只影响其数组位置，顺序信息与结构信息合为一体，
不再需要额外的顺序声明字段。hidden 的 tab/group 名为占位（`"hidden"` /
`"undefined"`，无 UI 语义），key 顺序为原 scope 分组的展开序，scope 信息
在每个 key 条目的 `scope` 字段。

生成器对数组做重复名检查（tab 名 / 组名 / key 名各自唯一，重复记入 warnings）。
printer 页序按 Marlin 类 gcode_flavor 的首次构建顺序输出（非 Marlin 时 Extruder 块会
移到 "Motion ability" 之前且该页被清空，见 `TabPrinter::build_unregular_pages`）。
`Extruder %d` 是模板页：每个挤出机实例化一页，页名运行时以 `_L("Extruder %d")`
格式化，翻译查 i18n 的 tabs 节同模板条目。

---

## type：存储类型与 default 写法

`type` 描述**存储/序列化类型**（C++ `ConfigOption` 子类），是底层切片做类型解析的锚点。
写 default 前先记两条规则：

1. **复数即向量**：`strings/floats/...` 表示 per-extruder（每个耗材槽一个值），
   default 必须是数组 `["PLA"]`，即使当前只有单挤出机。
2. **数值与百分比**：`float_or_percent` 系的 default 有两种形态——
   绝对值写数字 `0`，百分比写**带引号的字符串** `"400%"`；
   `percent` 存储为数值（UI 才加 %），default 写数字 `20`。

### 标量类型（单值）

| type | 存储内容 | default 写法 | 真实例子 |
|---|---|---|---|
| `float` | 小数 | `0.2` | `layer_height` |
| `int` | 整数 | `155` | `scarf_angle_threshold` |
| `bool` | 布尔 | `true` / `false` | `staggered_inner_seams` |
| `percent` | 百分比数值 | `20`（不带引号、不带 %） | `sparse_infill_density` |
| `float_or_percent` | 数值或百分比 | `0`（绝对）或 `"400%"`（百分比，带引号） | `line_width` / `infill_anchor` |
| `string` | 字符串 | `""` / `"文本"` | `sparse_infill_rotate_template` |
| `enum` | 枚举名，必须落在 `values` 内。**对外形式永远是字符串**：C++ 枚举整数值是实现细节，不进入任何序列化格式（ini/3MF/preset 均为字符串），解析侧不做数字转换 | `"aligned"` | `seam_position` |

```yaml
layer_height:
  type: "float"
  widget: "editline"
  scope: "print_object"
  default: 0.2
  min: 0

scarf_angle_threshold:
  type: "int"
  widget: "editline"
  scope: "print_region"
  default: 155
  min: 0
  max: 180

infill_anchor:
  type: "float_or_percent"
  widget: "combobox_edit"
  scope: "print_region"
  default: "400%"          # 百分比形态；数值形态见 line_width（default: 0）

seam_position:
  type: "enum"
  widget: "combobox"
  scope: "print_object"
  default: "aligned"       # 必须是 values 之一（硬枚举）
  values: ["nearest", "aligned", "aligned_back", "back", "random"]
```

### 向量类型（per-extruder，default 一律数组）

| type | 元素类型 | default 写法 | 真实例子 |
|---|---|---|---|
| `floats` | 小数 | `[30]` | `initial_layer_speed` |
| `ints` | 整数 | `[0]` | `slow_down_layers` |
| `bools` | 布尔 | `[true]` | `enable_overhang_speed` |
| `percents` | 百分比数值 | `[50]` | `accel_to_decel_factor` |
| `floats_or_percents` | 数值或 "N%" | `[30]` 或 `["100%"]` | `initial_layer_travel_speed` |
| `strings` | 字符串 | `["PLA"]` | `filament_type` |
| `enums` | 枚举名 | `["95%"]`，元素须在 `values` 内 | `overhang_fan_threshold` |

```yaml
filament_type:
  type: "strings"
  widget: "combobox_edit"
  default: ["PLA"]                          # 每个耗材槽一个材料名
  values: ["ABS", "ASA", "PLA", "PETG", ...]

overhang_fan_threshold:
  type: "enums"
  widget: "combobox"
  default: ["95%"]
  values: ["0%", "10%", "25%", "50%", "75%", "95%"]

initial_layer_travel_speed:
  type: "floats_or_percents"
  widget: "editline"
  default: ["100%"]
  min: 1
```

> **向量 ≠ 多选**：数组的长度对齐耗材槽数量（与 `filament_ids` 等长），不是让用户多选。
> machine limits 的 `floats` default 是固定的双列 `[500, 500]`（normal / silent 电机模式）。
> 向量类型并非 filament.yaml 专属：`initial_layer_speed`、`enable_overhang_speed` 等
> per-extruder 向量 key 也出现在 print.yaml 的 Speed tab（向量表达的是「每个耗材槽一个值」
> 的存储形态，与所在 preset 文件无关）。

### 几何与其他

| type | 存储内容 | default 写法 | 真实例子 |
|---|---|---|---|
| `point` | 二维坐标 | `[0.5, 0.5]` | `best_object_pos` |
| `points` | 二维坐标列表 | `[[0, 0], [200, 0]]`（数组的数组） | `bed_exclude_area` |
| `point3` | 三维坐标 | `[x, y, z]` | 当前无实例（仅 SLA 区 `scale_to_fit`） |
| `points3` | 三维坐标列表 | `[[x, y, z], ...]` | 当前无实例（全库未使用） |
| `datetime` | 时间戳 | ISO 字符串 | 当前无实例 |

### nullable（「自动 / 不覆盖」语义）

`nullable: true` 表示该 key 可取 `null`，含义是**「自动 / 不覆盖」而不是 0**——
写 `0` 是字面上的零，`null` 是交由另一层决定，解析 preset 时必须区别对待。
框架层（`ConfigOptionDef::nullable`、`coFloatsNullable` 等类型）支持这一语义。

当前使用 nullable 的是耗材预设 "Setting Overrides" 页的 **18 个回抽覆盖 key**
（`filament_retraction_length`、`filament_z_hop` 等，由 PrintConfig.cpp 末尾的
`add_nullable` 循环动态注册，widget 为 `checkbox_override_edit`）：

- **default 一律 `[null]`**：默认不覆盖，回抽参数跟随打印机预设的同名 base key
  （去掉 `filament_` 前缀）。源码运行时从 base key 拷贝 default 只是技术兜底，
  Schema 按「不覆盖」语义统一表达为 null。
- preset 里写非空值（如 `filament_retraction_length: [5]`）= 启用覆盖，
  对应 UI 上勾选行首 checkbox；写 `[null]` 或缺省 = 不覆盖（不勾选）。
- 其余属性（type/min/max/values/mode）与 base key 一致；type 为向量
  （floats/bools/percents/enums），每个元素都可独立为 null。

其余静态注册的 key 均未启用 nullable，Schema 中也看不到该属性。

---

## widget：控件类型

`widget` 描述**UI 生成时渲染成什么控件**，与 `type` 正交：UI 生成器看 widget，
类型解析看 type。同一个控件可以承载不同存储类型
（`combobox_edit` 既可以是 strings 的 `filament_type`，也可以是 float_or_percent 的 `infill_anchor`）。

| widget | 控件 | 用户看到什么 | 何时得到它 | 真实例子 |
|---|---|---|---|---|
| `editline` | 单行输入框 | 数值/文本输入框，可带 min/max 校验和侧文本（mm、% 等） | 默认控件 | `layer_height`（`0.2 mm`） |
| `textarea` | 多行文本 | 大段可编辑文本区，占满组宽 | 源码 `multiline = true` | `machine_end_gcode`、`notes` |
| `combobox` | 严格下拉 | 只能从候选中选，不能输入 | type 为 `enum/enums`，或任何带 `values` 的 key | `seam_position`（5 个选项） |
| `combobox_edit` | 可编辑下拉 | 下拉给候选，也允许输入候选外的值 | 源码 `gui_type = f_enum_open / i_enum_open` | `filament_type`（39 个候选 + 可输自定义材料名）、`infill_anchor`（档位 0/1/2/5/10/1000 + 任意值） |
| `checkbox` | 复选框 | 勾选框 | type `bool/bools` | `staggered_inner_seams` |
| `checkbox_override_edit` | 复选框 + 编辑框 | 行首勾选框门控后面的编辑框：勾选 = 本耗材覆盖该参数（编辑框可输入），未勾选 = 跟随打印机预设的同名参数（编辑框显示打印机侧值） | `add_nullable("filament_"+key)` 动态注册的回抽覆盖 key | `filament_retraction_length` |
| `color` | 颜色选择器 | 色板 + 取色 | 源码 `gui_type = color` | `default_filament_colour`（filament.yaml；`filament_colour` 本体在 hidden） |
| `edit_serialized` | 序列化串编辑 | 与 editline 同为单行框，但编辑的是**存储值的序列化串**（如 `bed_exclude_area` 的 points 显示为 `"0x0,200x0,200x200"`，提交时反序列化） | 源码 `gui_type = one_string`（"vector value, edited as a single string"） | `bed_exclude_area`（points 压平）、`thumbnails`、`bed_custom_model`/`bed_custom_texture`（已是 string，仅语义标记） |
| `point` | 坐标控件 | x/y 输入或专用对话框 | type `point/point3/points/points3` | `best_object_pos` |

### combobox 与 combobox_edit 的本质区别

```yaml
seam_position:                      # combobox：硬枚举
  type: "enum"
  values: ["nearest", "aligned", ...]
  # 值必须落在 values 内——UI 不提供输入口，序列化层强约束

filament_type:                      # combobox_edit：软枚举
  type: "strings"                   # 本质是自由字符串
  values: ["ABS", "ASA", ...]
  # values 只是候选建议——厂商 preset 可写 "PLA-HF" 等列表外的值，
  # UI 渲染成可输入的下拉，切片代码按字符串透传/比较
```

选择依据：第三方需要扩展值域（材料名、自定义档位）→ `combobox_edit`（软枚举）；
值域封闭且代码按枚举分支处理 → `combobox`（硬枚举）。

---

## 其他 key 属性

| 属性 | 含义 | 取值 / 来源 |
|---|---|---|
| `edit_enable` | 是否允许编辑（占位给联动禁用场景，如换料时锁定切料参数） | `true` / `false`；默认全 `true`，仅源码 `readonly = true` 的 key 为 `false`（当前唯一：`start_end_points`，值来自 `change_filament_gcode`，只展示） |
| `scope` | 切片侧类型边界（StaticPrintConfig 类归属） | `print_object / print_region / machine / gcode / print / none`（`none` = preset 元数据等） |
| `mode` | UI 显示层级 | `simple / advanced / develop` |
| `values` | 离散值域（下拉候选）；与 `min/max`（连续值域）相对，任何 type 都可能带 | 数组 |
| `min` / `max` | 连续值域 | 数值 |

**槽位绑定约定**（无字段标记，由页面语义蕴含）：`Extruder %d` 模板页内的向量 key
（floats/points/bools/... ）一律绑定第 i 个挤出机槽位（源码挂载全部带
`extruder_idx` 索引，等价于 C++ 的 `key#N` 控件路由）；filament 页的向量 key
绑定当前活跃耗材槽。生成器校验「带索引挂载只出现在 Extruder %d 页」，出现
例外会告警。

### i18n：展示层与翻译（display layer）

`src/projectcore/schema/i18n/{print,filament,printer,hidden}.yaml` 收录每个 key 的**展示层**——
源码 `def->label / full_label / tooltip / sidetext / category / enum_labels` 的原文
与可用翻译。结构与语义（type/widget/default/min/max…）留在主 YAML，
一切「显示什么、翻译成什么」都在这里，以 key 为单位组织：

```yaml
infill_lock_depth:
  label:
    text: "Infill lock depth"        # 源码原文（L() 的 msgid），永远存在
    zh_CN: "填充锁定深度"              # 有译文才出现该语言键
  tooltip:
    text: "The parameter sets the overlapping depth between the interior and skin."
    zh_CN: "该参数设置内部与外壳之间的重叠深度"
  sidetext:
    text: "mm"                        # 单位多为源码裸串，天然无语言键
  category:
    text: "Strength"
    zh_CN: "强度"
  enum_labels:                        # 与主文件 values 按下标平行
    - text: "0 (no open anchors)"
      zh_CN: "0 (无锚线)"
    - text: "1 mm"                    # 裸串单位标签不翻译，只有 text
```

规则：

- **语言键缺失 = 没有译文，回落 `text`**；**译文与原文相同的不输出**（如
  `"mm" → "mm"`）。文件保持稀疏，哪些字段缺翻译一目了然——它同时是
  翻译覆盖率的盘点工具。
- **`enum_labels` 与主文件 `values` 按下标平行**：`values[i]` 的显示标签是
  `enum_labels[i]`（两者文本本来就不同，如存储值 `"1000"` 对应标签
  `"1000 (unlimited)"`）。text 全量收录（含不翻译的裸串标签），两数组
  长度必然相等，**可以安全 zip**。
- tab/group 名在 `tabs:` / `groups:` 节，同 `{text, 语言}` 结构；
  `hidden.yaml` 无此二节（key 按 hidden 主文件的 scope 顺序扁平排列）。
  动态页 `Extruder %d` 同样收录在 `tabs:` 节——`%d` 原样保留，GUI 先查表取
  模板串再格式化实例编号。
- **i18n 文件本身是 mapping**（顶层 `keys:` 按 key 名索引）：它只按名查表、
  无顺序需求——顺序由主 YAML 的 `tabs[].groups[].keys[]` 数组承载，渲染时
  以主文件的下标为准、来这里取文本，两者职责分离。
- 加新语言：生成器 `PO_FILES` 加一个 po 路径，重跑后每种语言自动出现在
  每个字段下。
- 已知限制：machine limits 12 个 key 的 full_label/tooltip 在源码中由
  `boost::format` 拼接轴名（运行时生成，gettext 里备有 per-axis msgid），
  静态无法收录——这些 key 只有 sidetext/category。

## 已知盲区

- `thumb0`/`thumb1`：源码以 `coStrings` 注册却给出标量 `ConfigOptionString{""}`
  默认值，Schema 如实镜像为 `type: strings` + 标量 default——按向量解析会踩坑。

## 如何新增一个 key

以「加一个 float 类型的 `my_new_setting`」为例，共三处源码改动：

### 1. 定义：`src/libslic3r/PrintConfig.cpp`

放在 `PrintConfigDef::init_common_params` / `init_fff_params` 等初始化函数内：

```cpp
def = this->add("my_new_setting", coFloat);
def->label = L("My new setting");          // L() 内英文 = i18n msgid
def->tooltip = L("Explains what it does.");
def->category = L("Strength");              // Tab 页归属（质量/强度/速度/支撑/材料/其他）
def->mode = comAdvanced;                    // comSimple / comAdvanced / comDevelop
def->min = 0;
def->set_default_value(new ConfigOptionFloat(0.2));   // 圆括号标量、花括号向量均可被解析
```

枚举类另需：`enum_values.push_back("a")`（或 emplace_back）+ `enum_labels.push_back(L("A"))`，
并在同文件上方的 `t_config_enum_values s_keys_map_*` 表里登记 `"a" ↔ 枚举符号`。

控件微调（可选）：
- 可编辑下拉：`def->gui_type = ConfigOptionDef::GUIType::f_enum_open;`（配 enum_values）
- 多行文本：`def->multiline = true;`
- 颜色：`def->gui_type = ...::color;`

### 2. 归属（决定 scope）：`src/libslic3r/PrintConfig.hpp`

在对应的 StaticPrintConfig 类里加成员——类即 scope：

| 加到哪个类 | scope | 语义 |
|---|---|---|
| `PrintObjectConfig` | `print_object` | 对象级，可被对象覆盖 |
| `PrintRegionConfig` | `print_region` | 部件（region）级 |
| `MachineEnvelopeConfig` | `machine` | 机器限制 |
| `GCodeConfig` | `gcode` | G-code 输出期 |
| `PrintConfig` | `print` | 全局工艺 |
| （不加成员） | `none` | preset 元数据 / 非 StaticPrintConfig |

耗材维度没有独立类：key 放进 `init_filament_option_keys()`（filament.yaml 归属）
并用向量类型（`coFloats` 等）表达 per-extruder。

### 3. UI：`src/slic3r/GUI/Tab.cpp`（可选）

在对应 `Tab*::build` 的 page/optgroup 里挂载：

```cpp
optgroup->append_single_option_line("my_new_setting");
```

不挂载 = 自动进 `hidden.yaml`（参与切片但不进 UI）。

### 4. 同步 Schema + 翻译

```bash
python src/projectcore/schema/script/generate_schema.py
```

- 中文翻译：向 `localization/i18n/zh_CN/Snapmaker_Orca_zh_CN.po` 补
  `msgid "My new setting"` 的 `msgstr`（fuzzy 条目会被跳过），重跑脚本即进 i18n YAML。
- 脚本输出里出现 `warnings:` 即解析到未知形态，先修到 warnings 为空。

## 已知解析约定

- `machine_max_*` / `machine_speed_*` 等 12 个 key 由 `AxisDefault` 循环展开（x/y/z/e × 3 类），
  其 `floats` default 是双列 `[normal, silent]`。
- `project_schema_version` 运行时以 `project_schema.config_key` 注册，脚本以 `MANUAL_KEYS` 手工登记
  （scope 取 `gcode`，与 PrintConfig.hpp 中 GCodeConfig 的成员归属一致）。
- 重复 `add()` 同名 key = 对同一 ConfigOptionDef 的二次修改，Schema 取最终态
  （现存 4 处：`preset_names`、`outer_wall_acceleration`、`retract_lift_above/below`）。
- enum default（`coEnum`/`coEnumsGeneric`）优先查 `s_keys_map_*` 枚举表
  （支持 `int(E::Sym)` 包装、`E::Sym` 限定符与行尾 `// 注释` 条目），
  失败再走「符号小写去前缀」启发式；仍解析失败会出现在 warnings 里并保留原始符号。
- 跨 def 拷贝可被解析：`auto alias = def = this->add("key",...)` 绑定的局部别名，
  其后 `def->enum_values = alias->enum_values`、`append(def->enum_values, alias->...)`、
  `def->gui_type = alias->gui_type` 等拷贝语句会同步 values/labels/widget
  （如 `bottom_surface_pattern` 拷贝 `top_surface_pattern`、`infill_anchor_max` 拷贝 `infill_anchor`）。
- `def->min/max` 支持命名数值常量（如 `def->min = -max_temp`，`const int max_temp = 1500`）；
  浮点字面量的 `f` 后缀（`60.0f`）会被剥离。
- **`add_nullable` 循环被展开**：`for (const char *opt_key : { ... })` +
  `add_nullable(std::string("filament_") + opt_key, ...)` 生成 18 个 `filament_*`
  回抽覆盖 def——type/min/max/values/label 从已解析的同名 base def 拷贝，
  mode 按循环内 strcmp 名单（4 个 simple，其余 advanced），default 固定 `[null]`、
  `nullable: true`、widget `checkbox_override_edit`（见 nullable 节）。
  列表中被 `//` / `/* */` 注释掉的项（如 `filament_seam_gap`）不展开。
- 其余 key 非字面量的 `add(...)`（如 `project_schema.config_key`）不归属到前一个 def，
  属性行直接跳过（`project_schema_version` 由 `MANUAL_KEYS` 手工登记）。
- UI 侧 `create_line_with_widget(optgroup.get(), "key", ...)` 视为真实挂载
  （其内部调用 `create_single_option_line` + `append_line`）。
- UI 侧 `TabFilament::add_filament_overrides_page`（"Setting Overrides" 页）纳入
  filament.yaml：其中 `for (const std::string opt_key : { ... })` 字面量列表 +
  `append_single_option_line(opt_key);` 的循环挂载同样被展开（注释项剔除）。
- 展示层字段（label/full_label/tooltip/sidetext/category）支持跨行相邻字符串拼接
  （`L("a " "b")`）；RHS 为 `boost::format(...)` 的（machine limits，运行时拼轴名）
  跳过。这些字段只进 i18n 文件，主 YAML 不受影响。
- `enum_labels` 同时收录 `L("...")` 与裸串两种形态（"1 mm" 这类单位标签
  不翻译，i18n 中只有 `text`）。
