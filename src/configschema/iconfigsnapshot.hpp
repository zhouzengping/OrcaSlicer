#pragma once

#include <cstdint>
#include <list>

#include "slicervariant.hpp"
#include "iconfigmetadef.hpp"

namespace ConfigSchema
{

struct IConfigScopeSnapShot
{
    virtual ~IConfigScopeSnapShot() = default;
    virtual SlicerVariant value(const std::string& key) const = 0;
    virtual bool contains(const std::string& key) const = 0;
    virtual std::list<std::string> keys() const = 0;
};

struct IConfigSnapShot
{
    virtual ~IConfigSnapShot() = default;
    virtual SlicerVariant value(const std::string& key) const = 0;
    virtual bool contains(const std::string& key) const = 0;
    virtual std::list<std::string> keys() const = 0;
    virtual const IConfigScopeSnapShot* scopeConfig(ConfigMetaScope scope) const = 0;
    virtual const IConfigScopeSnapShot* modelObjectConfig(size_t modelObjectId) const = 0;
};

}
