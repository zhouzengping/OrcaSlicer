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

    void        set(const std::string& key, const SlicerVariant& v);
    void        set(const std::string& key, SlicerVariant&& v);
    bool        erase(const std::string& key);
    void        apply(const ConfigScopeSnapShot& other);
    void        apply(const std::unordered_map<std::string, SlicerVariant>& values);
    SlicerVariant* find(const std::string& key);
    const SlicerVariant* find(const std::string& key) const;
    size_t      size() const { return m_values.size(); }
    bool        empty() const { return m_values.empty(); }
    void        clear() { m_values.clear(); }

    std::string optString(const std::string& key) const;
    void        setString(const std::string& key, const std::string& v);
    std::vector<std::string> diff(const ConfigScopeSnapShot& other) const;

private:
    std::unordered_map<std::string, SlicerVariant> m_values;
};

class PROJECTCORE_API ConfigSnapshot : public IConfigSnapShot
{
public:
    ConfigSnapshot() = default;
    explicit ConfigSnapshot(std::shared_ptr<const IConfigSchemaManager> schema);

    SlicerVariant value(const std::string& key) const override;
    bool contains(const std::string& key) const override;
    std::list<std::string> keys() const override;
    const IConfigScopeSnapShot* scopeConfig(ConfigScopeType scope) const override;
    const IConfigScopeSnapShot* modelObjectConfig(size_t modelObjectId) const override;

    void        set(const std::string& key, const SlicerVariant& v);
    void        set(const std::string& key, SlicerVariant&& v);
    bool        erase(const std::string& key);
    void        apply(const ConfigSnapshot& other);
    void        apply(const ConfigScopeSnapShot& scope);
    void        applyVariant(const std::string& key, const SlicerVariant& v);
    SlicerVariant* find(const std::string& key);
    const SlicerVariant* find(const std::string& key) const;

    ConfigScopeSnapShot& modelObject(size_t modelObjectId);
    const ConfigScopeSnapShot* modelObjectPtr(size_t modelObjectId) const;
    bool        eraseModelObject(size_t modelObjectId);
    std::vector<size_t> modelObjectIds() const;

    void        setSchemaManager(std::shared_ptr<const IConfigSchemaManager> schema) { m_schema = std::move(schema); invalidateViews(); }
    const IConfigSchemaManager* schemaManager() const { return m_schema.get(); }

    size_t      size() const { return m_values.size(); }
    bool        empty() const { return m_values.empty(); }
    void        clear();

private:
    std::unordered_map<std::string, SlicerVariant> m_values;
    std::map<size_t, ConfigScopeSnapShot> m_modelObjects;
    std::shared_ptr<const IConfigSchemaManager> m_schema;
    mutable std::map<int, ConfigScopeSnapShot> m_scopeViews;
    mutable bool m_scopeViewsDirty = true;

    void invalidateViews() const { m_scopeViewsDirty = true; }
    void ensureScopeViews() const;
};

}
