#pragma once


#include <string>
#include <unordered_map>

#include "slicervariant.hpp"

namespace ConfigSchema
{

enum class ConfigMetaType
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
    Point,
    Points,
    Point3,
    Points3,
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


struct IConfigMetaData
{
    virtual ~IConfigMetaData() = default;
    virtual std::string key() const = 0;
    virtual ConfigMetaType type() const = 0;
    virtual ConfigMetaScope scope() const = 0;
    virtual ConfigMetaMode mode() const = 0;
    virtual ConfigMetaWidgetType widget() const = 0;
    virtual bool editEnabled() const = 0;
    virtual SlicerVariant defaultValue() const = 0;
    virtual SlicerVariant values() const = 0;
    virtual SlicerVariant min() const = 0;
    virtual SlicerVariant max() const = 0;
    virtual std::string hyperlinkUrl() const = 0;
    virtual SlicerVariant property(const std::string& propertyKey) const = 0;
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
    {"editserialized",          ConfigMetaWidgetType::EditSerialized},
    {"combobox",                ConfigMetaWidgetType::ComboBox},
    {"comboboxedit",            ConfigMetaWidgetType::ComboBoxEdit},
    {"checkbox",                ConfigMetaWidgetType::CheckBox},
    {"checkboxoverrideedit",    ConfigMetaWidgetType::CheckBoxOverrideEdit},
    {"pointedit",               ConfigMetaWidgetType::PointEdit},
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

}
