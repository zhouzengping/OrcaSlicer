#pragma once

#include <string>
#include <vector>

#include "ipreset.hpp"

namespace ConfigSchema
{

struct IPresetCollection
{
    virtual ~IPresetCollection() = default;

    virtual PresetType                  type() const = 0;
    virtual std::string                 sectionName() const = 0;
    virtual size_t                      size() const = 0;
    virtual bool                        empty() const = 0;

    virtual const IPreset&              defaultPreset() const = 0;

    virtual IPreset*                    findPreset(const std::string& name) = 0;
    virtual const IPreset*              findPreset(const std::string& name) const = 0;
    virtual IPreset*                    findPreset2(const std::string& name) = 0;
    virtual const IPreset*              findPreset2(const std::string& name) const = 0;

    virtual const IPreset*              getPresetParent(const IPreset& child) const = 0;
    virtual const IPreset*              getPresetBase(const IPreset& child) const = 0;

    virtual IPreset&                    selectPreset(size_t idx) = 0;
    virtual bool                        selectPresetByName(const std::string& name, bool force = false) = 0;
    virtual const IPreset&              getSelectedPreset() const = 0;
    virtual IPreset&                    getEditedPreset() = 0;
    virtual const IPreset&              getEditedPreset() const = 0;
    virtual std::string                 getSelectedPresetName() const = 0;
    virtual size_t                      getSelectedIdx() const = 0;
    virtual size_t                      firstVisibleIdx() const = 0;

    virtual bool                        discardCurrentChanges() = 0;

    virtual std::vector<std::string>    dirtyOptions() const = 0;
    virtual bool                        currentIsDirty() const = 0;
    virtual bool                        updateDirty() = 0;

    virtual bool                        saveCurrentPreset(const std::string& newName, bool detach = false) = 0;

    virtual bool                        deleteCurrentPreset() = 0;
    virtual bool                        deletePreset(const std::string& name) = 0;

    virtual bool                        updateCompatible(const std::vector<std::string>& activePrinterNames,
                                                         const std::vector<std::string>* activePrintNames = nullptr) = 0;

    virtual void                        setEditedValue(const std::string& key, const SlicerVariant& v) = 0;
};

}
