#pragma once

#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "configschema/iconfigschemamanager.hpp"
#include "configschema/iconfigsnapshot.hpp"
#include "projectcore_export.hpp"

namespace ConfigSchema
{

class PROJECTCORE_API ConfigScopeSnapShot : public IConfigScopeSnapShot
{
public:
    ConfigScopeSnapShot() = default;

    SlicerVariant value(const std::string& key) const override;
    bool contains(const std::string& key) const override;
    std::list<std::string> keys() const override;

    void setValue(const std::string& key, const SlicerVariant& v);
    void setValue(const std::string& key, SlicerVariant&& v);
    bool remove(const std::string& key);
    void reserve(size_t n) const;
    void clear();

    size_t size() const;
    bool empty() const;

    void apply(const ConfigScopeSnapShot& other);
    void apply(const std::unordered_map<std::string, SlicerVariant>& values);
    void applyVariant(const std::string& key, const SlicerVariant& v);

    std::vector<std::string> diff(const ConfigScopeSnapShot& other) const;

private:
    std::unordered_map<std::string, SlicerVariant> m_values;
};

class PROJECTCORE_API ConfigSnapshot : public IConfigSnapShot
{
public:
    ConfigSnapshot() = default;
    explicit ConfigSnapshot(std::weak_ptr<const IConfigSchemaManager> schema);

    SlicerVariant value(const std::string& key) const override;
    bool contains(const std::string& key) const override;
    std::list<std::string> keys() const override;

    const IConfigScopeSnapShot* scopeConfig(ConfigMetaScope scope) const override;
    const IConfigScopeSnapShot* modelObjectConfig(size_t modelObjectId) const override;

    void setValue(const std::string& key, const SlicerVariant& v);
    void setValue(const std::string& key, SlicerVariant&& v);
    bool remove(const std::string& key);
    void reserve(size_t n);
    void clear();

    size_t size() const;
    bool empty() const;

    void apply(const ConfigSnapshot& other);
    void apply(const ConfigScopeSnapShot& scope);
    void applyVariant(const std::string& key, const SlicerVariant& v);

    void setScopeConfig(ConfigMetaScope scope, const ConfigScopeSnapShot& scopeSnap);
    ConfigScopeSnapShot& scopeConfig(ConfigMetaScope scope);
    bool removeScopeConfig(ConfigMetaScope scope);
    void clearScopesConfig();

    void setModelObjectConfig(size_t modelObjectId, const ConfigScopeSnapShot& scope);
    ConfigScopeSnapShot& modelObject(size_t modelObjectId);
    bool removeModelObject(size_t modelObjectId);
    void clearModelObjectsConfig();

    std::vector<size_t> modelObjectIds() const;

    void setSchemaManager(std::weak_ptr<const IConfigSchemaManager> schema);
    const IConfigSchemaManager* schemaManager() const;


private:
    void invalidateViews() const { m_scopeViewsDirty = true; }
    void ensureScopeViews() const;

private:
    std::weak_ptr<const IConfigSchemaManager> m_schema;
    std::list<std::string> m_fullkeys;
    std::unordered_map<std::string, SlicerVariant> m_fullConfig;
    std::unordered_map<ConfigMetaScope, ConfigScopeSnapShot> m_scopesConfig;
    std::unordered_map<size_t, ConfigScopeSnapShot> m_modelObjectsConfig;
};

}
