#include "DevModeHelp.hpp"
#include "Utils.hpp"

#include <boost/filesystem.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <boost/nowide/fstream.hpp>
#include <boost/log/trivial.hpp>

namespace {

// Returns the position of the opening quote of `key` in `text`, or npos.
size_t find_key(const std::string& text, const std::string& key)
{
    std::string needle = "\"" + key + "\"";
    size_t      pos    = 0;
    while (pos < text.size()) {
        size_t found = text.find(needle, pos);
        if (found == std::string::npos)
            return std::string::npos;
        // Make sure the char after the closing quote (skipping whitespace) is ':'
        size_t after = found + needle.size();
        while (after < text.size() && (text[after] == ' ' || text[after] == '\t'))
            ++after;
        if (after < text.size() && text[after] == ':')
            return found;
        pos = found + 1;
    }
    return std::string::npos;
}

// Given position of the ':' separator, find [value_start, value_end).
// value_start: first non-whitespace char of the value.
// value_end:   one past the last char of the value token.
bool find_value_range(const std::string& text, size_t colon, size_t& value_start, size_t& value_end)
{
    size_t pos = colon + 1;
    while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t' || text[pos] == '\r' || text[pos] == '\n'))
        ++pos;
    if (pos >= text.size())
        return false;

    value_start = pos;
    char first  = text[pos];

    if (first == '"') {
        ++pos;
        while (pos < text.size()) {
            if (text[pos] == '\\') {
                pos += 2;
                continue;
            }
            if (text[pos] == '"') {
                ++pos;
                break;
            }
            ++pos;
        }
        value_end = pos;
    } else if (first == '[') {
        int depth = 0;
        while (pos < text.size()) {
            char c = text[pos];
            if (c == '[') {
                ++depth;
                ++pos;
            } else if (c == ']') {
                --depth;
                ++pos;
                if (depth == 0)
                    break;
            } else if (c == '"') {
                ++pos;
                while (pos < text.size()) {
                    if (text[pos] == '\\') {
                        pos += 2;
                        continue;
                    }
                    if (text[pos] == '"') {
                        ++pos;
                        break;
                    }
                    ++pos;
                }
            } else {
                ++pos;
            }
        }
        value_end = pos;
    } else {
        // number / bool / null
        while (pos < text.size() && text[pos] != ',' && text[pos] != '}' && text[pos] != '\r' && text[pos] != '\n')
            ++pos;
        value_end = pos;
        while (value_end > value_start && (text[value_end - 1] == ' ' || text[value_end - 1] == '\t'))
            --value_end;
    }

    return value_end > value_start;
}

static std::string escape_json_string(const std::string& s)
{
    std::ostringstream oss;
    for (unsigned char c : s) {
        switch (c) {
        case '"':  oss << "\\\""; break;
        case '\\': oss << "\\\\"; break;
        case '\n': oss << "\\n";  break;
        case '\r': oss << "\\r";  break;
        case '\t': oss << "\\t";  break;
        default:
            if (c < 0x20) {
                oss << "\\u" << std::hex << std::setw(4) << std::setfill('0') << (int)c;
            } else {
                oss << c;
            }
            break;
        }
    }
    return oss.str();
}

// Serialize a vector of strings as a compact JSON array: ["a", "b"]
std::string serialize_array(const std::vector<std::string>& values)
{
    std::ostringstream oss;
    oss << '[';
    for (size_t i = 0; i < values.size(); ++i) {
        if (i > 0)
            oss << ", ";
        oss << '"' << escape_json_string(values[i]) << '"';
    }
    oss << ']';
    return oss.str();
}

// Wrap a scalar value in quotes unless it is already a JSON literal.
std::string serialize_scalar(const std::string& v)
{
    if (!v.empty() && (v[0] == '-' || (v[0] >= '0' && v[0] <= '9') || v == "true" || v == "false" || v == "null"))
        return v;
    return "\"" + escape_json_string(v) + "\"";
}

// Detect the indentation used for existing keys (e.g. "    " for 4-space indent).
std::string detect_indent(const std::string& text)
{
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '"') {
            // Walk back to start of line
            size_t line_start = (i == 0) ? 0 : i - 1;
            while (line_start > 0 && text[line_start - 1] != '\n')
                --line_start;
            std::string indent;
            for (size_t j = line_start; j < i; ++j) {
                if (text[j] == ' ' || text[j] == '\t')
                    indent += text[j];
                else
                    break;
            }
            if (!indent.empty())
                return indent;
        }
    }
    return "    "; // fallback: 4 spaces
}

// Metadata keys that are not config options — skip when reading values.
const std::set<std::string>& metaDataKeys() {
    static const std::set<std::string> s_metadata_keys = {
        "name", "from", "version", "type", "setting_id", "base_id", "user_id",
        "filament_id", "updated_time", "inherits", "instantiation", "is_custom_defined",
        "url", "description", "force_update", "min_version"
    };
    return s_metadata_keys;
}



} // namespace

namespace Slic3r {

static nlohmann::json load_devmode_json()
{
    namespace fs = boost::filesystem;
    fs::path devmode_file = fs::path(data_dir()) / "devmode.json";
    boost::nowide::ifstream ifs(devmode_file.string());
    if (!ifs.is_open())
        return nlohmann::json{};
    nlohmann::json j = nlohmann::json::parse(ifs, nullptr, false);
    if (j.is_discarded() || !j.is_object())
        return nlohmann::json{};
    return j;
}

bool is_developer_mode()
{
    static bool s_devMode = []()->bool {
        auto j = load_devmode_json();
        if (j.empty())
            return false;
        auto it = j.find("is_developer_mode");
        return it != j.end() && it->is_boolean() && it->get<bool>();
    }();
    return s_devMode;
}

std::string get_dev_mode_work_path()
{
    if (!is_developer_mode())
        return std::string();

    static std::string s_devModeWorkPath = []()->std::string {
        auto j = load_devmode_json();
        if (j.empty())
            return std::string();
        auto it = j.find("dev_mode_work_path");
        if (it == j.end() || !it->is_string())
            return std::string();
        return it->get<std::string>();
    }();
    return s_devModeWorkPath;
}

struct Replacement {
    size_t      start;
    size_t      end;
    std::string value; // empty string means deletion of the whole entry
};

bool patch_preset_json(const std::string &file_path,
                       const std::map<std::string, std::string>              &scalar_patches,
                       const std::map<std::string, std::vector<std::string>> &vector_patches,
                       const std::set<std::string>                           &keys_to_remove)
{
    if (scalar_patches.empty() && vector_patches.empty() && keys_to_remove.empty())
        return true;

    boost::nowide::ifstream ifs(file_path, std::ios::binary);
    if (!ifs.is_open()) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": cannot open " << file_path;
        return false;
    }
    std::string text((std::istreambuf_iterator<char>(ifs)),
                      std::istreambuf_iterator<char>());
    ifs.close();

    std::vector<Replacement> replacements;
    std::vector<std::string> keys_to_add_scalar;
    std::vector<std::string> keys_to_add_vector;

    // --- Build new-value string for a key ---
    auto new_value_for_scalar = [&](const std::string &key) -> std::string {
        return serialize_scalar(scalar_patches.at(key));
    };
    auto new_value_for_vector = [&](const std::string &key) -> std::string {
        return serialize_array(vector_patches.at(key));
    };

    // --- MODIFY or flag for ADD (scalars) ---
    for (const auto &kv : scalar_patches) {
        size_t key_pos = find_key(text, kv.first);
        if (key_pos == std::string::npos) {
            keys_to_add_scalar.push_back(kv.first);
            continue;
        }
        std::string needle = "\"" + kv.first + "\"";
        size_t colon = text.find(':', key_pos + needle.size());
        size_t vs, ve;
        if (!find_value_range(text, colon, vs, ve)) continue;
        replacements.push_back({vs, ve, new_value_for_scalar(kv.first)});
    }

    // --- MODIFY or flag for ADD (vectors) ---
    for (const auto &kv : vector_patches) {
        size_t key_pos = find_key(text, kv.first);
        if (key_pos == std::string::npos) {
            keys_to_add_vector.push_back(kv.first);
            continue;
        }
        std::string needle = "\"" + kv.first + "\"";
        size_t colon = text.find(':', key_pos + needle.size());
        size_t vs, ve;
        if (!find_value_range(text, colon, vs, ve)) continue;
        replacements.push_back({vs, ve, new_value_for_vector(kv.first)});
    }

    // --- DELETE ---
    for (const auto &key : keys_to_remove) {
        size_t key_pos = find_key(text, key);
        if (key_pos == std::string::npos) {
            BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": key to remove not found: " << key;
            continue;
        }
        // Expand range to include the whole line: from start-of-line whitespace
        // through the trailing comma (if any) and the newline.
        size_t line_start = key_pos;
        while (line_start > 0 && text[line_start - 1] != '\n')
            --line_start;

        std::string needle = "\"" + key + "\"";
        size_t colon = text.find(':', key_pos + needle.size());
        size_t vs, ve;
        if (!find_value_range(text, colon, vs, ve)) continue;

        // Consume optional trailing comma and whitespace up to (and including) newline
        size_t end = ve;
        while (end < text.size() && (text[end] == ' ' || text[end] == '\t'))
            ++end;
        if (end < text.size() && text[end] == ',')
            ++end;
        while (end < text.size() && (text[end] == ' ' || text[end] == '\t'))
            ++end;
        if (end < text.size() && text[end] == '\r') ++end;
        if (end < text.size() && text[end] == '\n') ++end;

        replacements.push_back({line_start, end, ""});
    }

    // --- ADD new keys before the closing '}' ---
    if (!keys_to_add_scalar.empty() || !keys_to_add_vector.empty()) {
        size_t closing = text.rfind('}');
        if (closing == std::string::npos) {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": malformed JSON (no closing brace)";
            return false;
        }

        std::string indent = detect_indent(text);

        // Walk backwards from closing brace past whitespace/newlines to find
        // the end of the last real entry.
        size_t last_char = closing;
        while (last_char > 0 && (text[last_char - 1] == ' ' || text[last_char - 1] == '\t' ||
                                  text[last_char - 1] == '\r' || text[last_char - 1] == '\n'))
            --last_char;
        // last_char now points one past the last non-whitespace char before '}'.
        // If that char is not already a comma, insert one right there so it
        // appears at the end of the previous entry's line.
        bool need_comma = (last_char > 0 && text[last_char - 1] != ',');

        std::ostringstream new_entries;
        auto append_entry = [&](const std::string &key, const std::string &val) {
            new_entries << ",\n" << indent << '"' << key << "\": " << val;
        };

        for (const auto &key : keys_to_add_scalar)
            append_entry(key, new_value_for_scalar(key));
        for (const auto &key : keys_to_add_vector)
            append_entry(key, new_value_for_vector(key));

        std::string to_insert = new_entries.str();
        if (!to_insert.empty()) {
            // Insert right after the last real entry. If it already has a
            // trailing comma, drop the leading comma from our string.
            if (!need_comma)
                to_insert = to_insert.substr(1); // remove leading ','
            replacements.push_back({last_char, last_char, to_insert});
        }
    }

    if (replacements.empty())
        return true;

    // Apply replacements in reverse position order so offsets stay valid
    std::sort(replacements.begin(), replacements.end(),
              [](const Replacement &a, const Replacement &b) { return a.start > b.start; });

    for (const auto &r : replacements)
        text.replace(r.start, r.end - r.start, r.value);

    boost::nowide::ofstream ofs(file_path, std::ios::out | std::ios::trunc | std::ios::binary);
    if (!ofs.is_open()) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": cannot write " << file_path;
        return false;
    }
    ofs << text;

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": patched " << file_path;
    return true;
}

} // namespace Slic3r
