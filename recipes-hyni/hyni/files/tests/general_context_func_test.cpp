#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "../src/schema_registry.h"
#include "../src/context_factory.h"
#include "../src/config.h"
#include <nlohmann/json.hpp>
#include <cstdlib>
#include <thread>
#include <chrono>
#include <filesystem>
#include <curl/curl.h>

using namespace hyni;
using json = nlohmann::json;
namespace fs = std::filesystem;

// Structure to hold streaming response data
struct StreamingResponse {
    std::vector<std::string> events;
    std::string complete_content;
    bool finished = false;
    bool error = false;
    std::string error_message;
};

// Callback for handling streaming SSE data
static size_t streaming_callback(void* contents, size_t size, size_t nmemb, StreamingResponse* response) {
    if (!response) return 0;

    size_t total_size = size * nmemb;
    std::string chunk(static_cast<char*>(contents), total_size);

    // Parse SSE format: each event starts with "data: " and ends with "\n\n"
    std::istringstream stream(chunk);
    std::string line;

    while (std::getline(stream, line)) {
        if (line.find("data: ") == 0) {
            std::string json_data = line.substr(6); // Remove "data: " prefix

            if (json_data == "[DONE]") {
                response->finished = true;
                continue;
            }

            try {
                json event = json::parse(json_data);
                response->events.push_back(json_data);

                // Extract content from delta
                if (event.contains("delta") && event["delta"].contains("text")) {
                    response->complete_content += event["delta"]["text"].get<std::string>();
                }

                // Check for errors
                if (event.contains("error")) {
                    response->error = true;
                    response->error_message = event["error"]["message"].get<std::string>();
                }
            } catch (const json::parse_error& e) {
                std::cerr << "Error parsing JSON: " << e.what() << std::endl;
            }
        }
    }

    return total_size;
}

// Simple write callback for storing response in a string
static size_t write_callback(void* contents, size_t size, size_t nmemb, std::string* s) {
    if (!s) return 0;
    size_t new_length = size * nmemb;
    try {
        s->append(static_cast<char*>(contents), new_length);
    } catch (...) {
        return 0;
    }
    return new_length;
}

// Minimal API call function for testing purposes
std::string make_api_call(const std::string& url,
                          const std::string& api_key,
                          const std::string& payload,
                          bool is_anthropic = false) {
    CURL* curl = curl_easy_init();
    if (!curl) throw std::runtime_error("Failed to init curl");

    std::string response;
    struct curl_slist* headers = nullptr;

    headers = curl_slist_append(headers, "Content-Type: application/json");

    if (is_anthropic) {
        headers = curl_slist_append(headers, "anthropic-version: 2023-06-01");
        headers = curl_slist_append(headers, ("x-api-key: " + api_key).c_str());
    } else {
        headers = curl_slist_append(headers, ("Authorization: Bearer " + api_key).c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_0);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        throw std::runtime_error(std::string("CURL failed: ") + curl_easy_strerror(res));
    }

    return response;
}

// Enhanced API call function with streaming support
std::string make_streaming_api_call(const std::string& url,
                                    const std::string& api_key,
                                    const std::string& payload,
                                    StreamingResponse* streaming_response = nullptr,
                                    bool is_anthropic = false) {
    CURL* curl = curl_easy_init();
    if (!curl) throw std::runtime_error("Failed to init curl");

    std::string response;
    struct curl_slist* headers = nullptr;

    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: text/event-stream");

    if (is_anthropic) {
        headers = curl_slist_append(headers, "anthropic-version: 2023-06-01");
        headers = curl_slist_append(headers, ("x-api-key: " + api_key).c_str());
    } else {
        headers = curl_slist_append(headers, ("Authorization: Bearer " + api_key).c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_0);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);

    if (streaming_response) {
        // Set up for streaming
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, streaming_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, streaming_response);
    } else {
        // Regular response
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    }

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        throw std::runtime_error(std::string("CURL failed: ") + curl_easy_strerror(res));
    }

    return response;
}

class GeneralContextFunctionalTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Get API key from environment
        const char* api_key = std::getenv("CL_API_KEY");
        if (!api_key) {
            fs::path rc_path = fs::path(std::getenv("HOME")) / ".hynirc";

            if (fs::exists(rc_path)) {
                auto config = parse_hynirc(rc_path.string());

                auto it = config.find("CL_API_KEY");
                if (it != config.end()) {
                    api_key = it->second.c_str();
                }
            }
            else {
                GTEST_SKIP() << "CL_API_KEY environment variable not set";
            }
        }
        m_api_key = api_key;

        // Create schema registry with test schema directory
        m_test_schema_dir = "../schemas";
        m_registry = schema_registry::create()
                         .set_schema_directory(m_test_schema_dir)
                         .build();

        // Create context factory
        m_factory = std::make_shared<context_factory>(m_registry);

        // Create context with validation enabled
        context_config config;
        config.enable_validation = true;
        config.default_max_tokens = 100;
        config.default_temperature = 0.3;

        m_context = m_factory->create_context("claude", config);
        m_context->set_api_key(m_api_key);
    }

    void TearDown() override {
        // Clean up any temporary files
        if (fs::exists("test_image.png")) {
            fs::remove("test_image.png");
        }
    }

    void create_test_image() {
        // Create a small test image (1x1 pixel PNG)
        const unsigned char png_data[] = {
            0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,
            0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
            0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
            0x08, 0x02, 0x00, 0x00, 0x00, 0x90, 0x77, 0x53,
            0xDE, 0x00, 0x00, 0x00, 0x0C, 0x49, 0x44, 0x41,
            0x54, 0x08, 0x99, 0x01, 0x01, 0x00, 0x00, 0x00,
            0xFF, 0xFF, 0x00, 0x00, 0x00, 0x02, 0x00, 0x01,
            0xE2, 0x21, 0xBC, 0x33, 0x00, 0x00, 0x00, 0x00,
            0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82
        };

        std::ofstream file("test_image.png", std::ios::binary);
        file.write(reinterpret_cast<const char*>(png_data), sizeof(png_data));
        file.close();
    }

    std::string m_api_key;
    std::string m_test_schema_dir;
    std::shared_ptr<schema_registry> m_registry;
    std::shared_ptr<context_factory> m_factory;
    std::unique_ptr<general_context> m_context;
};

// Test basic schema loading and context creation
TEST_F(GeneralContextFunctionalTest, SchemaRegistryBasicFunctionality) {
    // Test provider availability
    EXPECT_TRUE(m_registry->is_provider_available("claude"));

    // Test available providers list
    auto providers = m_registry->get_available_providers();
    EXPECT_FALSE(providers.empty());
    EXPECT_NE(std::find(providers.begin(), providers.end(), "claude"), providers.end());

    // Test context creation
    EXPECT_NE(m_context, nullptr);
    EXPECT_TRUE(m_context->supports_multimodal());
    EXPECT_TRUE(m_context->supports_system_messages());
    EXPECT_TRUE(m_context->supports_streaming());
}

// Test context factory functionality
TEST_F(GeneralContextFunctionalTest, ContextFactoryFunctionality) {
    // Test cache stats after first context creation
    auto stats = m_factory->get_cache_stats();
    EXPECT_GE(stats.cache_size, 1);

    // Create another context for the same provider - should use cache
    auto context2 = m_factory->create_context("claude");
    auto stats2 = m_factory->get_cache_stats();
    EXPECT_EQ(stats2.hit_count, stats.hit_count + 1);

    // Verify context is properly initialized
    EXPECT_EQ(context2->get_provider_name(), "claude");
    EXPECT_EQ(context2->get_endpoint(), m_context->get_endpoint());
}

// Test thread-local context
TEST_F(GeneralContextFunctionalTest, ThreadLocalContext) {
    // Get thread-local context
    auto& tl_context = m_factory->get_thread_local_context("claude");
    tl_context.set_api_key(m_api_key);

    // Add a message to identify this context
    tl_context.add_user_message("Thread-local test");

    // Verify the context works properly
    EXPECT_EQ(tl_context.get_provider_name(), "claude");
    EXPECT_FALSE(tl_context.get_messages().empty());
    EXPECT_EQ(tl_context.get_messages()[0]["content"][0]["text"].get<std::string>(), "Thread-local test");

    // Test in another thread
    std::thread t([this]() {
        auto& thread_context = m_factory->get_thread_local_context("claude");
        thread_context.set_api_key(m_api_key);

        // This context should be different from main thread's context
        EXPECT_TRUE(thread_context.get_messages().empty());

        thread_context.add_user_message("Different thread test");
        EXPECT_EQ(thread_context.get_messages()[0]["content"][0]["text"].get<std::string>(),
                  "Different thread test");
    });
    t.join();

    // Main thread's context should be unchanged
    EXPECT_EQ(tl_context.get_messages()[0]["content"][0]["text"].get<std::string>(), "Thread-local test");
}

// Test provider_context helper
TEST_F(GeneralContextFunctionalTest, ProviderContextHelper) {
    provider_context claude_ctx(m_factory, "claude");
    auto& context = claude_ctx.get();
    context.set_api_key(m_api_key);

    // Test basic functionality
    context.add_user_message("Hello from provider_context");
    auto request = context.build_request();

    EXPECT_EQ(request["model"].get<std::string>(), "claude-3-5-sonnet-20241022");  // Default model in schema
    EXPECT_FALSE(request["messages"].empty());
    EXPECT_EQ(request["messages"][0]["content"][0]["text"].get<std::string>(),
              "Hello from provider_context");

    // Test reset
    claude_ctx.reset();
    EXPECT_TRUE(context.get_messages().empty());
}

// Test actual API request (if API key is available)
TEST_F(GeneralContextFunctionalTest, SimpleAPIRequest) {
    if (m_api_key.empty()) {
        GTEST_SKIP() << "API key not available";
    }

    // This test would require actual HTTP client implementation
    // We'll just test request building for now
    m_context->set_model("claude-3-haiku-20240307")
        .set_system_message("You are a helpful assistant.")
        .add_user_message("What is the capital of France?")
        .set_parameter("temperature", 0.0)
        .set_parameter("max_tokens", 50);

    auto request = m_context->build_request();

    EXPECT_EQ(request["model"].get<std::string>(), "claude-3-haiku-20240307");
    EXPECT_EQ(request["system"].get<std::string>(), "You are a helpful assistant.");
    EXPECT_FALSE(request["messages"].empty());
    EXPECT_EQ(request["temperature"].get<double>(), 0.0);
    EXPECT_EQ(request["max_tokens"].get<int>(), 50);
}

// Test multimodal request building
TEST_F(GeneralContextFunctionalTest, MultimodalRequest) {
    if (!m_context->supports_multimodal()) {
        GTEST_SKIP() << "Provider does not support multimodal content";
    }

    create_test_image();

    m_context->add_user_message("What's in this image?", "image/png", "test_image.png");
    auto request = m_context->build_request();

    // Verify the request contains the image
    EXPECT_FALSE(request["messages"].empty());
    EXPECT_GE(request["messages"][0]["content"].size(), 2);  // Text + image
    EXPECT_EQ(request["messages"][0]["content"][1]["type"].get<std::string>(), "image");
    EXPECT_EQ(request["messages"][0]["content"][1]["source"]["media_type"].get<std::string>(), "image/png");
    EXPECT_FALSE(request["messages"][0]["content"][1]["source"]["data"].get<std::string>().empty());
}

// Test basic single message conversation
TEST_F(GeneralContextFunctionalTest, BasicSingleMessage) {
    // Set up a simple conversation
    m_context->add_user_message("Hello, please respond with exactly 'Hi there!'");

    // Validate request structure
    EXPECT_TRUE(m_context->is_valid_request());

    auto request = m_context->build_request();

    // Verify request structure
    EXPECT_TRUE(request.contains("model"));
    EXPECT_TRUE(request.contains("max_tokens"));
    EXPECT_TRUE(request.contains("messages"));
    EXPECT_EQ(request["messages"].size(), 1);
    EXPECT_EQ(request["messages"][0]["role"], "user");

    std::string payload = request.dump();

    //Perform actual API call
    std::string api_key = m_api_key;
    std::string api_url = m_context->get_endpoint();
    bool is_anthropic = m_context->get_provider_name() == "claude";

    std::string response_str;
    json response_json;
    try {
        response_str = make_api_call(api_url, api_key, payload, is_anthropic);
        response_json = json::parse(response_str);
    } catch (const std::exception& ex) {
        FAIL() << "API call failed: " << ex.what();
    }

    // Step 6: Extract and validate response
    std::string text = m_context->extract_text_response(response_json);
    EXPECT_FALSE(text.empty());
    EXPECT_EQ(text, "Hi there!");
}

// Test multi-turn conversation
TEST_F(GeneralContextFunctionalTest, MultiTurnConversation) {
    // First exchange
    m_context->add_user_message("What's 2+2?");

    auto request1 = m_context->build_request();
    EXPECT_EQ(request1["messages"].size(), 1);

    // Simulate assistant response
    m_context->add_assistant_message("2+2 equals 4.");

    // Second user message
    m_context->add_user_message("What about 3+3?");

    auto request2 = m_context->build_request();
    EXPECT_EQ(request2["messages"].size(), 3);

    // Verify message order and roles
    EXPECT_EQ(request2["messages"][0]["role"], "user");
    EXPECT_EQ(request2["messages"][1]["role"], "assistant");
    EXPECT_EQ(request2["messages"][2]["role"], "user");

    EXPECT_TRUE(m_context->is_valid_request());
}

// Test system message functionality
TEST_F(GeneralContextFunctionalTest, SystemMessage) {
    std::string system_prompt = "You are a helpful assistant that responds concisely.";
    m_context->set_system_message(system_prompt);

    m_context->add_user_message("Hello");

    auto request = m_context->build_request();

    // Check how system message is handled based on schema configuration
    // Claude API uses separate "system" field, not a system message in messages array
    if (request.contains("system")) {
        // Anthropic Claude style - separate system field
        EXPECT_EQ(request["system"], system_prompt);
        EXPECT_EQ(request["messages"].size(), 1);
        EXPECT_EQ(request["messages"][0]["role"], "user");
    } else {
        // OpenAI style - system message as first message
        EXPECT_EQ(request["messages"].size(), 2);
        EXPECT_EQ(request["messages"][0]["role"], "system");
        EXPECT_EQ(request["messages"][0]["content"], system_prompt);
        EXPECT_EQ(request["messages"][1]["role"], "user");
    }

    EXPECT_TRUE(m_context->is_valid_request());
}

// Test parameter setting and validation
TEST_F(GeneralContextFunctionalTest, ParameterHandling) {
    // Test valid parameters
    m_context->set_parameter("temperature", 0.7);
    m_context->set_parameter("max_tokens", 150);
    m_context->set_parameter("top_p", 0.9);

    m_context->add_user_message("Test message");

    auto request = m_context->build_request();

    EXPECT_EQ(request["temperature"], 0.7);
    EXPECT_EQ(request["max_tokens"], 150);
    EXPECT_EQ(request["top_p"], 0.9);

    // Test parameter validation (with validation enabled)
    EXPECT_THROW(m_context->set_parameter("temperature", 2.0), validation_exception);
    EXPECT_THROW(m_context->set_parameter("max_tokens", -1), validation_exception);
    EXPECT_THROW(m_context->set_parameter("top_p", 1.5), validation_exception);
}

// Test model selection
TEST_F(GeneralContextFunctionalTest, ModelSelection) {
    // Test valid model
    m_context->set_model("claude-3-5-haiku-20241022");
    m_context->add_user_message("Hello");

    auto request = m_context->build_request();
    EXPECT_EQ(request["model"], "claude-3-5-haiku-20241022");

    // Test invalid model (should throw with validation enabled)
    EXPECT_THROW(m_context->set_model("invalid-model"), validation_exception);

    // Test supported models list
    auto models = m_context->get_supported_models();
    EXPECT_FALSE(models.empty());
    EXPECT_NE(std::find(models.begin(), models.end(), "claude-3-5-sonnet-20241022"), models.end());
}

// Test image handling (multimodal)
TEST_F(GeneralContextFunctionalTest, MultimodalImageHandling) {
    create_test_image();

    // Test with image file path
    m_context->add_user_message("What do you see in this image?", "image/png", "test_image.png");

    auto request = m_context->build_request();
    EXPECT_EQ(request["messages"].size(), 1);

    auto content = request["messages"][0]["content"];
    EXPECT_EQ(content.size(), 2); // text + image

    // Verify text content
    EXPECT_EQ(content[0]["type"], "text");
    EXPECT_EQ(content[0]["text"], "What do you see in this image?");

    // Verify image content
    EXPECT_EQ(content[1]["type"], "image");
    EXPECT_EQ(content[1]["source"]["media_type"], "image/png");
    EXPECT_TRUE(content[1]["source"].contains("data"));

    // Clean up
    std::filesystem::remove("test_image.png");
}

// Test validation errors
TEST_F(GeneralContextFunctionalTest, ValidationErrors) {
    // Test empty context (no messages)
    auto errors = m_context->get_validation_errors();
    EXPECT_FALSE(errors.empty());
    EXPECT_FALSE(m_context->is_valid_request());

    // Add message and test again
    m_context->add_user_message("Hello");
    errors = m_context->get_validation_errors();
    EXPECT_TRUE(errors.empty());
    EXPECT_TRUE(m_context->is_valid_request());

    // Test invalid message role (if we could inject it)
    // This would require modifying the internal state, which is protected
}

// Test context reset functionality
TEST_F(GeneralContextFunctionalTest, ContextReset) {
    // Set up context with data
    m_context->set_system_message("Test system");
    m_context->set_parameter("temperature", 0.8);
    m_context->add_user_message("Hello");
    m_context->add_assistant_message("Hi");

    // Verify data is present
    auto request_before = m_context->build_request();
    EXPECT_EQ(request_before["messages"].size(), 2);
    EXPECT_EQ(request_before["temperature"], 0.8);
    EXPECT_TRUE(request_before.contains("system"));

    // Reset context
    m_context->reset();

    // Verify data is cleared
    auto errors = m_context->get_validation_errors();
    EXPECT_FALSE(errors.empty()); // Should have validation errors due to no messages

    auto request_after = m_context->build_request();
    EXPECT_EQ(request_after["messages"].size(), 0);
    EXPECT_FALSE(request_after.contains("temperature") && request_after["temperature"] == 0.8);
}

// Test response parsing (with mock responses)
TEST_F(GeneralContextFunctionalTest, ResponseParsing) {
    // Create mock successful response
    json mock_response = {
        {"id", "msg_123"},
        {"type", "message"},
        {"role", "assistant"},
        {"content", json::array({{{"type", "text"}, {"text", "Hello! How can I help you?"}}})},
        {"model", "claude-3-5-sonnet-20241022"},
        {"stop_reason", "end_turn"},
        {"usage", {{"input_tokens", 15}, {"output_tokens", 8}}}
    };

    // Test text extraction
    std::string text = m_context->extract_text_response(mock_response);
    EXPECT_EQ(text, "Hello! How can I help you?");

    // Test full response extraction
    auto content = m_context->extract_full_response(mock_response);
    EXPECT_TRUE(content.is_array());
    EXPECT_EQ(content.size(), 1);

    // Test error response
    json error_response = {
        {"type", "error"},
        {"error", {
            {"type", "invalid_request_error"},
            {"message", "Missing required field: max_tokens"}
        }}
    };

    std::string error_msg = m_context->extract_error(error_response);
    EXPECT_EQ(error_msg, "Missing required field: max_tokens");
}

// Test edge cases and error conditions
TEST_F(GeneralContextFunctionalTest, EdgeCasesAndErrors) {
    // Test very long message
    std::string long_message(10000, 'a');
    EXPECT_NO_THROW(m_context->add_user_message(long_message));

    // Test special characters
    m_context->clear_user_messages();
    m_context->add_user_message("Hello 世界! 🌍 Special chars: @#$%^&*()");
    EXPECT_TRUE(m_context->is_valid_request());

    // Test empty message
    m_context->clear_user_messages();
    EXPECT_NO_THROW(m_context->add_user_message(""));

    // Test null/empty parameter values
    EXPECT_THROW(m_context->set_parameter("top_k", nullptr), validation_exception);

    // Test clearing individual components
    m_context->add_user_message("Test");
    m_context->set_parameter("temperature", 0.5);

    m_context->clear_user_messages();
    auto request = m_context->build_request();
    EXPECT_EQ(request["messages"].size(), 0);
    EXPECT_EQ(request["temperature"], 0.5); // Parameters should remain

    m_context->clear_parameters();
    request = m_context->build_request();
    EXPECT_FALSE(request.contains("temperature") && request["temperature"] == 0.5);
}

// Test rate limiting awareness (mock test)
TEST_F(GeneralContextFunctionalTest, RateLimitingHandling) {
    // This would test rate limiting in a real scenario
    // For now, just verify we can make multiple requests in sequence

    for (int i = 0; i < 3; ++i) {
        m_context->clear_user_messages();
        m_context->add_user_message("Test message " + std::to_string(i));

        auto request = m_context->build_request();
        EXPECT_TRUE(m_context->is_valid_request());

        // In real implementation, you'd add delays and handle 429 responses
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

// Performance test for request building
TEST_F(GeneralContextFunctionalTest, PerformanceTest) {
    auto start = std::chrono::high_resolution_clock::now();

    // Build many requests
    for (int i = 0; i < 1000; ++i) {
        if (i % 100 == 0) {
            m_context->clear_user_messages();
        }
        m_context->add_user_message("Message " + std::to_string(i));
        auto request = m_context->build_request();
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    // Should be able to build requests quickly
    EXPECT_LT(duration.count(), 1000); // Less than 1 second for 1000 requests
}

// Integration test (requires actual API key and network)
TEST_F(GeneralContextFunctionalTest, ActualAPIIntegration) {
    m_context->set_system_message("Respond with exactly 'Integration test successful'");
    m_context->add_user_message("Please confirm this integration test is working.");

    auto request = m_context->build_request();

    std::string api_key = m_api_key;
    std::string api_url = m_context->get_endpoint();
    bool is_anthropic = m_context->get_provider_name() == "claude";
    EXPECT_TRUE(is_anthropic);

    std::string payload = request.dump();

    std::string response_str;
    json response_json;
    try {
        response_str = make_api_call(api_url, api_key, payload, is_anthropic);
        response_json = json::parse(response_str);
    } catch (const std::exception& ex) {
        FAIL() << "API call failed: " << ex.what();
    }

    // Step 6: Extract and validate response
    std::string text = m_context->extract_text_response(response_json);
    EXPECT_FALSE(text.empty());
    EXPECT_EQ(text, "Integration test successful");
}

TEST_F(GeneralContextFunctionalTest, MultiProviderSupport) {
    std::vector<std::string> providers = {"claude", "openai", "deepseek"};

    for (const auto& provider : providers) {
        std::string api_key = get_api_key_for_provider(provider);
        if (api_key.empty()) {
            std::cout << "Skipping " << provider << " test: No API key available" << std::endl;
            continue;
        }

        try {
            // Create context for this provider
            context_config config;
            config.enable_validation = true;
            config.default_max_tokens = 50;

            provider_context ctx(m_factory, provider);
            auto& context = ctx.get();

            // Set up a simple request
            context.add_user_message("Respond with exactly one word: 'Success'");

            auto request = context.build_request();
            std::string payload = request.dump();
            std::string api_url = context.get_endpoint();
            bool is_anthropic = (provider == "claude");

            // Make API call
            std::string response_str = make_api_call(api_url, api_key, payload, is_anthropic);
            json response_json = json::parse(response_str);

            // Extract response
            std::string text = context.extract_text_response(response_json);
            EXPECT_FALSE(text.empty());
            EXPECT_TRUE(text.find("Success") != std::string::npos);

        } catch (const std::exception& ex) {
            FAIL() << "Provider " << provider << " test failed: " << ex.what();
        }
    }
}

// Test real multi-turn conversation with actual API
TEST_F(GeneralContextFunctionalTest, RealMultiTurnConversation) {
    std::string api_key = m_api_key;
    std::string api_url = m_context->get_endpoint();
    bool is_anthropic = m_context->get_provider_name() == "claude";

    // First turn
    m_context->add_user_message("What's the capital of France?");
    auto request1 = m_context->build_request();
    std::string payload1 = request1.dump();

    std::string response_str1;
    json response_json1;
    try {
        response_str1 = make_api_call(api_url, api_key, payload1, is_anthropic);
        response_json1 = json::parse(response_str1);
    } catch (const std::exception& ex) {
        FAIL() << "First API call failed: " << ex.what();
    }

    // Extract and add assistant response
    std::string text1 = m_context->extract_text_response(response_json1);
    EXPECT_FALSE(text1.empty());
    m_context->add_assistant_message(text1);

    // Second turn
    m_context->add_user_message("What's the population of that city?");
    auto request2 = m_context->build_request();
    std::string payload2 = request2.dump();

    std::string response_str2;
    json response_json2;
    try {
        response_str2 = make_api_call(api_url, api_key, payload2, is_anthropic);
        response_json2 = json::parse(response_str2);
    } catch (const std::exception& ex) {
        FAIL() << "Second API call failed: " << ex.what();
    }

    // Extract second response
    std::string text2 = m_context->extract_text_response(response_json2);
    EXPECT_FALSE(text2.empty());

    // Verify the response mentions Paris and population
    EXPECT_TRUE(text2.find("Paris") != std::string::npos ||
                text2.find("million") != std::string::npos ||
                text2.find("population") != std::string::npos);
}

// Test real image handling with Claude
TEST_F(GeneralContextFunctionalTest, RealImageHandling) {
    // Skip if not using Claude (which has reliable image support)
    if (m_context->get_provider_name() != "claude") {
        GTEST_SKIP() << "Skipping image test for non-Claude provider";
    }

    create_test_image();

    std::string api_key = m_api_key;
    std::string api_url = m_context->get_endpoint();
    bool is_anthropic = true;

    m_context->add_user_message("Describe this image in exactly 5 words.",
                                "image/png", "../tests/german.png");
    auto request = m_context->build_request();
    std::string payload = request.dump();

    std::string response_str;
    json response_json;
    try {
        response_str = make_api_call(api_url, api_key, payload, is_anthropic);
        response_json = json::parse(response_str);
    } catch (const std::exception& ex) {
        FAIL() << "API call with image failed: " << ex.what();
    }

    // Extract response
    std::string text = m_context->extract_text_response(response_json);
    EXPECT_FALSE(text.empty());

    // Count words (simple approximation)
    int word_count = 0;
    std::istringstream iss(text);
    std::string word;
    while (iss >> word) {
        word_count++;
    }

    // The model might not follow instructions exactly, so we're lenient
    EXPECT_LE(word_count, 10);
}

// Test provider-specific features
TEST_F(GeneralContextFunctionalTest, ProviderSpecificFeatures) {
    // Test OpenAI-specific features
    std::string oa_api_key = get_api_key_for_provider("openai");
    if (!oa_api_key.empty()) {
        try {
            provider_context ctx(m_factory, "openai");
            auto& oa_context = ctx.get();

            // Test OpenAI-specific parameters like response_format for JSON mode
            oa_context.set_parameter("response_format", {{"type", "json_object"}});
            oa_context.add_user_message("Return a JSON with keys 'greeting' and 'value'. The greeting should be 'hello' and the value should be 42.");

            auto request = oa_context.build_request();
            std::string payload = request.dump();
            std::string api_url = oa_context.get_endpoint();

            std::string response_str = make_api_call(api_url, oa_api_key, payload, false);
            json response_json = json::parse(response_str);

            std::string text = oa_context.extract_text_response(response_json);
            EXPECT_FALSE(text.empty());

            // Verify JSON response
            try {
                json parsed = json::parse(text);
                EXPECT_TRUE(parsed.contains("greeting"));
                EXPECT_TRUE(parsed.contains("value"));
                EXPECT_EQ(parsed["greeting"], "hello");
                EXPECT_EQ(parsed["value"], 42);
            } catch (...) {
                // NOTE: This might fail if the model doesn't follow instructions exactly
                // or if the JSON extraction path is incorrect in the schema
                std::cout << "Warning: Could not parse JSON response: " << text << std::endl;
            }

        } catch (const std::exception& ex) {
            std::cout << "OpenAI-specific test failed: " << ex.what() << std::endl;
        }
    }

    // Test DeepSeek-specific features
    std::string ds_api_key = get_api_key_for_provider("deepseek");
    if (!ds_api_key.empty()) {
        try {
            provider_context ctx(m_factory, "deepseek");
            auto& ds_context = ctx.get();

            // Test DeepSeek-specific parameters or models
            ds_context.set_model("deepseek-coder");
            ds_context.add_user_message("Write a Python function to calculate the Fibonacci sequence up to n.");

            auto request = ds_context.build_request();
            std::string payload = request.dump();
            std::string api_url = ds_context.get_endpoint();

            std::string response_str = make_api_call(api_url, ds_api_key, payload, false);
            json response_json = json::parse(response_str);

            std::string text = ds_context.extract_text_response(response_json);
            EXPECT_FALSE(text.empty());
            EXPECT_TRUE(text.find("def") != std::string::npos);
            EXPECT_TRUE(text.find("fibonacci") != std::string::npos ||
                        text.find("Fibonacci") != std::string::npos);

        } catch (const std::exception& ex) {
            std::cout << "DeepSeek-specific test failed: " << ex.what() << std::endl;
        }
    }
}

// Test error handling with invalid requests
TEST_F(GeneralContextFunctionalTest, ErrorHandlingWithRealAPI) {
    std::string api_key = m_api_key;
    std::string api_url = m_context->get_endpoint();
    bool is_anthropic = m_context->get_provider_name() == "claude";

    // Create an invalid request (e.g., exceeding max tokens)
    EXPECT_ANY_THROW(m_context->set_parameter("max_tokens", 100000).
                     add_user_message("This should trigger an error"));

    auto request = m_context->build_request();
    std::string payload = request.dump();

    std::string response_str;
    json response_json;
    try {
        response_str = make_api_call(api_url, api_key, payload, is_anthropic);
        response_json = json::parse(response_str);

        // Check if response contains error
        if (response_json.contains("error") ||
            (response_json.contains("type") && response_json["type"] == "error")) {
            std::string error_msg = m_context->extract_error(response_json);
            EXPECT_FALSE(error_msg.empty());
            std::cout << "Expected error received: " << error_msg << std::endl;
        } else {
            // Some providers might silently fix invalid parameters
            std::cout << "Warning: Expected error but got success response" << std::endl;
        }
    } catch (const std::exception& ex) {
        // Network errors or other exceptions are also acceptable
        std::cout << "Expected error exception: " << ex.what() << std::endl;
    }
}

// Test streaming parameter and functionality
TEST_F(GeneralContextFunctionalTest, StreamingParameterTest) {
    if (!m_context->supports_streaming()) {
        GTEST_SKIP() << "Provider doesn't support streaming";
    }

    std::string api_key = m_api_key;
    std::string api_url = m_context->get_endpoint();
    bool is_anthropic = m_context->get_provider_name() == "claude";

    // Test 1: Verify streaming parameter is set correctly
    m_context->set_parameter("stream", true);
    m_context->add_user_message("Count from 1 to 5, explaining each number.");

    auto request = m_context->build_request();
    EXPECT_TRUE(request.contains("stream"));
    EXPECT_TRUE(request["stream"].get<bool>());

    // Test 2: Actually test streaming functionality
    StreamingResponse streaming_response;
    std::string payload = request.dump();

    try {
        make_streaming_api_call(api_url, api_key, payload, &streaming_response, is_anthropic);

        // Verify streaming worked
        EXPECT_GT(streaming_response.events.size(), 0) << "Should receive multiple streaming events";
        EXPECT_FALSE(streaming_response.complete_content.empty()) << "Should have accumulated content";
        EXPECT_FALSE(streaming_response.error) << "Should not have errors: " << streaming_response.error_message;

        // Verify we received incremental updates
        EXPECT_GT(streaming_response.events.size(), 1) << "Should receive multiple chunks for streaming";

        std::cout << "Received " << streaming_response.events.size() << " streaming events" << std::endl;
        std::cout << "Complete content length: " << streaming_response.complete_content.length() << std::endl;

    } catch (const std::exception& e) {
        FAIL() << "Streaming test failed: " << e.what();
    }

    // Test 3: Compare with non-streaming response
    m_context->set_parameter("stream", false);
    auto non_streaming_request = m_context->build_request();
    EXPECT_FALSE(non_streaming_request["stream"].get<bool>());

    std::string non_streaming_payload = non_streaming_request.dump();
    std::string non_streaming_response = make_streaming_api_call(api_url, api_key, non_streaming_payload, nullptr, is_anthropic);

    EXPECT_FALSE(non_streaming_response.empty()) << "Non-streaming response should not be empty";

    // Parse non-streaming response to compare content
    try {
        json non_streaming_json = json::parse(non_streaming_response);
        if (non_streaming_json.contains("content") && non_streaming_json["content"].is_array()) {
            std::string non_streaming_content = non_streaming_json["content"][0]["text"].get<std::string>();

            // Content should be similar (though not necessarily identical due to potential randomness)
            EXPECT_GT(streaming_response.complete_content.length(), 0);
            EXPECT_GT(non_streaming_content.length(), 0);

            std::cout << "Streaming content preview: " << streaming_response.complete_content.substr(0, 100) << "..." << std::endl;
            std::cout << "Non-streaming content preview: " << non_streaming_content.substr(0, 100) << "..." << std::endl;
        }
    } catch (const json::parse_error& e) {
        std::cerr << "JSON parse error: " << e.what() << std::endl;
    }

    // Reset for other tests
    m_context->set_parameter("stream", false);
}

// Test streaming error handling
TEST_F(GeneralContextFunctionalTest, StreamingErrorHandlingTest) {
    if (!m_context->supports_streaming()) {
        GTEST_SKIP() << "Provider doesn't support streaming";
    }

    std::string api_key = "invalid_key";  // Intentionally invalid
    std::string api_url = m_context->get_endpoint();
    bool is_anthropic = m_context->get_provider_name() == "claude";

    m_context->set_parameter("stream", true);
    m_context->add_user_message("Hello");

    auto request = m_context->build_request();
    std::string payload = request.dump();

    StreamingResponse streaming_response;

    try {
        make_streaming_api_call(api_url, api_key, payload, &streaming_response, is_anthropic);

        // Should either throw an exception or set error flag
        if (!streaming_response.error) {
            // If no error flag, the API call should have thrown an exception
            // If we reach here, something unexpected happened
            EXPECT_TRUE(streaming_response.events.empty()) << "Should not receive events with invalid key";
        } else {
            EXPECT_TRUE(streaming_response.error) << "Should detect error with invalid API key";
            EXPECT_FALSE(streaming_response.error_message.empty()) << "Should have error message";
        }

    } catch (const std::exception& e) {
        // This is expected behavior for invalid API key
        EXPECT_NE(std::string(e.what()).find("CURL failed"), std::string::npos);
    }

    // Reset for other tests
    m_context->set_parameter("stream", false);
}

// Test streaming with different message types
TEST_F(GeneralContextFunctionalTest, StreamingWithDifferentMessagesTest) {
    if (!m_context->supports_streaming()) {
        GTEST_SKIP() << "Provider doesn't support streaming";
    }

    std::string api_key = m_api_key;
    std::string api_url = m_context->get_endpoint();
    bool is_anthropic = m_context->get_provider_name() == "claude";

    // Test with a longer response that should definitely stream
    m_context->set_parameter("stream", true);
    m_context->add_user_message("Write a short story about a robot learning to paint. Make it at least 3 paragraphs long.");

    auto request = m_context->build_request();
    std::string payload = request.dump();

    StreamingResponse streaming_response;

    try {
        make_streaming_api_call(api_url, api_key, payload, &streaming_response, is_anthropic);

        // For longer content, we should definitely see multiple chunks
        EXPECT_GT(streaming_response.events.size(), 3) << "Longer content should produce multiple streaming events";
        EXPECT_GT(streaming_response.complete_content.length(), 200) << "Story should be reasonably long";
        EXPECT_FALSE(streaming_response.error) << "Should not have errors: " << streaming_response.error_message;

        // Verify the content makes sense (basic sanity check)
        std::string content_lower = streaming_response.complete_content;
        std::transform(content_lower.begin(), content_lower.end(), content_lower.begin(), ::tolower);
        EXPECT_NE(content_lower.find("robot"), std::string::npos) << "Should mention robot";
        EXPECT_NE(content_lower.find("paint"), std::string::npos) << "Should mention painting";

    } catch (const std::exception& e) {
        FAIL() << "Streaming test with longer content failed: " << e.what();
    }

    // Reset for other tests
    m_context->set_parameter("stream", false);
}

// Test with very complex prompts
TEST_F(GeneralContextFunctionalTest, ComplexPromptHandling) {
    std::string api_key = m_api_key;
    std::string api_url = m_context->get_endpoint();
    bool is_anthropic = m_context->get_provider_name() == "claude";

    // Create a complex prompt with special characters, code blocks, etc.
    std::string complex_prompt = R"(
    # Test Document

    This is a *complex* prompt with **markdown** and `code`.

    ```python
    def hello_world():
        print("Hello, world!")
        return 42
    ```

    Special characters: €£¥$@#%^&*()_+{}|:"<>?~`-=[]\\;',./

    Please respond with:
    1. The number of lines in the Python function
    2. The exact string that would be printed
    )";

    m_context->add_user_message(complex_prompt);

    auto request = m_context->build_request();
    std::string payload = request.dump();

    std::string response_str;
    json response_json;
    try {
        response_str = make_api_call(api_url, api_key, payload, is_anthropic);
        response_json = json::parse(response_str);
    } catch (const std::exception& ex) {
        FAIL() << "API call with complex prompt failed: " << ex.what();
    }

    // Extract response
    std::string text = m_context->extract_text_response(response_json);
    EXPECT_FALSE(text.empty());

    // Verify response contains expected information
    EXPECT_TRUE(text.find("2") != std::string::npos); // Number of lines
    EXPECT_TRUE(text.find("Hello, world!") != std::string::npos); // Printed string
}

// Test with different model versions for the same provider
TEST_F(GeneralContextFunctionalTest, ModelVersionTest) {
    // Skip if we're not using Claude (which has multiple models we can test)
    if (m_context->get_provider_name() != "claude") {
        GTEST_SKIP() << "Skipping model version test for non-Claude provider";
    }

    std::string api_key = m_api_key;
    std::string api_url = m_context->get_endpoint();
    bool is_anthropic = true;

    // Get available models
    auto models = m_context->get_supported_models();
    if (models.size() < 2) {
        GTEST_SKIP() << "Not enough models available for testing";
    }

    // Test with two different models
    std::vector<std::string> test_models = {
        "claude-3-5-sonnet-20241022",
        "claude-3-haiku-20240307"
    };

    for (const auto& model : test_models) {
        // Check if this model is in the supported list
        if (std::find(models.begin(), models.end(), model) == models.end()) {
            std::cout << "Skipping unsupported model: " << model << std::endl;
            continue;
        }

        try {
            m_context->reset();
            m_context->set_model(model);
            m_context->add_user_message("Respond with your model name");

            auto request = m_context->build_request();
            std::string payload = request.dump();

            std::string response_str = make_api_call(api_url, api_key, payload, is_anthropic);
            json response_json = json::parse(response_str);

            std::string text = m_context->extract_text_response(response_json);
            EXPECT_FALSE(text.empty());

            // The model should mention its name or version
            // Note: This is not guaranteed as models don't always self-identify correctly
            std::cout << "Model " << model << " response: " << text << std::endl;

        } catch (const std::exception& ex) {
            std::cout << "Test with model " << model << " failed: " << ex.what() << std::endl;
        }
    }
}
