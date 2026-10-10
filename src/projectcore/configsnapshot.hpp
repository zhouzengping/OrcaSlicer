#pragma once

#include "configschema/iconfigsnapshot.hpp"

namespace ConfigSchema
{

class ConfigScopeSnapShot : public IConfigScopeSnapShot
{
public:
    virtual ~ConfigScopeSnapShot() = default;
    virtual SlicerVariant value(const std::string& key) const override;
    virtual bool contains(const std::string& key) const override;
    virtual std::list<std::string> keys() const override;
};

// fullconfig 的快照，包含所有作用域的配置项
class ConfigSnapshot : public IConfigSnapShot
{
public:
    ConfigSnapshot() = default;
    virtual ~ConfigSnapshot() = default;

    virtual SlicerVariant value(const std::string& key) const override;
    virtual bool contains(const std::string& key) const override;
    virtual std::list<std::string> keys() const override;
    virtual const IConfigScopeSnapShot* scopeConfig(ConfigScopeType scope) const override;
    virtual const IConfigScopeSnapShot* modelObjectConfig(size_t modelObjectId) const override;

    void setValue(const std::string& key, const SlicerVariant& value);
};


} // namespace ConfigSchema
