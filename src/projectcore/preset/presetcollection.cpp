#include "presetcollection.hpp"

#include <algorithm>
#include <filesystem>
#include <iterator>
#include <utility>

namespace fs = std::filesystem;

namespace ConfigSchema
{

PresetCollection::PresetCollection(PresetType type, const IConfigSchemaManager* schema,
                                   const std::string& defaultName)
    : m_type(type), m_schema(schema)
{
    Preset& dp = m_presets.emplace_back(type, defaultName, true);
    if (m_schema != nullptr)
        for (const std::string& key : m_schema->presetKeys(type))
            if (const IConfigMetaData* meta = m_schema->getMetaData(key))
                if (!meta->defaultValue().is_undef())
                    dp.config.setValue(key, meta->defaultValue());
    dp.loaded = true;
    ++m_numDefaultPresets;
    m_editedPreset = dp;
}

std::string PresetCollection::sectionName() const
{
    return Preset::dirName(m_type);
}

std::deque<Preset>::iterator PresetCollection::findPresetInternal(const std::string& name)
{
    Preset key(m_type, name, false);
    auto it = std::lower_bound(m_presets.begin() + m_numDefaultPresets, m_presets.end(), key,
                               [](const Preset& l, const Preset& k) { return l.name < k.name; });
    if (it == m_presets.end() || it->name != name)
        for (size_t i = 0; i < m_numDefaultPresets; ++i)
            if (m_presets[i].name == name)
            {
                it = m_presets.begin() + i;
                break;
            }
    return it;
}

std::deque<Preset>::const_iterator PresetCollection::findPresetInternal(const std::string& name) const
{
    return const_cast<PresetCollection*>(this)->findPresetInternal(name);
}

std::deque<Preset>::iterator PresetCollection::findPresetRenamed(const std::string& name)
{
    for (auto it = m_presets.begin() + m_numDefaultPresets; it != m_presets.end(); ++it)
        if (std::find(it->renamed_from.begin(), it->renamed_from.end(), name) != it->renamed_from.end())
            return it;
    return m_presets.end();
}

Preset* PresetCollection::findPreset(const std::string& name)
{
    auto it = findPresetInternal(name);
    return (it != m_presets.end() && it->name == name) ? &*it : nullptr;
}

const Preset* PresetCollection::findPreset(const std::string& name) const
{
    return const_cast<PresetCollection*>(this)->findPreset(name);
}

Preset* PresetCollection::findPreset2(const std::string& name)
{
    if (Preset* p = findPreset(name))
        return p;
    auto it = findPresetRenamed(name);
    return it != m_presets.end() ? &*it : nullptr;
}

const Preset* PresetCollection::findPreset2(const std::string& name) const
{
    return const_cast<PresetCollection*>(this)->findPreset2(name);
}

const Preset* PresetCollection::getPresetParent(const IPreset& child) const
{
    const Preset& preset = static_cast<const Preset&>(child);
    const std::string inherits = preset.inherits();
    if (inherits.empty())
        return nullptr;
    const Preset* parent = findPreset(inherits);
    if (parent == nullptr)
    {
        auto it = const_cast<PresetCollection*>(this)->findPresetRenamed(inherits);
        if (it != m_presets.end())
            parent = &*it;
    }
    return (parent == nullptr || parent == &preset) ? nullptr : parent;
}

const Preset* PresetCollection::getPresetBase(const IPreset& child) const
{
    const Preset& preset = static_cast<const Preset&>(child);
    if (preset.is_system || preset.is_default)
        return &preset;
    if (preset.inherits().empty())
        return &preset;
    const Preset* parent = findPreset2(preset.inherits());
    return parent != nullptr ? getPresetBase(*parent) : nullptr;
}

void PresetCollection::insertPresetSorted(Preset&& preset)
{
    auto it = std::lower_bound(m_presets.begin() + m_numDefaultPresets, m_presets.end(), preset,
                               [](const Preset& l, const Preset& k) { return l.name < k.name; });
    m_presets.insert(it, std::move(preset));
}

void PresetCollection::loadPresets(const std::string& dir, const std::string& subdir, bool isSystem,
                                   std::vector<std::string>* errors)
{
    const fs::path full = fs::absolute(fs::path(dir) / subdir);

    if (fs::exists(full / "base"))
        loadPresets(full.string(), "base", isSystem, errors);

    m_dirPath = full.string();
    std::error_code ec;
    if (!fs::exists(full))
    {
        fs::create_directory(full, ec);
        return;
    }

    std::vector<Preset> pending;
    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(full))
        if (entry.is_regular_file() && entry.path().extension() == ".json")
            files.push_back(entry.path());
    std::sort(files.begin(), files.end());
    for (const fs::path& path : files)
    {
        Preset preset(m_type, path.stem().string(), false);
        preset.file = path.string();
        fs::path infoPath = path;
        infoPath.replace_extension(".info");
        if (fs::exists(infoPath))
            preset.load_info(infoPath.string());

        std::vector<std::string> unknownKeys;
        bool skipFile = false;
        if (!preset.load(preset.file, m_schema, &unknownKeys, &skipFile))
        {
            if (!skipFile)
            {
                preset.remove_files();
                if (errors != nullptr)
                    errors->push_back("parse failed: " + preset.file);
            }
            continue;
        }
        if (!unknownKeys.empty() && errors != nullptr)
            for (const std::string& key : unknownKeys)
                errors->push_back("unknown key \"" + key + "\" in " + preset.file);
        if (findPreset(preset.name) != nullptr)
        {
            if (errors != nullptr)
                errors->push_back("preset already present: " + preset.name);
            continue;
        }
        if (preset.version.empty())
            continue;
        pending.push_back(std::move(preset));
    }

    bool progress = true;
    while (!pending.empty() && progress)
    {
        progress = false;
        for (auto it = pending.begin(); it != pending.end();)
        {
            const std::string inheritsValue = it->inherits();
            const Preset*    parent         = inheritsValue.empty() ? nullptr : findPreset2(inheritsValue);
            if (!inheritsValue.empty() && parent == nullptr && it->custom_defined.empty())
            {
                ++it;
                continue;
            }

            ConfigScopeSnapShot base = parent != nullptr ? parent->config : defaultPreset().config;
            if (parent != nullptr && !it->filament_id_from_json)
                it->filament_id = parent->filament_id;
            ConfigScopeSnapShot fileDiff = std::move(it->config);
            it->config = std::move(base);
            it->config.apply(fileDiff);

            it->is_system = isSystem;
            it->loaded    = true;
            if (!it->setting_id.empty() && it->setting_id == it->base_id)
                it->setting_id.clear();

            insertPresetSorted(std::move(*it));
            it       = pending.erase(it);
            progress = true;
        }
    }
    if (!pending.empty() && errors != nullptr)
        for (const Preset& preset : pending)
            errors->push_back("can not find parent for config " + preset.file);
}

size_t PresetCollection::firstVisibleIdx() const
{
    size_t firstVisible = size_t(-1);
    for (size_t idx = m_numDefaultPresets; idx < m_presets.size(); ++idx)
    {
        if (!m_presets[idx].is_visible)
            continue;
        if (firstVisible == size_t(-1))
            firstVisible = idx;
        if (m_type != PresetType::Filament)
            break;
        if (m_presets[idx].name.find("PLA") != std::string::npos)
        {
            firstVisible = idx;
            break;
        }
    }
    if (firstVisible == size_t(-1))
        firstVisible = 0;
    return firstVisible;
}

Preset& PresetCollection::selectPreset(size_t idx)
{
    for (Preset& preset : m_presets)
        preset.is_dirty = false;
    if (idx >= m_presets.size())
        idx = firstVisibleIdx();
    m_idxSelected = idx;
    m_editedPreset = m_presets[idx];
    return m_presets[idx];
}

bool PresetCollection::selectPresetByName(const std::string& nameWithSuffix, bool force)
{
    const std::string name = Preset::removeSuffixModified(nameWithSuffix);
    auto it = findPresetInternal(name);
    size_t idx = 0;
    if (it != m_presets.end() && it->name == name && it->is_visible)
        idx = size_t(it - m_presets.begin());
    else
    {
        bool found = false;
        if (m_type == PresetType::Printer && it != m_presets.end() && it->name == name && !it->is_visible)
        {
            const std::string printerModel = it->config.value("printer_model").to_string();
            if (!printerModel.empty())
                for (size_t i = m_numDefaultPresets; i < m_presets.size(); ++i)
                    if (m_presets[i].is_visible && m_presets[i].config.value("printer_model").to_string() == printerModel)
                    {
                        idx = i;
                        found = true;
                        break;
                    }
        }
        if (!found)
            idx = firstVisibleIdx();
    }
    if (m_idxSelected != idx || force)
    {
        selectPreset(idx);
        return true;
    }
    return false;
}

Preset& PresetCollection::getSelectedPreset()
{
    if (m_idxSelected >= m_presets.size())
        return m_editedPreset;
    return m_presets[m_idxSelected];
}

const Preset& PresetCollection::getSelectedPreset() const
{
    if (m_idxSelected >= m_presets.size())
        return m_editedPreset;
    return m_presets[m_idxSelected];
}

std::string PresetCollection::getSelectedPresetName() const
{
    if (m_idxSelected == size_t(-1) || m_idxSelected >= m_presets.size())
        return std::string();
    return m_presets[m_idxSelected].name;
}

bool PresetCollection::discardCurrentChanges()
{
    if (m_idxSelected >= m_presets.size())
        return false;
    m_presets[m_idxSelected].reset_dirty();
    m_editedPreset = m_presets[m_idxSelected];
    m_editedPreset.is_dirty = false;
    return true;
}

std::vector<std::string> PresetCollection::dirtyOptions() const
{
    if (m_idxSelected >= m_presets.size())
        return {};
    return m_editedPreset.config.diff(m_presets[m_idxSelected].config);
}

bool PresetCollection::updateDirty()
{
    const bool wasDirty = getSelectedPreset().is_dirty;
    const bool isDirty = currentIsDirty();
    getSelectedPreset().is_dirty = isDirty;
    m_editedPreset.is_dirty = isDirty;
    return wasDirty != isDirty;
}

static void updateSettingsId(Preset& preset, const std::string& newName)
{
    switch (preset.type)
    {
    case PresetType::Print:
        preset.config.setValue("print_settings_id", SlicerVariant(newName));
        break;
    case PresetType::Filament:
    {
        SlicerVariant v = preset.config.value("filament_settings_id");
        if (v.is_undef() || !v.is_vector())
            v.deserialize(std::string(), ValueType::Strings);
        if (v.empty())
            v.append(newName);
        else
            v.set_elem(0, newName);
        preset.config.setValue("filament_settings_id", std::move(v));
        break;
    }
    case PresetType::Printer:
        preset.config.setValue("printer_settings_id", SlicerVariant(newName));
        break;
    default:
        break;
    }
}

bool PresetCollection::saveCurrentPreset(const std::string& newName, bool detach)
{
    return saveCurrentPreset(newName, detach, nullptr);
}

bool PresetCollection::saveCurrentPreset(const std::string& newName, bool detach, const IPreset* currentPrinter)
{
    Preset currPreset = m_editedPreset;
    std::string finalInherits;

    auto it = findPresetInternal(newName);
    if (it != m_presets.end() && it->name == newName)
    {
        Preset& preset = *it;
        if (preset.is_default || preset.is_system)
            return false;
        preset.config = std::move(currPreset.config);
        preset.is_visible = true;
        if (detach)
        {
            preset.setInherits(std::string());
            preset.alias.clear();
            preset.renamed_from.clear();
        }
        updateSettingsId(preset, newName);
        finalInherits = preset.inherits();
        if (finalInherits.empty())
            for (Preset& child : m_presets)
                if (child.inherits() == preset.name && !child.is_default)
                    child.reload(preset, m_schema);
    }
    else
    {
        it = m_presets.insert(it, currPreset);
        Preset& preset = *it;
        const std::string oldName = preset.name;
        const std::string oldInherits = preset.inherits();
        preset.name = newName;
        preset.alias.clear();
        preset.renamed_from.clear();
        preset.setting_id.clear();
        preset.is_default = false;
        preset.is_system = false;
        preset.is_project_embedded = false;
        preset.is_visible = true;
        preset.is_dirty = false;
        preset.loaded = true;
        if (m_idxSelected != size_t(-1) && m_idxSelected >= size_t(it - m_presets.begin()))
            ++m_idxSelected;
        if (detach)
            preset.setInherits(std::string());
        else if (currPreset.is_system || (currPreset.isUser() && oldInherits.empty()))
            preset.setInherits(oldName);
        if (currentPrinter != nullptr && currPreset.is_system && m_type == PresetType::Filament)
        {
            SlicerVariant compatiblePrinters = preset.config.value("compatible_printers");
            if (compatiblePrinters.is_vector() && compatiblePrinters.empty())
            {
                compatiblePrinters.set_elem(0, currentPrinter->presetName());
                preset.config.setValue("compatible_printers", std::move(compatiblePrinters));
            }
        }
        updateSettingsId(preset, newName);
        preset.file = pathForPreset(preset);
        finalInherits = preset.inherits();
    }

    selectPresetByName(newName, true);

    Preset* parentPreset = nullptr;
    if (!finalInherits.empty())
    {
        parentPreset = findPreset(finalInherits);
        if (parentPreset != nullptr && getSelectedPreset().base_id.empty())
            getSelectedPreset().base_id = parentPreset->setting_id;
    }
    getSelectedPreset().save(parentPreset, m_schema);
    return true;
}

bool PresetCollection::deleteCurrentPreset()
{
    if (m_idxSelected >= m_presets.size())
        return false;
    Preset& selected = m_presets[m_idxSelected];
    if (selected.is_default)
        return false;
    if (getPresetBase(selected) == &selected)
        for (const Preset& child : m_presets)
            if (!child.is_default && child.inherits() == selected.name)
                return false;

    if (!selected.is_system)
        selected.remove_files();
    m_presets.erase(m_presets.begin() + m_idxSelected);

    size_t newIdx = m_idxSelected;
    if (newIdx < m_presets.size())
        for (; newIdx < m_presets.size() && !m_presets[newIdx].is_visible; ++newIdx) {}
    if (newIdx == m_presets.size())
        for (--newIdx; newIdx > 0 && !m_presets[newIdx].is_visible; --newIdx) {}
    selectPreset(newIdx);
    return true;
}

bool PresetCollection::deletePreset(const std::string& name)
{
    auto it = findPresetInternal(name);
    if (it == m_presets.end() || it->name != name)
        return false;
    if (it->is_default)
        return false;
    if (!it->is_system)
        it->remove_files();
    if (size_t(it - m_presets.begin()) == m_idxSelected)
    {
        m_idxSelected = size_t(it - m_presets.begin());
        return deleteCurrentPreset();
    }
    if (m_idxSelected != size_t(-1) && m_idxSelected > size_t(it - m_presets.begin()))
        --m_idxSelected;
    m_presets.erase(it);
    return true;
}

std::string PresetCollection::pathFromName(const std::string& name, bool toBaseDir) const
{
    std::string fileName = name.size() > 5 && name.compare(name.size() - 5, 5, ".json") == 0
                               ? name : name + ".json";
    if (toBaseDir)
        return (fs::path(m_dirPath) / "base" / fileName).make_preferred().string();
    return (fs::path(m_dirPath) / fileName).make_preferred().string();
}

std::string PresetCollection::pathForPreset(const Preset& preset) const
{
    return pathFromName(preset.name, preset.inherits().empty());
}

static bool nameListCompatible(const ConfigScopeSnapShot& config, const char* key,
                               const std::vector<std::string>& activeNames)
{
    const SlicerVariant v = config.value(key);
    if (v.is_undef() || !v.is_vector() || v.empty())
        return true;
    for (size_t i = 0; i < v.size(); ++i)
    {
        if (v.elem_is_nil(i))
            continue;
        if (std::find(activeNames.begin(), activeNames.end(), v.elem_string(i)) != activeNames.end())
            return true;
    }
    return false;
}

bool PresetCollection::updateCompatible(const std::vector<std::string>& activePrinterNames,
                                        const std::vector<std::string>* activePrintNames)
{
    bool someCompatible = false;
    for (size_t idx = m_numDefaultPresets; idx < m_presets.size(); ++idx)
    {
        Preset& preset = m_presets[idx];
        bool compatible = nameListCompatible(preset.config, "compatible_printers", activePrinterNames);
        if (activePrintNames != nullptr)
            compatible = compatible && nameListCompatible(preset.config, "compatible_prints", *activePrintNames);
        preset.is_compatible = compatible;
        if (idx == m_idxSelected)
            m_editedPreset.is_compatible = compatible;
        someCompatible = someCompatible || compatible;
    }
    return someCompatible;
}

void PresetCollection::setEditedValue(const std::string& key, const SlicerVariant& v)
{
    m_editedPreset.config.setValue(key, v);
}

}
