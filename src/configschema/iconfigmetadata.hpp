#pragma once

#include "iconfigmetadef.hpp"
#include "slicervariant.hpp"

namespace ConfigSchema
{

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

}
