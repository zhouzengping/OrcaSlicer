#include "preset.hpp"

#include <algorithm>
#include <ctime>
#include <fstream>
#include <sstream>

#include "../configschemamanager.hpp"
#include "nlohmann/json.hpp"

namespace ConfigSchema
{

using json = nlohmann::json;

const char* Preset::kKeyVersion       = "version";
const char* Preset::kKeyName          = "name";
const char* Preset::kKeyFrom          = "from";
const char* Preset::kKeyInherits      = "inherits";
const char* Preset::kKeyRenamedFrom   = "renamed_from";
const char* Preset::kKeyType          = "type";
const char* Preset::kKeySettingId     = "setting_id";
const char* Preset::kKeyBaseId        = "base_id";
const char* Preset::kKeyUserId        = "user_id";
const char* Preset::kKeyFilamentId    = "filament_id";
const char* Preset::kKeyDescription   = "description";
const char* Preset::kKeyInstantiation = "instantiation";
const char* Preset::kKeyIsCustom      = "is_custom_defined";
const char* Preset::kKeySyncInfo      = "sync_info";
const char* Preset::kKeyUpdatedTime   = "updated_time";

Preset::Preset(PresetType type_, const std::string& name_, bool isDefault)
    : type(type_), is_default(isDefault), name(name_)
{
}

std::string Preset::suffixModified()
{
    return " (modified)";
}

std::string Preset::removeSuffixModified(const std::string& name)
{
    const std::string suffix = suffixModified();
    return name.compare(0, suffix.size(), suffix) == 0 ? name.substr(suffix.size()) : name;
}

std::string Preset::label() const
{
    return (is_dirty ? suffixModified() : std::string()) + name;
}

bool Preset::isMetaKey(const std::string& key)
{
    static const std::vector<std::string> metaKeys = {
        kKeyVersion, kKeyName, kKeyFrom, kKeyInherits, kKeyRenamedFrom, kKeyType,
        kKeySettingId, kKeyBaseId, kKeyUserId, kKeyFilamentId, kKeyDescription,
        kKeyInstantiation, kKeyIsCustom, kKeySyncInfo, kKeyUpdatedTime,
    };
    return std::find(metaKeys.begin(), metaKeys.end(), key) != metaKeys.end();
}

bool Preset::load(const std::string& filePath, const IConfigSchemaManager* schema,
                  std::vector<std::string>* unknownKeys, bool* skipFile)
{
    file = filePath;
    filament_id_from_json = false;
    std::ifstream f(filePath, std::ios::binary);
    if (!f)
        return false;

    json j;
    try
    {
        f >> j;
    }
    catch (const std::exception&)
    {
        return false;
    }
    if (!j.is_object())
        return false;

    for (auto it = j.begin(); it != j.end(); ++it)
    {
        const std::string& key = it.key();
        const json& value = it.value();

        if (isMetaKey(key))
        {
            if (key == kKeyVersion)             { if (value.is_string()) version = value.get<std::string>(); }
            else if (key == kKeyName)           { if (value.is_string()) name = value.get<std::string>(); }
            else if (key == kKeyInherits)
            {
                if (value.is_string())
                    setInherits(value.get<std::string>());
            }
            else if (key == kKeyRenamedFrom)
            {
                renamed_from.clear();
                if (value.is_array())
                    for (const auto& v : value)
                        if (v.is_string())
                            renamed_from.push_back(v.get<std::string>());
            }
            else if (key == kKeyInstantiation)  { if (value.is_string()) is_visible = value.get<std::string>() != "false"; }
            else if (key == kKeyIsCustom)       { if (value.is_string()) custom_defined = value.get<std::string>(); }
            else if (key == kKeySettingId)      { if (value.is_string()) setting_id = value.get<std::string>(); }
            else if (key == kKeyBaseId)         { if (value.is_string()) base_id = value.get<std::string>(); }
            else if (key == kKeyUserId)         { if (value.is_string()) user_id = value.get<std::string>(); }
            else if (key == kKeyFilamentId)
            {
                if (value.is_string())
                {
                    filament_id          = value.get<std::string>();
                    filament_id_from_json = true;
                }
            }
            else if (key == kKeyDescription)    { if (value.is_string()) description = value.get<std::string>(); }
            else if (key == kKeySyncInfo)       { if (value.is_string()) sync_info = value.get<std::string>(); }
            else if (key == kKeyUpdatedTime)
            {
                if (value.is_number_integer())      updated_time = value.get<long long>();
                else if (value.is_string())         updated_time = atoll(value.get<std::string>().c_str());
            }
            else if (key == kKeyType)
            {
                PresetType fileType;
                if (value.is_string() && typeFromDirName(value.get<std::string>(), fileType) && fileType != type)
                {
                    if (skipFile != nullptr)
                        *skipFile = true;
                    return false;
                }
            }
            continue;
        }

        const IConfigMetaData* meta = schema != nullptr ? schema->getMetaData(key) : nullptr;
        if (meta == nullptr)
        {
            if (unknownKeys != nullptr)
                unknownKeys->push_back(key);
            continue;
        }

        const ValueType vt = meta->type();
        const ValueType elem = (ValueType)((uint16_t)vt & ~g_typeVectorFlag);
        const bool isEnum = elem == ValueType::Enum;

        auto enumIndexByName = [meta](const std::string& text) -> int
        {
            const SlicerVariant& values = meta->values();
            if (!values.is_vector())
                return -1;
            for (size_t i = 0; i < values.size(); ++i)
                if (!values.elem_is_nil(i) && values.elem_string(i) == text)
                    return (int)i;
            return -1;
        };

        if ((uint16_t)vt & g_typeVectorFlag)
        {
            SlicerVariant v;
            v.deserialize(std::string(), vt);
            bool ok = true;
            if (value.is_array())
            {
                for (const auto& e : value)
                {
                    if (e.is_null())
                    {
                        v.append_nil();
                        continue;
                    }
                    const std::string text = e.is_string() ? e.get<std::string>() : e.dump();
                    if (!v.append_parse(elem, text))
                    {
                        if (isEnum)
                        {
                            const int idx = enumIndexByName(text);
                            if (idx >= 0)
                            {
                                v.set_elem(v.size(), EnumValue{ idx });
                                continue;
                            }
                        }
                        ok = false;
                        break;
                    }
                }
            }
            else if (value.is_string())
            {
                ok = v.deserialize(value.get<std::string>(), vt);
            }
            else if (value.is_number() || value.is_boolean())
            {
                ok = v.deserialize(value.is_boolean() ? (value.get<bool>() ? "1" : "0") : value.dump(), vt);
            }
            if (!ok)
                continue;
            config.setValue(key, std::move(v));
        }
        else
        {
            std::string text;
            if (value.is_string())         text = value.get<std::string>();
            else if (value.is_number())    text = value.dump();
            else if (value.is_boolean())   text = value.get<bool>() ? "1" : "0";
            else                           continue;

            std::optional<SlicerVariant> parsed = SlicerVariant::parse(vt, text);
            if (!parsed && isEnum)
            {
                const int idx = enumIndexByName(text);
                if (idx >= 0)
                    parsed = SlicerVariant(EnumValue{ idx });
            }
            if (parsed)
                config.setValue(key, std::move(*parsed));
        }
    }

    loaded = true;
    return true;
}

static std::string enumIndexTextToName(const IConfigMetaData* meta, const std::string& text)
{
    if (meta == nullptr || text == "nil")
        return text;
    char* end = nullptr;
    const long idx = strtol(text.c_str(), &end, 10);
    if (end == nullptr || *end != '\0')
        return text;
    const SlicerVariant& values = meta->values();
    if (!values.is_vector() || idx < 0 || idx >= (long)values.size() || values.elem_is_nil((size_t)idx))
        return text;
    return values.elem_string((size_t)idx);
}

bool Preset::save(const Preset* parent, const IConfigSchemaManager* schema) const
{
    if (is_project_embedded)
        return true;

    std::string fromStr;
    if (isUser())                           fromStr = "User";
    else if (is_project_embedded)           fromStr = "Project";
    else if (is_system)                     fromStr = "System";
    else                                    fromStr = "Default";

    std::vector<std::string> keysToSave;
    if (parent != nullptr)
    {
        keysToSave = config.diff(parent->config);
        for (const std::string& key : config.keys())
        {
            if (parent->config.contains(key))
                continue;
            if (std::find(keysToSave.begin(), keysToSave.end(), key) != keysToSave.end())
                continue;
            bool differsFromDefault = true;
            if (schema != nullptr)
            {
                if (const IConfigMetaData* meta = schema->getMetaData(key))
                {
                    const SlicerVariant v = config.value(key);
                    differsFromDefault = v.is_undef() || !(meta->defaultValue() == v);
                }
            }
            if (differsFromDefault)
                keysToSave.push_back(key);
        }
        std::sort(keysToSave.begin(), keysToSave.end());
    }
    else
    {
        const std::list<std::string> sorted = config.keys();
        keysToSave.assign(sorted.begin(), sorted.end());
    }

    json j;
    if (!version.empty())        j[kKeyVersion] = version;
    j[kKeyName] = name;
    j[kKeyFrom] = fromStr;

    const std::string inheritsStr = inherits();
    if (!inheritsStr.empty())
        j[kKeyInherits] = inheritsStr;
    if (!renamed_from.empty())
        j[kKeyRenamedFrom] = renamed_from;
    if (!custom_defined.empty())
        j[kKeyIsCustom] = custom_defined;
    if (!filament_id.empty())
        j[kKeyFilamentId] = filament_id;
    if (!base_id.empty())
        j[kKeyBaseId] = base_id;

    for (const std::string& key : keysToSave)
    {
        if (key == kKeyInherits)
            continue;
        const IConfigMetaData* meta = schema != nullptr ? schema->getMetaData(key) : nullptr;
        const bool isEnum = meta != nullptr &&
            ((uint16_t)meta->type() & ~g_typeVectorFlag) == (uint16_t)ValueType::Enum;
        const SlicerVariant v = config.value(key);
        if (v.is_undef())
            continue;
        if (v.is_vector())
        {
            std::vector<std::string> elems = v.serialize_vector();
            if (isEnum)
                for (std::string& e : elems)
                    e = enumIndexTextToName(meta, e);
            j[key] = json(std::move(elems));
        }
        else
        {
            std::string text = v.serialize();
            if (isEnum)
                text = enumIndexTextToName(meta, text);
            j[key] = text;
        }
    }

    std::filesystem::path p(file);
    if (p.has_parent_path())
        std::filesystem::create_directories(p.parent_path());

    {
        std::ofstream f(file, std::ios::binary | std::ios::trunc);
        if (!f)
            return false;
        f << j.dump(4) << std::endl;
    }

    std::filesystem::path infoPath = p;
    infoPath.replace_extension(".info");
    save_info(infoPath.string());
    return true;
}

bool Preset::reload(const Preset& parent, const IConfigSchemaManager* schema)
{
    Preset tmp;
    if (!tmp.load(file, schema))
    {
        config = parent.config;
        return false;
    }
    ConfigScopeSnapShot fresh = parent.config;
    fresh.apply(tmp.config);
    config = std::move(fresh);
    return true;
}

bool Preset::load_info(const std::string& infoFile)
{
    std::ifstream f(infoFile);
    if (!f)
        return false;

    std::string line;
    while (std::getline(f, line))
    {
        const size_t eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        std::string key = line.substr(0, eq);
        const size_t vb = line.find_first_not_of(' ', eq + 1);
        const std::string value = vb == std::string::npos ? std::string() : line.substr(vb);
        for (char& c : key)
            if (c == ' ' || c == '\t' || c == '\r') c = '_';

        if (key == "sync_info")          sync_info = value;
        else if (key == "user_id")       user_id = value;
        else if (key == "setting_id")    setting_id = value == "null" ? std::string() : value;
        else if (key == "base_id")       base_id = value == "null" ? std::string() : value;
        else if (key == "updated_time")  updated_time = atoll(value.c_str());
    }

    if (updated_time == 0)
    {
        updated_time = (long long)time(nullptr);
        save_info(infoFile);
    }
    return true;
}

bool Preset::save_info(const std::string& infoFile) const
{
    if (is_project_embedded)
        return true;
    std::ofstream f(infoFile, std::ios::binary | std::ios::trunc);
    if (!f)
        return false;
    const std::string syncToSave = sync_info == "hold" ? std::string() : sync_info;
    f << "sync_info = " << syncToSave << "\n";
    f << "user_id = " << user_id << "\n";
    f << "setting_id = " << setting_id << "\n";
    f << "base_id = " << base_id << "\n";
    f << "updated_time = " << updated_time << "\n";
    return true;
}

void Preset::remove_files()
{
    if (is_project_embedded)
        return;
    if (!file.empty())
    {
        std::error_code ec;
        std::filesystem::remove(file, ec);
        std::filesystem::path infoPath = file;
        infoPath.replace_extension(".info");
        std::filesystem::remove(infoPath, ec);
    }
}

std::string Preset::dirName(PresetType type)
{
    switch (type)
    {
    case PresetType::Print:    return "process";
    case PresetType::Filament: return "filament";
    case PresetType::Printer:  return "machine";
    default:                   return std::string();
    }
}

std::string Preset::iotTypeName(PresetType type)
{
    return presetTypeToString(type);
}

bool Preset::typeFromDirName(const std::string& name, PresetType& out)
{
    if (name == "process")                       { out = PresetType::Print;    return true; }
    if (name == "filament")                      { out = PresetType::Filament; return true; }
    if (name == "machine")                       { out = PresetType::Printer;  return true; }
    if (name == "print")                         { out = PresetType::Print;    return true; }
    if (name == "printer")                       { out = PresetType::Printer;  return true; }
    return false;
}

bool Preset::typeFromIotName(const std::string& name, PresetType& out)
{
    return presetTypeFromString(name, out);
}

}
