#pragma once


#include <list>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "configschema/iconfigschemadisplay.hpp"
#include "nlohmann/json.hpp"

namespace ConfigSchema
{

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


I18nField parseI18nField(const nlohmann::json& f);

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

} // namespace ConfigSchema
