#pragma once

#include "slicervariant.hpp"

namespace ConfigScopeName
{
constexpr const char* g_none        = "none";
constexpr const char* g_print_object = "print_object";
constexpr const char* g_print_region = "print_region";
constexpr const char* g_machine     = "machine";
constexpr const char* g_print       = "print";
constexpr const char* g_gcode       = "gcode";
}

struct IConfigScopeSnapShot
{
    virtual SlicerVariant value(const std::string& key) const = 0;
    virtual bool contains(const std::string& key) const = 0;
    virtual std::list<std::string> keys() const = 0;
};

struct IConfigSnapShot
{
    virtual SlicerVariant value(const std::string& key) const = 0;
    virtual bool contains(const std::string& key) const = 0;
    virtual std::list<std::string> keys() const = 0;
    virtual const IConfigScopeSnapShot* scopeConfig(const std::string& scope) const = 0;
};
