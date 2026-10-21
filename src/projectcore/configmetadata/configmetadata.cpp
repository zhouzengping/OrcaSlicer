#include "configmetadata.hpp"

namespace ConfigSchema
{

void ConfigMetaData::setProperty(const std::string& propertyKey, const SlicerVariant& value)
{
    m_properties[propertyKey] = value;
}

void ConfigMetaData::setUiProperty(const std::string& propertyKey, const SlicerVariant& value)
{
    m_uiProperties[propertyKey] = value;
}

std::string ConfigMetaData::key() const
{
    auto it = m_properties.find("key");
    return it != m_properties.end() ? it->second.to_string() : std::string();
}

ConfigMetaType ConfigMetaData::type() const
{
    auto it = m_properties.find("type");
    if (it == m_properties.end())
        return ConfigMetaType::String;
    auto m = s_configMetaTypeMap.find(it->second.to_string());
    return m != s_configMetaTypeMap.end() ? m->second : ConfigMetaType::String;
}

ConfigMetaScope ConfigMetaData::scope() const
{
    auto it = m_properties.find("scope");
    if (it == m_properties.end())
        return ConfigMetaScope::None;
    auto m = s_configMetaScopeMap.find(it->second.to_string());
    return m != s_configMetaScopeMap.end() ? m->second : ConfigMetaScope::None;
}

SlicerVariant ConfigMetaData::defaultValue() const
{
    return property("default");
}

SlicerVariant ConfigMetaData::values() const
{
    return property("values");
}

SlicerVariant ConfigMetaData::min() const
{
    return property("min");
}

SlicerVariant ConfigMetaData::max() const
{
    return property("max");
}

SlicerVariant ConfigMetaData::property(const std::string& propertyKey) const
{
    auto it = m_properties.find(propertyKey);
    return it != m_properties.end() ? it->second : SlicerVariant();
}

bool ConfigMetaData::editEnabled() const
{
    auto it = m_uiProperties.find("edit_enable");
    return it == m_uiProperties.end() ? true : it->second.to_bool();
}

ConfigMetaMode ConfigMetaData::mode() const
{
    auto it = m_uiProperties.find("mode");
    if (it == m_uiProperties.end())
        return ConfigMetaMode::Simple;
    auto m = s_configMetaModeMap.find(it->second.to_string());
    return m != s_configMetaModeMap.end() ? m->second : ConfigMetaMode::Simple;
}

ConfigMetaWidgetType ConfigMetaData::widget() const
{
    auto it = m_uiProperties.find("widget");
    if (it == m_uiProperties.end())
        return ConfigMetaWidgetType::EditLine;
    auto m = s_configMetaWidgetTypeMap.find(it->second.to_string());
    return m != s_configMetaWidgetTypeMap.end() ? m->second : ConfigMetaWidgetType::EditLine;
}

std::string ConfigMetaData::hyperlinkUrl() const
{
    auto it = m_uiProperties.find("hyperlink_url");
    return it != m_uiProperties.end() ? it->second.to_string() : std::string();
}

SlicerVariant ConfigMetaData::uiProperty(const std::string& propertyKey) const
{
    auto it = m_uiProperties.find(propertyKey);
    return it != m_uiProperties.end() ? it->second : SlicerVariant();
}

}
