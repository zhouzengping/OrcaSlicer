#include "semver.h"

namespace orca::plugin {

namespace {

// Numeric fields must stay within int range; values beyond are rejected.
constexpr int s_max_field = 2147483647;

} // namespace

bool parse_semver(const std::string& text, Semver& out)
{
    out = Semver{};

    int  fields[3]     = {0, 0, 0};
    int  field         = 0;
    bool field_started = false;   // current field consumed at least one digit
    bool leading_zero  = false;   // current field opened with '0'; "0" alone is fine, "01" is not

    for (const char c : text)
    {
        if (c == '.')
        {
            if (field >= 2 || !field_started)
            {
                return false;   // more than three fields or an empty one
            }
            ++field;
            field_started = false;
            leading_zero  = false;
            continue;
        }

        if (c < '0' || c > '9')
        {
            return false;   // also rejects '-' pre-release and '+' build suffixes
        }

        if (!field_started)
        {
            field_started = true;
            leading_zero  = (c == '0');
        }
        else if (leading_zero)
        {
            return false;
        }

        const int digit = c - '0';
        if (fields[field] > (s_max_field - digit) / 10)
        {
            return false;   // overflow
        }
        fields[field] = fields[field] * 10 + digit;
    }

    if (field != 2 || !field_started)
    {
        return false;   // exactly three non-empty fields required
    }

    out.major = fields[0];
    out.minor = fields[1];
    out.patch = fields[2];
    return true;
}

int compare_semver(const Semver& a, const Semver& b)
{
    if (a.major != b.major)
    {
        return a.major < b.major ? -1 : 1;
    }
    if (a.minor != b.minor)
    {
        return a.minor < b.minor ? -1 : 1;
    }
    if (a.patch != b.patch)
    {
        return a.patch < b.patch ? -1 : 1;
    }
    return 0;
}

} // namespace orca::plugin
