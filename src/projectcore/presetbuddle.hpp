#pragma once

#include <memory>
#include <string>
#include <vector>

#include "configschema/iconfigschemamanager.hpp"
#include "configsnapshot.hpp"
#include "preset/ipresetcollection.hpp"
#include "projectcore_export.hpp"

namespace ConfigSchema
{

class PROJECTCORE_API PresetBundle
{
public:
    struct PresetPreferences
    {
        std::string printer_model_id;
        std::string printer_variant;
        std::string filament;
    };

    struct ObsoletePresets
    {
        std::vector<std::string> prints;
        std::vector<std::string> filaments;
        std::vector<std::string> printers;
    };

    explicit PresetBundle(std::shared_ptr<const IConfigSchemaManager> schema);
    ~PresetBundle();

    IPresetCollection*       prints();
    const IPresetCollection* prints() const;
    IPresetCollection*       filaments();
    const IPresetCollection* filaments() const;
    IPresetCollection*       printers();
    const IPresetCollection* printers() const;

    const std::vector<std::string>& filament_presets() const;
    void                    set_filament_presets(const std::vector<std::string>& presets);

    const IConfigScopeSnapShot* project_config() const;
    void                    set_project_config_value(const std::string& key, const SlicerVariant& v);

    const IConfigSchemaManager* schema() const;

    bool                    has_defaults_only() const;

    void                    load_presets(const std::string& rootDir, bool isSystem,
                                         std::vector<std::string>* errors = nullptr);

    void                    load_selections(const std::string& file,
                                            std::vector<std::string>* errors = nullptr);
    void                    export_selections(const std::string& file) const;

    bool                    save_current_preset(PresetType type, const std::string& newName, bool detach = false);

    void                    set_num_filaments(size_t n);
    void                    set_filament_preset(size_t idx, const std::string& name);

    bool                    update_compatible();

    ConfigSnapshot          full_config() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}
