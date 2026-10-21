#pragma once

#include <string>
#include <unordered_map>

#include "configschema/iconfigmetadata.hpp"

namespace ConfigSchema
{

class ConfigMetaData : public IConfigMetaData
{
public:
    virtual ~ConfigMetaData() = default;

    void setProperty(const std::string& propertyKey, const SlicerVariant& value);
    void setUiProperty(const std::string& propertyKey, const SlicerVariant& value);

    std::string key() const override;
    ConfigMetaType type() const override;
    ConfigMetaScope scope() const override;
    SlicerVariant defaultValue() const override;
    SlicerVariant values() const override;
    SlicerVariant min() const override;
    SlicerVariant max() const override;
    SlicerVariant property(const std::string& propertyKey) const override;

    // ui
    bool editEnabled() const override;
    ConfigMetaMode mode() const override;
    ConfigMetaWidgetType widget() const override;
    std::string hyperlinkUrl() const override;
    SlicerVariant uiProperty(const std::string& propertyKey) const override;

private:
    std::unordered_map<std::string, SlicerVariant> m_properties;
    std::unordered_map<std::string, SlicerVariant> m_uiProperties;
};

}
