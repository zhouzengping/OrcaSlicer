#pragma once

#include <cstdint>
#include <list>

#include "slicervariant.hpp"

namespace ConfigSchema
{

enum ConfigScopeType
{
    None = 0,
    PrintObject,
    PrintRegion,
    Machine,
    Print,
    GCode,
};


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
    virtual const IConfigScopeSnapShot* scopeConfig(ConfigScopeType scope) const = 0;
    virtual const IConfigScopeSnapShot* modelObjectConfig(size_t modelObjectId) const = 0;
};


static std::unordered_map<std::string, ConfigScopeType> s_configScopeTypeMap = {
    {"none",            ConfigScopeType::None},
    {"print_object",    ConfigScopeType::PrintObject},
    {"print_region",    ConfigScopeType::PrintRegion},
    {"machine",         ConfigScopeType::Machine},
    {"print",           ConfigScopeType::Print},
    {"gcode",           ConfigScopeType::GCode},
};


} // namespace ConfigSchema
