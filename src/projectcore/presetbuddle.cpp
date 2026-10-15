#include "presetbuddle.hpp"

#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <utility>

#include "configsnapshot.hpp"
#include "nlohmann/json.hpp"
#include "preset/presetcollection.hpp"

namespace ConfigSchema
{

using json = nlohmann::json;

static void copyElem(SlicerVariant& dst, size_t dstIdx, const SlicerVariant& src, size_t srcIdx)
{
    if (src.elem_is_nil(srcIdx))
    {
        dst.set_elem_nil(dstIdx);
        return;
    }
    switch ((SlicerVariant::ValueType)((uint16_t)src.type() & ~g_typeVectorFlag))
    {
    case SlicerVariant::ValueType::Bool:            dst.set_elem(dstIdx, src.elem_bool(srcIdx)); break;
    case SlicerVariant::ValueType::Int:             dst.set_elem(dstIdx, src.elem_int(srcIdx)); break;
    case SlicerVariant::ValueType::Float:           dst.set_elem(dstIdx, src.elem_double(srcIdx)); break;
    case SlicerVariant::ValueType::String:          dst.set_elem(dstIdx, src.elem_string(srcIdx)); break;
    case SlicerVariant::ValueType::Percent:         dst.set_elem(dstIdx, src.elem_percent(srcIdx)); break;
    case SlicerVariant::ValueType::FloatOrPercent:  dst.set_elem(dstIdx, src.elem_float_or_percent(srcIdx)); break;
    case SlicerVariant::ValueType::Enum:            dst.set_elem(dstIdx, src.elem_enum(srcIdx)); break;
    case SlicerVariant::ValueType::Point:           dst.set_elem(dstIdx, src.elem_point(srcIdx)); break;
    case SlicerVariant::ValueType::Point3:          dst.set_elem(dstIdx, src.elem_point3(srcIdx)); break;
    default: break;
    }
}

static const Preset* resolveFilamentPreset(const PresetCollection& filaments, const std::string& name)
{
    const Preset* preset = filaments.findPreset(name);
    if (preset == nullptr)
        preset = &filaments.firstVisiblePreset();
    if (preset->name == filaments.getSelectedPresetName())
        preset = &filaments.getEditedPreset();
    return preset;
}

struct PresetBundle::Impl
{
    PresetCollection prints;
    PresetCollection filaments;
    PresetCollection printers;

    std::vector<std::string> filament_presets;
    std::map<int, ConfigScopeSnapShot> filament_ams_list;
    std::vector<std::vector<std::string>> ams_multi_color_filment;
    std::map<int, std::pair<std::string, std::string>> machine_filaments;

    Preset const* calibrate_printer = nullptr;
    std::set<Preset const*> calibrate_filaments;

    ConfigScopeSnapShot project_config;

    PresetBundle::ObsoletePresets obsolete_presets;

    bool validation_mode = false;
    std::string vendor_to_validate;
    int m_errors = 0;
    std::shared_ptr<const IConfigSchemaManager> m_schema;

    Impl(std::shared_ptr<const IConfigSchemaManager> schema)
        : prints(PresetType::Print, schema.get(), "Default Setting"),
          filaments(PresetType::Filament, schema.get(), "Default Filament"),
          printers(PresetType::Printer, schema.get(), "Default Printer"),
          m_schema(std::move(schema))
    {
        filament_presets.push_back(filaments.getSelectedPresetName());
    }

    void loadPresets(const std::string& rootDir, bool isSystem, std::vector<std::string>* errors)
    {
        prints.loadPresets(rootDir, Preset::dirName(PresetType::Print), isSystem, errors);
        filaments.loadPresets(rootDir, Preset::dirName(PresetType::Filament), isSystem, errors);
        printers.loadPresets(rootDir, Preset::dirName(PresetType::Printer), isSystem, errors);
    }

    void loadSelections(const std::string& file, std::vector<std::string>* errors);
    void exportSelections(const std::string& file) const;

    void setNumFilaments(size_t n)
    {
        const size_t oldCount = filament_presets.size();
        if (n > oldCount && oldCount != 0)
            filament_presets.resize(n, filament_presets.back());
        else
            filament_presets.resize(n);

        static const char* slotKeys[] = {
            "filament_colour", "filament_multi_colors", "filament_volume_type",
            "filament_flow_support", "filament_ids", "filament_settings_id",
        };
        for (const char* key : slotKeys)
        {
            SlicerVariant* v = project_config.find(key);
            if (v != nullptr && v->is_vector() && !v->empty())
                v->resize(n);
        }
    }

    bool updateCompatible()
    {
        const Preset& printerEdited = printers.getEditedPreset();
        std::vector<std::string> printerNames = { printerEdited.name };
        if (!printerEdited.is_system)
        {
            const std::string inheritsName = printerEdited.inherits();
            if (!inheritsName.empty())
                printerNames.push_back(inheritsName);
        }
        const std::vector<std::string> printNames = { prints.getEditedPreset().name };
        bool some = prints.updateCompatible(printerNames, nullptr);
        some = filaments.updateCompatible(printerNames, &printNames) || some;
        return some;
    }

    ConfigSnapshot mergeFullConfig() const;
};

void PresetBundle::Impl::loadSelections(const std::string& file, std::vector<std::string>* errors)
{
    std::ifstream f(file, std::ios::binary);
    if (!f)
    {
        if (errors != nullptr)
            errors->push_back("selections file not found: " + file);
        return;
    }
    json j;
    try
    {
        f >> j;
    }
    catch (const std::exception&)
    {
        if (errors != nullptr)
            errors->push_back("selections file parse failed: " + file);
        return;
    }

    const json empty = json::object();
    const json& presets = j.value("presets", empty);
    if (!presets.is_object())
        return;

    auto optString = [&presets](const char* key) -> std::string {
        const auto it = presets.find(key);
        return it != presets.end() && it->is_string() ? it->get<std::string>() : std::string();
    };

    const std::string machine = optString("machine");
    printers.selectPresetByName(machine, true);

    const std::string process = optString("process");
    if (!process.empty())
        prints.selectPresetByName(process);

    const std::string filament = optString("filament");
    if (!filament.empty())
        filaments.selectPresetByName(filament);

    filament_presets.clear();
    filament_presets.push_back(filaments.getSelectedPresetName());
    if (const auto it = presets.find("filaments"); it != presets.end() && it->is_array())
        for (const auto& name : *it)
            if (name.is_string())
                filament_presets.push_back(name.get<std::string>());

    updateCompatible();
}

void PresetBundle::Impl::exportSelections(const std::string& file) const
{
    json presets;
    presets["process"]  = prints.getSelectedPresetName();
    presets["filament"] = filament_presets.empty() ? filaments.getSelectedPresetName() : filament_presets.front();
    presets["machine"]  = printers.getSelectedPresetName();
    if (filament_presets.size() > 1)
    {
        json arr = json::array();
        for (size_t i = 1; i < filament_presets.size(); ++i)
            arr.push_back(filament_presets[i]);
        presets["filaments"] = std::move(arr);
    }

    json j;
    j["presets"] = std::move(presets);
    std::ofstream f(file, std::ios::binary | std::ios::trunc);
    if (!f)
        return;
    f << j.dump(4) << std::endl;
}

ConfigSnapshot PresetBundle::Impl::mergeFullConfig() const
{
    ConfigSnapshot out(m_schema);

    if (m_schema != nullptr)
        for (const std::string& key : m_schema->keys())
            if (const IConfigMetaData* meta = m_schema->getMetaData(key))
                if (!meta->defaultValue().is_undef())
                    out.set(key, meta->defaultValue());

    out.apply(prints.getEditedPreset().config);
    out.apply(filaments.defaultPreset().config);
    out.apply(printers.getEditedPreset().config);
    out.apply(project_config);

    const size_t numFilaments = filament_presets.empty() ? 1 : filament_presets.size();

    std::vector<std::string> compatiblePrintersCondition;
    std::vector<std::string> compatiblePrintsCondition;
    std::vector<std::string> inherits;
    std::vector<std::string> filamentIds;
    compatiblePrintersCondition.push_back(prints.getEditedPreset().config.optString("compatible_printers_condition"));
    inherits.push_back(prints.getEditedPreset().inherits());

    if (numFilaments <= 1)
    {
        const std::string firstName = filament_presets.empty() ? filaments.getSelectedPresetName()
                                                               : filament_presets.front();
        const Preset* filamentPreset = resolveFilamentPreset(filaments, firstName);
        out.apply(filamentPreset->config);
        compatiblePrintersCondition.push_back(filamentPreset->config.optString("compatible_printers_condition"));
        compatiblePrintsCondition.push_back(filamentPreset->config.optString("compatible_prints_condition"));
        inherits.push_back(filamentPreset->inherits());
        filamentIds.push_back(filamentPreset->filament_id);
    }
    else
    {
        std::vector<const ConfigScopeSnapShot*> filamentConfigs;
        std::vector<const Preset*>               filamentPresetPtrs;
        for (const std::string& name : filament_presets)
        {
            const Preset* preset = resolveFilamentPreset(filaments, name);
            filamentPresetPtrs.push_back(preset);
            filamentConfigs.push_back(&preset->config);
        }
        while (filamentConfigs.size() < numFilaments)
        {
            const Preset* preset = &filaments.firstVisiblePreset();
            filamentPresetPtrs.push_back(preset);
            filamentConfigs.push_back(&preset->config);
        }
        for (size_t i = 0; i < numFilaments; ++i)
        {
            compatiblePrintersCondition.push_back(filamentConfigs[i]->optString("compatible_printers_condition"));
            compatiblePrintsCondition.push_back(filamentConfigs[i]->optString("compatible_prints_condition"));
            inherits.push_back(filamentConfigs[i]->optString("inherits"));
            filamentIds.push_back(filamentPresetPtrs[i]->filament_id);
        }

        for (const std::string& key : filaments.defaultPreset().config.keys())
        {
            if (key == "compatible_prints" || key == "compatible_printers")
                continue;
            SlicerVariant* dst = out.find(key);
            if (dst == nullptr)
                continue;
            if (!dst->is_vector())
            {
                if (const SlicerVariant* src = filamentConfigs.front()->find(key))
                    out.set(key, *src);
            }
            else
            {
                dst->resize(numFilaments);
                for (size_t i = 0; i < numFilaments; ++i)
                {
                    const SlicerVariant* src = filamentConfigs[i]->find(key);
                    if (src != nullptr && src->is_vector() && !src->empty())
                        copyElem(*dst, i, *src, 0);
                }
            }
        }
    }

    inherits.push_back(printers.getEditedPreset().inherits());

    out.erase("compatible_prints");
    out.erase("compatible_prints_condition");
    out.erase("compatible_printers");
    out.erase("compatible_printers_condition");
    out.erase("inherits");

    auto clampIntKey = [&out, numFilaments](const char* key, int lo, int hi, int fallback) {
        const SlicerVariant* v = out.find(key);
        if (v == nullptr || v->is_undef() || v->is_vector())
        {
            out.set(key, SlicerVariant((int64_t)fallback));
            return;
        }
        int val = (int)v->to_int();
        if (val < lo || val > hi)
            val = fallback;
        out.set(key, SlicerVariant((int64_t)val));
    };
    for (const char* key : {"support_filament", "support_interface_filament", "wipe_tower_filament"})
        clampIntKey(key, 0, int(numFilaments), 0);
    for (const char* key : {"wall_filament", "sparse_infill_filament", "solid_infill_filament"})
        clampIntKey(key, 1, int(numFilaments), 1);

    out.set("print_settings_id", SlicerVariant(prints.getSelectedPresetName()));
    out.set("printer_settings_id", SlicerVariant(printers.getSelectedPresetName()));
    {
        SlicerVariant v;
        v.deserialize(std::string(), SlicerVariant::ValueType::Strings);
        for (const std::string& name : filament_presets)
            v.append(name);
        if (v.empty())
            v.append(filaments.getSelectedPresetName());
        out.set("filament_settings_id", std::move(v));
    }
    {
        SlicerVariant v;
        v.deserialize(std::string(), SlicerVariant::ValueType::Strings);
        for (const std::string& id : filamentIds)
            v.append(id);
        out.set("filament_ids", std::move(v));
    }

    auto addIfSomeNonEmpty = [&out](std::vector<std::string>&& values, const char* key) {
        for (const std::string& v : values)
            if (!v.empty())
            {
                SlicerVariant v2;
                v2.deserialize(std::string(), SlicerVariant::ValueType::Strings);
                for (const std::string& s : values)
                    v2.append(s);
                out.set(key, std::move(v2));
                return;
            }
    };
    addIfSomeNonEmpty(std::move(compatiblePrintersCondition), "compatible_machine_expression_group");
    addIfSomeNonEmpty(std::move(compatiblePrintsCondition), "compatible_process_expression_group");
    addIfSomeNonEmpty(std::move(inherits), "inherits_group");

    return out;
}

PresetBundle::PresetBundle(std::shared_ptr<const IConfigSchemaManager> schema)
    : m_impl(new Impl(std::move(schema)))
{
}

PresetBundle::~PresetBundle() = default;

IPresetCollection* PresetBundle::prints() { return &m_impl->prints; }
const IPresetCollection* PresetBundle::prints() const { return &m_impl->prints; }
IPresetCollection* PresetBundle::filaments() { return &m_impl->filaments; }
const IPresetCollection* PresetBundle::filaments() const { return &m_impl->filaments; }
IPresetCollection* PresetBundle::printers() { return &m_impl->printers; }
const IPresetCollection* PresetBundle::printers() const { return &m_impl->printers; }

const std::vector<std::string>& PresetBundle::filament_presets() const { return m_impl->filament_presets; }
void PresetBundle::set_filament_presets(const std::vector<std::string>& presets) { m_impl->filament_presets = presets; }

const IConfigScopeSnapShot* PresetBundle::project_config() const { return &m_impl->project_config; }
void PresetBundle::set_project_config_value(const std::string& key, const SlicerVariant& v)
{
    m_impl->project_config.set(key, v);
}

const IConfigSchemaManager* PresetBundle::schema() const { return m_impl->m_schema.get(); }

bool PresetBundle::has_defaults_only() const
{
    return m_impl->prints.has_defaults_only() && m_impl->filaments.has_defaults_only() &&
           m_impl->printers.has_defaults_only();
}

void PresetBundle::load_presets(const std::string& rootDir, bool isSystem, std::vector<std::string>* errors)
{
    m_impl->loadPresets(rootDir, isSystem, errors);
}

void PresetBundle::load_selections(const std::string& file, std::vector<std::string>* errors)
{
    m_impl->loadSelections(file, errors);
}

void PresetBundle::export_selections(const std::string& file) const
{
    m_impl->exportSelections(file);
}

bool PresetBundle::save_current_preset(PresetType type, const std::string& newName, bool detach)
{
    const IPreset* currentPrinter = &m_impl->printers.getEditedPreset();
    switch (type)
    {
    case PresetType::Print:    return m_impl->prints.saveCurrentPreset(newName, detach, currentPrinter);
    case PresetType::Filament: return m_impl->filaments.saveCurrentPreset(newName, detach, currentPrinter);
    case PresetType::Printer:  return m_impl->printers.saveCurrentPreset(newName, detach, currentPrinter);
    default:                   return false;
    }
}

void PresetBundle::set_num_filaments(size_t n)
{
    m_impl->setNumFilaments(n);
}

void PresetBundle::set_filament_preset(size_t idx, const std::string& name)
{
    if (idx >= m_impl->filament_presets.size())
        return;
    m_impl->filament_presets[idx] = Preset::removeSuffixModified(name);
}

bool PresetBundle::update_compatible()
{
    return m_impl->updateCompatible();
}

ConfigSnapshot PresetBundle::full_config() const
{
    return m_impl->mergeFullConfig();
}

}
