#include "configsnapshot.hpp"

#include <algorithm>

namespace ConfigSchema
{

SlicerVariant ConfigScopeSnapShot::value(const std::string& key) const
{
    auto it = m_values.find(key);
    return it != m_values.end() ? it->second : SlicerVariant();
}

bool ConfigScopeSnapShot::contains(const std::string& key) const
{
    return m_values.find(key) != m_values.end();
}

std::list<std::string> ConfigScopeSnapShot::keys() const
{
    std::vector<std::string> sorted;
    sorted.reserve(m_values.size());
    for (const auto& kv : m_values)
        sorted.push_back(kv.first);
    std::sort(sorted.begin(), sorted.end());
    return std::list<std::string>(sorted.begin(), sorted.end());
}

void ConfigScopeSnapShot::setValue(const std::string& key, const SlicerVariant& v)
{
    m_values[key] = v;
}

bool ConfigScopeSnapShot::remove(const std::string& key)
{
    return m_values.erase(key) != 0;
}

void ConfigScopeSnapShot::reserve(size_t n) const
{
    const_cast<std::unordered_map<std::string, SlicerVariant>&>(m_values).reserve(n);
}

void ConfigScopeSnapShot::clear()
{
    m_values.clear();
}

size_t ConfigScopeSnapShot::size() const
{
    return m_values.size();
}

bool ConfigScopeSnapShot::empty() const
{
    return m_values.empty();
}

void ConfigScopeSnapShot::apply(const ConfigScopeSnapShot& other)
{
    for (const auto& kv : other.m_values)
    {
        auto it = m_values.find(kv.first);
        if (it == m_values.end())
            m_values.emplace(kv.first, kv.second);
        else
            it->second.apply(kv.second);
    }
}

void ConfigScopeSnapShot::apply(const std::unordered_map<std::string, SlicerVariant>& values)
{
    for (const auto& kv : values)
    {
        auto it = m_values.find(kv.first);
        if (it == m_values.end())
            m_values.emplace(kv.first, kv.second);
        else
            it->second.apply(kv.second);
    }
}

void ConfigScopeSnapShot::applyVariant(const std::string& key, const SlicerVariant& v)
{
    auto it = m_values.find(key);
    if (it == m_values.end())
        m_values.emplace(key, v);
    else
        it->second.apply(v);
}

std::vector<std::string> ConfigScopeSnapShot::diff(const ConfigScopeSnapShot& other) const
{
    std::vector<std::string> out;
    for (const auto& kv : m_values)
    {
        auto it = other.m_values.find(kv.first);
        if (it == other.m_values.end() || !(it->second == kv.second))
            out.push_back(kv.first);
    }
    for (const auto& kv : other.m_values)
        if (m_values.find(kv.first) == m_values.end())
            out.push_back(kv.first);
    std::sort(out.begin(), out.end());
    return out;
}

ConfigSnapshot::ConfigSnapshot()
{
}

ConfigSnapshot::ConfigSnapshot(std::weak_ptr<const IConfigSchemaManager> schema)
    : m_schema(std::move(schema))
{
}

SlicerVariant ConfigSnapshot::value(const std::string& key) const
{
    auto it = m_fullConfig.find(key);
    return it != m_fullConfig.end() ? it->second : SlicerVariant();
}

bool ConfigSnapshot::contains(const std::string& key) const
{
    return m_fullConfig.find(key) != m_fullConfig.end();
}

std::list<std::string> ConfigSnapshot::keys() const
{
    std::vector<std::string> sorted(m_fullkeys.begin(), m_fullkeys.end());
    std::sort(sorted.begin(), sorted.end());
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
    return std::list<std::string>(sorted.begin(), sorted.end());
}

const IConfigScopeSnapShot* ConfigSnapshot::scopeConfig(ConfigMetaScope scope) const
{
    auto it = m_scopesConfig.find(scope);
    return it != m_scopesConfig.end() ? &it->second : nullptr;
}

const IConfigScopeSnapShot* ConfigSnapshot::modelObjectConfig(size_t modelObjectId) const
{
    auto it = m_modelObjectsConfig.find(modelObjectId);
    return it != m_modelObjectsConfig.end() ? &it->second : nullptr;
}

void ConfigSnapshot::setValue(const std::string& key, const SlicerVariant& v, ConfigMetaScope scope)
{
    const auto [it, inserted] = m_fullConfig.insert_or_assign(key, v);
    if (inserted)
        m_fullkeys.push_back(key);

    ConfigMetaScope target = scope;
    if (target == ConfigMetaScope::None)
        if (const IConfigSchemaManager* schema = schemaManager())
            if (const IConfigMetaData* meta = schema->getMetaData(key))
                target = meta->scope();
    if (target != ConfigMetaScope::None)
        m_scopesConfig[target].setValue(key, it->second);
}

bool ConfigSnapshot::remove(const std::string& key)
{
    if (m_fullConfig.erase(key) == 0)
        return false;
    m_fullkeys.remove(key);
    for (auto& kv : m_scopesConfig)
        kv.second.remove(key);
    return true;
}

void ConfigSnapshot::reserve(size_t n)
{
    m_fullConfig.reserve(n);
}

void ConfigSnapshot::clear()
{
    m_fullkeys.clear();
    m_fullConfig.clear();
    m_scopesConfig.clear();
    m_modelObjectsConfig.clear();
}

size_t ConfigSnapshot::size() const
{
    return m_fullConfig.size();
}

bool ConfigSnapshot::empty() const
{
    return m_fullConfig.empty();
}

void ConfigSnapshot::apply(const ConfigSnapshot& other)
{
    for (const auto& kv : other.m_fullConfig)
        applyVariant(kv.first, kv.second);
}

void ConfigSnapshot::apply(const ConfigScopeSnapShot& scope)
{
    for (const std::string& key : scope.keys())
        applyVariant(key, scope.value(key));
}

void ConfigSnapshot::applyVariant(const std::string& key, const SlicerVariant& v)
{
    auto it = m_fullConfig.find(key);
    if (it == m_fullConfig.end())
    {
        m_fullkeys.push_back(key);
        it = m_fullConfig.emplace(key, v).first;
    }
    else
        it->second.apply(v);

    if (const IConfigSchemaManager* schema = schemaManager())
        if (const IConfigMetaData* meta = schema->getMetaData(key))
        {
            const ConfigMetaScope scope = meta->scope();
            if (scope != ConfigMetaScope::None)
                m_scopesConfig[scope].setValue(key, it->second);
        }
}

ConfigScopeSnapShot& ConfigSnapshot::scopeConfig(ConfigMetaScope scope)
{
    return m_scopesConfig[scope];
}

void ConfigSnapshot::setModelObjectConfig(size_t modelObjectId, const ConfigScopeSnapShot& scope)
{
    m_modelObjectsConfig[modelObjectId] = scope;
}

void ConfigSnapshot::setModelObjectConfig(size_t modelObjectId, const std::string& key, const SlicerVariant& v)
{
    m_modelObjectsConfig[modelObjectId].setValue(key, v);
}

ConfigScopeSnapShot& ConfigSnapshot::modelObject(size_t modelObjectId)
{
    return m_modelObjectsConfig[modelObjectId];
}

bool ConfigSnapshot::removeModelObject(size_t modelObjectId)
{
    return m_modelObjectsConfig.erase(modelObjectId) != 0;
}

void ConfigSnapshot::clearModelObjectsConfig()
{
    m_modelObjectsConfig.clear();
}

std::vector<size_t> ConfigSnapshot::modelObjectIds() const
{
    std::vector<size_t> out;
    out.reserve(m_modelObjectsConfig.size());
    for (const auto& kv : m_modelObjectsConfig)
        out.push_back(kv.first);
    return out;
}

void ConfigSnapshot::setSchemaManager(std::weak_ptr<const IConfigSchemaManager> schema)
{
    m_schema = std::move(schema);
}

const IConfigSchemaManager* ConfigSnapshot::schemaManager() const
{
    return m_schema.lock().get();
}

}
