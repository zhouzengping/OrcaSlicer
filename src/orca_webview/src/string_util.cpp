#include "string_util.h"

#if defined(_WIN32)
    #include <windows.h>
#endif

namespace orca::wv::detail {

#if defined(_WIN32)

std::wstring utf8_to_wide(const std::string& utf8)
{
    if (utf8.empty())
        return std::wstring();

    int length = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
    if (length <= 0)
        return std::wstring();

    std::wstring wide(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), wide.data(), length);
    return wide;
}

std::string wide_to_utf8(const std::wstring& wide)
{
    if (wide.empty())
        return std::string();

    int length = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0)
        return std::string();

    std::string utf8(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), utf8.data(), length, nullptr, nullptr);
    return utf8;
}

#endif // _WIN32

std::string shell_quote(const std::string& value)
{
    std::string quoted = "'";
    for (char c : value)
    {
        if (c == '\'')
            quoted += "'\\''";
        else
            quoted += c;
    }
    quoted += "'";
    return quoted;
}

} // namespace orca::wv::detail
