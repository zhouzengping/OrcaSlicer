#pragma once

#include <deque>
#include <string>
#include <vector>

#include "ipresetcollection.hpp"
#include "preset.hpp"

namespace ConfigSchema
{

class PresetCollection : public IPresetCollection
{
public:
    typedef std::deque<Preset>::iterator       Iterator;
    typedef std::deque<Preset>::const_iterator ConstIterator;

    PresetCollection(PresetType type, const IConfigSchemaManager* schema,
                     const std::string& defaultName = "Default Setting");

    PresetType      type() const override { return m_type; }
    std::string     sectionName() const override;
    const std::deque<Preset>& all() const { return m_presets; }

    Iterator        begin() { return m_presets.begin() + m_numDefaultPresets; }
    ConstIterator   begin() const { return m_presets.cbegin() + m_numDefaultPresets; }
    ConstIterator   cbegin() const { return begin(); }
    Iterator        end() { return m_presets.end(); }
    ConstIterator   end() const { return m_presets.cend(); }
    ConstIterator   cend() const { return end(); }
    size_t          size() const override { return m_presets.size(); }
    bool            empty() const override { return m_presets.empty(); }

    const Preset&   defaultPreset() const override { return m_presets.front(); }
    bool            has_defaults_only() const { return m_presets.size() <= m_numDefaultPresets; }

    void            loadPresets(const std::string& dir, const std::string& subdir,
                                bool isSystem = false,
                                std::vector<std::string>* errors = nullptr);

    Preset*         findPreset(const std::string& name) override;
    const Preset*   findPreset(const std::string& name) const override;
    Preset*         findPreset2(const std::string& name) override;
    const Preset*   findPreset2(const std::string& name) const override;

    const Preset*   getPresetParent(const IPreset& child) const override;
    const Preset*   getPresetBase(const IPreset& child) const override;

    Preset&         selectPreset(size_t idx) override;
    bool            selectPresetByName(const std::string& name, bool force = false) override;
    const Preset&   getSelectedPreset() const override;
    Preset&         getSelectedPreset();
    Preset&         getEditedPreset() override { return m_editedPreset; }
    const Preset&   getEditedPreset() const override { return m_editedPreset; }
    std::string     getSelectedPresetName() const override;
    size_t          getSelectedIdx() const override { return m_idxSelected; }
    size_t          firstVisibleIdx() const override;
    const Preset&   firstVisiblePreset() const { return m_presets[firstVisibleIdx()]; }

    bool            discardCurrentChanges() override;

    std::vector<std::string> dirtyOptions() const override;
    bool            currentIsDirty() const override { return !dirtyOptions().empty(); }
    bool            updateDirty() override;

    bool            saveCurrentPreset(const std::string& newName, bool detach = false) override;
    bool            saveCurrentPreset(const std::string& newName, bool detach, const IPreset* currentPrinter);

    bool            deleteCurrentPreset() override;
    bool            deletePreset(const std::string& name) override;

    std::string     pathFromName(const std::string& name, bool toBaseDir = false) const;
    std::string     pathForPreset(const Preset& preset) const;

    bool            updateCompatible(const std::vector<std::string>& activePrinterNames,
                                      const std::vector<std::string>* activePrintNames = nullptr) override;

    void            setEditedValue(const std::string& key, const SlicerVariant& v) override;

private:
    std::deque<Preset>::iterator       findPresetInternal(const std::string& name);
    std::deque<Preset>::const_iterator findPresetInternal(const std::string& name) const;
    std::deque<Preset>::iterator       findPresetRenamed(const std::string& name);
    void                                insertPresetSorted(Preset&& preset);

    PresetType                    m_type = PresetType::Hidden;
    const IConfigSchemaManager*   m_schema = nullptr;
    std::deque<Preset>            m_presets;
    size_t                        m_numDefaultPresets = 0;
    Preset                        m_editedPreset;
    size_t                        m_idxSelected = size_t(-1);
    std::string                   m_dirPath;
};

}
