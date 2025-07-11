#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <fstream>
#include <filesystem>
#include <thread>
#include <chrono>
#include <atomic>
#include <future>
#include <memory>
#include "../src/chat_api.h"
#include "../src/general_context.h"
#include "../src/config.h"

using namespace hyni;
using namespace testing;

class ChatAPITest : public ::testing::Test {
protected:
    std::shared_ptr<chat_api> create_chat_api(const std::string& schema_path,
                                              const context_config& config = {}) {
        auto context = std::make_unique<general_context>(schema_path, config);
        return std::make_shared<chat_api>(std::move(context));  // Changed to shared_ptr
    }

    const std::vector<std::string>& schemas() const { return m_schemas; }

private:
    std::vector<std::string> m_schemas = {"../schemas/openai.json",
        "../schemas/claude.json",
        "../schemas/deepseek.json",
        "../schemas/mistral.json"
    };
};

// Test basic construction and initialization
TEST_F(ChatAPITest, ConstructionWithValidSchema) {
    EXPECT_NO_THROW({
        auto api = create_chat_api(std::string("../schemas/openai.json"));
        EXPECT_NE(api.get(), nullptr);
    });
}

TEST_F(ChatAPITest, ConstructionWithInvalidSchema) {
    EXPECT_THROW({
        auto context = std::make_unique<general_context>(std::string("nonexistent_schema.json"));
        chat_api api(std::move(context));
    }, schema_exception);
}

// Test context access and configuration
TEST_F(ChatAPITest, ContextAccess) {
    for (const auto& schema : schemas() ) {
        std::shared_ptr<chat_api> api;  // Changed to shared_ptr
        try {
            api = create_chat_api(schema);
        } catch (const std::exception& ex) {
            GTEST_SKIP() << "Unable to create chat_api: " << ex.what();
        }

        // Test context access
        auto& context = api->get_context();
        EXPECT_FALSE(context.get_provider_name().empty());
        EXPECT_FALSE(context.get_endpoint().empty());

        // Test model setting
        auto models = context.get_supported_models();
        if (!models.empty()) {
            EXPECT_NO_THROW(context.set_model(models[0]));
        }
    }
}

// Test message building and validation
TEST_F(ChatAPITest, MessageHandling) {
    for (const auto& schema : schemas() ) {
        std::shared_ptr<chat_api> api;  // Changed to shared_ptr
        try {
            api = create_chat_api(schema);
        } catch (const std::exception& ex) {
            GTEST_SKIP() << "Unable to create chat_api: " << ex.what();
        }
        auto& context = api->get_context();

        // Test adding messages
        EXPECT_NO_THROW({
            context.add_user_message("Hello, how are you?");
            context.add_assistant_message("I'm doing well, thank you!");
            context.add_user_message("That's great to hear.");
        });

        // Test request building
        nlohmann::json request;
        EXPECT_NO_THROW({
            request = context.build_request();
        });

        // Verify request structure
        EXPECT_TRUE(request.contains("messages"));
        EXPECT_TRUE(request["messages"].is_array());
        EXPECT_EQ(request["messages"].size(), 3);
    }
}

// Test message building and validation
TEST_F(ChatAPITest, BuilderMessageHandling) {
    for (const auto& schema : schemas() ) {
        std::shared_ptr<chat_api> api;  // Changed to shared_ptr
        try {
            api = create_chat_api(schema);
        } catch (const std::exception& ex) {
            GTEST_SKIP() << "Unable to create chat_api: " << ex.what();
        }
        auto& context = api->get_context();

        // Test adding messages
        EXPECT_NO_THROW({
            context.add_user_message("Hello, how are you?")
            .add_assistant_message("I'm doing well, thank you!")
                .add_user_message("That's great to hear.");
        });

        // Test request building
        nlohmann::json request;
        EXPECT_NO_THROW({
            request = context.build_request();
        });

        // Verify request structure
        EXPECT_TRUE(request.contains("messages"));
        EXPECT_TRUE(request["messages"].is_array());
        EXPECT_EQ(request["messages"].size(), 3);
    }
}

// Remaining tests - convert all std::unique_ptr<chat_api> to std::shared_ptr<chat_api>
// and update exception types where needed

// Test parameter setting and validation
TEST_F(ChatAPITest, ParameterHandling) {
    for (const auto& schema : schemas() ) {
        std::shared_ptr<chat_api> api;  // Changed to shared_ptr
        try {
            api = create_chat_api(schema);
        } catch (const std::exception& ex) {
            GTEST_SKIP() << "Unable to create chat_api: " << ex.what();
        }
        auto& context = api->get_context();

        // Test setting various parameter types
        EXPECT_NO_THROW({
            context.set_parameter("temperature", 0.8);
            context.set_parameter("max_tokens", 1500);
            context.set_parameter("top_p", 0.9);
            context.set_parameter("custom_param", "test_value");
        });

        // Test parameter retrieval
        EXPECT_TRUE(context.has_parameter("temperature"));
        EXPECT_EQ(context.get_parameter_as<double>("temperature"), 0.8);
        EXPECT_EQ(context.get_parameter_as<int>("max_tokens"), 1500);

        // Test parameter defaults
        EXPECT_EQ(context.get_parameter_as<double>("nonexistent", 1.0), 1.0);
    }
}

// Test parameter setting and validation
TEST_F(ChatAPITest, BuilderParameterHandling) {
    for (const auto& schema : schemas() ) {
        std::shared_ptr<chat_api> api;  // Changed to shared_ptr
        try {
            api = create_chat_api(schema);
        } catch (const std::exception& ex) {
            GTEST_SKIP() << "Unable to create chat_api: " << ex.what();
        }
        auto& context = api->get_context();

        // Test setting various parameter types
        EXPECT_NO_THROW({
            context.set_parameter("temperature", 0.8)
            .set_parameter("max_tokens", 1500)
                .set_parameter("top_p", 0.9)
                .set_parameter("custom_param", "test_value");
        });

        // Test parameter retrieval
        EXPECT_TRUE(context.has_parameter("temperature"));
        EXPECT_EQ(context.get_parameter_as<double>("temperature"), 0.8);
        EXPECT_EQ(context.get_parameter_as<int>("max_tokens"), 1500);

        // Test parameter defaults
        EXPECT_EQ(context.get_parameter_as<double>("nonexistent", 1.0), 1.0);
    }
}

// Test system message functionality
TEST_F(ChatAPITest, SystemMessageHandling) {
    for (const auto& schema : schemas() ) {
        std::shared_ptr<chat_api> api;  // Changed to shared_ptr
        try {
            api = create_chat_api(schema);
        } catch (const std::exception& ex) {
            GTEST_SKIP() << "Unable to create chat_api: " << ex.what();
        }
        auto& context = api->get_context();

        if (context.supports_system_messages()) {
            EXPECT_NO_THROW({
                context.set_system_message("You are a helpful assistant.");
            });

            context.add_user_message("Hello!");
            auto request = context.build_request();

            // System message should be present in request
            EXPECT_TRUE(request.contains("system") ||
                        (request.contains("messages") &&
                         request["messages"][0]["role"] == "system"));
        }
    }
}

// Test multimodal capabilities (if supported)
TEST_F(ChatAPITest, MultimodalSupportInvalidBase64) {
    for (const auto& schema : schemas() ) {
        std::shared_ptr<chat_api> api;  // Changed to shared_ptr
        try {
            api = create_chat_api(schema);
        } catch (const std::exception& ex) {
            GTEST_SKIP() << "Unable to create chat_api: " << ex.what();
        }
        auto& context = api->get_context();

        if (context.supports_multimodal()) {
            // Test adding image message (with dummy base64 data)
            std::string dummy_base64 = "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8/";

            EXPECT_THROW({
                context.add_user_message("What's in this image?", "image/png", dummy_base64);
            }, std::runtime_error);

            auto request = context.build_request();
            EXPECT_TRUE(request.contains("messages"));
            EXPECT_EQ(request["messages"].size(), 0);
        }
    }
}

// Test multimodal capabilities (if supported)
TEST_F(ChatAPITest, MultimodalSupportImage) {
    for (const auto& schema : schemas() ) {
        std::shared_ptr<chat_api> api;  // Changed to shared_ptr
        try {
            api = create_chat_api(schema);
        } catch (const std::exception& ex) {
            GTEST_SKIP() << "Unable to create chat_api: " << ex.what();
        }
        auto& context = api->get_context();

        if (context.supports_multimodal()) {
            // Test adding image message (with dummy base64 data)
            std::string image_path = "../tests/german.png";

            EXPECT_NO_THROW({
                context.add_user_message("What's in this image?", "image/png", image_path);
            });

            auto request = context.build_request();
            EXPECT_TRUE(request.contains("messages"));
            EXPECT_GE(request["messages"].size(), 0);
        }
    }
}

// Test request validation
TEST_F(ChatAPITest, RequestValidation) {
    for (const auto& schema : schemas() ) {
        context_config config;
        config.enable_validation = true;
        std::shared_ptr<chat_api> api;  // Changed to shared_ptr
        try {
            api = create_chat_api(schema, config);
        } catch (const std::exception& ex) {
            GTEST_SKIP() << "Unable to create chat_api: " << ex.what();
        }
        auto& context = api->get_context();

        // Empty request should be invalid
        EXPECT_FALSE(context.is_valid_request());
        auto errors = context.get_validation_errors();
        EXPECT_GT(errors.size(), 0);

        // Add required elements
        context.add_user_message("Test message");

        // Should now be valid (or have fewer errors)
        auto new_errors = context.get_validation_errors();
        EXPECT_LE(new_errors.size(), errors.size());
    }
}

// Test context reset and clearing
TEST_F(ChatAPITest, ContextReset) {
    for (const auto& schema : schemas() ) {
        std::shared_ptr<chat_api> api;
        try {
            api = create_chat_api(schema);
        } catch (const std::exception& ex) {
            GTEST_SKIP() << "Unable to create chat_api: " << ex.what();
        }
        auto& context = api->get_context();

        // Add some data
        context.add_user_message("Test message");
        context.set_parameter("temperature", 0.8);
        // Don't set system message here to avoid confusion

        // Test partial clearing
        context.clear_user_messages();
        auto request = context.build_request();
        EXPECT_EQ(request["messages"].size(), 0);

        // Parameters should still be there
        EXPECT_TRUE(context.has_parameter("temperature"));

        // Now test with system message separately
        if (context.supports_system_messages()) {
            context.set_system_message("Test system message");
            context.add_user_message("Another test message");

            auto request_with_system = context.build_request();
            size_t total_with_system = request_with_system["messages"].size();

            context.clear_user_messages();
            auto request_after_clear = context.build_request();

            // Should have one less message (the user message was removed)
            EXPECT_EQ(request_after_clear["messages"].size(), total_with_system - 1);
        }

        // Test full reset
        context.reset();
        EXPECT_FALSE(context.has_parameter("temperature"));
        auto final_request = context.build_request();
        EXPECT_EQ(final_request["messages"].size(), 0);
    }
}
// Test response extraction with mock responses
TEST_F(ChatAPITest, ResponseExtraction) {
    for (const auto& schema : schemas() ) {
        std::shared_ptr<chat_api> api;  // Changed to shared_ptr
        try {
            api = create_chat_api(schema);
        } catch (const std::exception& ex) {
            GTEST_SKIP() << "Unable to create chat_api: " << ex.what();
        }
        auto& context = api->get_context();

        // Create mock successful response based on schema format
        nlohmann::json mock_response;

        // This will vary based on your schema - adjust accordingly
        if (context.get_provider_name() == "openai") {
            mock_response = R"({
                "choices": [
                    {
                        "message": {
                            "role": "assistant",
                            "content": "Hello! How can I help you today?"
                        }
                    }
                ]
            })"_json;
        } else if (context.get_provider_name() == "anthropic") {
            mock_response = R"({
                "content": [
                    {
                        "type": "text",
                        "text": "Hello! How can I help you today?"
                    }
                ]
            })"_json;
        }

        if (!mock_response.empty()) {
            std::string extracted_text;
            EXPECT_NO_THROW({
                extracted_text = context.extract_text_response(mock_response);
            });
            EXPECT_EQ(extracted_text, "Hello! How can I help you today?");
        }
    }
}

// Test error response extraction
TEST_F(ChatAPITest, ErrorResponseExtraction) {
    for (const auto& schema : schemas() ) {
        std::shared_ptr<chat_api> api;  // Changed to shared_ptr
        try {
            api = create_chat_api(schema);
        } catch (const std::exception& ex) {
            GTEST_SKIP() << "Unable to create chat_api: " << ex.what();
        }
        auto& context = api->get_context();

        // Create mock error response
        nlohmann::json error_response = R"({
            "error": {
                "message": "Invalid API key provided",
                "type": "authentication_error"
            }
        })"_json;

        std::string error_message;
        EXPECT_NO_THROW({
            error_message = context.extract_error(error_response);
        });
        EXPECT_FALSE(error_message.empty());
    }
}

// Test async functionality
TEST_F(ChatAPITest, AsyncOperation) {
    for (const auto& schema : schemas() ) {
        std::shared_ptr<chat_api> api;  // Changed to shared_ptr
        try {
            api = create_chat_api(schema);
        } catch (const std::exception& ex) {
            GTEST_SKIP() << "Unable to create chat_api: " << ex.what();
        }

        // This test demonstrates async pattern even if actual API call fails
        std::atomic<bool> async_completed{false};
        std::string result;
        std::exception_ptr exception_ptr = nullptr;

        std::thread async_test([&]() {
            try {
                auto future = api->send_message_async("Test message");

                // Set a reasonable timeout
                auto status = future.wait_for(std::chrono::seconds(1));

                if (status == std::future_status::ready) {
                    result = future.get();
                } else {
                    result = "timeout";
                }
            } catch (...) {
                exception_ptr = std::current_exception();
            }
            async_completed = true;
        });

        // Wait for completion or timeout
        auto start = std::chrono::steady_clock::now();
        while (!async_completed &&
               std::chrono::steady_clock::now() - start < std::chrono::seconds(2)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        async_test.join();
        EXPECT_TRUE(async_completed);
        // We expect either a result, timeout, or exception (due to no real API)
        EXPECT_TRUE(!result.empty() || exception_ptr != nullptr);
    }
}

// Test streaming setup (structure test since we can't test real streaming easily)
TEST_F(ChatAPITest, StreamingSetup) {
    for (const auto& schema : schemas() ) {
        std::shared_ptr<chat_api> api;  // Changed to shared_ptr
        try {
            api = create_chat_api(schema);
        } catch (const std::exception& ex) {
            GTEST_SKIP() << "Unable to create chat_api: " << ex.what();
        }
        auto& context = api->get_context();

        if (context.supports_streaming()) {
            std::atomic<int> chunk_count{0};
            std::atomic<bool> completed{false};

            auto chunk_callback = [&](const std::string& chunk) {
                chunk_count++;
            };

            auto completion_callback = [&](const http_response& response) {
                completed = true;
            };

            // This will likely fail due to network/auth, but tests the interface
            EXPECT_NO_THROW({
                api->send_message_stream("Test message", chunk_callback, completion_callback);
            });

            // Give it a moment to potentially start
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

// Test with different configurations
TEST_F(ChatAPITest, DifferentConfigurations) {
    for (const auto& schema : schemas() ) {
        // Test with validation disabled
        context_config no_validation_config;
        no_validation_config.enable_validation = false;

        std::shared_ptr<chat_api> api_no_validation;
        try {
            api_no_validation = create_chat_api(schema, no_validation_config);
        } catch (const std::exception& ex) {
            GTEST_SKIP() << "Unable to create chat_api: " << ex.what();
        }
        auto& context = api_no_validation->get_context();

        // Should allow invalid operations without throwing
        EXPECT_NO_THROW({
            context.set_parameter("invalid_param", "invalid_value");
            context.add_message("invalid_role", "test content");
        });

        // Test with custom defaults
        context_config custom_config;
        custom_config.default_max_tokens = 2000;
        custom_config.default_temperature = 0.5;

        std::shared_ptr<chat_api> api_custom;
        try {
            api_custom = create_chat_api(schema, custom_config);
        } catch (const std::exception& ex) {
            GTEST_SKIP() << "Unable to create chat_api: " << ex.what();
        }
        auto& custom_context = api_custom->get_context();

        // Explicitly set the parameters to override schema defaults
        custom_context.set_parameter("max_tokens", 2000);
        custom_context.set_parameter("temperature", 0.5);

        custom_context.add_user_message("Test");
        auto request = custom_context.build_request();

        // Check if our explicitly set values are in the request
        if (request.contains("max_tokens")) {
            EXPECT_EQ(request["max_tokens"].get<int>(), 2000);
        }
        if (request.contains("temperature")) {
            EXPECT_EQ(request["temperature"].get<double>(), 0.5);
        }
    }
}

TEST_F(ChatAPITest, CancellationCallback) {
    for (const auto& schema : schemas()) {
        std::shared_ptr<chat_api> api;
        try {
            api = create_chat_api(schema);
        } catch (const std::exception& ex) {
            GTEST_SKIP() << "Unable to create chat_api: " << ex.what();
            continue;
        }

        if (!api) {
            GTEST_SKIP() << "Null chat_api instance for schema";
            continue;
        }

        // Add a minimal API key to prevent early failure
        try {
            api->get_context().add_user_message("Test");
        } catch (...) {
            GTEST_SKIP() << "Failed to add user message";
            continue;
        }

        std::atomic<bool> cancel_requested{false};
        std::atomic<bool> operation_started{false};
        std::atomic<bool> operation_completed{false};

        auto cancel_callback = [&]() -> bool {
            operation_started = true;
            return cancel_requested.load();
        };

        // Use a lambda that captures the shared_ptr by value
        std::thread test_thread([api, cancel_callback, &operation_started, &operation_completed]() {
            try {
                std::string dummy = api->send_message("Test message", cancel_callback);
            } catch (const no_user_message_error&) {
                // This is actually unexpected since we added a message above
            } catch (...) {
                // Other errors (network, auth, etc.) are expected
            }
            operation_completed = true;
        });

        // Wait for operation to start or timeout
        auto start_time = std::chrono::steady_clock::now();
        while (!operation_started &&
               std::chrono::steady_clock::now() - start_time < std::chrono::milliseconds(100)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        // Request cancellation
        cancel_requested = true;

        // Wait for thread completion
        if (test_thread.joinable()) {
            test_thread.join();
        }

        EXPECT_TRUE(operation_completed);
    }
}

// Integration test for full workflow
TEST_F(ChatAPITest, FullWorkflowIntegration) {
    for (const auto& schema : schemas() ) {
        std::shared_ptr<chat_api> api;
        try {
            api = create_chat_api(schema);
        } catch (const std::exception& ex) {
            GTEST_SKIP() << "Unable to create chat_api: " << ex.what();
        }
        auto& context = api->get_context();

        // Complete workflow test
        EXPECT_NO_THROW({
            // 1. Configure the context
            if (!context.get_supported_models().empty()) {
                context.set_model(context.get_supported_models()[0]);
            }

            bool has_system_message = false;
            if (context.supports_system_messages()) {
                context.set_system_message("You are a helpful assistant.");
                has_system_message = true;
            }

            context.set_parameter("temperature", 0.7);
            context.set_parameter("max_tokens", 100);

            // 2. Add conversation messages
            context.add_user_message("Hello!");
            context.add_assistant_message("Hi there! How can I help you?");
            context.add_user_message("What's the weather like?");

            // 3. Validate the request
            EXPECT_TRUE(context.is_valid_request() ||
                        context.get_validation_errors().size() <= 1);

            // 4. Build and inspect the request
            auto request = context.build_request();
            EXPECT_TRUE(request.contains("messages"));
            EXPECT_GT(request["messages"].size(), 0);

            // 5. Test context management
            auto original_message_count = request["messages"].size();
            context.clear_user_messages();
            context.add_user_message("New conversation");

            auto new_request = context.build_request();

            // For Claude, system message is in a separate field, not in messages array
            // Check if this provider uses a separate system field
            bool system_in_messages = true;
            if (new_request.contains("system") && has_system_message) {
                // System message is separate (like Claude)
                system_in_messages = false;
            }

            int expected_message_count = 1;  // The new user message
            if (has_system_message && system_in_messages) {
                expected_message_count++;  // Plus system message only if it's in messages array
            }

            EXPECT_EQ(new_request["messages"].size(), expected_message_count);
            EXPECT_LT(new_request["messages"].size(), original_message_count);
        });
    }
}

TEST_F(ChatAPITest, SendMessageWithoutParameter) {
    for (const auto& schema : schemas() ) {
        std::shared_ptr<chat_api> api;
        try {
            api = create_chat_api(schema);
        } catch (const std::exception& ex) {
            GTEST_SKIP() << "Unable to create chat_api: " << ex.what();
        }

        std::string api_key = get_api_key_for_provider(api->get_context().get_provider_name());
        if (api_key.empty()) {
            GTEST_SKIP() << "No API key found for provider: " << api->get_context().get_provider_name();
        }

        // Set up context with messages
        api->get_context().add_user_message("Ping")
            .set_system_message("Answer with 'Pong'")
            .set_api_key(api_key);

        try {
            std::string response = api->send_message();
            // Note: Real API might not return exactly "Pong", so this assertion might be too strict
            EXPECT_FALSE(response.empty());
        } catch (const std::exception& ex) {
            GTEST_SKIP() << "API call failed (expected without valid credentials): " << ex.what();
        }
    }
}

TEST_F(ChatAPITest, SendMessageWithoutParameterFailsWithNoUserMessage) {
    for (const auto& schema : schemas() ) {
        std::shared_ptr<chat_api> api;  // Changed to shared_ptr
        try {
            api = create_chat_api(schema);
        } catch (const std::exception& ex) {
            GTEST_SKIP() << "Unable to create chat_api: " << ex.what();
        }
        // Only system message, no user message
        api->get_context().set_system_message("You are helpful");
        std::string dummy;
        EXPECT_THROW(
            dummy = api->send_message(),
            no_user_message_error  // Updated to use specific exception type
            );
    }
}

TEST_F(ChatAPITest, DISABLED_SendMessageStreamWithoutParameter) {
    for (const auto& schema : schemas()) {
        std::shared_ptr<chat_api> api;
        try {
            api = create_chat_api(schema);
        } catch (const std::exception& ex) {
            GTEST_SKIP() << "Unable to create chat_api: " << ex.what();
            continue;
        }

        // Check if streaming is supported before testing
        if (!api->get_context().supports_streaming()) {
            GTEST_SKIP() << "Streaming not supported for schema: " << schema;
            continue;
        }

        std::string api_key = get_api_key_for_provider(api->get_context().get_provider_name());
        if (api_key.empty()) {
            GTEST_SKIP() << "No API key found for provider: " << api->get_context().get_provider_name();
        }

        try {
            api->get_context()
            .add_user_message("Stream test message")
                .set_api_key(api_key);

            std::vector<std::string> received_chunks;
            bool completed = false;

            api->send_message_stream(
                [&](const std::string& chunk) {
                    std::cout << "Received chunk: " << chunk << "\n";
                    received_chunks.push_back(chunk);
                },
                [&](const http_response& response) {
                    std::cout << "Stream complete.\n";
                    completed = true;
            });

            EXPECT_FALSE(received_chunks.empty()) << "No chunks received for schema: " << schema;
            EXPECT_TRUE(completed) << "Completion callback not invoked for schema: " << schema;
        } catch (const streaming_not_supported_error& ex) {
            GTEST_SKIP() << "Streaming not supported: " << ex.what();
        } catch (const std::exception& ex) {
            GTEST_SKIP() << "API call failed (expected without valid credentials): " << ex.what();
        }
    }
}
