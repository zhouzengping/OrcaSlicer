#pragma once

#include <string>
#include <map>
#include <set>
#include <vector>

namespace Slic3r {

class AppConfig;

void set_app_config(AppConfig* config);

// developer mode check
bool is_developer_mode();

// Patch a preset JSON file in-place with minimal text changes.
// Preserves all whitespace, field order, and formatting of untouched content.
//
// - scalar_patches: keys to add or update with a scalar value (string/number/bool).
//   Value is the raw serialized form from ConfigOption::serialize().
// - vector_patches: keys to add or update with an array value.
//   Each element is a serialized string from ConfigOptionVector::vserialize().
// - keys_to_remove: keys to delete entirely (including the trailing comma if any).
//
// Returns true on success.
bool patch_preset_json(const std::string &file_path,
                       const std::map<std::string, std::string>              &scalar_patches,
                       const std::map<std::string, std::vector<std::string>> &vector_patches,
                       const std::set<std::string>                           &keys_to_remove = {});

} // namespace Slic3r
