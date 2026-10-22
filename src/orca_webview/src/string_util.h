#pragma once

// Internal string helpers for orca_webview.

#include <string>

namespace orca::wv::detail {

/*
 * UTF-8 <-> UTF-16 conversions (Windows backend use).
 * On non-Windows builds the wide forms are thin no-ops kept for interface
 * stability; mac/linux backends work in UTF-8 directly.
 */
#if defined(_WIN32)
std::wstring utf8_to_wide(const std::string& utf8);
std::string  wide_to_utf8(const std::wstring& wide);
#else
inline std::wstring utf8_to_wide(const std::string& utf8)
{
    return std::wstring(utf8.begin(), utf8.end());
}

inline std::string wide_to_utf8(const std::wstring& wide)
{
    return std::string(wide.begin(), wide.end());
}
#endif

/*
 * Single-quote shell quoting for the macOS fallback launcher.
 */
std::string shell_quote(const std::string& value);

} // namespace orca::wv::detail
