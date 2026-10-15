#pragma once

#include <string>
#include <vector>

#include "configschema/iconfigschemamanager.hpp"
#include "configschema/iconfigsnapshot.hpp"

namespace ConfigSchema
{

struct IPreset
{
    virtual ~IPreset() = default;

    virtual PresetType                  presetType() const = 0;
    virtual const std::string&          presetName() const = 0;
    virtual const std::string&          presetFile() const = 0;
    virtual std::string                 inherits() const = 0;
    virtual std::string                 label() const = 0;
    virtual bool                        isDefault() const = 0;
    virtual bool                        isSystem() const = 0;
    virtual bool                        isVisible() const = 0;
    virtual bool                        isDirty() const = 0;
    virtual bool                        isCompatible() const = 0;
    virtual bool                        isProjectEmbedded() const = 0;
    virtual bool                        isLoaded() const = 0;
    virtual bool                        isUser() const = 0;

    virtual const IConfigScopeSnapShot* configSnapshot() const = 0;

    virtual const std::string&          presetAlias() const = 0;
    virtual const std::vector<std::string>& renamedFrom() const = 0;
    virtual const std::string&          presetVersion() const = 0;
    virtual const std::string&          settingId() const = 0;
    virtual const std::string&          filamentId() const = 0;
    virtual const std::string&          userId() const = 0;
    virtual const std::string&          baseId() const = 0;
    virtual const std::string&          syncInfo() const = 0;
    virtual const std::string&          customDefined() const = 0;
    virtual const std::string&          presetDescription() const = 0;
    virtual long long                   updatedTime() const = 0;
};

}
