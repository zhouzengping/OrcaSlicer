#include "configdisplay.hpp"

namespace ConfigSchema
{

I18nField parseI18nField(const nlohmann::json& f)
{
    I18nField out;
    if (f.is_object())
    {
        for (auto it = f.begin(); it != f.end(); ++it)
            if (it.value().is_string())
                out.langs[it.key()] = it.value().get<std::string>();
    }
    else if (f.is_string())
    {
        out.langs[""] = f.get<std::string>();
    }
    return out;
}

void ConfigSchemaI18N::mergeKey(const std::string& key, const nlohmann::json& node)
{
    auto& fields = m_keys[key];
    for (auto it = node.begin(); it != node.end(); ++it)
    {
        if (it.key() == "enum_labels")
        {
            std::vector<I18nField> labels;
            if (it.value().is_array())
                for (const auto& el : it.value())
                    labels.push_back(parseI18nField(el));
            m_enumLabels[key] = std::move(labels);
        }
        else if (it.value().is_object() || it.value().is_string())
        {
            I18nField f = parseI18nField(it.value());
            if (!f.langs.empty())
                fields[it.key()] = std::move(f);
        }
    }
}

}
