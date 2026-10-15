#include "projectcore.hpp"

#include "configschemamanager.hpp"

namespace ConfigSchema
{

ProjectCore::ProjectCore(std::shared_ptr<const IConfigSchemaManager> schema)
    : m_schema(std::move(schema))
{
}

std::unique_ptr<ProjectCore> ProjectCore::create(const std::string& schemaDir, std::string& err)
{
    auto manager = loadConfigSchemaManager(schemaDir, err);
    if (manager == nullptr)
        return nullptr;
    auto schema = std::shared_ptr<const IConfigSchemaManager>(std::move(manager));
    auto core   = std::unique_ptr<ProjectCore>(new ProjectCore(schema));
    core->m_bundle = std::make_unique<PresetBundle>(schema);
    return core;
}

}
