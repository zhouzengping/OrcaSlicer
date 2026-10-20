#pragma once

#include <cstdio>
#include <filesystem>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "../configsnapshot.hpp"
#include "ipreset.hpp"

namespace ConfigSchema
{

class Preset : public IPreset
{
public:
    static const char* kKeyVersion;
    static const char* kKeyName;
    static const char* kKeyFrom;
    static const char* kKeyInherits;
    static const char* kKeyRenamedFrom;
    static const char* kKeyType;
    static const char* kKeySettingId;
    static const char* kKeyBaseId;
    static const char* kKeyUserId;
    static const char* kKeyFilamentId;
    static const char* kKeyDescription;
    static const char* kKeyInstantiation;
    static const char* kKeyIsCustom;
    static const char* kKeySyncInfo;
    static const char* kKeyUpdatedTime;

    Preset() = default;
    Preset(PresetType type, const std::string& name, bool isDefault = false);

    PresetType      type = PresetType::Hidden;
    bool            is_default = false;
    bool            is_system = false;
    bool            is_visible = true;
    bool            is_dirty = false;
    bool            is_compatible = true;
    bool            is_project_embedded = false;
    bool            loaded = false;

    std::string     name;
    std::string     file;

    std::string     alias;
    std::vector<std::string> renamed_from;

    std::string     version;
    std::string     setting_id;
    std::string     filament_id;
    std::string     user_id;
    std::string     base_id;
    std::string     sync_info;
    std::string     custom_defined;
    std::string     description;
    long long       updated_time = 0;
    bool            filament_id_from_json = false;

    ConfigScopeSnapShot config;

    PresetType      presetType() const override { return type; }
    const std::string& presetName() const override { return name; }
    const std::string& presetFile() const override { return file; }
    bool            isDefault() const override { return is_default; }
    bool            isSystem() const override { return is_system; }
    bool            isVisible() const override { return is_visible; }
    bool            isDirty() const override { return is_dirty; }
    bool            isCompatible() const override { return is_compatible; }
    bool            isProjectEmbedded() const override { return is_project_embedded; }
    bool            isLoaded() const override { return loaded; }
    bool            isUser() const override { return !is_default && !is_system && !is_project_embedded; }
    const IConfigScopeSnapShot* configSnapshot() const override { return &config; }
    const std::string& presetAlias() const override { return alias; }
    const std::vector<std::string>& renamedFrom() const override { return renamed_from; }
    const std::string& presetVersion() const override { return version; }
    const std::string& settingId() const override { return setting_id; }
    const std::string& filamentId() const override { return filament_id; }
    const std::string& userId() const override { return user_id; }
    const std::string& baseId() const override { return base_id; }
    const std::string& syncInfo() const override { return sync_info; }
    const std::string& customDefined() const override { return custom_defined; }
    const std::string& presetDescription() const override { return description; }
    long long       updatedTime() const override { return updated_time; }

    std::string     inherits() const override { return config.value(kKeyInherits).to_string(); }
    void            setInherits(const std::string& v) { config.setValue(kKeyInherits, SlicerVariant(v)); }

    std::string     label() const override;

    void            set_dirty(bool dirty = true) { is_dirty = dirty; }
    void            reset_dirty() { is_dirty = false; }
    void            set_dirty_from(const ConfigScopeSnapShot& other) { is_dirty = !config.diff(other).empty(); }

    static std::string suffixModified();
    static std::string removeSuffixModified(const std::string& name);

    bool            load(const std::string& filePath, const IConfigSchemaManager* schema,
                         std::vector<std::string>* unknownKeys = nullptr,
                         bool* skipFile = nullptr);

    bool            save(const Preset* parent, const IConfigSchemaManager* schema) const;

    bool            reload(const Preset& parent, const IConfigSchemaManager* schema);

    bool            load_info(const std::string& file);
    bool            save_info(const std::string& file) const;
    void            remove_files();

    static std::string dirName(PresetType type);
    static std::string iotTypeName(PresetType type);
    static bool        typeFromDirName(const std::string& dirName, PresetType& out);
    static bool        typeFromIotName(const std::string& name, PresetType& out);

    bool            operator<(const Preset& other) const { return name < other.name; }

private:
    static bool     isMetaKey(const std::string& key);
};

}
