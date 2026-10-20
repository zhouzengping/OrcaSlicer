#include "presetbuddleimpl.hpp"

#include <fstream>
#include <utility>

#include "nlohmann/json.hpp"

namespace ConfigSchema
{

using json = nlohmann::json;

namespace {

void copyElem(SlicerVariant& dst, size_t dstIdx, const SlicerVariant& src, size_t srcIdx)
{
    if (src.elem_is_nil(srcIdx))
    {
        dst.set_elem_nil(dstIdx);
        return;
    }
    switch ((ValueType)((uint16_t)src.type() & ~g_typeVectorFlag))
    {
    case ValueType::Bool:            dst.set_elem(dstIdx, src.elem_bool(srcIdx)); break;
    case ValueType::Int:             dst.set_elem(dstIdx, src.elem_int(srcIdx)); break;
    case ValueType::Float:           dst.set_elem(dstIdx, src.elem_double(srcIdx)); break;
    case ValueType::String:          dst.set_elem(dstIdx, src.elem_string(srcIdx)); break;
    case ValueType::Percent:         dst.set_elem(dstIdx, src.elem_percent(srcIdx)); break;
    case ValueType::FloatOrPercent:  dst.set_elem(dstIdx, src.elem_float_or_percent(srcIdx)); break;
    case ValueType::Enum:            dst.set_elem(dstIdx, src.elem_enum(srcIdx)); break;
    case ValueType::Point:           dst.set_elem(dstIdx, src.elem_point(srcIdx)); break;
    case ValueType::Point3:          dst.set_elem(dstIdx, src.elem_point3(srcIdx)); break;
    default: break;
    }
}

const Preset* resolveFilamentPreset(const PresetCollection& filaments, const std::string& name)
{
    const Preset* preset = filaments.findPreset(name);
    if (preset == nullptr)
        preset = &filaments.firstVisiblePreset();
    if (preset->name == filaments.getSelectedPresetName())
        preset = &filaments.getEditedPreset();
    return preset;
}

}

PresetBundle::Impl::Impl(std::shared_ptr<const IConfigSchemaManager> schema)
    : prints(PresetType::Print, schema.get(), "Default Setting"),
      filaments(PresetType::Filament, schema.get(), "Default Filament"),
      printers(PresetType::Printer, schema.get(), "Default Printer"),
      m_schema(std::move(schema))
{
    filament_presets.push_back(filaments.getSelectedPresetName());
}

void PresetBundle::Impl::loadPresets(const std::string& rootDir, bool isSystem, std::vector<std::string>* errors)
{
    prints.loadPresets(rootDir, Preset::dirName(PresetType::Print), isSystem, errors);
    filaments.loadPresets(rootDir, Preset::dirName(PresetType::Filament), isSystem, errors);
    printers.loadPresets(rootDir, Preset::dirName(PresetType::Printer), isSystem, errors);
}

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

void PresetBundle::Impl::setNumFilaments(size_t n)
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
        if (!project_config.contains(key))
            continue;
        SlicerVariant v = project_config.value(key);
        if (v.is_vector() && !v.empty())
        {
            v.resize(n);
            project_config.setValue(key, std::move(v));
        }
    }
}

bool PresetBundle::Impl::updateCompatible()
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

ConfigSnapshot PresetBundle::Impl::mergeFullConfig() const
{
    ConfigSnapshot out(m_schema);

    if (m_schema != nullptr)
        for (const std::string& key : m_schema->keys())
            if (const IConfigMetaData* meta = m_schema->getMetaData(key))
                if (!meta->defaultValue().is_undef())
                    out.setValue(key, meta->defaultValue());

    out.apply(prints.getEditedPreset().config);
    out.apply(filaments.defaultPreset().config);
    out.apply(printers.getEditedPreset().config);
    out.apply(project_config);

    const size_t numFilaments = filament_presets.empty() ? 1 : filament_presets.size();

    std::vector<std::string> compatiblePrintersCondition;
    std::vector<std::string> compatiblePrintsCondition;
    std::vector<std::string> inherits;
    std::vector<std::string> filamentIds;
    compatiblePrintersCondition.push_back(prints.getEditedPreset().config.value("compatible_printers_condition").to_string());
    inherits.push_back(prints.getEditedPreset().inherits());

    if (numFilaments <= 1)
    {
        const std::string firstName = filament_presets.empty() ? filaments.getSelectedPresetName()
                                                               : filament_presets.front();
        const Preset* filamentPreset = resolveFilamentPreset(filaments, firstName);
        out.apply(filamentPreset->config);
        compatiblePrintersCondition.push_back(filamentPreset->config.value("compatible_printers_condition").to_string());
        compatiblePrintsCondition.push_back(filamentPreset->config.value("compatible_prints_condition").to_string());
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
            compatiblePrintersCondition.push_back(filamentConfigs[i]->value("compatible_printers_condition").to_string());
            compatiblePrintsCondition.push_back(filamentConfigs[i]->value("compatible_prints_condition").to_string());
            inherits.push_back(filamentConfigs[i]->value("inherits").to_string());
            filamentIds.push_back(filamentPresetPtrs[i]->filament_id);
        }

        for (const std::string& key : filaments.defaultPreset().config.keys())
        {
            if (key == "compatible_prints" || key == "compatible_printers")
                continue;
            SlicerVariant dst = out.value(key);
            if (dst.is_undef())
                continue;
            if (!dst.is_vector())
            {
                if (filamentConfigs.front()->contains(key))
                    out.setValue(key, filamentConfigs.front()->value(key));
            }
            else
            {
                dst.resize(numFilaments);
                for (size_t i = 0; i < numFilaments; ++i)
                {
                    if (!filamentConfigs[i]->contains(key))
                        continue;
                    const SlicerVariant src = filamentConfigs[i]->value(key);
                    if (src.is_vector() && !src.empty())
                        copyElem(dst, i, src, 0);
                }
                out.setValue(key, std::move(dst));
            }
        }
    }

    inherits.push_back(printers.getEditedPreset().inherits());

    out.remove("compatible_prints");
    out.remove("compatible_prints_condition");
    out.remove("compatible_printers");
    out.remove("compatible_printers_condition");
    out.remove("inherits");

    auto clampIntKey0 = [&out, numFilaments](const char* key) {
        const SlicerVariant v = out.value(key);
        if (v.is_undef() || v.is_vector())
        {
            out.setValue(key, SlicerVariant((int64_t)0));
            return;
        }
        int val = (int)v.to_int();
        if (val < 0)                    val = 0;
        if (val > (int)numFilaments)    val = (int)numFilaments;
        out.setValue(key, SlicerVariant((int64_t)val));
    };
    auto resetIntKey1 = [&out, numFilaments](const char* key) {
        const SlicerVariant v = out.value(key);
        if (v.is_undef() || v.is_vector())
        {
            out.setValue(key, SlicerVariant((int64_t)1));
            return;
        }
        int val = (int)v.to_int();
        if (val < 1 || val > (int)numFilaments)
            val = 1;
        out.setValue(key, SlicerVariant((int64_t)val));
    };
    for (const char* key : {"support_filament", "support_interface_filament", "wipe_tower_filament"})
        clampIntKey0(key);
    for (const char* key : {"wall_filament", "sparse_infill_filament", "solid_infill_filament"})
        resetIntKey1(key);

    out.setValue("print_settings_id", SlicerVariant(prints.getSelectedPresetName()));
    out.setValue("printer_settings_id", SlicerVariant(printers.getSelectedPresetName()));
    {
        SlicerVariant v;
        v.deserialize(std::string(), ValueType::Strings);
        for (const std::string& name : filament_presets)
            v.append(name);
        if (v.empty())
            v.append(filaments.getSelectedPresetName());
        out.setValue("filament_settings_id", std::move(v));
    }
    {
        SlicerVariant v;
        v.deserialize(std::string(), ValueType::Strings);
        for (const std::string& id : filamentIds)
            v.append(id);
        out.setValue("filament_ids", std::move(v));
    }

    auto addIfSomeNonEmpty = [&out](std::vector<std::string>&& values, const char* key) {
        for (const std::string& v : values)
            if (!v.empty())
            {
                SlicerVariant v2;
                v2.deserialize(std::string(), ValueType::Strings);
                for (const std::string& s : values)
                    v2.append(s);
                out.setValue(key, std::move(v2));
                return;
            }
    };
    addIfSomeNonEmpty(std::move(compatiblePrintersCondition), "compatible_machine_expression_group");
    addIfSomeNonEmpty(std::move(compatiblePrintsCondition), "compatible_process_expression_group");
    addIfSomeNonEmpty(std::move(inherits), "inherits_group");

    return out;
}

}
