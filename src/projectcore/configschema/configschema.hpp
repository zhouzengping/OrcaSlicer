#pragma once

#include <list>
#include <memory>
#include <string>

#include "iconfigsnapshot.hpp"
#include "configmetadata.hpp"
#include "configschemadisplay.hpp"

namespace ConfigSchema
{
// 先用这个，后续替换为统一的枚举
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

    virtual IConfigSchemaI18N* i18n() const = 0;
    virtual IConfigSchemaDisplay* display(PresetType presetType) const = 0;
};

static std::unordered_map<std::string, PresetType> s_presetTypeMap = {
    {"hidden",      PresetType::Hidden},
    {"print",       PresetType::Print},
    {"filament",    PresetType::Filament},
    {"printer",     PresetType::Printer},
};

std::unique_ptr<IConfigSchemaManager> loadConfigSchemaManager(const std::string& schemaDir);

} // namespace ConfigSchema
