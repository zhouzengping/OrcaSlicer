#pragma once

#include <list>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "configmetadata/configmetadata.hpp"
#include "configschema/iconfigschemamanager.hpp"
#include "configdisplay.hpp"
#include "projectcore_export.hpp"
#include "nlohmann/json.hpp"

namespace ConfigSchema
{

PROJECTCORE_API std::string         toString(PresetType type);
PROJECTCORE_API bool                presetTypeFromString(const std::string& s, PresetType& out);

PROJECTCORE_API std::string         presetTypeFileName(PresetType type);

PROJECTCORE_API SlicerVariant::ValueType variantTypeOf(ConfigMetaType type);

PROJECTCORE_API std::unique_ptr<IConfigSchemaManager> loadConfigSchemaManager(const std::string& schemaDir, std::string& err);


class ConfigSchemaManager : public IConfigSchemaManager
{
public:
    std::vector<std::unique_ptr<ConfigMetaData>> m_metaDatas;

    std::list<std::string> m_metaDataKeys;
    std::unordered_map<PresetType, std::list<std::string>> m_presetKeys;
    std::unordered_map<ConfigMetaScope, std::list<std::string>> m_scopeKeys;
    std::unordered_map<ConfigMetaScope, std::list<ConfigMetaData*>> m_byScope;
    std::unordered_map<std::string, std::unique_ptr<ConfigMetaData>> m_byKey;

    std::shared_ptr<ConfigSchemaI18N> m_i18n;
    std::unordered_map<PresetType, std::unique_ptr<ConfigSchemaDisplay>> m_display;

public:
    ConfigSchemaManager();
    ~ConfigSchemaManager() override;

    IConfigMetaData* getMetaData(const std::string& key) const override;
    bool contains(const std::string& key) const override;
    std::list<std::string> keys() const override;

    std::list<std::string> presetKeys(PresetType type) const override;
    std::list<std::string> scopeKeys(ConfigMetaScope scope) const override;
    std::list<IConfigMetaData*> scopeMetaDatas(ConfigMetaScope scope) const override;

    IConfigSchemaI18N* i18n() const override;
    IConfigSchemaDisplay* display(PresetType presetType) const override;
};

} // namespace ConfigSchema
