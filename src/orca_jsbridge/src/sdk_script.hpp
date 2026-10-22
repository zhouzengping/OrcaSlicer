#pragma once

/*
 * Internal declarations shared by protocol.cpp / bridge.cpp and the generated
 * SDK TU. Never included from public headers.
 */

#include <string>

namespace orca::jb::detail {

// Defined in ${CMAKE_CURRENT_BINARY_DIR}/generated/orca_sdk_js.cpp, built from
// sdk/orca_sdk.js by cmake/EmbedJs.cmake.
extern const char kSdkInitScript[];

// ^[a-z0-9_]+\.[a-z0-9_]+$ validation without pulling <regex> into the DLL.
bool valid_method_name(const std::string& name);

} // namespace orca::jb::detail
