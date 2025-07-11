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

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "../src/general_context.h"
#include "../src/schema_registry.h"
#include "../src/context_factory.h"
#include "../src/config.h"
#include <nlohmann/json.hpp>
#include <curl/curl.h>
#include <filesystem>
#include <fstream>
#include <ctime>
#include <iomanip>
#include <chrono>
#include <numeric>
#include <thread>
#include <sstream>

using namespace hyni;
using json = nlohmann::json;
namespace fs = std::filesystem;

// Response structure for Claude streaming
struct ClaudeStreamResponse {
    std::vector<json> events;
    std::string complete_text;
    bool finished = false;
    std::string stop_reason;
    json usage;
    std::string error_message;
};

// Write callback for CURL
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

// Streaming callback for Claude's SSE format
static size_t claude_stream_callback(void* contents, size_t size, size_t nmemb,
                                     ClaudeStreamResponse* response) {
    if (!response) return 0;

    size_t total_size = size * nmemb;
    std::string chunk(static_cast<char*>(contents), total_size);

    std::istringstream stream(chunk);
    std::string line;

    while (std::getline(stream, line)) {
        if (line.find("event: ") == 0) {
            std::string event_type = line.substr(7);

            // Get the data line
            if (std::getline(stream, line) && line.find("data: ") == 0) {
                std::string json_data = line.substr(6);

                try {
                    json event_json = json::parse(json_data);
                    event_json["_event_type"] = event_type; // Add event type for processing
                    response->events.push_back(event_json);

                    // Process different event types
                    if (event_type == "content_block_delta") {
                        if (event_json.contains("delta") && event_json["delta"].contains("text")) {
                            response->complete_text += event_json["delta"]["text"].get<std::string>();
                        }
                    } else if (event_type == "message_stop") {
                        response->finished = true;
                    } else if (event_type == "message_delta") {
                        if (event_json.contains("delta") && event_json["delta"].contains("stop_reason")) {
                            response->stop_reason = event_json["delta"]["stop_reason"];
                        }
                        if (event_json.contains("usage")) {
                            response->usage = event_json["usage"];
                        }
                    }

                } catch (const json::parse_error& e) {
                    // Not all lines are valid JSON
                }
            }
        }
    }

    return total_size;
}

class ClaudeIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Get API key from environment or config
        const char* api_key = std::getenv("CL_API_KEY");
        if (!api_key) {
            fs::path rc_path = fs::path(std::getenv("HOME")) / ".hynirc";
            if (fs::exists(rc_path)) {
                auto config = parse_hynirc(rc_path.string());
                auto it = config.find("CL_API_KEY");
                if (it != config.end()) {
                    m_api_key = it->second;
                }
            }
        } else {
            m_api_key = api_key;
        }

        if (m_api_key.empty()) {
            GTEST_SKIP() << "CL_API_KEY not set. Skipping Claude integration tests.";
        }

        // Create schema registry and factory
        m_registry = schema_registry::create()
                        .set_schema_directory("../schemas")
                        .build();
        m_factory = std::make_shared<context_factory>(m_registry);

        // Create Claude context
        context_config config;
        config.enable_validation = true;
        config.default_max_tokens = 100;  // Keep costs low for tests
        config.default_temperature = 0.0; // Deterministic responses

        m_context = m_factory->create_context("claude", config);
        m_context->set_api_key(m_api_key);
    }

    void TearDown() override {
        // Clean up test files
        if (fs::exists("test_image.png")) {
            fs::remove("test_image.png");
        }
    }

    // Helper function to make API call
    json make_claude_call(const json& request) {
        CURL* curl = curl_easy_init();
        if (!curl) throw std::runtime_error("Failed to init CURL");

        std::string response_str;
        struct curl_slist* headers = nullptr;

        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, ("x-api-key: " + m_api_key).c_str());
        headers = curl_slist_append(headers, "anthropic-version: 2023-06-01");

        std::string payload = request.dump();

        curl_easy_setopt(curl, CURLOPT_URL, "https://api.anthropic.com/v1/messages");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_str);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

        CURLcode res = curl_easy_perform(curl);

        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK) {
            throw std::runtime_error("CURL error: " + std::string(curl_easy_strerror(res)));
        }

        json response = json::parse(response_str);

        // Check for API errors
        if (http_code >= 400) {
            std::string error_msg = "API Error (HTTP " + std::to_string(http_code) + ")";
            if (response.contains("error") && response["error"].contains("message")) {
                error_msg += ": " + response["error"]["message"].get<std::string>();
            }
            throw std::runtime_error(error_msg);
        }

        return response;
    }

    // Helper function for streaming API call
    ClaudeStreamResponse make_claude_stream_call(const json& request) {
        CURL* curl = curl_easy_init();
        if (!curl) throw std::runtime_error("Failed to init CURL");

        ClaudeStreamResponse response;
        struct curl_slist* headers = nullptr;

        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, ("x-api-key: " + m_api_key).c_str());
        headers = curl_slist_append(headers, "anthropic-version: 2023-06-01");
        headers = curl_slist_append(headers, "Accept: text/event-stream");

        std::string payload = request.dump();

        curl_easy_setopt(curl, CURLOPT_URL, "https://api.anthropic.com/v1/messages");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, claude_stream_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

        CURLcode res = curl_easy_perform(curl);

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK) {
            throw std::runtime_error("CURL error: " + std::string(curl_easy_strerror(res)));
        }

        return response;
    }

    void create_test_image() {
        // Create a small red square PNG (10x10 pixels)
        const unsigned char png_data[] = {
            0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D,
            0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x0A,
            0x08, 0x02, 0x00, 0x00, 0x00, 0x02, 0x50, 0x58, 0xEA, 0x00, 0x00, 0x00,
            0x1D, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x62, 0xF8, 0xCF, 0xC0, 0x00,
            0x00, 0x03, 0x03, 0x03, 0x03, 0x18, 0x30, 0x60, 0xC0, 0x80, 0x01, 0x03,
            0x06, 0x0C, 0x18, 0x30, 0x00, 0x00, 0x1E, 0x1E, 0x03, 0x03, 0x3C, 0xAF,
            0x8C, 0x5D, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42,
            0x60, 0x82
        };

        std::ofstream file("test_image.png", std::ios::binary);
        file.write(reinterpret_cast<const char*>(png_data), sizeof(png_data));
        file.close();
    }

    std::string m_api_key;
    std::shared_ptr<schema_registry> m_registry;
    std::shared_ptr<context_factory> m_factory;
    std::unique_ptr<general_context> m_context;
};

// Test basic text-only prompt
TEST_F(ClaudeIntegrationTest, BasicTextPrompt) {
    m_context->set_model("claude-3-haiku-20240307")
             .add_user_message("Reply with exactly: 'Hello from Claude'");

    auto request = m_context->build_request();

    // Verify request structure
    EXPECT_EQ(request["model"], "claude-3-haiku-20240307");
    EXPECT_FALSE(request["messages"].empty());
    EXPECT_TRUE(request.contains("max_tokens")); // Required for Claude

    // Make API call
    json response;
    ASSERT_NO_THROW(response = make_claude_call(request));

    // Verify response structure
    EXPECT_TRUE(response.contains("id"));
    EXPECT_TRUE(response.contains("type"));
    EXPECT_EQ(response["type"], "message");
    EXPECT_TRUE(response.contains("role"));
    EXPECT_EQ(response["role"], "assistant");
    EXPECT_TRUE(response.contains("content"));
    EXPECT_TRUE(response.contains("model"));
    EXPECT_TRUE(response.contains("stop_reason"));

    // Extract text using context method
    std::string text = m_context->extract_text_response(response);
    EXPECT_FALSE(text.empty());
    EXPECT_EQ(text, "Hello from Claude");
}

// Test system message functionality
TEST_F(ClaudeIntegrationTest, SystemMessage) {
    m_context->set_model("claude-3-haiku-20240307")
             .set_system_message("You are a calculator. Only respond with numbers.")
             .add_user_message("What is 2 + 2?");

    auto request = m_context->build_request();

    // Verify system message in request - Claude uses separate field
    EXPECT_TRUE(request.contains("system"));
    EXPECT_EQ(request["system"], "You are a calculator. Only respond with numbers.");
    EXPECT_EQ(request["messages"].size(), 1);

    json response;
    ASSERT_NO_THROW(response = make_claude_call(request));

    std::string text = m_context->extract_text_response(response);
    EXPECT_FALSE(text.empty());
    EXPECT_TRUE(text.find("4") != std::string::npos);
}

// Test multi-turn conversation
TEST_F(ClaudeIntegrationTest, MultiTurnConversation) {
    m_context->set_model("claude-3-haiku-20240307");

    // First turn
    m_context->add_user_message("My name is TestBot. What's my name?");

    auto request1 = m_context->build_request();
    json response1;
    ASSERT_NO_THROW(response1 = make_claude_call(request1));

    std::string text1 = m_context->extract_text_response(response1);
    EXPECT_TRUE(text1.find("TestBot") != std::string::npos);

    // Add assistant response and continue conversation
    m_context->add_assistant_message(text1)
             .add_user_message("What did I just tell you my name was?");

    auto request2 = m_context->build_request();
    EXPECT_EQ(request2["messages"].size(), 3);

    json response2;
    ASSERT_NO_THROW(response2 = make_claude_call(request2));

    std::string text2 = m_context->extract_text_response(response2);
    EXPECT_TRUE(text2.find("TestBot") != std::string::npos);
}

// Test with different models
TEST_F(ClaudeIntegrationTest, DifferentModels) {
    std::vector<std::string> models_to_test = {
        "claude-3-haiku-20240307",
        "claude-3-5-sonnet-20241022"
    };

    for (const auto& model : models_to_test) {
        m_context->reset();

        try {
            m_context->set_model(model)
                     .add_user_message("Reply with your model name");

            auto request = m_context->build_request();
            EXPECT_EQ(request["model"], model);

            json response = make_claude_call(request);
            std::string text = m_context->extract_text_response(response);
            EXPECT_FALSE(text.empty());

            std::cout << "Model " << model << " responded: "
                     << text.substr(0, 50) << "..." << std::endl;

        } catch (const std::exception& e) {
            std::cout << "Model " << model << " test skipped: " << e.what() << std::endl;
        }
    }
}

// Test parameter settings
TEST_F(ClaudeIntegrationTest, ParameterSettings) {
    m_context->set_model("claude-3-haiku-20240307")
             .set_parameter("temperature", 0.0)
             .set_parameter("max_tokens", 50)
             .set_parameter("top_p", 0.9)
             .set_parameter("top_k", 10)  // Claude-specific
             .add_user_message("Write exactly 10 words about AI");

    auto request = m_context->build_request();

    // Verify parameters in request
    EXPECT_EQ(request["temperature"], 0.0);
    EXPECT_EQ(request["max_tokens"], 50);
    EXPECT_EQ(request["top_p"], 0.9);
    EXPECT_EQ(request["top_k"], 10);

    json response;
    ASSERT_NO_THROW(response = make_claude_call(request));

    std::string text = m_context->extract_text_response(response);
    EXPECT_FALSE(text.empty());
    EXPECT_LE(text.length(), 200); // Should be limited by max_tokens
}

// Test image input (Vision)
TEST_F(ClaudeIntegrationTest, ImageInput) {
    create_test_image();

    m_context->set_model("claude-3-haiku-20240307")  // Vision-capable model
             .add_user_message("What color is the square in this image? Reply with one word only.",
                              "image/png", "test_image.png");

    auto request = m_context->build_request();

    // Verify multimodal content structure (Claude format)
    EXPECT_EQ(request["messages"][0]["content"].size(), 2);
    EXPECT_EQ(request["messages"][0]["content"][1]["type"], "image");
    EXPECT_EQ(request["messages"][0]["content"][1]["source"]["type"], "base64");

    json response;
    try {
        response = make_claude_call(request);
        std::string text = m_context->extract_text_response(response);
        EXPECT_FALSE(text.empty());

        // Should identify the red color
        std::string text_lower = text;
        std::transform(text_lower.begin(), text_lower.end(), text_lower.begin(), ::tolower);
        EXPECT_TRUE(text_lower.find("red") != std::string::npos);

    } catch (const std::exception& e) {
        std::cout << "Vision test note: " << e.what() << std::endl;
    }
}

// Test streaming functionality
TEST_F(ClaudeIntegrationTest, StreamingResponse) {
    m_context->set_model("claude-3-haiku-20240307")
             .set_parameter("stream", true)
             .add_user_message("Count from 1 to 5");

    auto request = m_context->build_request();
    EXPECT_TRUE(request["stream"].get<bool>());

    ClaudeStreamResponse response;
    ASSERT_NO_THROW(response = make_claude_stream_call(request));

    // Verify streaming worked
    EXPECT_GT(response.events.size(), 1) << "Should receive multiple events";
    EXPECT_FALSE(response.complete_text.empty());
    EXPECT_TRUE(response.finished);

    // Content should contain numbers 1-5
    for (int i = 1; i <= 5; ++i) {
        EXPECT_TRUE(response.complete_text.find(std::to_string(i)) != std::string::npos)
            << "Missing number " << i << " in response";
    }
}

// Test stop sequences
TEST_F(ClaudeIntegrationTest, StopSequences) {
    m_context->set_model("claude-3-haiku-20240307")
             .set_parameter("stop_sequences", json::array({"STOP", "END"}))  // Remove "\n\n"
             .add_user_message("Count from 1 to 10, then write STOP");

    auto request = m_context->build_request();
    EXPECT_TRUE(request["stop_sequences"].is_array());
    EXPECT_EQ(request["stop_sequences"].size(), 2);

    json response;
    ASSERT_NO_THROW(response = make_claude_call(request));

    std::string text = m_context->extract_text_response(response);
    EXPECT_FALSE(text.empty());

    // Check stop reason
    if (response.contains("stop_reason")) {
        std::string stop_reason = response["stop_reason"];
        EXPECT_TRUE(stop_reason == "stop_sequence" ||
                    stop_reason == "end_turn" ||
                    stop_reason == "max_tokens")
            << "Unexpected stop reason: " << stop_reason;
    }

    // Response should stop at or before "STOP"
    size_t stop_pos = text.find("STOP");
    if (stop_pos != std::string::npos) {
        // If STOP is found, it should be at the end
        EXPECT_TRUE(stop_pos == text.length() - 4 ||
                    text.substr(stop_pos + 4).find_first_not_of(" \n\r\t") == std::string::npos)
            << "Text continues after STOP sequence";
    }
}

TEST_F(ClaudeIntegrationTest, StopSequenceVariations) {
    // Test 1: Single word stop sequence
    m_context->reset();
    m_context->set_model("claude-3-haiku-20240307")
             .set_parameter("stop_sequences", json::array({"DONE"}))
             .add_user_message("Count from 1 to 5 then say DONE");

    auto request = m_context->build_request();
    json response = make_claude_call(request);
    std::string text = m_context->extract_text_response(response);

    // Check if stopped at DONE
    if (response["stop_reason"] == "stop_sequence") {
        EXPECT_TRUE(text.find("DONE") == std::string::npos ||
                    text.rfind("DONE") == text.length() - 4)
            << "DONE should be at the end or not present";
    }

    // Test 2: Multiple stop sequences
    m_context->reset();
    m_context->set_model("claude-3-haiku-20240307")
             .set_parameter("stop_sequences", json::array({".", "!", "?"}))
             .add_user_message("Write a sentence");

    request = m_context->build_request();
    response = make_claude_call(request);
    text = m_context->extract_text_response(response);

    // Should stop at first punctuation
    EXPECT_FALSE(text.empty());

    // Test 3: Phrase as stop sequence
    m_context->reset();
    m_context->set_model("claude-3-haiku-20240307")
             .set_parameter("stop_sequences", json::array({"The End"}))
             .add_user_message("Tell a very short story and end with 'The End'");

    request = m_context->build_request();
    response = make_claude_call(request);
    text = m_context->extract_text_response(response);

    if (response["stop_reason"] == "stop_sequence") {
        EXPECT_TRUE(text.find("The End") == std::string::npos ||
                    text.rfind("The End") == text.find_last_not_of(" \n\r\t") - 6)
            << "Stop sequence handling issue";
    }

    // Test 4: Edge case - empty array (should work)
    m_context->reset();
    m_context->set_model("claude-3-haiku-20240307")
             .set_parameter("stop_sequences", json::array())
             .add_user_message("Say hello");

    request = m_context->build_request();
    ASSERT_NO_THROW(response = make_claude_call(request));
    text = m_context->extract_text_response(response);
    EXPECT_FALSE(text.empty());
}

// Test usage tracking
TEST_F(ClaudeIntegrationTest, UsageTracking) {
    m_context->set_model("claude-3-haiku-20240307")
             .add_user_message("Hello");

    auto request = m_context->build_request();
    json response;
    ASSERT_NO_THROW(response = make_claude_call(request));

    // Verify usage information
    EXPECT_TRUE(response.contains("usage"));
    auto usage = response["usage"];
    EXPECT_TRUE(usage.contains("input_tokens"));
    EXPECT_TRUE(usage.contains("output_tokens"));

    EXPECT_GT(usage["input_tokens"].get<int>(), 0);
    EXPECT_GT(usage["output_tokens"].get<int>(), 0);
}

// Test with very long context
TEST_F(ClaudeIntegrationTest, LongContextHandling) {
    // Claude supports up to 200k tokens
    std::string long_text;
    for (int i = 0; i < 100; ++i) {
        long_text += "This is sentence number " + std::to_string(i) + ". ";
    }
    long_text += "\n\nSummarize the above in 5 words.";

    m_context->set_model("claude-3-haiku-20240307")
             .set_parameter("max_tokens", 20)
             .add_user_message(long_text);

    auto request = m_context->build_request();

    json response;
    ASSERT_NO_THROW(response = make_claude_call(request));

    std::string text = m_context->extract_text_response(response);
    EXPECT_FALSE(text.empty());
    EXPECT_LE(text.length(), 100);
}

// Test error handling
TEST_F(ClaudeIntegrationTest, ErrorHandling) {
    // Test 1: Invalid model
    m_context->reset();
    EXPECT_THROW(m_context->set_model("invalid-model-xyz"), validation_exception);

    // Test 2: Invalid temperature for Claude (max 1.0)
    m_context->reset();
    EXPECT_THROW(m_context->set_parameter("temperature", 1.5), validation_exception);

    // Test 3: Missing required max_tokens
    json bad_request = {
        {"model", "claude-3-haiku-20240307"},
        {"messages", json::array({
            {{"role", "user"}, {"content", json::array({
                {{"type", "text"}, {"text", "Hello"}}
            })}}
        })}
        // Missing max_tokens (required for Claude)
    };

    EXPECT_THROW(make_claude_call(bad_request), std::runtime_error);

    // Test 4: Invalid API key
    m_context->reset();
    m_context->set_model("claude-3-haiku-20240307")
             .add_user_message("Hello");

    auto request = m_context->build_request();

    // Make call with invalid key
    CURL* curl = curl_easy_init();
    ASSERT_TRUE(curl);

    std::string response_str;
    struct curl_slist* headers = nullptr;

    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "x-api-key: invalid_key_123");
    headers = curl_slist_append(headers, "anthropic-version: 2023-06-01");

    std::string payload = request.dump();

    curl_easy_setopt(curl, CURLOPT_URL, "https://api.anthropic.com/v1/messages");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_str);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    CURLcode res = curl_easy_perform(curl);
    ASSERT_EQ(res, CURLE_OK);

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    EXPECT_EQ(http_code, 401) << "Should get 401 for invalid API key";
}

// Test Claude-specific features
TEST_F(ClaudeIntegrationTest, ClaudeSpecificFeatures) {
    // Test top_k parameter (Claude-specific)
    m_context->set_model("claude-3-haiku-20240307")
             .set_parameter("top_k", 5)
             .set_parameter("temperature", 0.7)
             .add_user_message("Generate a creative word");

    auto request = m_context->build_request();
    EXPECT_EQ(request["top_k"], 5);

    json response;
    ASSERT_NO_THROW(response = make_claude_call(request));

    std::string text = m_context->extract_text_response(response);
    EXPECT_FALSE(text.empty());
}

// Test beta features
TEST_F(ClaudeIntegrationTest, BetaFeatures) {
    // Test with beta header if needed
    // This would test any beta features when available

    m_context->set_model("claude-3-haiku-20240307")
             .add_user_message("Test message");

    auto request = m_context->build_request();

    // If testing beta features, would add beta header
    // For now, just verify normal operation
    json response;
    ASSERT_NO_THROW(response = make_claude_call(request));

    EXPECT_FALSE(m_context->extract_text_response(response).empty());
}

// Performance test
TEST_F(ClaudeIntegrationTest, PerformanceMetrics) {
    m_context->set_model("claude-3-haiku-20240307")
             .add_user_message("Reply with 'OK'");

    auto request = m_context->build_request();

    auto start = std::chrono::high_resolution_clock::now();
    json response = make_claude_call(request);
    auto end = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    std::cout << "Claude API call took: " << duration.count() << "ms" << std::endl;

    // Verify response time is reasonable
    EXPECT_LT(duration.count(), 10000); // Less than 10 seconds

    // Extract and verify response
    std::string text = m_context->extract_text_response(response);
    EXPECT_FALSE(text.empty());
}

TEST_F(ClaudeIntegrationTest, ValidateAgainstLiveAPI) {
    // Claude doesn't have a models endpoint like OpenAI,
    // but we can validate by attempting to use each model
    auto schema_models = m_context->get_supported_models();

    std::cout << "\n=== Validating Claude Models ===" << std::endl;
    std::cout << "Testing " << schema_models.size() << " models from schema..." << std::endl;

    std::map<std::string, bool> model_status;
    std::map<std::string, std::string> model_errors;

    for (const auto& model : schema_models) {
        m_context->reset();
        m_context->set_model(model)
                 .set_parameter("max_tokens", 5)  // Minimal tokens to reduce cost
                 .add_user_message("Hi");

        auto request = m_context->build_request();

        try {
            json response = make_claude_call(request);
            model_status[model] = true;

            // Extract actual model used (might be different from requested)
            if (response.contains("model")) {
                std::string actual_model = response["model"];
                if (actual_model != model) {
                    std::cout << "  - " << model << ": ✓ (actual: " << actual_model << ")" << std::endl;
                } else {
                    std::cout << "  - " << model << ": ✓" << std::endl;
                }
            }
        } catch (const std::exception& e) {
            model_status[model] = false;
            model_errors[model] = e.what();
            std::cout << "  - " << model << ": ✗ (" << e.what() << ")" << std::endl;
        }
    }

    // Report summary
    int working_models = 0;
    int deprecated_models = 0;

    for (const auto& [model, status] : model_status) {
        if (status) {
            working_models++;
        } else {
            deprecated_models++;
        }
    }

    std::cout << "\nSummary: " << working_models << " working, "
              << deprecated_models << " deprecated/unavailable" << std::endl;

    // Warn if many models are failing
    if (deprecated_models > static_cast<int>(schema_models.size() / 2)) {
        std::cout << "WARNING: More than half of the models in schema are not working. "
                  << "Consider updating the schema." << std::endl;
    }
}

// Test API version compatibility
TEST_F(ClaudeIntegrationTest, APIVersionCompatibility) {
    // Test with current API version
    std::string current_version = "2023-06-01";

    m_context->set_model("claude-3-haiku-20240307")
             .add_user_message("Reply with 'OK'");

    auto request = m_context->build_request();

    // Test with current version
    {
        CURL* curl = curl_easy_init();
        ASSERT_TRUE(curl);

        std::string response_str;
        struct curl_slist* headers = nullptr;

        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, ("x-api-key: " + m_api_key).c_str());
        headers = curl_slist_append(headers, ("anthropic-version: " + current_version).c_str());

        std::string payload = request.dump();

        curl_easy_setopt(curl, CURLOPT_URL, "https://api.anthropic.com/v1/messages");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_str);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

        CURLcode res = curl_easy_perform(curl);

        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        EXPECT_EQ(res, CURLE_OK) << "Request with version " << current_version << " failed";
        EXPECT_EQ(http_code, 200) << "Got HTTP " << http_code << " with version " << current_version;

        if (http_code == 200) {
            std::cout << "API version " << current_version << " is working correctly" << std::endl;
        }
    }

    // Test with older versions to see deprecation
    std::vector<std::string> older_versions = {
        "2023-01-01",  // Older version
        "2022-11-01"   // Much older
    };

    std::cout << "\n=== Testing API Version Compatibility ===" << std::endl;

    for (const auto& version : older_versions) {
        CURL* curl = curl_easy_init();
        if (!curl) continue;

        std::string response_str;
        struct curl_slist* headers = nullptr;

        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, ("x-api-key: " + m_api_key).c_str());
        headers = curl_slist_append(headers, ("anthropic-version: " + version).c_str());

        std::string payload = request.dump();

        curl_easy_setopt(curl, CURLOPT_URL, "https://api.anthropic.com/v1/messages");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_str);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

        CURLcode res = curl_easy_perform(curl);

        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if (res == CURLE_OK && http_code < 400) {
            std::cout << "Version " << version << ": ✓ Still supported" << std::endl;
        } else {
            std::cout << "Version " << version << ": ✗ Not supported (HTTP " << http_code << ")" << std::endl;
        }
    }
}

// Test feature compatibility matrix
TEST_F(ClaudeIntegrationTest, FeatureCompatibilityMatrix) {
    struct FeatureTest {
        std::string name;
        std::function<void(general_context&)> setup;
        bool is_supported;  // Expected support based on schema
    };

    std::vector<FeatureTest> features = {
        {"streaming", [](general_context& ctx) {
            ctx.set_parameter("stream", true);
        }, true},
        {"system_message", [](general_context& ctx) {
            ctx.set_system_message("You are a helpful assistant");
        }, true},
        {"stop_sequences", [](general_context& ctx) {
            ctx.set_parameter("stop_sequences", json::array({"STOP"}));
        }, true},
        {"temperature_0", [](general_context& ctx) {
            ctx.set_parameter("temperature", 0.0);
        }, true},
        {"temperature_1", [](general_context& ctx) {
            ctx.set_parameter("temperature", 1.0);
        }, true},
        {"top_k", [](general_context& ctx) {
            ctx.set_parameter("top_k", 10);
        }, true},
        {"top_p", [](general_context& ctx) {
            ctx.set_parameter("top_p", 0.9);
        }, true},
        {"multimodal", [this](general_context& ctx) {
            create_test_image();
            ctx.add_user_message("Describe this", "image/png", "test_image.png");
        }, true}
    };

    std::cout << "\n=== Claude Feature Compatibility Matrix ===" << std::endl;
    std::cout << std::setw(20) << "Model";
    for (const auto& feature : features) {
        std::cout << " | " << std::setw(15) << feature.name;
    }
    std::cout << std::endl;
    std::cout << std::string(20 + features.size() * 18, '-') << std::endl;

    // Test with different Claude models
    std::vector<std::string> test_models = {
        "claude-3-haiku-20240307",
        "claude-3-5-sonnet-20241022"
    };

    for (const auto& model : test_models) {
        std::cout << std::setw(20) << model;

        for (const auto& feature : features) {
            m_context->reset();
            m_context->set_model(model)
                     .set_parameter("max_tokens", 5);

            try {
                feature.setup(*m_context);

                // Don't add another message for multimodal test
                if (feature.name != "multimodal") {
                    m_context->add_user_message("Say 'OK'");
                }

                auto request = m_context->build_request();

                // For streaming, use streaming call
                if (feature.name == "streaming") {
                    ClaudeStreamResponse response = make_claude_stream_call(request);
                    std::cout << " | " << std::setw(15) << "✓";
                } else {
                    json response = make_claude_call(request);
                    std::cout << " | " << std::setw(15) << "✓";
                }
            } catch (const std::exception&) {
                std::cout << " | " << std::setw(15) << "✗";
            }
        }
        std::cout << std::endl;
    }
}

// Test rate limits
TEST_F(ClaudeIntegrationTest, RateLimitValidation) {
    std::cout << "\n=== Testing Claude Rate Limits ===" << std::endl;

    // Make rapid requests to test rate limiting
    const int num_requests = 5;
    std::vector<double> request_times;

    m_context->set_model("claude-3-haiku-20240307")
             .set_parameter("max_tokens", 5);

    for (int i = 0; i < num_requests; ++i) {
        m_context->clear_user_messages();
        m_context->add_user_message("Reply with 'OK' #" + std::to_string(i));

        auto request = m_context->build_request();

        auto start = std::chrono::high_resolution_clock::now();

        try {
            json response = make_claude_call(request);

            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
            request_times.push_back(duration.count());

            std::cout << "Request " << (i + 1) << ": " << duration.count() << "ms";

            // Check if we're being rate limited
            if (response.contains("usage")) {
                auto usage = response["usage"];
                std::cout << " (tokens: " << usage["input_tokens"].get<int>()
                         << " in, " << usage["output_tokens"].get<int>() << " out)";
            }
            std::cout << std::endl;

        } catch (const std::exception& e) {
            std::string error_msg = e.what();
            if (error_msg.find("429") != std::string::npos ||
                error_msg.find("rate") != std::string::npos) {
                std::cout << "Request " << (i + 1) << ": Rate limited (expected)" << std::endl;
                break;
            } else {
                std::cout << "Request " << (i + 1) << ": Error - " << error_msg << std::endl;
            }
        }

        // Small delay to be respectful
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    // Calculate average response time
    if (!request_times.empty()) {
        double avg_time = std::accumulate(request_times.begin(), request_times.end(), 0.0)
                         / request_times.size();
        std::cout << "\nAverage response time: " << avg_time << "ms" << std::endl;
    }
}

// Test context window limits
TEST_F(ClaudeIntegrationTest, ContextWindowLimits) {
    std::cout << "\n=== Testing Claude Context Window Limits ===" << std::endl;

    // Test with progressively larger contexts
    std::vector<int> context_sizes = {100, 1000, 5000, 10000};

    for (int size : context_sizes) {
        m_context->reset();
        m_context->set_model("claude-3-haiku-20240307")
                 .set_parameter("max_tokens", 10);

        // Create a message of approximately 'size' tokens
        // Rough estimate: 1 token ≈ 4 characters
        std::string message;
        for (int i = 0; i < size / 4; ++i) {
            message += "word ";
        }
        message += "\nHow many words approximately?";

        m_context->add_user_message(message);

        try {
            auto request = m_context->build_request();
            json response = make_claude_call(request);

            if (response.contains("usage")) {
                int input_tokens = response["usage"]["input_tokens"].get<int>();
                std::cout << "Context size ~" << size << " tokens: ✓ "
                         << "(actual: " << input_tokens << " tokens)" << std::endl;
            }
        } catch (const std::exception& e) {
            std::cout << "Context size ~" << size << " tokens: ✗ (" << e.what() << ")" << std::endl;
        }
    }
}

// Test beta features
TEST_F(ClaudeIntegrationTest, BetaFeatureDiscovery) {
    std::cout << "\n=== Testing Claude Beta Features ===" << std::endl;

    // Test with beta header for new features
    std::vector<std::string> beta_features = {
        "max-tokens-3-5-sonnet-2024-07-15",  // Extended output for Sonnet 3.5
        "prompt-caching-2024-07-31"           // Prompt caching beta
    };

    for (const auto& beta : beta_features) {
        m_context->reset();
        m_context->set_model("claude-3-haiku-20240307")
                 .add_user_message("Test beta feature");

        auto request = m_context->build_request();

        CURL* curl = curl_easy_init();
        if (!curl) continue;

        std::string response_str;
        struct curl_slist* headers = nullptr;

        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, ("x-api-key: " + m_api_key).c_str());
        headers = curl_slist_append(headers, "anthropic-version: 2023-06-01");
        headers = curl_slist_append(headers, ("anthropic-beta: " + beta).c_str());

        std::string payload = request.dump();

        curl_easy_setopt(curl, CURLOPT_URL, "https://api.anthropic.com/v1/messages");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_str);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

        CURLcode res = curl_easy_perform(curl);

        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if (res == CURLE_OK && http_code == 200) {
            std::cout << "Beta feature '" << beta << "': ✓ Available" << std::endl;

            // Parse response to see if beta features are active
            try {
                json response = json::parse(response_str);
                // Check for beta-specific fields in response
            } catch (...) {}
        } else {
            std::cout << "Beta feature '" << beta << "': ✗ Not available or not applicable" << std::endl;
        }
    }
}

TEST_F(ClaudeIntegrationTest, ValidationSummary) {
    std::cout << "\n=== Claude API Validation Summary ===" << std::endl;

    // Safer time handling
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    char time_buffer[100];
    if (std::strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S",
                      std::localtime(&time_t_now))) {
        std::cout << "Date: " << time_buffer << std::endl;
    } else {
        std::cout << "Date: " << "(unable to format time)" << std::endl;
    }

    std::cout << "Provider: Claude (Anthropic)" << std::endl;

    // Get schema info
    try {
        auto schema = m_context->get_schema();
        if (schema.contains("provider") && schema["provider"].is_object()) {
            auto provider = schema["provider"];
            if (provider.contains("api_version") && provider["api_version"].is_string()) {
                std::cout << "API Version: " << provider["api_version"].get<std::string>() << std::endl;
            }
            if (provider.contains("last_validated") && provider["last_validated"].is_string()) {
                std::cout << "Schema Last Validated: " << provider["last_validated"].get<std::string>() << std::endl;
            }
        }
    } catch (const std::exception& e) {
        std::cout << "Error reading schema: " << e.what() << std::endl;
    }

    // Test basic functionality
    std::cout << "\nBasic Functionality:" << std::endl;

    // Simple request
    try {
        m_context->reset();
        m_context->set_model("claude-3-haiku-20240307")
                 .add_user_message("Reply with 'OK'");
        json response = make_claude_call(m_context->build_request());
        std::cout << "  ✓ Basic requests working" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "  ✗ Basic requests failing: " << e.what() << std::endl;
    }

    // System messages
    try {
        m_context->reset();
        m_context->set_model("claude-3-haiku-20240307")
                 .set_system_message("Test")
                 .add_user_message("OK");
        make_claude_call(m_context->build_request());
        std::cout << "  ✓ System messages working" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "  ✗ System messages failing: " << e.what() << std::endl;
    }

    // Streaming
    try {
        m_context->reset();
        m_context->set_model("claude-3-haiku-20240307")
                 .set_parameter("stream", true)
                 .add_user_message("OK");
        make_claude_stream_call(m_context->build_request());
        std::cout << "  ✓ Streaming working" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "  ✗ Streaming failing: " << e.what() << std::endl;
    }

    // Vision
    try {
        create_test_image();
        m_context->reset();
        m_context->set_model("claude-3-haiku-20240307")
                 .add_user_message("Describe", "image/png", "test_image.png");
        make_claude_call(m_context->build_request());
        std::cout << "  ✓ Vision/multimodal working" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "  ✗ Vision/multimodal failing: " << e.what() << std::endl;
    }

    std::cout << "\nRecommendations:" << std::endl;

    // Check schema age
    try {
        auto schema = m_context->get_schema();
        if (schema.contains("provider") &&
            schema["provider"].contains("last_validated") &&
            schema["provider"]["last_validated"].is_string()) {

            std::string last_validated = schema["provider"]["last_validated"].get<std::string>();

            // Parse date safely
            std::tm tm = {};
            std::istringstream ss(last_validated);
            if (ss >> std::get_time(&tm, "%Y-%m-%d")) {
                auto last_time = std::mktime(&tm);
                if (last_time != -1) {
                    auto now = std::time(nullptr);
                    double days_old = std::difftime(now, last_time) / (60 * 60 * 24);

                    if (days_old > 30) {
                        std::cout << "  • Update schema validation date (currently "
                                 << static_cast<int>(days_old) << " days old)" << std::endl;
                    } else {
                        std::cout << "  • Schema is up to date ("
                                 << static_cast<int>(days_old) << " days old)" << std::endl;
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        std::cout << "  • Unable to check schema age: " << e.what() << std::endl;
    }

    std::cout << "  • Monitor Anthropic's API changelog for updates" << std::endl;
    std::cout << "  • Test with production workloads before deploying schema changes" << std::endl;
    std::cout << "\n=== End of Validation Summary ===" << std::endl;
}

TEST_F(ClaudeIntegrationTest, EdgeCasesComprehensive) {
    // Test empty message - Claude rejects this
    m_context->set_model("claude-3-haiku-20240307")
             .add_user_message("");

    // Claude requires non-empty text content
    EXPECT_THROW(make_claude_call(m_context->build_request()), std::runtime_error);

    // Test message with only whitespace - Claude also rejects this
    m_context->reset();
    m_context->set_model("claude-3-haiku-20240307")
             .add_user_message("   \n\t   ");

    // Claude requires non-whitespace text
    EXPECT_THROW(make_claude_call(m_context->build_request()), std::runtime_error);

    // Test with valid minimal content
    m_context->reset();
    m_context->set_model("claude-3-haiku-20240307")
             .add_user_message(".");  // Single character is valid

    auto request = m_context->build_request();
    json response;
    EXPECT_NO_THROW(response = make_claude_call(request));

    // Test alternating pattern without system
    m_context->reset();
    m_context->set_model("claude-3-haiku-20240307")
             .add_user_message("Hi")
             .add_assistant_message("Hello")
             .add_user_message("Bye");

    request = m_context->build_request();
    EXPECT_EQ(request["messages"].size(), 3);
    EXPECT_NO_THROW(make_claude_call(request));

    // Test very short but valid messages
    m_context->reset();
    m_context->set_model("claude-3-haiku-20240307")
             .add_user_message("a");  // Single letter

    EXPECT_NO_THROW(make_claude_call(m_context->build_request()));

    // Test with Unicode
    m_context->reset();
    m_context->set_model("claude-3-haiku-20240307")
             .add_user_message("🙂");  // Single emoji

    EXPECT_NO_THROW(make_claude_call(m_context->build_request()));
}

TEST_F(ClaudeIntegrationTest, TokenLimitExceeded) {
    // Test exceeding max_tokens for output - validation happens at parameter setting
    m_context->set_model("claude-3-haiku-20240307");

    // This should throw during parameter validation
    EXPECT_THROW(m_context->set_parameter("max_tokens", 8193), validation_exception);

    // Test valid max_tokens boundary
    EXPECT_NO_THROW(m_context->set_parameter("max_tokens", 8192));
    EXPECT_NO_THROW(m_context->set_parameter("max_tokens", 1));

    // Test invalid max_tokens values
    EXPECT_THROW(m_context->set_parameter("max_tokens", 0), validation_exception);
    EXPECT_THROW(m_context->set_parameter("max_tokens", -1), validation_exception);
    EXPECT_THROW(m_context->set_parameter("max_tokens", 10000), validation_exception);
}

TEST_F(ClaudeIntegrationTest, ClaudeValidationRules) {
    std::cout << "\n=== Claude-Specific Validation Rules ===" << std::endl;

    // Document empty message behavior
    std::cout << "1. Empty messages:" << std::endl;
    m_context->set_model("claude-3-haiku-20240307")
             .add_user_message("");
    try {
        make_claude_call(m_context->build_request());
        std::cout << "   ✓ Empty messages allowed" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "   ✗ Empty messages rejected: " << e.what() << std::endl;
    }

    // Document whitespace-only message behavior
    std::cout << "2. Whitespace-only messages:" << std::endl;
    m_context->reset();
    m_context->set_model("claude-3-haiku-20240307")
             .add_user_message("   ");
    try {
        make_claude_call(m_context->build_request());
        std::cout << "   ✓ Whitespace-only messages allowed" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "   ✗ Whitespace-only messages rejected: " << e.what() << std::endl;
    }

    // Document minimum valid message
    std::cout << "3. Minimum valid message:" << std::endl;
    std::vector<std::string> test_messages = {".", "a", "1", "!", "🙂"};
    for (const auto& msg : test_messages) {
        m_context->reset();
        m_context->set_model("claude-3-haiku-20240307")
                 .add_user_message(msg);
        try {
            make_claude_call(m_context->build_request());
            std::cout << "   ✓ '" << msg << "' is valid" << std::endl;
            break;  // Found minimum valid
        } catch (const std::exception&) {
            std::cout << "   ✗ '" << msg << "' is invalid" << std::endl;
        }
    }

    // Document max_tokens limits
    std::cout << "4. Max tokens limits:" << std::endl;
    std::vector<int> token_tests = {0, 1, 8192, 8193};
    for (int tokens : token_tests) {
        m_context->reset();
        m_context->set_model("claude-3-haiku-20240307");
        try {
            m_context->set_parameter("max_tokens", tokens);
            std::cout << "   ✓ max_tokens=" << tokens << " is valid" << std::endl;
        } catch (const validation_exception&) {
            std::cout << "   ✗ max_tokens=" << tokens << " is invalid (schema validation)" << std::endl;
        } catch (const std::exception& e) {
            std::cout << "   ✗ max_tokens=" << tokens << " error: " << e.what() << std::endl;
        }
    }

    // Document stop sequence rules
    std::cout << "5. Stop sequence rules:" << std::endl;
    std::vector<std::vector<std::string>> stop_tests = {
        {},                    // Empty array
        {"STOP"},             // Single word
        {"\n"},               // Single newline
        {"\n\n"},             // Double newline
        {"   "},              // Spaces only
        {"END", "STOP"},      // Multiple
    };

    for (const auto& stops : stop_tests) {
        m_context->reset();
        m_context->set_model("claude-3-haiku-20240307")
                 .set_parameter("stop_sequences", json(stops))
                 .add_user_message("Test");
        try {
            make_claude_call(m_context->build_request());
            std::cout << "   ✓ stop_sequences=" << json(stops).dump() << " is valid" << std::endl;
        } catch (const std::exception& e) {
            std::cout << "   ✗ stop_sequences=" << json(stops).dump()
                     << " rejected: " << e.what() << std::endl;
        }
    }
}
