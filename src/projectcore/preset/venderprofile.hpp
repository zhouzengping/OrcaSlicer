#pragma once

#include <string>
#include <vector>
#include <map>
#include <optional>



struct VendorProfileConfig
{
    std::string name;
    std::string sub_path;
    std::string type;
    std::string version; 
    std::string from;
};

struct VendorProfile
{
    std::string name;
    std::string version;
    std::string url;
    std::string description;

    std::vector<VendorProfileConfig> machine_model_list;
    std::vector<VendorProfileConfig> process_list;
    std::vector<VendorProfileConfig> filament_list;
    std::vector<VendorProfileConfig> machine_list;

    std::map<std::string, std::string> config_maps;
    static std::optional<VendorProfile> from_json(const std::string& path, std::string& err);
};