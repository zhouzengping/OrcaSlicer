#pragma once

#include <memory>
#include <string>

#include "configschema/iconfigschemamanager.hpp"
#include "presetbuddle.hpp"
#include "projectcore_export.hpp"

namespace ConfigSchema
{

class PROJECTCORE_API ProjectCore
{
public:
    static std::unique_ptr<ProjectCore> create(const std::string& schemaDir, std::string& err);

    const IConfigSchemaManager* schema() const { return m_schema.get(); }
    PresetBundle*               bundle() const { return m_bundle.get(); }

    ConfigSnapshot              full_config() const { return m_bundle->full_config(); }

private:
    explicit ProjectCore(std::shared_ptr<const IConfigSchemaManager> schema);

    std::shared_ptr<const IConfigSchemaManager> m_schema;
    std::unique_ptr<PresetBundle>               m_bundle;
};

}
