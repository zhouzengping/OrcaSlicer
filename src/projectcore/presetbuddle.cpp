#include "presetbuddle.hpp"

#include "preset/presetbuddleimpl.hpp"

namespace ConfigSchema
{

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
    m_impl->project_config.setValue(key, v);
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
