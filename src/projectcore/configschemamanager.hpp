#pragma once

#include <list>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "configmetadata/configmetadata.hpp"
#include "configschema/iconfigschemamanager.hpp"
#include "projectcore_export.hpp"
#include "nlohmann/json.hpp"

namespace ConfigSchema
{

PROJECTCORE_API std::string         toString(PresetType type);
PROJECTCORE_API bool                presetTypeFromString(const std::string& s, PresetType& out);

PROJECTCORE_API std::string         presetTypeFileName(PresetType type);

PROJECTCORE_API SlicerVariant::ValueType variantTypeOf(ConfigMetaType type);

PROJECTCORE_API std::unique_ptr<IConfigSchemaManager> loadConfigSchemaManager(const std::string& schemaDir, std::string& err);

struct I18nField
{
    std::map<std::string, std::string> langs;

    const std::string& text(const std::string& lang) const
    {
        static const std::string s_empty;
        if (!lang.empty())
        {
            auto it = langs.find(lang);
            if (it != langs.end() && !it->second.empty())
                return it->second;
        }
        auto it = langs.find("");
        return it != langs.end() ? it->second : s_empty;
    }

    bool hasLang(const std::string& lang) const
    {
        auto it = langs.find(lang);
        return it != langs.end() && !it->second.empty();
    }
};

class ConfigSchemaI18N : public IConfigSchemaI18N
{
public:
    std::unordered_map<std::string, std::unordered_map<std::string, I18nField>> m_keys;
    std::unordered_map<std::string, I18nField> m_tabs;
    std::unordered_map<std::string, I18nField> m_groups;
    std::unordered_map<std::string, std::vector<I18nField>> m_enumLabels;

    void mergeKey(const std::string& key, const nlohmann::json& node);

    std::string text(const std::string& key, const std::string& fieldId,
                     const std::string& lang) const override
    {
        auto it = m_keys.find(key);
        if (it == m_keys.end())
            return std::string();
        auto f = it->second.find(fieldId);
        return f != it->second.end() ? f->second.text(lang) : std::string();
    }

    bool hasField(const std::string& key, const std::string& fieldId) const override
    {
        auto it = m_keys.find(key);
        return it != m_keys.end() && it->second.find(fieldId) != it->second.end();
    }

    bool hasTranslation(const std::string& key, const std::string& fieldId,
                        const std::string& lang) const override
    {
        auto it = m_keys.find(key);
        if (it == m_keys.end())
            return false;
        auto f = it->second.find(fieldId);
        return f != it->second.end() && f->second.hasLang(lang);
    }

    std::list<std::string> enumLabels(const std::string& key, const std::string& lang) const override
    {
        std::list<std::string> out;
        auto it = m_enumLabels.find(key);
        if (it == m_enumLabels.end())
            return out;
        for (const auto& f : it->second)
            out.push_back(f.text(lang));
        return out;
    }
};

class ConfigSchemaGroup : public IConfigSchemaGroup
{
public:
    std::string m_name;
    I18nField   m_title;
    std::vector<IConfigMetaData*> m_keys;

    std::string name() const override { return m_name; }
    std::string displayName(const std::string& lang) const override { return m_title.text(lang).empty() ? m_name : m_title.text(lang); }
    std::list<std::string> keys() const override
    {
        std::list<std::string> out;
        for (const auto* m : m_keys) out.push_back(m->key());
        return out;
    }
    std::list<IConfigMetaData*> metaDatas() const override
    {
        return std::list<IConfigMetaData*>(m_keys.begin(), m_keys.end());
    }
};

class ConfigSchemaTab : public IConfigSchemaTab
{
public:
    std::string m_name;
    I18nField   m_title;
    std::vector<std::unique_ptr<ConfigSchemaGroup>> m_groups;

    std::string name() const override { return m_name; }
    std::string displayName(const std::string& lang) const override { return m_title.text(lang).empty() ? m_name : m_title.text(lang); }
    std::list<IConfigSchemaGroup*> groups() const override
    {
        std::list<IConfigSchemaGroup*> out;
        for (const auto& g : m_groups) out.push_back(g.get());
        return out;
    }
};

class ConfigSchemaDisplay : public IConfigSchemaDisplay
{
public:
    std::vector<std::unique_ptr<ConfigSchemaTab>> m_tabs;

    std::list<IConfigSchemaTab*> tabs() const override
    {
        std::list<IConfigSchemaTab*> out;
        for (const auto& t : m_tabs) out.push_back(t.get());
        return out;
    }
};

class ConfigSchemaManager : public IConfigSchemaManager
{
public:
    std::vector<std::unique_ptr<ConfigMetaData>> m_metaDatas;
    std::unordered_map<std::string, ConfigMetaData*> m_byKey;
    std::unordered_map<PresetType, std::unordered_set<std::string>> m_presetKeys;
    std::unordered_map<ConfigMetaScope, std::vector<ConfigMetaData*>> m_byScope;
    std::shared_ptr<ConfigSchemaI18N> m_i18n;
    std::unordered_map<PresetType, std::unique_ptr<ConfigSchemaDisplay>> m_display;

    IConfigMetaData* getMetaData(const std::string& key) const override
    {
        auto it = m_byKey.find(key);
        return it != m_byKey.end() ? it->second : nullptr;
    }

    bool contains(const std::string& key) const override
    {
        return m_byKey.find(key) != m_byKey.end();
    }

    std::list<std::string> keys() const override
    {
        std::list<std::string> out;
        for (const auto& m : m_metaDatas)
            out.push_back(m->key());
        return out;
    }

    std::list<std::string> scopeKeys(ConfigMetaScope scope) const override
    {
        std::list<std::string> out;
        auto it = m_byScope.find(scope);
        if (it != m_byScope.end())
            for (const auto* m : it->second)
                out.push_back(m->key());
        return out;
    }

    std::list<IConfigMetaData*> scopeMetaDatas(ConfigMetaScope scope) const override
    {
        std::list<IConfigMetaData*> out;
        auto it = m_byScope.find(scope);
        if (it != m_byScope.end())
            out.assign(it->second.begin(), it->second.end());
        return out;
    }

    std::list<std::string> presetKeys(PresetType type) const override
    {
        std::list<std::string> out;
        auto it = m_presetKeys.find(type);
        if (it != m_presetKeys.end())
            out.assign(it->second.begin(), it->second.end());
        out.sort();
        return out;
    }

    IConfigSchemaI18N* i18n() const override { return m_i18n.get(); }

    IConfigSchemaDisplay* display(PresetType presetType) const override
    {
        auto it = m_display.find(presetType);
        return it != m_display.end() ? it->second.get() : nullptr;
    }
};

}
