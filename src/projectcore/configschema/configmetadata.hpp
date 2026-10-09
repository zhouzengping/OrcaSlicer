#pragma once


#include <unordered_map>

// 用于表示配置元数据类型的枚举
enum Class ConfigMetaType
{
    String = 0,
    Strings,
    Int,
    Ints,
    Float,
    Floats,
    Bool,
    Bools,
    Percent,
    Percents,
    FloatOrPercent,
    FloatOrPercents,
    Enum,
    Enums,
    Point,      // 2D point
    Points,     // 2D points
    Point3,     // 3D point
    Points3,    // 3D points
};

// 用于表示配置元数据的UI控件类型的枚举
enum Class ConfigMetaWidgetType
{
    EditLine = 0,
    TextArea,
    EditSerialized,
    ComboBox,
    ComboBoxEdit,
    CheckBox,
    CheckBoxOverrideEdit,
    PointEdit,
    Color,
    FilePicker,
};

// 用于表示配置元数据的作用域的枚举
enum Class ConfigMetaScope
{
    None = 0,
    PrintObject,
    PrintRegion,
    Machine,
    Print,
    Gcode,
};


enum Class ConfigMetaMode
{
    Simple = 0,
    Advanced,
    Develop,
};


struct IConfigMetaData
{
    virtual ~IConfigMetaData() = default;
    virtual ConfigMetaType type() const = 0;                                // 返回配置项的类型，例如 "int", "float", "string" 等
    virtual ConfigMetaScope scope() const = 0;                              // 返回配置项的作用域，例如 "machine", "print" 等
    virtual ConfigMetaMode mode() const = 0;                                // 返回配置项的模式，例如 "normal", "advanced" 等
    virtual ConfigMetaWidgetType widget() const = 0;                        // 返回配置项的 UI 控件类型，例如 "slider", "checkbox", "dropdown" 等
    virtual bool editEnabled() const = 0;                                   // 返回配置项是否可编辑
    virtual SlicerVariant defaultValue() const = 0;                         // 返回配置项的默认值
    virtual SlicerVariant values() const = 0;                               // 返回配置项的可选值列表,例如vector<std::string> 或 vector<int> 等
    virtual SlicerVariant min() const = 0;                                  // 返回配置项的最小值
    virtual SlicerVariant max() const = 0;                                  // 返回配置项的最大值
    virtual SlicerVariant property(const std::string& propertyKey) const = 0;   // 返回配置项的特定属性值 
};


static std::unordered_map<std::string, ConfigMetaType> s_configMetaTypeMap = {
    {"string",                  ConfigMetaType::String},
    {"strings",                 ConfigMetaType::Strings},
    {"int",                     ConfigMetaType::Int},
    {"ints",                    ConfigMetaType::Ints},
    {"float",                   ConfigMetaType::Float},
    {"floats",                  ConfigMetaType::Floats},
    {"bool",                    ConfigMetaType::Bool},
    {"bools",                   ConfigMetaType::Bools},
    {"percent",                 ConfigMetaType::Percent},
    {"percents",                ConfigMetaType::Percents},
    {"float_or_percent",        ConfigMetaType::FloatOrPercent},
    {"float_or_percents",       ConfigMetaType::FloatOrPercents},
    {"enum",                    ConfigMetaType::Enum},
    {"enums",                   ConfigMetaType::Enums},
    {"point",                   ConfigMetaType::Point},
    {"points",                  ConfigMetaType::Points},
    {"point3",                  ConfigMetaType::Point3},
    {"points3",                 ConfigMetaType::Points3}
};


static std::unordered_map<std::string, ConfigMetaWidgetType> s_configMetaWidgetTypeMap = {
    {"editline",                ConfigMetaWidgetType::EditLine},
    {"textarea",                ConfigMetaWidgetType::TextArea},
    {"editserialized",          ConfigMetaWidgetType::EditSerialized},
    {"combobox",                ConfigMetaWidgetType::ComboBox},
    {"comboboxedit",            ConfigMetaWidgetType::ComboBoxEdit},
    {"checkbox",                ConfigMetaWidgetType::CheckBox},
    {"checkboxoverrideedit",    ConfigMetaWidgetType::CheckBoxOverrideEdit},
    {"pointedit",               ConfigMetaWidgetType::PointEdit},
    {"color",                   ConfigMetaWidgetType::Color},
    {"filepicker",              ConfigMetaWidgetType::FilePicker}
};


static std::unordered_map<std::string, ConfigMetaScope> s_configMetaScopeMap = {
    {"none",                    ConfigMetaScope::None},
    {"print_object",            ConfigMetaScope::PrintObject},
    {"print_region",            ConfigMetaScope::PrintRegion},
    {"machine",                 ConfigMetaScope::Machine},
    {"print",                   ConfigMetaScope::Print},
    {"gcode",                   ConfigMetaScope::Gcode}
};