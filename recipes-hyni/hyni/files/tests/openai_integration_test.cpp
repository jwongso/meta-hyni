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
#include <chrono>
#include <thread>
#include <sstream>

using namespace hyni;
using json = nlohmann::json;
namespace fs = std::filesystem;

// Response structure for streaming
struct OpenAIStreamResponse {
    std::vector<json> chunks;
    std::string complete_text;
    bool finished = false;
    std::string finish_reason;
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

// Streaming callback for Server-Sent Events
static size_t stream_callback(void* contents, size_t size, size_t nmemb, OpenAIStreamResponse* response) {
    if (!response) return 0;

    size_t total_size = size * nmemb;
    std::string chunk(static_cast<char*>(contents), total_size);

    std::istringstream stream(chunk);
    std::string line;

    while (std::getline(stream, line)) {
        if (line.find("data: ") == 0) {
            std::string json_data = line.substr(6);

            if (json_data == "[DONE]") {
                response->finished = true;
                continue;
            }

            try {
                json chunk_json = json::parse(json_data);
                response->chunks.push_back(chunk_json);

                // Extract delta content
                if (chunk_json.contains("choices") && !chunk_json["choices"].empty()) {
                    auto& choice = chunk_json["choices"][0];
                    if (choice.contains("delta") && choice["delta"].contains("content")) {
                        response->complete_text += choice["delta"]["content"].get<std::string>();
                    }
                    if (choice.contains("finish_reason") && !choice["finish_reason"].is_null()) {
                        response->finish_reason = choice["finish_reason"];
                    }
                }

                // Extract usage data if present
                if (chunk_json.contains("usage")) {
                    response->usage = chunk_json["usage"];
                }

            } catch (const json::parse_error& e) {
                // Not all lines are valid JSON, skip
            }
        }
    }

    return total_size;
}

class OpenAIIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Get API key from environment or config
        const char* api_key = std::getenv("OA_API_KEY");
        if (!api_key) {
            fs::path rc_path = fs::path(std::getenv("HOME")) / ".hynirc";
            if (fs::exists(rc_path)) {
                auto config = parse_hynirc(rc_path.string());
                auto it = config.find("OA_API_KEY");
                if (it != config.end()) {
                    m_api_key = it->second;
                }
            }
        } else {
            m_api_key = api_key;
        }

        if (m_api_key.empty()) {
            GTEST_SKIP() << "OA_API_KEY not set. Skipping OpenAI integration tests.";
        }

        // Create schema registry and factory
        m_registry = schema_registry::create()
                        .set_schema_directory("../schemas")
                        .build();
        m_factory = std::make_shared<context_factory>(m_registry);

        // Create OpenAI context
        context_config config;
        config.enable_validation = true;
        config.default_max_tokens = 100;  // Keep costs low for tests
        config.default_temperature = 0.0; // Deterministic responses

        m_context = m_factory->create_context("openai", config);
        m_context->set_api_key(m_api_key);
    }

    void TearDown() override {
        // Clean up test files
        if (fs::exists("test_image.png")) {
            fs::remove("test_image.png");
        }
    }

    // Helper function to make API call
    json make_openai_call(const json& request) {
        CURL* curl = curl_easy_init();
        if (!curl) throw std::runtime_error("Failed to init CURL");

        std::string response_str;
        struct curl_slist* headers = nullptr;

        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, ("Authorization: Bearer " + m_api_key).c_str());

        std::string payload = request.dump();

        curl_easy_setopt(curl, CURLOPT_URL, "https://api.openai.com/v1/chat/completions");
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

    json make_openai_call(const json& request, const std::string& api_key_override) {
        CURL* curl = curl_easy_init();
        if (!curl) throw std::runtime_error("Failed to init CURL");

        std::string response_str;
        struct curl_slist* headers = nullptr;

        headers = curl_slist_append(headers, "Content-Type: application/json");

        // Use override key if provided, otherwise use member variable
        std::string api_key_to_use = api_key_override.empty() ? m_api_key : api_key_override;
        headers = curl_slist_append(headers, ("Authorization: Bearer " + api_key_to_use).c_str());

        std::string payload = request.dump();

        curl_easy_setopt(curl, CURLOPT_URL, "https://api.openai.com/v1/chat/completions");
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
    OpenAIStreamResponse make_openai_stream_call(const json& request) {
        CURL* curl = curl_easy_init();
        if (!curl) throw std::runtime_error("Failed to init CURL");

        OpenAIStreamResponse response;
        struct curl_slist* headers = nullptr;

        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, ("Authorization: Bearer " + m_api_key).c_str());
        headers = curl_slist_append(headers, "Accept: text/event-stream");

        std::string payload = request.dump();

        curl_easy_setopt(curl, CURLOPT_URL, "https://api.openai.com/v1/chat/completions");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, stream_callback);
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
TEST_F(OpenAIIntegrationTest, BasicTextPrompt) {
    m_context->set_model("gpt-3.5-turbo")
             .add_user_message("Reply with exactly: 'Hello from OpenAI'");

    auto request = m_context->build_request();

    // Verify request structure
    EXPECT_EQ(request["model"], "gpt-3.5-turbo");
    EXPECT_FALSE(request["messages"].empty());

    // Make API call
    json response;
    ASSERT_NO_THROW(response = make_openai_call(request));

    // Verify response structure
    EXPECT_TRUE(response.contains("id"));
    EXPECT_TRUE(response.contains("object"));
    EXPECT_EQ(response["object"], "chat.completion");
    EXPECT_TRUE(response.contains("created"));
    EXPECT_TRUE(response.contains("model"));
    EXPECT_TRUE(response.contains("choices"));
    EXPECT_FALSE(response["choices"].empty());

    // Extract text using context method
    std::string text = m_context->extract_text_response(response);
    EXPECT_FALSE(text.empty());
    EXPECT_EQ(text, "Hello from OpenAI");
}

// Test system message functionality
TEST_F(OpenAIIntegrationTest, SystemMessage) {
    m_context->set_model("gpt-3.5-turbo")
             .set_system_message("You are a calculator. Only respond with numbers.")
             .add_user_message("What is 2 + 2?");

    auto request = m_context->build_request();

    // Verify system message in request
    EXPECT_EQ(request["messages"].size(), 2);
    EXPECT_EQ(request["messages"][0]["role"], "system");
    EXPECT_EQ(request["messages"][0]["content"], "You are a calculator. Only respond with numbers.");

    json response;
    ASSERT_NO_THROW(response = make_openai_call(request));

    std::string text = m_context->extract_text_response(response);
    EXPECT_FALSE(text.empty());

    // Response should be just "4" or contain "4"
    EXPECT_TRUE(text.find("4") != std::string::npos);
}

// Test multi-turn conversation
TEST_F(OpenAIIntegrationTest, MultiTurnConversation) {
    m_context->set_model("gpt-3.5-turbo");

    // First turn
    m_context->add_user_message("My name is TestBot. What's my name?");

    auto request1 = m_context->build_request();
    json response1;
    ASSERT_NO_THROW(response1 = make_openai_call(request1));

    std::string text1 = m_context->extract_text_response(response1);
    EXPECT_TRUE(text1.find("TestBot") != std::string::npos);

    // Add assistant response and continue conversation
    m_context->add_assistant_message(text1)
             .add_user_message("What did I just tell you my name was?");

    auto request2 = m_context->build_request();
    EXPECT_EQ(request2["messages"].size(), 3);

    json response2;
    ASSERT_NO_THROW(response2 = make_openai_call(request2));

    std::string text2 = m_context->extract_text_response(response2);
    EXPECT_TRUE(text2.find("TestBot") != std::string::npos);
}

// Test with different models
TEST_F(OpenAIIntegrationTest, DifferentModels) {
    std::vector<std::string> models_to_test = {"gpt-3.5-turbo", "gpt-4o"};

    for (const auto& model : models_to_test) {
        m_context->reset();

        try {
            m_context->set_model(model)
                     .add_user_message("Reply with your model name");

            auto request = m_context->build_request();
            EXPECT_EQ(request["model"], model);

            json response = make_openai_call(request);
            std::string text = m_context->extract_text_response(response);
            EXPECT_FALSE(text.empty());

            std::cout << "Model " << model << " responded: " << text.substr(0, 50) << "..." << std::endl;

        } catch (const std::exception& e) {
            // Some models might not be available depending on account
            std::cout << "Model " << model << " test skipped: " << e.what() << std::endl;
        }
    }
}

// Test parameter validation
TEST_F(OpenAIIntegrationTest, ParameterSettings) {
    m_context->set_model("gpt-3.5-turbo")
             .set_parameter("temperature", 0.0)
             .set_parameter("max_tokens", 50)
             .set_parameter("top_p", 0.9)
             .set_parameter("frequency_penalty", 0.5)
             .set_parameter("presence_penalty", 0.5)
             .add_user_message("Write exactly 10 words about AI");

    auto request = m_context->build_request();

    // Verify parameters in request
    EXPECT_EQ(request["temperature"], 0.0);
    EXPECT_EQ(request["max_tokens"], 50);
    EXPECT_EQ(request["top_p"], 0.9);
    EXPECT_EQ(request["frequency_penalty"], 0.5);
    EXPECT_EQ(request["presence_penalty"], 0.5);

    json response;
    ASSERT_NO_THROW(response = make_openai_call(request));

    std::string text = m_context->extract_text_response(response);
    EXPECT_FALSE(text.empty());
    EXPECT_LE(text.length(), 200); // Should be limited by max_tokens
}

// Test image input (Vision)
TEST_F(OpenAIIntegrationTest, ImageInput) {
    create_test_image();

    m_context->set_model("gpt-4o")  // Vision-capable model
             .add_user_message("What color is the square in this image? Reply with one word only.",
                              "image/png", "test_image.png");

    auto request = m_context->build_request();

    // Verify multimodal content structure
    EXPECT_EQ(request["messages"][0]["content"].size(), 2);
    EXPECT_EQ(request["messages"][0]["content"][1]["type"], "image_url");

    json response;
    try {
        response = make_openai_call(request);
        std::string text = m_context->extract_text_response(response);
        EXPECT_FALSE(text.empty());

        // Should identify the red color
        std::string text_lower = text;
        std::transform(text_lower.begin(), text_lower.end(), text_lower.begin(), ::tolower);
        EXPECT_TRUE(text_lower.find("red") != std::string::npos);

    } catch (const std::exception& e) {
        // Vision might not be available for all accounts
        std::cout << "Vision test skipped: " << e.what() << std::endl;
    }
}

// Test streaming functionality
TEST_F(OpenAIIntegrationTest, StreamingResponse) {
    m_context->set_model("gpt-3.5-turbo")
             .set_parameter("stream", true)
             .add_user_message("Count from 1 to 5");

    auto request = m_context->build_request();
    EXPECT_TRUE(request["stream"].get<bool>());

    OpenAIStreamResponse response;
    ASSERT_NO_THROW(response = make_openai_stream_call(request));

    // Verify streaming worked
    EXPECT_GT(response.chunks.size(), 1) << "Should receive multiple chunks";
    EXPECT_FALSE(response.complete_text.empty());
    EXPECT_TRUE(response.finished);
    EXPECT_FALSE(response.finish_reason.empty());

    // Content should contain numbers 1-5
    for (int i = 1; i <= 5; ++i) {
        EXPECT_TRUE(response.complete_text.find(std::to_string(i)) != std::string::npos)
            << "Missing number " << i << " in response";
    }
}

// Test JSON mode
TEST_F(OpenAIIntegrationTest, JSONMode) {
    m_context->set_model("gpt-3.5-turbo")
             .set_parameter("response_format", json{{"type", "json_object"}})
             .add_user_message("Return a JSON object with keys 'status' (value: 'ok') and 'number' (value: 42)");

    auto request = m_context->build_request();
    EXPECT_EQ(request["response_format"]["type"], "json_object");

    json response;
    ASSERT_NO_THROW(response = make_openai_call(request));

    std::string text = m_context->extract_text_response(response);
    EXPECT_FALSE(text.empty());

    // Parse and verify JSON response
    json parsed;
    ASSERT_NO_THROW(parsed = json::parse(text));
    EXPECT_TRUE(parsed.contains("status"));
    EXPECT_TRUE(parsed.contains("number"));
    EXPECT_EQ(parsed["status"], "ok");
    EXPECT_EQ(parsed["number"], 42);
}

// Test error handling with invalid requests
TEST_F(OpenAIIntegrationTest, ErrorHandling) {
    // Test 1: Invalid model
    m_context->reset();
    EXPECT_THROW(m_context->set_model("invalid-model-xyz"), validation_exception);

    // Test 2: Invalid temperature
    m_context->reset();
    EXPECT_THROW(m_context->set_parameter("temperature", 3.0), validation_exception);

    // Test 3: Missing required fields
    json bad_request = {
        {"model", "gpt-3.5-turbo"}
        // Missing messages
    };

    EXPECT_THROW(make_openai_call(bad_request), std::runtime_error);

    // Test 4: Invalid API key
    m_context->reset();
    m_context->set_model("gpt-3.5-turbo")
             .add_user_message("Hello");

    auto request = m_context->build_request();

    // Use invalid API key
    EXPECT_THROW({
        make_openai_call(request, "invalid_key_123");
    }, std::runtime_error);
}

// Test rate limiting behavior
TEST_F(OpenAIIntegrationTest, RateLimiting) {
    // Make several rapid requests to test rate limiting handling
    m_context->set_model("gpt-3.5-turbo");

    for (int i = 0; i < 3; ++i) {
        m_context->clear_user_messages();
        m_context->add_user_message("Reply with 'OK'");

        auto request = m_context->build_request();

        try {
            json response = make_openai_call(request);
            std::string text = m_context->extract_text_response(response);
            EXPECT_FALSE(text.empty());

        } catch (const std::exception& e) {
            // Check if it's a rate limit error
            std::string error_msg = e.what();
            if (error_msg.find("429") != std::string::npos ||
                error_msg.find("rate") != std::string::npos) {
                std::cout << "Rate limit hit as expected" << std::endl;
                break;
            } else {
                throw; // Re-throw if not rate limit error
            }
        }

        // Small delay between requests
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

// Test stop sequences
TEST_F(OpenAIIntegrationTest, StopSequences) {
    m_context->set_model("gpt-3.5-turbo")
             .set_parameter("stop", json::array({"STOP", "\n\n"}))
             .add_user_message("Count from 1 to 10, then write STOP");

    auto request = m_context->build_request();
    EXPECT_TRUE(request["stop"].is_array());

    json response;
    ASSERT_NO_THROW(response = make_openai_call(request));

    std::string text = m_context->extract_text_response(response);
    EXPECT_FALSE(text.empty());

    // Response should stop before or at "STOP"
    EXPECT_TRUE(text.find("STOP") == std::string::npos ||
                text.find("STOP") == text.length() - 4);
}

// Test usage tracking
TEST_F(OpenAIIntegrationTest, UsageTracking) {
    m_context->set_model("gpt-3.5-turbo")
             .add_user_message("Hello");

    auto request = m_context->build_request();
    json response;
    ASSERT_NO_THROW(response = make_openai_call(request));

    // Verify usage information
    EXPECT_TRUE(response.contains("usage"));
    auto usage = response["usage"];
    EXPECT_TRUE(usage.contains("prompt_tokens"));
    EXPECT_TRUE(usage.contains("completion_tokens"));
    EXPECT_TRUE(usage.contains("total_tokens"));

    EXPECT_GT(usage["prompt_tokens"].get<int>(), 0);
    EXPECT_GT(usage["completion_tokens"].get<int>(), 0);
    EXPECT_EQ(usage["total_tokens"].get<int>(),
              usage["prompt_tokens"].get<int>() + usage["completion_tokens"].get<int>());
}

// Test with very long input
TEST_F(OpenAIIntegrationTest, LongInput) {
    // Create a long prompt (but within limits)
    std::string long_text;
    for (int i = 0; i < 100; ++i) {
        long_text += "This is sentence number " + std::to_string(i) + ". ";
    }
    long_text += "\n\nSummarize the above in 5 words.";

    m_context->set_model("gpt-3.5-turbo")
             .set_parameter("max_tokens", 20)
             .add_user_message(long_text);

    auto request = m_context->build_request();

    json response;
    ASSERT_NO_THROW(response = make_openai_call(request));

    std::string text = m_context->extract_text_response(response);
    EXPECT_FALSE(text.empty());
    EXPECT_LE(text.length(), 100); // Should be short due to max_tokens
}

// Test finish reasons
TEST_F(OpenAIIntegrationTest, FinishReasons) {
    // Test 1: Normal completion
    m_context->set_model("gpt-3.5-turbo")
             .add_user_message("Say 'Hello'");

    auto request1 = m_context->build_request();
    json response1 = make_openai_call(request1);

    EXPECT_TRUE(response1["choices"][0].contains("finish_reason"));
    std::string finish_reason1 = response1["choices"][0]["finish_reason"];
    EXPECT_EQ(finish_reason1, "stop");

    // Test 2: Length limit
    m_context->reset();
    m_context->set_model("gpt-3.5-turbo")
             .set_parameter("max_tokens", 5)
             .add_user_message("Write a long story about dragons");

    auto request2 = m_context->build_request();
    json response2 = make_openai_call(request2);

    std::string finish_reason2 = response2["choices"][0]["finish_reason"];
    EXPECT_EQ(finish_reason2, "length");
}

// Performance test
TEST_F(OpenAIIntegrationTest, PerformanceMetrics) {
    m_context->set_model("gpt-3.5-turbo")
             .add_user_message("Reply with 'OK'");

    auto request = m_context->build_request();

    auto start = std::chrono::high_resolution_clock::now();
    json response = make_openai_call(request);
    auto end = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    std::cout << "API call took: " << duration.count() << "ms" << std::endl;

    // Verify response time is reasonable (adjust based on your connection)
    EXPECT_LT(duration.count(), 10000); // Less than 10 seconds

    // Extract and verify response
    std::string text = m_context->extract_text_response(response);
    EXPECT_FALSE(text.empty());
}

TEST_F(OpenAIIntegrationTest, EdgeCaseContent) {
    // Test empty message
    m_context->set_model("gpt-3.5-turbo")
             .add_user_message("");
    auto request = m_context->build_request();
    EXPECT_NO_THROW(make_openai_call(request));

    // Test Unicode and special characters
    m_context->reset();
    m_context->set_model("gpt-3.5-turbo")
             .add_user_message("Test émojis 🚀 and 中文 characters: \n\t\"quotes\"");
    request = m_context->build_request();
    json response = make_openai_call(request);
    EXPECT_FALSE(m_context->extract_text_response(response).empty());

    // Test maximum message length (near token limit)
    m_context->reset();
    std::string very_long_message(10000, 'a');  // Very long message
    m_context->set_model("gpt-3.5-turbo")
             .set_parameter("max_tokens", 10)  // Limit response
             .add_user_message(very_long_message + " Reply with 'OK'");
    EXPECT_NO_THROW(make_openai_call(m_context->build_request()));
}

TEST_F(OpenAIIntegrationTest, ParameterCombinations) {
    // Test n parameter (multiple completions)
    m_context->set_model("gpt-3.5-turbo")
             .set_parameter("n", 2)  // Request 2 completions
             .set_parameter("max_tokens", 10)
             .add_user_message("Say hello");

    auto request = m_context->build_request();
    json response = make_openai_call(request);

    EXPECT_EQ(response["choices"].size(), 2);

    // Test seed parameter for reproducibility
    m_context->reset();
    m_context->set_model("gpt-3.5-turbo")
             .set_parameter("seed", 12345)
             .set_parameter("temperature", 0.7)
             .add_user_message("Generate a random number");

    request = m_context->build_request();
    json response1 = make_openai_call(request);
    std::string text1 = m_context->extract_text_response(response1);

    // Same seed should give similar results
    json response2 = make_openai_call(request);
    std::string text2 = m_context->extract_text_response(response2);

    // Note: OpenAI doesn't guarantee exact reproducibility, but results should be similar
    std::cout << "Seed test - Response 1: " << text1 << "\nResponse 2: " << text2 << std::endl;
}

TEST_F(OpenAIIntegrationTest, StreamingResponseFormat) {
    // Test that streaming responses follow the expected format
    m_context->set_model("gpt-3.5-turbo")
             .set_parameter("stream", true)
             .add_user_message("Count to 3");

    auto request = m_context->build_request();
    OpenAIStreamResponse response = make_openai_stream_call(request);

    // Verify delta format in chunks
    for (const auto& chunk : response.chunks) {
        if (chunk.contains("choices") && !chunk["choices"].empty()) {
            EXPECT_TRUE(chunk["choices"][0].contains("delta"));
            // Verify the delta structure matches schema
            if (chunk["choices"][0]["delta"].contains("content")) {
                EXPECT_TRUE(chunk["choices"][0]["delta"]["content"].is_string());
            }
        }
    }
}

TEST_F(OpenAIIntegrationTest, ModelSpecificFeatures) {
    // Test max_tokens limits for different models
    std::map<std::string, int> model_limits = {
        {"gpt-3.5-turbo", 4096},
        {"gpt-4o", 4096}
    };

    for (const auto& [model, limit] : model_limits) {
        m_context->reset();
        m_context->set_model(model);

        // Should accept valid limit
        EXPECT_NO_THROW(m_context->set_parameter("max_tokens", limit));

        // Should reject over limit (if validation is strict)
        if (m_context->get_schema()["parameters"]["max_tokens"].contains("model_specific")) {
            EXPECT_THROW(m_context->set_parameter("max_tokens", limit + 1), validation_exception);
        }
    }
}

TEST_F(OpenAIIntegrationTest, ResponseFormatVariations) {
    // Test with logprobs
    m_context->set_model("gpt-3.5-turbo")
             .set_parameter("logprobs", true)
             .set_parameter("top_logprobs", 2)
             .add_user_message("Say 'yes' or 'no'");

    auto request = m_context->build_request();
    json response = make_openai_call(request);

    // Check if logprobs are included
    if (response["choices"][0].contains("logprobs")) {
        EXPECT_TRUE(response["choices"][0]["logprobs"].is_object());
    }

    // Test user parameter for tracking
    m_context->reset();
    m_context->set_model("gpt-3.5-turbo")
             .set_parameter("user", "test_user_123")
             .add_user_message("Hello");

    EXPECT_NO_THROW(make_openai_call(m_context->build_request()));
}

TEST_F(OpenAIIntegrationTest, ValidateAgainstActualAPI) {
    // Test that all models in schema actually work
    auto models = m_context->get_supported_models();

    for (const auto& model : models) {
        // Skip expensive models in automated tests
        if (model.find("gpt-4") != std::string::npos &&
            model != "gpt-4o-mini") {
            continue;  // Skip expensive GPT-4 models
        }

        m_context->reset();
        m_context->set_model(model)
                 .set_parameter("max_tokens", 5)  // Keep costs low
                 .add_user_message("Reply with 'OK'");

        try {
            auto request = m_context->build_request();
            json response = make_openai_call(request);
            EXPECT_FALSE(m_context->extract_text_response(response).empty())
                << "Model " << model << " should return a response";
        } catch (const std::exception& e) {
            ADD_FAILURE() << "Model " << model << " failed: " << e.what();
        }
    }
}

TEST_F(OpenAIIntegrationTest, ValidateParameterRanges) {
    // Test actual API limits vs schema limits

    // Test max_tokens upper limit
    m_context->set_model("gpt-3.5-turbo")
             .set_parameter("max_tokens", 4096)  // Schema says this is max
             .add_user_message("Say hi");

    EXPECT_NO_THROW(make_openai_call(m_context->build_request()));

    // Test temperature boundaries
    for (double temp : {0.0, 1.0, 2.0}) {
        m_context->reset();
        m_context->set_model("gpt-3.5-turbo")
                 .set_parameter("temperature", temp)
                 .set_parameter("max_tokens", 5)
                 .add_user_message("Hi");

        EXPECT_NO_THROW(make_openai_call(m_context->build_request()))
            << "Temperature " << temp << " should be valid";
    }
}

TEST_F(OpenAIIntegrationTest, StreamingEdgeCases) {
    // Test streaming with multiple messages
    m_context->set_model("gpt-3.5-turbo")
             .set_parameter("stream", true)
             .add_user_message("Count to 3")
             .add_assistant_message("1, 2, 3")
             .add_user_message("Now count to 5");

    auto request = m_context->build_request();
    OpenAIStreamResponse response = make_openai_stream_call(request);

    EXPECT_TRUE(response.finished);
    EXPECT_FALSE(response.complete_text.empty());

    // Test streaming with stop sequence
    m_context->reset();
    m_context->set_model("gpt-3.5-turbo")
             .set_parameter("stream", true)
             .set_parameter("stop", json::array({"STOP"}))
             .add_user_message("Count from 1 to 10 then say STOP");

    response = make_openai_stream_call(m_context->build_request());
    EXPECT_TRUE(response.finished);
    EXPECT_TRUE(response.finish_reason == "stop" ||
                response.finish_reason == "stop_sequence");
}

TEST_F(OpenAIIntegrationTest, ComprehensiveSchemaValidation) {
    // Use many features together
    m_context->set_model("gpt-4o")
             .set_system_message("You are a helpful assistant.")
             .set_parameter("temperature", 0.8)
             .set_parameter("max_tokens", 150)
             .set_parameter("stop", json::array({"\n\n"}))
             .set_parameter("response_format", json{{"type", "json_object"}})
             .add_user_message("List 3 colors as JSON with format: {\"colors\": [...]}")
             .add_assistant_message("{\"colors\": [\"red\"]}")
             .add_user_message("Add two more colors");

    auto request = m_context->build_request();
    json response = make_openai_call(request);

    // Verify complex response handling
    std::string text = m_context->extract_text_response(response);
    json parsed = json::parse(text);
    EXPECT_TRUE(parsed.contains("colors"));
    EXPECT_EQ(parsed["colors"].size(), 3);
}

TEST_F(OpenAIIntegrationTest, ValidateAgainstLiveAPI) {
    // Fetch actual available models from OpenAI
    CURL* curl = curl_easy_init();
    ASSERT_TRUE(curl) << "Failed to init CURL";

    std::string response_str;
    struct curl_slist* headers = nullptr;

    headers = curl_slist_append(headers, ("Authorization: Bearer " + m_api_key).c_str());

    curl_easy_setopt(curl, CURLOPT_URL, "https://api.openai.com/v1/models");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_str);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    CURLcode res = curl_easy_perform(curl);

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    ASSERT_EQ(res, CURLE_OK) << "Failed to fetch models: " << curl_easy_strerror(res);
    ASSERT_EQ(http_code, 200) << "Failed to fetch models, HTTP " << http_code;

    json models_response = json::parse(response_str);
    ASSERT_TRUE(models_response.contains("data"));

    // Extract chat models
    std::set<std::string> available_chat_models;
    std::map<std::string, json> model_details;

    for (const auto& model : models_response["data"]) {
        std::string model_id = model["id"];

        // Filter for chat models (gpt models)
        if (model_id.find("gpt") != std::string::npos &&
            model_id.find("instruct") == std::string::npos) {  // Exclude instruct models
            available_chat_models.insert(model_id);
            model_details[model_id] = model;
        }
    }

    // Get models from our schema
    auto schema_models = m_context->get_supported_models();
    std::set<std::string> schema_model_set(schema_models.begin(), schema_models.end());

    // Check for missing models in our schema
    std::vector<std::string> missing_in_schema;
    for (const auto& api_model : available_chat_models) {
        if (schema_model_set.find(api_model) == schema_model_set.end()) {
            missing_in_schema.push_back(api_model);
        }
    }

    // Check for deprecated models in our schema
    std::vector<std::string> deprecated_in_schema;
    for (const auto& schema_model : schema_models) {
        if (available_chat_models.find(schema_model) == available_chat_models.end()) {
            // Some models might be aliases or simplified names, check if it works
            try {
                m_context->reset();
                m_context->set_model(schema_model)
                         .set_parameter("max_tokens", 5)
                         .add_user_message("Hi");

                json response = make_openai_call(m_context->build_request());
                // Model works, it's just an alias
            } catch (const std::exception& e) {
                deprecated_in_schema.push_back(schema_model);
            }
        }
    }

    // Report findings
    if (!missing_in_schema.empty()) {
        std::cout << "\n=== New models available in API but not in schema ===" << std::endl;
        for (const auto& model : missing_in_schema) {
            std::cout << "  - " << model;
            if (model_details[model].contains("created")) {
                auto created = model_details[model]["created"].get<int64_t>();
                std::time_t t = created;
                std::cout << " (created: " << std::put_time(std::localtime(&t), "%Y-%m-%d") << ")";
            }
            std::cout << std::endl;
        }
    }

    if (!deprecated_in_schema.empty()) {
        std::cout << "\n=== Models in schema that may be deprecated ===" << std::endl;
        for (const auto& model : deprecated_in_schema) {
            std::cout << "  - " << model << std::endl;
        }
    }

    // This is a warning, not a failure - APIs evolve
    if (!missing_in_schema.empty() || !deprecated_in_schema.empty()) {
        std::cout << "\nConsider updating the OpenAI schema file." << std::endl;
    }
}

TEST_F(OpenAIIntegrationTest, ModelCapabilityDetection) {
    // Test model capabilities by checking their properties
    CURL* curl = curl_easy_init();
    ASSERT_TRUE(curl);

    std::string response_str;
    struct curl_slist* headers = nullptr;

    headers = curl_slist_append(headers, ("Authorization: Bearer " + m_api_key).c_str());

    curl_easy_setopt(curl, CURLOPT_URL, "https://api.openai.com/v1/models");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_str);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    ASSERT_EQ(res, CURLE_OK);

    json models_response = json::parse(response_str);

    // Check specific model capabilities
    std::cout << "\n=== Model Capabilities ===" << std::endl;

    for (const auto& model_name : m_context->get_supported_models()) {
        std::cout << model_name << ":" << std::endl;

        // Find model in API response
        for (const auto& model : models_response["data"]) {
            if (model["id"] == model_name) {
                // Check permissions and capabilities
                if (model.contains("permission") && model["permission"].is_array() &&
                    !model["permission"].empty()) {
                    auto perm = model["permission"][0];

                    std::cout << "  - Allow fine tuning: "
                              << (perm.contains("allow_fine_tuning") ?
                                  perm["allow_fine_tuning"].dump() : "unknown") << std::endl;
                    std::cout << "  - Allow sampling: "
                              << (perm.contains("allow_sampling") ?
                                  perm["allow_sampling"].dump() : "unknown") << std::endl;
                }

                // Test actual capabilities
                // Test vision capability
                if (model_name.find("vision") != std::string::npos ||
                    model_name.find("gpt-4") != std::string::npos) {
                    m_context->reset();
                    create_test_image();

                    try {
                        m_context->set_model(model_name)
                                 .set_parameter("max_tokens", 10)
                                 .add_user_message("Describe this image in one word",
                                                  "image/png", "test_image.png");

                        make_openai_call(m_context->build_request());
                        std::cout << "  - Vision: ✓ supported" << std::endl;
                    } catch (const std::exception&) {
                        std::cout << "  - Vision: ✗ not supported" << std::endl;
                    }
                }

                // Test JSON mode capability
                m_context->reset();
                try {
                    m_context->set_model(model_name)
                             .set_parameter("response_format", json{{"type", "json_object"}})
                             .set_parameter("max_tokens", 20)
                             .add_user_message("Return JSON with key 'test' value 'ok'");

                    json response = make_openai_call(m_context->build_request());
                    std::string text = m_context->extract_text_response(response);
                    json parsed = json::parse(text);
                    std::cout << "  - JSON mode: ✓ supported" << std::endl;
                } catch (const std::exception&) {
                    std::cout << "  - JSON mode: ✗ not supported" << std::endl;
                }

                break;
            }
        }
    }
}

TEST_F(OpenAIIntegrationTest, APILimitsValidation) {
    // Test actual API limits against our schema

    // Test context length limits for each model
    std::map<std::string, int> known_context_limits = {
        {"gpt-3.5-turbo", 16385},
        {"gpt-4", 8192},
        {"gpt-4-turbo", 128000},
        {"gpt-4o", 128000},
        {"gpt-4o-mini", 128000}
    };

    for (const auto& [model, expected_limit] : known_context_limits) {
        // Check if model is in our schema
        auto models = m_context->get_supported_models();
        if (std::find(models.begin(), models.end(), model) != models.end()) {
            // Create a message near the limit
            int test_size = std::min(1000, expected_limit / 4);  // Use 1/4 of limit for testing
            std::string long_message(test_size, 'a');

            m_context->reset();
            m_context->set_model(model)
                     .set_parameter("max_tokens", 5)
                     .add_user_message(long_message + " Reply with 'OK'");

            try {
                json response = make_openai_call(m_context->build_request());
                std::cout << model << " handled " << test_size << " chars successfully" << std::endl;
            } catch (const std::exception& e) {
                std::cout << model << " failed with " << test_size << " chars: " << e.what() << std::endl;
            }
        }
    }
}

TEST_F(OpenAIIntegrationTest, FeatureCompatibilityMatrix) {
    // Create a compatibility matrix for all models and features
    struct FeatureTest {
        std::string name;
        std::function<void(general_context&)> setup;
    };

    std::vector<FeatureTest> features = {
        {"streaming", [](general_context& ctx) {
            ctx.set_parameter("stream", true);
        }},
        {"json_mode", [](general_context& ctx) {
            ctx.set_parameter("response_format", json{{"type", "json_object"}});
        }},
        {"system_message", [](general_context& ctx) {
            ctx.set_system_message("You are a helpful assistant");
        }},
        {"stop_sequences", [](general_context& ctx) {
            ctx.set_parameter("stop", json::array({"STOP"}));
        }},
        {"high_temperature", [](general_context& ctx) {
            ctx.set_parameter("temperature", 2.0);
        }},
        {"logprobs", [](general_context& ctx) {
            ctx.set_parameter("logprobs", true);
            ctx.set_parameter("top_logprobs", 2);
        }}
    };

    std::cout << "\n=== Feature Compatibility Matrix ===" << std::endl;
    std::cout << std::setw(20) << "Model";
    for (const auto& feature : features) {
        std::cout << " | " << std::setw(12) << feature.name;
    }
    std::cout << std::endl;
    std::cout << std::string(20 + features.size() * 15, '-') << std::endl;

    for (const auto& model : m_context->get_supported_models()) {
        // Skip expensive models
        if (model.find("gpt-4") != std::string::npos && model != "gpt-4o-mini") {
            continue;
        }

        std::cout << std::setw(20) << model;

        for (const auto& feature : features) {
            m_context->reset();
            m_context->set_model(model)
                     .set_parameter("max_tokens", 5);

            try {
                feature.setup(*m_context);
                m_context->add_user_message("Say 'OK'");

                auto request = m_context->build_request();

                // For streaming, use streaming call
                if (feature.name == "streaming") {
                    OpenAIStreamResponse response = make_openai_stream_call(request);
                    std::cout << " | " << std::setw(12) << "✓";
                } else {
                    json response = make_openai_call(request);
                    std::cout << " | " << std::setw(12) << "✓";
                }
            } catch (const std::exception&) {
                std::cout << " | " << std::setw(12) << "✗";
            }
        }
        std::cout << std::endl;
    }
}
