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

#include "schema_registry.h"
#include <mutex>
#include <atomic>
#include <fstream>

namespace hyni {

/**
 * @class context_factory
 * @brief Factory for creating general_context instances with schema caching
 * @note Thread-safe. Can be shared across threads.
 */
class context_factory {
public:
    explicit context_factory(std::shared_ptr<schema_registry> registry)
        : m_registry(std::move(registry)) {
        if (!m_registry) {
            throw std::invalid_argument("Registry cannot be null");
        }
    }

    /**
     * @brief Creates a new context instance
     * @note Each call creates a new independent instance suitable for thread-local use
     */
    std::unique_ptr<general_context> create_context(const std::string& provider_name,
                                                    const context_config& config = {}) const {
        auto schema_path = m_registry->resolve_schema_path(provider_name);

        if (!std::filesystem::exists(schema_path)) {
            throw schema_exception("Schema file not found for provider: " + provider_name +
                                   " at " + schema_path.string());
        }

        // Use cached schema if available
        auto cached_schema = get_cached_schema(schema_path);
        if (cached_schema) {
            return std::make_unique<general_context>(*cached_schema, config);
        }

        // Load and cache new schema
        auto schema = load_and_cache_schema(schema_path);
        return std::make_unique<general_context>(*schema, config);
    }

    /**
     * @brief Gets or creates a thread-local context
     * @note The context is created on first access per thread
     */
    general_context& get_thread_local_context(const std::string& provider_name,
                                              const context_config& config = {}) const {
        thread_local std::unordered_map<std::string, std::unique_ptr<general_context>> tl_contexts;

        auto& context = tl_contexts[provider_name];
        if (!context) {
            context = create_context(provider_name, config);
        }
        return *context;
    }

    /**
     * @brief Clears the schema cache
     * @note Useful for development/testing when schemas change
     */
    void clear_cache() const {
        std::unique_lock lock(m_cache_mutex);
        m_schema_cache.clear();
    }

    /**
     * @brief Gets cache statistics
     */
    struct cache_stats {
        size_t cache_size;
        size_t hit_count;
        size_t miss_count;
        double hit_rate() const {
            auto total = hit_count + miss_count;
            return total > 0 ? static_cast<double>(hit_count) / total : 0.0;
        }
    };

    cache_stats get_cache_stats() const {
        std::shared_lock lock(m_cache_mutex);
        return {
            m_schema_cache.size(),
            m_cache_hits.load(),
            m_cache_misses.load(),
        };
    }

private:
    std::shared_ptr<schema_registry> m_registry;

    // Schema cache - shared across all threads
    mutable std::unordered_map<std::string, std::shared_ptr<nlohmann::json>> m_schema_cache;
    mutable std::shared_mutex m_cache_mutex;
    mutable std::atomic<size_t> m_cache_hits{0};
    mutable std::atomic<size_t> m_cache_misses{0};

    std::shared_ptr<nlohmann::json> get_cached_schema(const std::filesystem::path& path) const {
        std::shared_lock lock(m_cache_mutex);
        auto it = m_schema_cache.find(path.string());
        if (it != m_schema_cache.end()) {
            m_cache_hits.fetch_add(1, std::memory_order_relaxed);
            return it->second;
        }
        m_cache_misses.fetch_add(1, std::memory_order_relaxed);
        return nullptr;
    }

    std::shared_ptr<nlohmann::json> load_and_cache_schema(const std::filesystem::path& path) const {
        auto schema = std::make_shared<nlohmann::json>();
        std::ifstream file(path);
        if (!file.is_open()) {
            throw schema_exception("Failed to open schema file: " + path.string());
        }

        try {
            file >> *schema;
        } catch (const nlohmann::json::parse_error& e) {
            throw schema_exception("Failed to parse schema JSON at " + path.string() +
                                   ": " + e.what());
        }

        // Cache it
        std::unique_lock lock(m_cache_mutex);
        m_schema_cache[path.string()] = schema;
        return schema;
    }
};

/**
 * @class provider_context
 * @brief Convenience wrapper for a specific provider
 */
class provider_context {
public:
    provider_context(std::shared_ptr<context_factory> factory,
                     const std::string& provider_name,
                     const context_config& config = {})
        : m_factory(std::move(factory))
        , m_provider_name(provider_name)
        , m_config(config) {}

    general_context& get() {
        struct thread_cache {
            std::string provider_name;
            std::unique_ptr<general_context> context;
        };
        thread_local thread_cache cache;

        if (!cache.context || cache.provider_name != m_provider_name) {
            cache.provider_name = m_provider_name;
            cache.context = m_factory->create_context(m_provider_name, m_config);
        }
        return *cache.context;
    }

    void reset() {
        get().reset();
    }

private:
    std::shared_ptr<context_factory> m_factory;
    std::string m_provider_name;
    context_config m_config;
};

} // hyni
