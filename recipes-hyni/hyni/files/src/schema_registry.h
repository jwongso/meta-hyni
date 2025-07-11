// -------------------------------------------------------------------------------------------------
//
// Copyright (C) all of the contributors. All rights reserved.
//
// This software, including documentation, is protected by copyright controlled by
// contributors. All rights are reserved. Copying, including reproducing, storing,
// adapting or translating, any or all of this material requires the prior written
// consent of all contributors.
//
// -------------------------------------------------------------------------------------------------

#pragma once

#include "general_context.h"
#include <unordered_map>
#include <memory>
#include <shared_mutex>
#include <set>

namespace hyni {

/**
 * @class schema_registry
 * @brief Immutable configuration for schema paths
 * @note Thread-safe after construction. Create once and share across threads.
 */
class schema_registry {
public:
    class builder {
    public:
        builder& set_schema_directory(const std::string& directory) {
            m_schema_directory = std::filesystem::path(directory);
            return *this;
        }

        builder& register_schema(const std::string& provider_name, const std::string& schema_path) {
            if (provider_name.empty()) {
                throw std::invalid_argument("Provider name cannot be empty");
            }
            m_provider_paths[provider_name] = std::filesystem::path(schema_path);
            return *this;
        }

        builder& register_schemas(const std::unordered_map<std::string, std::string>& schemas) {
            for (const auto& [name, path] : schemas) {
                register_schema(name, path);
            }
            return *this;
        }

        std::shared_ptr<schema_registry> build() const {
            return std::shared_ptr<schema_registry>(new schema_registry(*this));
        }

    private:
        friend class schema_registry;
        std::filesystem::path m_schema_directory = "./schemas";
        std::unordered_map<std::string, std::filesystem::path> m_provider_paths;
    };

    static builder create() { return builder{}; }

    std::filesystem::path resolve_schema_path(const std::string& provider_name) const {
        if (provider_name.empty()) {
            throw std::invalid_argument("Provider name cannot be empty");
        }

        auto it = m_provider_paths.find(provider_name);
        if (it != m_provider_paths.end()) {
            return std::filesystem::absolute(it->second);
        }

        return std::filesystem::absolute(m_schema_directory / (provider_name + ".json"));
    }

    std::vector<std::string> get_available_providers() const {
        std::set<std::string> providers; // Use set for uniqueness

        // From registered paths
        for (const auto& [name, path] : m_provider_paths) {
            if (std::filesystem::exists(path)) {
                providers.insert(name);
            }
        }

        // From schema directory
        if (std::filesystem::exists(m_schema_directory)) {
            for (const auto& entry : std::filesystem::directory_iterator(m_schema_directory)) {
                if (entry.is_regular_file() && entry.path().extension() == ".json") {
                    std::string provider_name = entry.path().stem().string();
                    providers.insert(provider_name);
                }
            }
        }

        return std::vector<std::string>(providers.begin(), providers.end());
    }

    bool is_provider_available(const std::string& provider_name) const {
        return std::filesystem::exists(resolve_schema_path(provider_name));
    }

private:
    explicit schema_registry(const builder& b)
        : m_schema_directory(b.m_schema_directory)
        , m_provider_paths(b.m_provider_paths) {}

    const std::filesystem::path m_schema_directory;
    const std::unordered_map<std::string, std::filesystem::path> m_provider_paths;
};

} // hyni
