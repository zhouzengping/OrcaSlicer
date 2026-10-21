#pragma once

#include <string>
#include <unordered_map>

namespace ConfigSchema
{

enum class PresetType
{
    Hidden = 0,
    Print,
    Filament,
    Printer,
};

// SlicerVariant value type and related enums
inline constexpr uint16_t g_typeVectorFlag = 0x1000;
enum class ConfigMetaType : uint16_t
{
    Undef   = 0x0000,
    String  = 0x0001,            Strings    = 0x1001,
    Int     = 0x0002,            Ints       = 0x1002,
    Float   = 0x0003,            Floats     = 0x1003,
    Bool    = 0x0004,            Bools      = 0x1004,
    Percent = 0x0005,            Percents   = 0x1005,
    FloatOrPercent = 0x0006,     FloatOrPercents = 0x1006,
    Enum    = 0x0007,            Enums      = 0x1007,
    Point   = 0x0008,            Points     = 0x1008,
    Point3  = 0x0009,            Points3    = 0x1009,
};

enum class ConfigMetaWidgetType
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

enum class ConfigMetaScope
{
    None = 0,
    PrintObject,
    PrintRegion,
    Machine,
    Print,
    Gcode,
};


enum class ConfigMetaMode
{
    Simple = 0,
    Advanced,
    Develop,
};

inline std::unordered_map<std::string, PresetType> s_presetString2TypeMap = {
    {"hidden",   PresetType::Hidden},
    {"print",    PresetType::Print},
    {"filament", PresetType::Filament},
    {"printer",  PresetType::Printer}
};

inline std::unordered_map<PresetType, std::string> s_presetType2StringMap = {
    {PresetType::Hidden,   "hidden"},
    {PresetType::Print,    "print"},
    {PresetType::Filament, "filament"},
    {PresetType::Printer,  "printer"}
};

inline std::unordered_map<std::string, ConfigMetaType> s_configMetaTypeMap = {
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


inline std::unordered_map<std::string, ConfigMetaWidgetType> s_configMetaWidgetTypeMap = {
    {"editline",                ConfigMetaWidgetType::EditLine},
    {"textarea",                ConfigMetaWidgetType::TextArea},
    {"edit_serialized",         ConfigMetaWidgetType::EditSerialized},
    {"combobox",                ConfigMetaWidgetType::ComboBox},
    {"combobox_edit",           ConfigMetaWidgetType::ComboBoxEdit},
    {"checkbox",                ConfigMetaWidgetType::CheckBox},
    {"checkbox_override_edit",  ConfigMetaWidgetType::CheckBoxOverrideEdit},
    {"point",                   ConfigMetaWidgetType::PointEdit},
    {"color",                   ConfigMetaWidgetType::Color},
    {"filepicker",              ConfigMetaWidgetType::FilePicker}
};


inline std::unordered_map<std::string, ConfigMetaScope> s_configMetaScopeMap = {
    {"none",                    ConfigMetaScope::None},
    {"print_object",            ConfigMetaScope::PrintObject},
    {"print_region",            ConfigMetaScope::PrintRegion},
    {"machine",                 ConfigMetaScope::Machine},
    {"print",                   ConfigMetaScope::Print},
    {"gcode",                   ConfigMetaScope::Gcode}
};


inline std::unordered_map<std::string, ConfigMetaMode> s_configMetaModeMap = {
    {"simple",                  ConfigMetaMode::Simple},
    {"advanced",                ConfigMetaMode::Advanced},
    {"develop",                 ConfigMetaMode::Develop}
};

} // namespace ConfigSchema