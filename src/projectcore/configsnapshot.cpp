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

void ConfigScopeSnapShot::set(const std::string& key, const SlicerVariant& v)
{
    m_values[key] = v;
}

void ConfigScopeSnapShot::set(const std::string& key, SlicerVariant&& v)
{
    m_values[key] = std::move(v);
}

bool ConfigScopeSnapShot::erase(const std::string& key)
{
    return m_values.erase(key) != 0;
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

SlicerVariant* ConfigScopeSnapShot::find(const std::string& key)
{
    auto it = m_values.find(key);
    return it != m_values.end() ? &it->second : nullptr;
}

const SlicerVariant* ConfigScopeSnapShot::find(const std::string& key) const
{
    auto it = m_values.find(key);
    return it != m_values.end() ? &it->second : nullptr;
}

std::string ConfigScopeSnapShot::optString(const std::string& key) const
{
    const SlicerVariant* v = find(key);
    if (v == nullptr || v->is_undef())
        return std::string();
    if (v->is_vector())
        return v->empty() || v->elem_is_nil(0) ? std::string() : v->elem_string(0);
    return v->to_string();
}

void ConfigScopeSnapShot::setString(const std::string& key, const std::string& v)
{
    m_values[key] = SlicerVariant(v);
}

std::vector<std::string> ConfigScopeSnapShot::diff(const ConfigScopeSnapShot& other) const
{
    std::vector<std::string> out;
    for (const auto& kv : m_values)
    {
        const SlicerVariant* v = other.find(kv.first);
        if (v == nullptr || *v != kv.second)
            out.push_back(kv.first);
    }
    for (const auto& kv : other.m_values)
        if (m_values.find(kv.first) == m_values.end())
            out.push_back(kv.first);
    std::sort(out.begin(), out.end());
    return out;
}

ConfigSnapshot::ConfigSnapshot(std::shared_ptr<const IConfigSchemaManager> schema)
    : m_schema(std::move(schema))
{
}

SlicerVariant ConfigSnapshot::value(const std::string& key) const
{
    auto it = m_values.find(key);
    return it != m_values.end() ? it->second : SlicerVariant();
}

bool ConfigSnapshot::contains(const std::string& key) const
{
    return m_values.find(key) != m_values.end();
}

std::list<std::string> ConfigSnapshot::keys() const
{
    std::vector<std::string> sorted;
    sorted.reserve(m_values.size());
    for (const auto& kv : m_values)
        sorted.push_back(kv.first);
    std::sort(sorted.begin(), sorted.end());
    return std::list<std::string>(sorted.begin(), sorted.end());
}

void ConfigSnapshot::ensureScopeViews() const
{
    if (!m_scopeViewsDirty)
        return;

    m_scopeViews.clear();
    if (m_schema)
    {
        const int scopeCount = (int)ConfigMetaScope::GCode + 1;
        for (int s = 1; s < scopeCount; ++s)
        {
            ConfigScopeSnapShot& view = m_scopeViews[s];
            for (const std::string& key : m_schema->scopeKeys((ConfigMetaScope)s))
            {
                auto it = m_values.find(key);
                if (it != m_values.end())
                    view.set(key, it->second);
            }
        }
    }
    m_scopeViewsDirty = false;
}

const IConfigScopeSnapShot* ConfigSnapshot::scopeConfig(ConfigMetaScope scope) const
{
    if (scope == ConfigMetaScope::None)
        return nullptr;
    ensureScopeViews();
    auto it = m_scopeViews.find(scope);
    return it != m_scopeViews.end() ? &it->second : nullptr;
}

const IConfigScopeSnapShot* ConfigSnapshot::modelObjectConfig(size_t modelObjectId) const
{
    auto it = m_modelObjects.find(modelObjectId);
    return it != m_modelObjects.end() ? &it->second : nullptr;
}

void ConfigSnapshot::set(const std::string& key, const SlicerVariant& v)
{
    m_values[key] = v;
    invalidateViews();
}

void ConfigSnapshot::set(const std::string& key, SlicerVariant&& v)
{
    m_values[key] = std::move(v);
    invalidateViews();
}

bool ConfigSnapshot::erase(const std::string& key)
{
    const bool removed = m_values.erase(key) != 0;
    if (removed)
        invalidateViews();
    return removed;
}

void ConfigSnapshot::apply(const ConfigSnapshot& other)
{
    for (const auto& kv : other.m_values)
    {
        auto it = m_values.find(kv.first);
        if (it == m_values.end())
            m_values.emplace(kv.first, kv.second);
        else
            it->second.apply(kv.second);
    }
    invalidateViews();
}

void ConfigSnapshot::apply(const ConfigScopeSnapShot& scope)
{
    for (const auto& kv : scope.keys())
    {
        const SlicerVariant* v = scope.find(kv);
        if (v == nullptr)
            continue;
        auto it = m_values.find(kv);
        if (it == m_values.end())
            m_values.emplace(kv, *v);
        else
            it->second.apply(*v);
    }
    invalidateViews();
}

void ConfigSnapshot::applyVariant(const std::string& key, const SlicerVariant& v)
{
    auto it = m_values.find(key);
    if (it == m_values.end())
        m_values.emplace(key, v);
    else
        it->second.apply(v);
    invalidateViews();
}

SlicerVariant* ConfigSnapshot::find(const std::string& key)
{
    auto it = m_values.find(key);
    if (it == m_values.end())
        return nullptr;
    invalidateViews();
    return &it->second;
}

const SlicerVariant* ConfigSnapshot::find(const std::string& key) const
{
    auto it = m_values.find(key);
    return it != m_values.end() ? &it->second : nullptr;
}

ConfigScopeSnapShot& ConfigSnapshot::modelObject(size_t modelObjectId)
{
    return m_modelObjects[modelObjectId];
}

const ConfigScopeSnapShot* ConfigSnapshot::modelObjectPtr(size_t modelObjectId) const
{
    auto it = m_modelObjects.find(modelObjectId);
    return it != m_modelObjects.end() ? &it->second : nullptr;
}

bool ConfigSnapshot::eraseModelObject(size_t modelObjectId)
{
    return m_modelObjects.erase(modelObjectId) != 0;
}

std::vector<size_t> ConfigSnapshot::modelObjectIds() const
{
    std::vector<size_t> out;
    out.reserve(m_modelObjects.size());
    for (const auto& kv : m_modelObjects)
        out.push_back(kv.first);
    return out;
}

void ConfigSnapshot::clear()
{
    m_values.clear();
    m_modelObjects.clear();
    invalidateViews();
}

}
