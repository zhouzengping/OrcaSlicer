#include "venderprofile.hpp"

#include <fstream>
#include <sstream>
#include <utility>

#include "nlohmann/json.hpp"

using json = nlohmann::json;

namespace {

bool readFileText(const std::string& path, std::string& out)
{
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    if (out.size() >= 3 && (unsigned char)out[0] == 0xEF && (unsigned char)out[1] == 0xBB && (unsigned char)out[2] == 0xBF)
        out.erase(0, 3);
    return true;
}

std::vector<VendorProfileConfig> parseConfigList(const json& j, const char* key)
{
    std::vector<VendorProfileConfig> out;
    const auto it = j.find(key);
    if (it == j.end() || !it->is_array())
        return out;
    for (const auto& item : *it)
    {
        if (!item.is_object())
            continue;
        VendorProfileConfig c;
        c.name     = item.value("name", std::string());
        c.sub_path = item.value("sub_path", std::string());
        c.type     = item.value("type", std::string());
        c.version  = item.value("version", std::string());
        c.from     = item.value("from", std::string());
        if (c.name.empty() || c.sub_path.empty())
            continue;
        out.push_back(std::move(c));
    }
    return out;
}

}

std::optional<VendorProfile> VendorProfile::from_json(const std::string& path, std::string& err)
{
    std::string text;
    if (!readFileText(path, text))
    {
        err = "cannot read " + path;
        return std::nullopt;
    }
    json j;
    try
    {
        j = json::parse(text);
    }
    catch (const std::exception& e)
    {
        err = "parse " + path + " failed: " + e.what();
        return std::nullopt;
    }
    if (!j.is_object())
    {
        err = path + " is not a json object";
        return std::nullopt;
    }

    VendorProfile out;
    try
    {
        out.name        = j.value("name", std::string());
        out.version     = j.value("version", std::string());
        out.url         = j.value("url", std::string());
        out.description = j.value("description", std::string());

        out.machine_model_list = parseConfigList(j, "machine_model_list");
        out.process_list      = parseConfigList(j, "process_list");
        out.filament_list     = parseConfigList(j, "filament_list");
        out.machine_list      = parseConfigList(j, "machine_list");
    }
    catch (const std::exception& e)
    {
        err = "parse " + path + " fields failed: " + e.what();
        return std::nullopt;
    }

    auto fillConfigMaps = [&out](const std::vector<VendorProfileConfig>& list) {
        for (const VendorProfileConfig& c : list)
            out.config_maps[c.name] = c.sub_path;
    };
    fillConfigMaps(out.machine_model_list);
    fillConfigMaps(out.process_list);
    fillConfigMaps(out.filament_list);
    fillConfigMaps(out.machine_list);

    if (out.name.empty())
    {
        err = path + " has no name";
        return std::nullopt;
    }
    return out;
}
