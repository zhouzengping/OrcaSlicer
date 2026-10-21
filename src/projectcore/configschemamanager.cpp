#include "configschemamanager.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>

namespace ConfigSchema
{

using json = nlohmann::json;

namespace {

bool strToDouble(const std::string& s, double& out)
{
    const char* p = s.c_str();
    while (*p == ' ' || *p == '\t') ++p;
    if (!*p) return false;
    char* end = nullptr;
    out = strtod(p, &end);
    return end != p && *end == '\0';
}

int enumIndexOf(const std::vector<std::string>& names, const std::string& s)
{
    for (size_t i = 0; i < names.size(); ++i)
        if (names[i] == s)
            return (int)i;
    return -1;
}

void appendJsonElem(SlicerVariant& v, ValueType elem, const json& j,
                    const std::vector<std::string>& enumNames)
{
    using VT = ValueType;
    if (j.is_null())
    {
        v.append_nil();
        return;
    }
    switch (elem)
    {
    case VT::String:
        v.append(j.is_string() ? j.get<std::string>() : j.dump());
        return;
    case VT::Int:
        if (j.is_number_integer())        v.append(j.get<int64_t>());
        else if (j.is_number_float())     v.append((int64_t)j.get<double>());
        else if (j.is_boolean())          v.append(j.get<bool>() ? (int64_t)1 : (int64_t)0);
        else                              v.append_nil();
        return;
    case VT::Float:
        if (j.is_number())                v.append(j.get<double>());
        else if (j.is_boolean())          v.append(j.get<bool>() ? 1.0 : 0.0);
        else                              v.append_nil();
        return;
    case VT::Percent:
        if (j.is_number())                v.append(Percent{ j.get<double>() });
        else                              v.append_nil();
        return;
    case VT::FloatOrPercent:
        if (j.is_number())
            v.append(FloatOrPercent{ j.get<double>(), false });
        else if (j.is_string())
        {
            std::string t = j.get<std::string>();
            bool pct = !t.empty() && t.back() == '%';
            if (pct) t.pop_back();
            double d = 0.0;
            if (strToDouble(t, d))
                v.append(FloatOrPercent{ d, pct });
            else
                v.append_nil();
        }
        else
            v.append_nil();
        return;
    case VT::Bool:
        if (j.is_boolean())               v.append(j.get<bool>());
        else if (j.is_number())           v.append(j.get<double>() != 0.0);
        else                              v.append_nil();
        return;
    case VT::Enum:
    {
        int idx = -1;
        if (j.is_string())                idx = enumIndexOf(enumNames, j.get<std::string>());
        else if (j.is_number_integer())   idx = (int)j.get<int64_t>();
        if (idx >= 0) v.append(EnumValue{ (int32_t)idx });
        else          v.append_nil();
        return;
    }
    case VT::Point:
        if (j.is_array() && j.size() >= 2 && j[0].is_number() && j[1].is_number())
            v.append(Point(j[0].get<double>(), j[1].get<double>()));
        else
            v.append_nil();
        return;
    case VT::Point3:
        if (j.is_array() && j.size() >= 3 && j[0].is_number() && j[1].is_number() && j[2].is_number())
            v.append(Point3(j[0].get<double>(), j[1].get<double>(), j[2].get<double>()));
        else
            v.append_nil();
        return;
    default:
        v.append(j.is_string() ? j.get<std::string>() : j.dump());
        return;
    }
}

SlicerVariant variantFromJson(ValueType t, const json& j,
                              const std::vector<std::string>& enumNames)
{
    using VT = ValueType;
    if (j.is_null())
        return SlicerVariant();

    if ((uint16_t(t) & g_typeVectorFlag) != 0)
    {
        SlicerVariant v;
        v.deserialize(std::string(), t);
        const VT elem = (VT)((uint16_t)t & ~g_typeVectorFlag);
        if (j.is_array())
        {
            for (const auto& e : j)
                appendJsonElem(v, elem, e, enumNames);
        }
        else
        {
            appendJsonElem(v, elem, j, enumNames);
        }
        return v;
    }

    SlicerVariant v;
    v.deserialize(std::string(), (VT)((uint16_t)t | g_typeVectorFlag));
    appendJsonElem(v, t, j, enumNames);
    SlicerVariant out;
    switch (t)
    {
    case VT::String:  out = SlicerVariant(v.elem_string(0)); break;
    case VT::Int:     out = SlicerVariant(v.elem_int64(0)); break;
    case VT::Float:   out = SlicerVariant(v.elem_double(0)); break;
    case VT::Percent: out = SlicerVariant(v.elem_percent(0)); break;
    case VT::FloatOrPercent: out = SlicerVariant(v.elem_float_or_percent(0)); break;
    case VT::Bool:    out = SlicerVariant(v.elem_bool(0)); break;
    case VT::Enum:    out = SlicerVariant(v.elem_enum(0)); break;
    case VT::Point:   out = SlicerVariant(v.elem_point(0)); break;
    case VT::Point3:  out = SlicerVariant(v.elem_point3(0)); break;
    default:          out = SlicerVariant(v.elem_string(0)); break;
    }
    return out;
}

SlicerVariant jsonToVariant(const json& j, const std::vector<std::string>& enumNames)
{
    if (j.is_boolean())
        return SlicerVariant(j.get<bool>());
    if (j.is_number_integer())
        return SlicerVariant(j.get<int64_t>());
    if (j.is_number_float())
        return SlicerVariant(j.get<double>());
    if (j.is_string())
        return SlicerVariant(j.get<std::string>());
    if (j.is_array())
    {
        SlicerVariant v;
        v.deserialize(std::string(), ValueType::Strings);
        for (const auto& e : j)
            appendJsonElem(v, ValueType::String, e, enumNames);
        return v;
    }
    return variantFromJson(ValueType::String, j, enumNames);
}

bool readTextFile(const std::filesystem::path& p, std::string& out)
{
    std::ifstream f(p, std::ios::binary);
    if (!f)
        return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    if (out.size() >= 3 && (unsigned char)out[0] == 0xEF && (unsigned char)out[1] == 0xBB && (unsigned char)out[2] == 0xBF)
        out.erase(0, 3);
    return true;
}

void buildMetaDataSchema(ConfigSchemaManager& mgr, const json& main, PresetType presetType)
{
    auto& presetKeyList = mgr.m_presetKeys[presetType];

    for (const auto& mj : main["metadata"])
    {
        auto md = std::make_unique<ConfigMetaData>();
        const std::string keyStr = mj.value("key", std::string());
        if (keyStr.empty())
            continue;
        if (mgr.m_byKey.find(keyStr) != mgr.m_byKey.end())
            continue;

        const std::string typeStr = mj.value("type", std::string("string"));
        ConfigMetaType typeEnum = ConfigMetaType::String;
        {
            auto it = s_configMetaTypeMap.find(typeStr);
            typeEnum = it != s_configMetaTypeMap.end() ? it->second : ConfigMetaType::String;
        }
        const std::string scopeStr = mj.value("scope", std::string("none"));
        ConfigMetaScope scopeEnum = ConfigMetaScope::None;
        {
            auto it = s_configMetaScopeMap.find(scopeStr);
            scopeEnum = it != s_configMetaScopeMap.end() ? it->second : ConfigMetaScope::None;
        }

        std::vector<std::string> enumNames;
        if (mj.contains("values") && mj["values"].is_array())
            for (const auto& val : mj["values"])
                enumNames.push_back(val.is_string() ? val.get<std::string>() : val.dump());

        for (auto it = mj.begin(); it != mj.end(); ++it)
        {
            if (it.key() == "ui")
            {
                if (it.value().is_object())
                    for (auto uit = it.value().begin(); uit != it.value().end(); ++uit)
                        md->setUiProperty(uit.key(), jsonToVariant(uit.value(), enumNames));
                continue;
            }
            if (it.key() == "default")
            {
                md->setProperty("default", variantFromJson(typeEnum, it.value(), enumNames));
                continue;
            }
            md->setProperty(it.key(), jsonToVariant(it.value(), enumNames));
        }

        presetKeyList.push_back(keyStr);
        mgr.m_scopeKeys[scopeEnum].push_back(keyStr);
        mgr.m_byScope[scopeEnum].push_back(md.get());
        mgr.m_metaDataKeys.push_back(keyStr);
        mgr.m_byKey[keyStr] = std::move(md);
    }
}

void buildLayoutSchema(ConfigSchemaManager& mgr, ConfigSchemaI18N& i18n, const json& main,
                       const std::string& preset, PresetType presetType)
{
    auto display = std::make_unique<ConfigSchemaDisplay>();
    const bool hasLayout = main.contains("layouts") && main["layouts"].is_object()
        && main["layouts"].contains("tabs") && main["layouts"]["tabs"].is_array();
    if (hasLayout)
    {
        for (const auto& tj : main["layouts"]["tabs"])
        {
            auto tab = std::make_unique<ConfigSchemaTab>();
            tab->m_name = tj.value("name", std::string());
            {
                auto it = i18n.m_tabs.find(tab->m_name);
                if (it != i18n.m_tabs.end())
                    tab->m_title = it->second;
            }
            if (tj.contains("groups") && tj["groups"].is_array())
                for (const auto& gj : tj["groups"])
                {
                    auto group = std::make_unique<ConfigSchemaGroup>();
                    group->m_name = gj.value("name", std::string());
                    {
                        auto it = i18n.m_groups.find(group->m_name);
                        if (it != i18n.m_groups.end())
                            group->m_title = it->second;
                    }
                    if (gj.contains("keys") && gj["keys"].is_array())
                        for (const auto& kj : gj["keys"])
                            if (kj.is_string())
                            {
                                auto mit = mgr.m_byKey.find(kj.get<std::string>());
                                if (mit != mgr.m_byKey.end())
                                    group->m_keys.push_back(mit->second.get());
                            }
                    tab->m_groups.push_back(std::move(group));
                }
            display->m_tabs.push_back(std::move(tab));
        }
    }
    else
    {
        auto& presetKeyList = mgr.m_presetKeys[presetType];
        auto tab = std::make_unique<ConfigSchemaTab>();
        tab->m_name = preset;
        auto group = std::make_unique<ConfigSchemaGroup>();
        group->m_name = "(metadata)";
        for (const auto& key : presetKeyList)
        {
            auto mit = mgr.m_byKey.find(key);
            if (mit != mgr.m_byKey.end())
                group->m_keys.push_back(mit->second.get());
        }
        std::sort(group->m_keys.begin(), group->m_keys.end(),
                  [](const IConfigMetaData* a, const IConfigMetaData* b) { return a->key() < b->key(); });
        tab->m_groups.push_back(std::move(group));
        display->m_tabs.push_back(std::move(tab));
    }
    mgr.m_display[presetType] = std::move(display);
}

void buildPresetSchema(ConfigSchemaManager& mgr, ConfigSchemaI18N& i18n, const json& main,
                       const json& i18nJson, const std::string& preset, PresetType presetType)
{
    buildMetaDataSchema(mgr, main, presetType);

    if (i18nJson.is_object())
    {
        if (i18nJson.contains("keys") && i18nJson["keys"].is_object())
            for (auto it = i18nJson["keys"].begin(); it != i18nJson["keys"].end(); ++it)
                if (it.value().is_object())
                    i18n.mergeKey(it.key(), it.value());
        if (i18nJson.contains("tabs") && i18nJson["tabs"].is_object())
            for (auto it = i18nJson["tabs"].begin(); it != i18nJson["tabs"].end(); ++it)
                if (it.value().is_object())
                    i18n.m_tabs[it.key()] = parseI18nField(it.value());
        if (i18nJson.contains("groups") && i18nJson["groups"].is_object())
            for (auto it = i18nJson["groups"].begin(); it != i18nJson["groups"].end(); ++it)
                if (it.value().is_object())
                    i18n.m_groups[it.key()] = parseI18nField(it.value());
    }

    buildLayoutSchema(mgr, i18n, main, preset, presetType);
}

bool loadPresetSchema(ConfigSchemaManager& mgr, ConfigSchemaI18N& i18n,
                      const std::filesystem::path& schemaDir, PresetType presetType, std::string& err)
{
    const std::string preset = presetTypeToString(presetType);
    std::string text;
    const std::filesystem::path mainPath = schemaDir / "metadata" / (preset + ".json");
    if (!readTextFile(mainPath, text))
    {
        err = "cannot read " + mainPath.string();
        return false;
    }
    json main;
    try
    {
        main = json::parse(text);
    }
    catch (const std::exception& e)
    {
        err = "parse " + preset + ".json failed: " + e.what();
        return false;
    }

    json i18nJson;
    const std::filesystem::path i18nPath = schemaDir / "i18n" / (preset + ".json");
    if (readTextFile(i18nPath, text))
    {
        try { i18nJson = json::parse(text); }
        catch (const std::exception&) {}
    }

    if (!main.contains("metadata") || !main["metadata"].is_array())
    {
        err = preset + ".json has no metadata[]";
        return false;
    }

    try
    {
        buildPresetSchema(mgr, i18n, main, i18nJson, preset, presetType);
    }
    catch (const std::exception& e)
    {
        err = preset + ".json load fields failed: " + e.what();
        return false;
    }
    return true;
}

}

ConfigSchemaManager::ConfigSchemaManager() = default;

ConfigSchemaManager::~ConfigSchemaManager() = default;

IConfigMetaData* ConfigSchemaManager::getMetaData(const std::string& key) const
{
    auto it = m_byKey.find(key);
    return it != m_byKey.end() ? it->second.get() : nullptr;
}

bool ConfigSchemaManager::contains(const std::string& key) const
{
    return m_byKey.find(key) != m_byKey.end();
}

std::list<std::string> ConfigSchemaManager::keys() const
{
    return m_metaDataKeys;
}

std::list<std::string> ConfigSchemaManager::presetKeys(PresetType type) const
{
    std::list<std::string> out;
    auto it = m_presetKeys.find(type);
    if (it != m_presetKeys.end())
        out = it->second;
    out.sort();
    return out;
}

std::list<std::string> ConfigSchemaManager::scopeKeys(ConfigMetaScope scope) const
{
    auto it = m_scopeKeys.find(scope);
    return it != m_scopeKeys.end() ? it->second : std::list<std::string>();
}

std::list<IConfigMetaData*> ConfigSchemaManager::scopeMetaDatas(ConfigMetaScope scope) const
{
    std::list<IConfigMetaData*> out;
    auto it = m_byScope.find(scope);
    if (it != m_byScope.end())
        out.assign(it->second.begin(), it->second.end());
    return out;
}

IConfigSchemaI18N* ConfigSchemaManager::i18n() const
{
    return m_i18n.get();
}

IConfigSchemaDisplay* ConfigSchemaManager::display(PresetType presetType) const
{
    auto it = m_display.find(presetType);
    return it != m_display.end() ? it->second.get() : nullptr;
}

std::string presetTypeToString(PresetType type)
{
    auto it = s_presetType2StringMap.find(type);
    if (it != s_presetType2StringMap.end())
        return it->second;
    return std::string();
}

bool presetTypeFromString(const std::string& s, PresetType& out)
{
    auto it = s_presetString2TypeMap.find(s);
    if (it != s_presetString2TypeMap.end())
    {
        out = it->second;
        return true;
    }
    return false;
}

std::unique_ptr<IConfigSchemaManager> loadConfigSchemaManager(const std::string& schemaDir, std::string& err)
{
    auto mgr = std::make_unique<ConfigSchemaManager>();
    auto i18n = std::make_shared<ConfigSchemaI18N>();

    const PresetType loadOrder[] = { PresetType::Print, PresetType::Filament, PresetType::Printer, PresetType::Hidden };
    for (PresetType type : loadOrder)
    {
        if (!loadPresetSchema(*mgr, *i18n, schemaDir, type, err))
            return nullptr;
    }

    mgr->m_i18n = i18n;
    return std::unique_ptr<IConfigSchemaManager>(mgr.release());
}

}
