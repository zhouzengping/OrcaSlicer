#pragma once

#include <list>
#include <memory>
#include <string>
#include <unordered_set>

#include "iconfigsnapshot.hpp"
#include "iconfigmetadata.hpp"
#include "iconfigschemadisplay.hpp"
#include "../projectcore/projectcore_export.hpp"

namespace ConfigSchema
{

enum class PresetType
{
    Hidden = 0,
    Print,
    Filament,
    Printer,
};


struct IConfigSchemaManager
{
    virtual ~IConfigSchemaManager() = default;

    virtual IConfigMetaData* getMetaData(const std::string& key) const = 0;

    virtual bool contains(const std::string& key) const = 0;
    virtual std::list<std::string> keys() const = 0;

    virtual std::list<std::string> scopeKeys(ConfigScopeType scope) const = 0;
    virtual std::list<IConfigMetaData*> scopeMetaDatas(ConfigScopeType scope) const = 0;

    virtual std::list<std::string> presetKeys(PresetType type) const = 0;

    virtual IConfigSchemaI18N* i18n() const = 0;
    virtual IConfigSchemaDisplay* display(PresetType presetType) const = 0;
};

}
