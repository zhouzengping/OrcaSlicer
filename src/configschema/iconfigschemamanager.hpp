#pragma once

#include <list>
#include <string>

#include "iconfigsnapshot.hpp"
#include "iconfigmetadata.hpp"
#include "iconfigschemadisplay.hpp"

namespace ConfigSchema
{

struct IConfigSchemaManager
{
    virtual ~IConfigSchemaManager() = default;

    virtual IConfigMetaData* getMetaData(const std::string& key) const = 0;

    virtual bool contains(const std::string& key) const = 0;
    virtual std::list<std::string> keys() const = 0;

    virtual std::list<std::string> scopeKeys(ConfigMetaScope scope) const = 0;
    virtual std::list<IConfigMetaData*> scopeMetaDatas(ConfigMetaScope scope) const = 0;

    virtual std::list<std::string> presetKeys(PresetType type) const = 0;

    virtual IConfigSchemaI18N* i18n() const = 0;
    virtual IConfigSchemaDisplay* display(PresetType presetType) const = 0;
};

}
