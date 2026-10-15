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

void appendJsonElem(SlicerVariant& v, SlicerVariant::ValueType elem, const json& j,
                    const std::vector<std::string>& enumNames)
{
    using VT = SlicerVariant::ValueType;
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
            v.append(SlicerVariant::Point(j[0].get<double>(), j[1].get<double>()));
        else
            v.append_nil();
        return;
    case VT::Point3:
        if (j.is_array() && j.size() >= 3 && j[0].is_number() && j[1].is_number() && j[2].is_number())
            v.append(SlicerVariant::Point3(j[0].get<double>(), j[1].get<double>(), j[2].get<double>()));
        else
            v.append_nil();
        return;
    default:
        v.append(j.is_string() ? j.get<std::string>() : j.dump());
        return;
    }
}

SlicerVariant variantFromJson(SlicerVariant::ValueType t, const json& j,
                              const std::vector<std::string>& enumNames)
{
    using VT = SlicerVariant::ValueType;
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

I18nField parseI18nField(const json& f)
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

bool loadPresetSchema(ConfigSchemaManager& mgr, ConfigSchemaI18N& i18n,
                      const std::filesystem::path& schemaDir, PresetType presetType, std::string& err)
{
    const std::string preset = presetTypeFileName(presetType);
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

    auto& presetKeySet = mgr.m_presetKeys[presetType];

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

        const json noObj = json::object();
        const json ui = (mj.contains("ui") && mj["ui"].is_object()) ? mj["ui"] : noObj;

        md->setProperty("key", SlicerVariant(keyStr));
        md->setProperty("type", SlicerVariant(typeStr));
        md->setProperty("scope", SlicerVariant(scopeStr));
        md->setProperty("widget", SlicerVariant(ui.value("widget", std::string("editline"))));
        md->setProperty("mode", SlicerVariant(ui.value("mode", std::string("simple"))));
        md->setProperty("edit_enable", SlicerVariant(ui.value("edit_enable", true)));
        md->setProperty("nullable", SlicerVariant(mj.value("nullable", false)));

        std::vector<std::string> enumNames;
        if (mj.contains("values") && mj["values"].is_array())
            for (const auto& val : mj["values"])
                enumNames.push_back(val.is_string() ? val.get<std::string>() : val.dump());
        if (!enumNames.empty())
        {
            SlicerVariant v;
            v.deserialize(std::string(), SlicerVariant::ValueType::Strings);
            for (const auto& n : enumNames)
                v.append(n);
            md->setProperty("values", std::move(v));
        }

        if (mj.contains("min") && mj["min"].is_number())
            md->setProperty("min", SlicerVariant(mj["min"].get<double>()));
        if (mj.contains("max") && mj["max"].is_number())
            md->setProperty("max", SlicerVariant(mj["max"].get<double>()));

        md->setProperty("default", variantFromJson(variantTypeOf(typeEnum),
                                                   mj.contains("default") ? mj["default"] : json(nullptr),
                                                   enumNames));

        for (auto it = mj.begin(); it != mj.end(); ++it)
        {
            if (it.key() == "key" || it.key() == "type" || it.key() == "scope"
                || it.key() == "nullable" || it.key() == "default" || it.key() == "min"
                || it.key() == "max" || it.key() == "values" || it.key() == "ui")
                continue;
            md->setProperty(it.key(), variantFromJson(
                it.value().is_array() ? SlicerVariant::ValueType::Strings : SlicerVariant::ValueType::String,
                it.value(), enumNames));
        }

        presetKeySet.insert(keyStr);
        mgr.m_byScope[scopeEnum].push_back(md.get());
        mgr.m_byKey[keyStr] = md.get();
        mgr.m_metaDatas.push_back(std::move(md));
    }

    if (i18nJson.is_object())
    {
        if (i18nJson.contains("keys") && i18nJson["keys"].is_object())
            for (auto it = i18nJson["keys"].begin(); it != i18nJson["keys"].end(); ++it)
                if (it.value().is_object())
                    i18n.mergeKey(it.key(), it.value());
        if (i18nJson.contains("tabs") && i18nJson["tabs"].is_object())
            for (auto it = i18nJson["tabs"].begin(); it != i18nJson["tabs"].end(); ++it)
                if (it.value().is_object() && it.value().contains("label"))
                    i18n.m_tabs[it.key()] = parseI18nField(it.value()["label"]);
        if (i18nJson.contains("groups") && i18nJson["groups"].is_object())
            for (auto it = i18nJson["groups"].begin(); it != i18nJson["groups"].end(); ++it)
                if (it.value().is_object() && it.value().contains("label"))
                    i18n.m_groups[it.key()] = parseI18nField(it.value()["label"]);
    }

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
                                    group->m_keys.push_back(mit->second);
                            }
                    tab->m_groups.push_back(std::move(group));
                }
            display->m_tabs.push_back(std::move(tab));
        }
    }
    else
    {
        auto tab = std::make_unique<ConfigSchemaTab>();
        tab->m_name = preset;
        auto group = std::make_unique<ConfigSchemaGroup>();
        group->m_name = "(metadata)";
        for (const auto& key : presetKeySet)
        {
            auto mit = mgr.m_byKey.find(key);
            if (mit != mgr.m_byKey.end())
                group->m_keys.push_back(mit->second);
        }
        std::sort(group->m_keys.begin(), group->m_keys.end(),
                  [](const IConfigMetaData* a, const IConfigMetaData* b) { return a->key() < b->key(); });
        tab->m_groups.push_back(std::move(group));
        display->m_tabs.push_back(std::move(tab));
    }
    mgr.m_display[presetType] = std::move(display);

    return true;
}

}

void ConfigSchemaI18N::mergeKey(const std::string& key, const json& node)
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

std::string toString(PresetType type)
{
    switch (type)
    {
    case PresetType::Hidden:   return "hidden";
    case PresetType::Print:    return "print";
    case PresetType::Filament: return "filament";
    case PresetType::Printer:  return "printer";
    default:                   return std::string();
    }
}

bool presetTypeFromString(const std::string& s, PresetType& out)
{
    if (s == "hidden")            { out = PresetType::Hidden;   return true; }
    if (s == "print")             { out = PresetType::Print;    return true; }
    if (s == "filament")          { out = PresetType::Filament; return true; }
    if (s == "printer")           { out = PresetType::Printer;  return true; }
    return false;
}

std::string presetTypeFileName(PresetType type)
{
    return toString(type);
}

SlicerVariant::ValueType variantTypeOf(ConfigMetaType type)
{
    const uint16_t flag = g_typeVectorFlag;
    switch (type)
    {
    case ConfigMetaType::String:            return SlicerVariant::ValueType::String;
    case ConfigMetaType::Strings:           return (SlicerVariant::ValueType)(flag | (uint16_t)SlicerVariant::ValueType::String);
    case ConfigMetaType::Int:               return SlicerVariant::ValueType::Int;
    case ConfigMetaType::Ints:              return (SlicerVariant::ValueType)(flag | (uint16_t)SlicerVariant::ValueType::Int);
    case ConfigMetaType::Float:             return SlicerVariant::ValueType::Float;
    case ConfigMetaType::Floats:            return (SlicerVariant::ValueType)(flag | (uint16_t)SlicerVariant::ValueType::Float);
    case ConfigMetaType::Bool:              return SlicerVariant::ValueType::Bool;
    case ConfigMetaType::Bools:             return (SlicerVariant::ValueType)(flag | (uint16_t)SlicerVariant::ValueType::Bool);
    case ConfigMetaType::Percent:           return SlicerVariant::ValueType::Percent;
    case ConfigMetaType::Percents:          return (SlicerVariant::ValueType)(flag | (uint16_t)SlicerVariant::ValueType::Percent);
    case ConfigMetaType::FloatOrPercent:    return SlicerVariant::ValueType::FloatOrPercent;
    case ConfigMetaType::FloatOrPercents:   return (SlicerVariant::ValueType)(flag | (uint16_t)SlicerVariant::ValueType::FloatOrPercent);
    case ConfigMetaType::Enum:              return SlicerVariant::ValueType::Enum;
    case ConfigMetaType::Enums:             return (SlicerVariant::ValueType)(flag | (uint16_t)SlicerVariant::ValueType::Enum);
    case ConfigMetaType::Point:             return SlicerVariant::ValueType::Point;
    case ConfigMetaType::Points:            return (SlicerVariant::ValueType)(flag | (uint16_t)SlicerVariant::ValueType::Point);
    case ConfigMetaType::Point3:            return SlicerVariant::ValueType::Point3;
    case ConfigMetaType::Points3:           return (SlicerVariant::ValueType)(flag | (uint16_t)SlicerVariant::ValueType::Point3);
    default:                                return SlicerVariant::ValueType::String;
    }
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
