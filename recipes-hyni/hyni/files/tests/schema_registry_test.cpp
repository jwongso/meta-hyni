#include "../src/schema_registry.h"
#include "../src/context_factory.h"
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>

namespace hyni {
namespace testing {

class SchemaRegistryTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create test directory structure
        std::filesystem::create_directory("test_schemas");

        // Create dummy schema files
        createDummySchemaFile("test_schemas/provider1.json");
        createDummySchemaFile("test_schemas/provider2.json");

        // Create a custom schema directory
        std::filesystem::create_directory("custom_schemas");
        createDummySchemaFile("custom_schemas/provider3.json");
    }

    void TearDown() override {
        // Clean up test directories and files
        std::filesystem::remove_all("test_schemas");
        std::filesystem::remove_all("custom_schemas");
    }

    void createDummySchemaFile(const std::string& path) {
        std::ofstream file(path);
        file << "{ \"provider\": { \"name\": \"test\", \"display_name\": \"Test AI\" }, "
             << "\"api\": { \"endpoint\": \"https://test.com/api\" }, "
             << "\"request_template\": {}, "
             << "\"message_format\": { \"structure\": {}, \"content_types\": {} }, "
             << "\"response_format\": { \"success\": { \"text_path\": [\"choices\", 0, \"message\", \"content\"] } } }";
        file.close();
    }
};

// Test builder pattern
TEST_F(SchemaRegistryTest, BuilderPattern) {
    auto registry = schema_registry::create()
    .set_schema_directory("test_schemas")
        .register_schema("custom_provider", "custom_schemas/provider3.json")
        .build();

    ASSERT_NE(registry, nullptr);
    EXPECT_TRUE(registry->is_provider_available("provider1"));
    EXPECT_TRUE(registry->is_provider_available("provider2"));
    EXPECT_TRUE(registry->is_provider_available("custom_provider"));
}

// Test path resolution
TEST_F(SchemaRegistryTest, ResolveSchemaPath) {
    auto registry = schema_registry::create()
    .set_schema_directory("test_schemas")
        .register_schema("custom_provider", "custom_schemas/provider3.json")
        .build();

    auto path1 = registry->resolve_schema_path("provider1");
    auto path2 = registry->resolve_schema_path("custom_provider");

    EXPECT_TRUE(path1.is_absolute());
    EXPECT_TRUE(path2.is_absolute());
    EXPECT_TRUE(path1.string().find("test_schemas/provider1.json") != std::string::npos);
    EXPECT_TRUE(path2.string().find("custom_schemas/provider3.json") != std::string::npos);
}

// Test empty provider name
TEST_F(SchemaRegistryTest, EmptyProviderName) {
    auto registry = schema_registry::create().build();

    EXPECT_THROW(registry->resolve_schema_path(""), std::invalid_argument);
    EXPECT_THROW(registry->is_provider_available(""), std::invalid_argument);
}

// Test getting available providers
TEST_F(SchemaRegistryTest, GetAvailableProviders) {
    auto registry = schema_registry::create()
    .set_schema_directory("test_schemas")
        .register_schema("custom_provider", "custom_schemas/provider3.json")
        .build();

    auto providers = registry->get_available_providers();

    EXPECT_EQ(3, providers.size());
    EXPECT_TRUE(std::find(providers.begin(), providers.end(), "provider1") != providers.end());
    EXPECT_TRUE(std::find(providers.begin(), providers.end(), "provider2") != providers.end());
    EXPECT_TRUE(std::find(providers.begin(), providers.end(), "custom_provider") != providers.end());
}

// Test provider availability check
TEST_F(SchemaRegistryTest, IsProviderAvailable) {
    auto registry = schema_registry::create()
    .set_schema_directory("test_schemas")
        .build();

    EXPECT_TRUE(registry->is_provider_available("provider1"));
    EXPECT_TRUE(registry->is_provider_available("provider2"));
    EXPECT_FALSE(registry->is_provider_available("nonexistent_provider"));
}

class ContextFactoryTest : public SchemaRegistryTest {
protected:
    std::shared_ptr<schema_registry> registry;
    std::shared_ptr<context_factory> factory;

    void SetUp() override {
        SchemaRegistryTest::SetUp();
        registry = schema_registry::create()
                       .set_schema_directory("test_schemas")
                       .register_schema("custom_provider", "custom_schemas/provider3.json")
                       .build();
        factory = std::make_shared<context_factory>(registry);
    }
};

// Test context creation
TEST_F(ContextFactoryTest, CreateContext) {
    // Should succeed for existing providers
    auto context1 = factory->create_context("provider1");
    EXPECT_NE(context1, nullptr);
    EXPECT_EQ(context1->get_provider_name(), "test");

    // Should throw for non-existent providers
    EXPECT_THROW(factory->create_context("nonexistent_provider"), schema_exception);
}

// Test context creation with config
TEST_F(ContextFactoryTest, CreateContextWithConfig) {
    context_config config;
    config.default_max_tokens = 100;
    config.default_temperature = 0.7;

    auto context = factory->create_context("provider1", config);
    EXPECT_NE(context, nullptr);

    // Verify config was applied (requires extending general_context to expose config)
    // EXPECT_EQ(context->get_config().default_max_tokens, 100);
}

// Test schema caching
TEST_F(ContextFactoryTest, SchemaCaching) {
    // First creation should cache the schema
    auto context1 = factory->create_context("provider1");
    auto stats1 = factory->get_cache_stats();
    EXPECT_EQ(stats1.cache_size, 1);
    EXPECT_EQ(stats1.hit_count, 0);
    EXPECT_EQ(stats1.miss_count, 1);

    // Second creation should use cached schema
    auto context2 = factory->create_context("provider1");
    auto stats2 = factory->get_cache_stats();
    EXPECT_EQ(stats2.cache_size, 1);
    EXPECT_EQ(stats2.hit_count, 1);
    EXPECT_EQ(stats2.miss_count, 1);

    // Different provider should miss cache
    auto context3 = factory->create_context("provider2");
    auto stats3 = factory->get_cache_stats();
    EXPECT_EQ(stats3.cache_size, 2);
    EXPECT_EQ(stats3.hit_count, 1);
    EXPECT_EQ(stats3.miss_count, 2);
}

// Test cache clearing
TEST_F(ContextFactoryTest, ClearCache) {
    factory->create_context("provider1");
    factory->create_context("provider2");

    auto stats1 = factory->get_cache_stats();
    EXPECT_EQ(stats1.cache_size, 2);

    factory->clear_cache();

    auto stats2 = factory->get_cache_stats();
    EXPECT_EQ(stats2.cache_size, 0);
    EXPECT_EQ(stats2.hit_count, 0);
    EXPECT_EQ(stats2.miss_count, 0);
}

// Test thread-local context
TEST_F(ContextFactoryTest, ThreadLocalContext) {
    // Get thread-local context
    auto& context1 = factory->get_thread_local_context("provider1");
    auto& context2 = factory->get_thread_local_context("provider1");

    // Should be the same instance within the same thread
    EXPECT_EQ(&context1, &context2);

    // Test in another thread
    std::thread t([this]() {
        auto& context_thread = factory->get_thread_local_context("provider1");
        // Address should be different from main thread's context
        EXPECT_NE(&context_thread, &factory->get_thread_local_context("provider1"));
    });
    t.join();
}

// Test multi-threaded access
TEST_F(ContextFactoryTest, MultiThreadedAccess) {
    const int NUM_THREADS = 10;
    std::vector<std::thread> threads;
    std::atomic<int> success_count = 0;

    for (int i = 0; i < NUM_THREADS; i++) {
        threads.emplace_back([this, &success_count]() {
            try {
                auto context = factory->create_context("provider1");
                auto& tl_context = factory->get_thread_local_context("provider2");
                success_count++;
            } catch (const std::exception& e) {
                // Should not happen
                FAIL() << "Exception in thread: " << e.what();
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(success_count, NUM_THREADS);

    auto stats = factory->get_cache_stats();
    EXPECT_EQ(stats.cache_size, 2); // Only two unique providers used
}

// Test provider_context helper
TEST_F(ContextFactoryTest, ProviderContextHelper) {
    provider_context claude_ctx(factory, "provider1");

    // First access creates the context
    auto& context1 = claude_ctx.get();
    EXPECT_EQ(context1.get_provider_name(), "test");

    // Second access reuses it
    auto& context2 = claude_ctx.get();
    EXPECT_EQ(&context1, &context2);

    // Test reset
    context1.add_user_message("Hello");
    EXPECT_FALSE(context1.get_messages().empty());

    claude_ctx.reset();
    EXPECT_TRUE(context1.get_messages().empty());
}

// Test null registry handling
TEST_F(ContextFactoryTest, NullRegistry) {
    EXPECT_THROW(context_factory(nullptr), std::invalid_argument);
}

// Test invalid schema file
TEST_F(ContextFactoryTest, InvalidSchemaFile) {
    // Create invalid JSON file
    std::ofstream file("test_schemas/invalid.json");
    file << "{ invalid json";
    file.close();

    EXPECT_THROW(factory->create_context("invalid"), schema_exception);
}

// Test immutability of registry
TEST_F(ContextFactoryTest, RegistryImmutability) {
    auto registry1 = schema_registry::create()
    .set_schema_directory("test_schemas")
        .build();

    auto registry2 = schema_registry::create()
                         .set_schema_directory("custom_schemas")
                         .build();

    // Different registry instances should maintain their own settings
    EXPECT_TRUE(registry1->is_provider_available("provider1"));
    EXPECT_FALSE(registry1->is_provider_available("provider3"));

    EXPECT_FALSE(registry2->is_provider_available("provider1"));
    EXPECT_TRUE(registry2->is_provider_available("provider3"));
}

} // namespace testing
} // namespace hyni
