#ifndef HYNI_CONFIG_H
#define HYNI_CONFIG_H

#include <string>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

static std::unordered_map<std::string, std::string> parse_hynirc(const std::string& file_path) {
    std::unordered_map<std::string, std::string> config;
    std::ifstream file(file_path);
    std::string line;

    while (std::getline(file, line)) {
        size_t delimiter_pos = line.find('=');
        if (delimiter_pos != std::string::npos) {
            std::string key = line.substr(0, delimiter_pos);
            std::string value = line.substr(delimiter_pos + 1);
            // Trim whitespace
            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t") + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t") + 1);
            config[key] = value;
        }
    }

    return config;
}

static std::string get_api_key_for_provider(const std::string& provider) {
    std::string env_var;
    // Make sure the name is [provider][name] in the schema.
    if (provider == "openai") {
        env_var = "OA_API_KEY";
    } else if (provider == "deepseek") {
        env_var = "DS_API_KEY";
    } else if (provider == "claude") {
        env_var = "CL_API_KEY";
    } else if (provider == "mistral") {
        env_var = "MS_API_KEY";
    } else {
        return "";
    }

    // Try environment variable first
    const char* api_key = std::getenv(env_var.c_str());
    if (api_key) {
        return api_key;
    }

    // Try .hynirc file
    fs::path rc_path = fs::path(std::getenv("HOME")) / ".hynirc";
    if (fs::exists(rc_path)) {
        auto config = parse_hynirc(rc_path.string());
        auto it = config.find(env_var);
        if (it != config.end()) {
            return it->second;
        }
    }

    return "";
}

namespace hyni
{
const std::string GENERAL_SYSPROMPT =
    "You are a helpful assistant";

const std::string BEHAVIORAL_SYSPROMPT = "";

const std::string SYSTEM_DESIGN_SYSPROMPT = "";

const std::string& get_commit_hash();

} // namespace hyni

#endif // HYNI_CONFIG_H
