#pragma once

#include <string>
#include <map>
#include <set>
#include <vector>

namespace Slic3r {

// developer mode check
bool is_developer_mode();

// Returns the custom vendor directory path from devmode.json "dev_mode_work_path",
// or empty string if not set. When non-empty, this path replaces data_dir()/system
// for vendor preset files only (e.g. Snapmaker.json and Snapmaker/ directory).
std::string get_dev_mode_work_path();

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
