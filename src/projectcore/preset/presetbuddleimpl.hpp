#pragma once

#include "../presetbuddle.hpp"

#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "presetcollection.hpp"

namespace ConfigSchema
{

struct PresetBundle::Impl
{
    PresetCollection prints;
    PresetCollection filaments;
    PresetCollection printers;

    std::vector<std::string> filament_presets;
    std::map<int, ConfigScopeSnapShot> filament_ams_list;
    std::vector<std::vector<std::string>> ams_multi_color_filment;
    std::map<int, std::pair<std::string, std::string>> machine_filaments;

    Preset const* calibrate_printer = nullptr;
    std::set<Preset const*> calibrate_filaments;

    ConfigScopeSnapShot project_config;

    PresetBundle::ObsoletePresets obsolete_presets;

    bool validation_mode = false;
    std::string vendor_to_validate;
    int m_errors = 0;
    std::shared_ptr<const IConfigSchemaManager> m_schema;

    explicit Impl(std::shared_ptr<const IConfigSchemaManager> schema);

    void loadPresets(const std::string& rootDir, bool isSystem, std::vector<std::string>* errors);
    void loadSelections(const std::string& file, std::vector<std::string>* errors);
    void exportSelections(const std::string& file) const;

    void setNumFilaments(size_t n);
    bool updateCompatible();

    ConfigSnapshot mergeFullConfig() const;
};

}
