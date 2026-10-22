#pragma once

/*
 * Minimal semantic-version helpers, private to orca_plugin_host.
 * This phase accepts plain MAJOR.MINOR.PATCH only: numeric fields, no
 * leading zeros ("01" is rejected), no pre-release / build suffixes.
 */

#include <string>

namespace orca::plugin {

struct Semver
{
    int major = 0;
    int minor = 0;
    int patch = 0;
};

// Strict MAJOR.MINOR.PATCH parse; fills out and returns true on success.
bool parse_semver(const std::string& text, Semver& out);

// -1 when a < b, 0 when equal, 1 when a > b.
int compare_semver(const Semver& a, const Semver& b);

} // namespace orca::plugin
