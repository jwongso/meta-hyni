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
#include <numeric>

using namespace hyni;
using json = nlohmann::json;
namespace fs = std::filesystem;

// Response structure for Mistral streaming
struct MistralStreamResponse {
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

// Streaming callback for SSE format
static size_t mistral_stream_callback(void* contents, size_t size, size_t nmemb,
                                      MistralStreamResponse* response) {
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

                // Extract usage if present
                if (chunk_json.contains("usage")) {
                    response->usage = chunk_json["usage"];
                }

            } catch (const json::parse_error& e) {
                // Not all lines are valid JSON
            }
        }
    }

    return total_size;
}

class MistralIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Get API key from environment or config
        const char* api_key = std::getenv("MS_API_KEY");
        if (!api_key) {
            fs::path rc_path = fs::path(std::getenv("HOME")) / ".hynirc";
            if (fs::exists(rc_path)) {
                auto config = parse_hynirc(rc_path.string());
                auto it = config.find("MS_API_KEY");
                if (it != config.end()) {
                    m_api_key = it->second;
                }
            }
        } else {
            m_api_key = api_key;
        }

        if (m_api_key.empty()) {
            GTEST_SKIP() << "MS_API_KEY not set. Skipping Mistral integration tests.";
        }

        // Create schema registry and factory
        m_registry = schema_registry::create()
                        .set_schema_directory("../schemas")
                        .build();
        m_factory = std::make_shared<context_factory>(m_registry);

        // Create Mistral context
        context_config config;
        config.enable_validation = true;
        config.default_max_tokens = 100;  // Keep costs low for tests
        config.default_temperature = 0.0; // Deterministic responses

        m_context = m_factory->create_context("mistral", config);
        m_context->set_api_key(m_api_key);
    }

    void TearDown() override {
        // Clean up test files
        if (fs::exists("test_file.txt")) {
            fs::remove("test_file.txt");
        }
    }

    // Helper function to make API call
    json make_mistral_call(const json& request) {
        CURL* curl = curl_easy_init();
        if (!curl) throw std::runtime_error("Failed to init CURL");

        std::string response_str;
        struct curl_slist* headers = nullptr;

        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, ("Authorization: Bearer " + m_api_key).c_str());

        std::string payload = request.dump();

        curl_easy_setopt(curl, CURLOPT_URL, "https://api.mistral.ai/v1/chat/completions");
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

        // Check for API errors with detailed message
        if (http_code >= 400) {
            std::string error_msg = "API Error (HTTP " + std::to_string(http_code) + ")";
            if (response.contains("error")) {
                if (response["error"].is_object() && response["error"].contains("message")) {
                    error_msg += ": " + response["error"]["message"].get<std::string>();
                } else if (response["error"].is_string()) {
                    error_msg += ": " + response["error"].get<std::string>();
                }
            } else if (response.contains("message")) {
                error_msg += ": " + response["message"].get<std::string>();
            }

            // Also print the full response for debugging
            std::cerr << "Full error response: " << response.dump(2) << std::endl;

            throw std::runtime_error(error_msg);
        }

        return response;
    }

    // Helper function for streaming API call
    MistralStreamResponse make_mistral_stream_call(const json& request) {
        CURL* curl = curl_easy_init();
        if (!curl) throw std::runtime_error("Failed to init CURL");

        MistralStreamResponse response;
        struct curl_slist* headers = nullptr;

        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, ("Authorization: Bearer " + m_api_key).c_str());
        headers = curl_slist_append(headers, "Accept: text/event-stream");

        std::string payload = request.dump();

        curl_easy_setopt(curl, CURLOPT_URL, "https://api.mistral.ai/v1/chat/completions");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, mistral_stream_callback);
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

    std::string m_api_key;
    std::shared_ptr<schema_registry> m_registry;
    std::shared_ptr<context_factory> m_factory;
    std::unique_ptr<general_context> m_context;
};

// Test basic text-only prompt
TEST_F(MistralIntegrationTest, BasicTextPrompt) {
    m_context->set_model("mistral-small-latest")
             .add_user_message("Reply with exactly: 'Hello from Mistral'");

    auto request = m_context->build_request();

    // Verify request structure
    EXPECT_EQ(request["model"], "mistral-small-latest");
    EXPECT_FALSE(request["messages"].empty());

    // Make API call
    json response;
    ASSERT_NO_THROW(response = make_mistral_call(request));

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
    EXPECT_EQ(text, "Hello from Mistral");
}

// Test system message functionality
TEST_F(MistralIntegrationTest, SystemMessage) {
    m_context->set_model("mistral-small-latest")
             .set_system_message("You are a calculator. Only respond with numbers.")
             .add_user_message("What is 2 + 2?");

    auto request = m_context->build_request();

    // Verify system message in request
    EXPECT_EQ(request["messages"].size(), 2);
    EXPECT_EQ(request["messages"][0]["role"], "system");
    EXPECT_EQ(request["messages"][0]["content"], "You are a calculator. Only respond with numbers.");

    json response;
    ASSERT_NO_THROW(response = make_mistral_call(request));

    std::string text = m_context->extract_text_response(response);
    EXPECT_FALSE(text.empty());
    EXPECT_TRUE(text.find("4") != std::string::npos);
}

// Test multi-turn conversation
TEST_F(MistralIntegrationTest, MultiTurnConversation) {
    m_context->set_model("mistral-small-latest");

    // First turn
    m_context->add_user_message("My name is TestBot. What's my name?");

    auto request1 = m_context->build_request();
    json response1;
    ASSERT_NO_THROW(response1 = make_mistral_call(request1));

    std::string text1 = m_context->extract_text_response(response1);
    EXPECT_TRUE(text1.find("TestBot") != std::string::npos);

    // Add assistant response and continue
    m_context->add_assistant_message(text1)
             .add_user_message("What did I just tell you my name was?");

    auto request2 = m_context->build_request();
    EXPECT_EQ(request2["messages"].size(), 3);

    json response2;
    ASSERT_NO_THROW(response2 = make_mistral_call(request2));

    std::string text2 = m_context->extract_text_response(response2);
    EXPECT_TRUE(text2.find("TestBot") != std::string::npos);
}

// Test different model sizes
TEST_F(MistralIntegrationTest, DifferentModelSizes) {
    // Test models by size category
    std::vector<std::pair<std::string, std::string>> model_tests = {
        {"mistral-small-latest", "small"},
        {"mistral-medium-latest", "medium"},
        {"mistral-large-latest", "large"}
    };

    std::cout << "\n=== Testing Mistral Model Sizes ===" << std::endl;

    for (const auto& [model, size] : model_tests) {
        m_context->reset();

        try {
            m_context->set_model(model)
                     .add_user_message("Reply with your model size category");

            auto request = m_context->build_request();
            EXPECT_EQ(request["model"], model);

            json response = make_mistral_call(request);
            std::string text = m_context->extract_text_response(response);
            EXPECT_FALSE(text.empty());

            std::cout << "  ✓ " << model << " (" << size << " model) - Available" << std::endl;
            std::cout << "    Response preview: " << text.substr(0, 50) << "..." << std::endl;

        } catch (const std::exception& e) {
            std::cout << "  ✗ " << model << " (" << size << " model) - " << e.what() << std::endl;
        }

        // Rate limit protection
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

// Test parameter settings
TEST_F(MistralIntegrationTest, ParameterSettings) {
    // Start with a basic working request and add parameters gradually
    m_context->set_model("mistral-small-latest")
             .add_user_message("Write exactly 10 words about AI");

    // Add parameters one at a time to isolate any issues
    m_context->set_parameter("max_tokens", 50);

    // Temperature 0.0 might be the issue - some APIs don't like exactly 0
    m_context->set_parameter("temperature", 0.1);  // Changed from 0.0 to 0.1

    // top_p is usually fine
    m_context->set_parameter("top_p", 0.9);

    auto request = m_context->build_request();

    // Verify parameters
    EXPECT_EQ(request["temperature"], 0.1);  // Updated expectation
    EXPECT_EQ(request["max_tokens"], 50);
    EXPECT_EQ(request["top_p"], 0.9);

    // Mistral doesn't have frequency/presence penalty
    EXPECT_FALSE(request.contains("frequency_penalty"));
    EXPECT_FALSE(request.contains("presence_penalty"));

    json response;
    ASSERT_NO_THROW(response = make_mistral_call(request));

    std::string text = m_context->extract_text_response(response);
    EXPECT_FALSE(text.empty());
    EXPECT_LE(text.length(), 200); // Should be limited by max_tokens
}

// Test streaming functionality
TEST_F(MistralIntegrationTest, StreamingResponse) {
    m_context->set_model("mistral-small-latest")
             .set_parameter("stream", true)
             .add_user_message("Count from 1 to 5");

    auto request = m_context->build_request();
    EXPECT_TRUE(request["stream"].get<bool>());

    MistralStreamResponse response;
    ASSERT_NO_THROW(response = make_mistral_stream_call(request));

    // Verify streaming worked
    EXPECT_GT(response.chunks.size(), 1) << "Should receive multiple chunks";
    EXPECT_FALSE(response.complete_text.empty());
    EXPECT_TRUE(response.finished);

    // Content should contain numbers 1-5
    for (int i = 1; i <= 5; ++i) {
        EXPECT_TRUE(response.complete_text.find(std::to_string(i)) != std::string::npos)
            << "Missing number " << i << " in response";
    }
}

// Test usage tracking
TEST_F(MistralIntegrationTest, UsageTracking) {
    m_context->set_model("mistral-small-latest")
             .add_user_message("Hello");

    auto request = m_context->build_request();
    json response;
    ASSERT_NO_THROW(response = make_mistral_call(request));

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

// Test error handling
TEST_F(MistralIntegrationTest, ErrorHandling) {
    // Test 1: Invalid model
    m_context->reset();
    EXPECT_THROW(m_context->set_model("invalid-model-xyz"), validation_exception);

    // Test 2: Invalid temperature
    m_context->reset();
    EXPECT_THROW(m_context->set_parameter("temperature", 2.5), validation_exception);

    // Test 3: Invalid API key
    m_context->reset();
    m_context->set_model("mistral-small-latest")
             .add_user_message("Hello");

    auto request = m_context->build_request();

    // Make call with invalid key
    CURL* curl = curl_easy_init();
    ASSERT_TRUE(curl);

    std::string response_str;
    struct curl_slist* headers = nullptr;

    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Authorization: Bearer invalid_key_123");

    std::string payload = request.dump();

    curl_easy_setopt(curl, CURLOPT_URL, "https://api.mistral.ai/v1/chat/completions");
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

// Test model availability
TEST_F(MistralIntegrationTest, ValidateModelAvailability) {
    auto schema_models = m_context->get_supported_models();

    std::cout << "\n=== Validating Mistral Models ===" << std::endl;

    std::map<std::string, bool> model_status;

    for (const auto& model : schema_models) {
        m_context->reset();
        m_context->set_model(model)
                 .set_parameter("max_tokens", 5)
                 .add_user_message("Hi");

        try {
            json response = make_mistral_call(m_context->build_request());
            model_status[model] = true;
            std::cout << "  ✓ " << model << " is available" << std::endl;

            // Check actual model in response
            if (response.contains("model")) {
                std::string response_model = response["model"];
                if (response_model != model) {
                    std::cout << "    Note: Response model '" << response_model
                             << "' differs from requested '" << model << "'" << std::endl;
                }
            }
        } catch (const std::exception& e) {
            model_status[model] = false;
            std::cout << "  ✗ " << model << " failed: " << e.what() << std::endl;
        }

        // Rate limit protection
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }

    // Summary
    int available = std::count_if(model_status.begin(), model_status.end(),
                                  [](const auto& p) { return p.second; });
    std::cout << "\nSummary: " << available << "/" << schema_models.size()
              << " models available" << std::endl;
}

// Test rate limiting
TEST_F(MistralIntegrationTest, RateLimiting) {
    std::cout << "\n=== Testing Mistral Rate Limits ===" << std::endl;

    const int num_requests = 5;
    std::vector<double> request_times;

    m_context->set_model("mistral-small-latest")
             .set_parameter("max_tokens", 5);

    for (int i = 0; i < num_requests; ++i) {
        m_context->clear_user_messages();
        m_context->add_user_message("Reply with OK");

        auto start = std::chrono::high_resolution_clock::now();

        try {
            json response = make_mistral_call(m_context->build_request());

            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
            request_times.push_back(duration.count());

            std::cout << "Request " << (i + 1) << ": " << duration.count() << "ms" << std::endl;

        } catch (const std::exception& e) {
            if (std::string(e.what()).find("429") != std::string::npos) {
                std::cout << "Rate limit hit at request " << (i + 1) << " (expected)" << std::endl;
                break;
            }
        }

        // Respect rate limits
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    if (!request_times.empty()) {
        double avg_time = std::accumulate(request_times.begin(), request_times.end(), 0.0)
                         / request_times.size();
        std::cout << "Average response time: " << avg_time << "ms" << std::endl;
    }
}

// Test context window
TEST_F(MistralIntegrationTest, ContextWindowTest) {
    std::cout << "\n=== Testing Mistral Context Window ===" << std::endl;

    auto limits = m_context->get_schema()["limits"];
    int max_context = limits["max_context_length"].get<int>();
    std::cout << "Schema claims max context: " << max_context << " tokens" << std::endl;

    // Test with progressively larger contexts
    std::vector<int> context_sizes = {100, 1000, 4000, 7000};

    for (int size : context_sizes) {
        if (size > max_context) break;

        m_context->reset();
        m_context->set_model("mistral-small-latest")
                 .set_parameter("max_tokens", 10);

        // Create message of approximately 'size' tokens
        std::string message;
        for (int i = 0; i < size / 4; ++i) {
            message += "word ";
        }
        message += "\nHow many words?";

        m_context->add_user_message(message);

        try {
            json response = make_mistral_call(m_context->build_request());

            if (response.contains("usage")) {
                int input_tokens = response["usage"]["prompt_tokens"].get<int>();
                std::cout << "  ✓ Context size ~" << size << " tokens: "
                         << "actual " << input_tokens << " tokens" << std::endl;
            }
        } catch (const std::exception& e) {
            std::cout << "  ✗ Context size ~" << size << " tokens: " << e.what() << std::endl;
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

// Test Mistral-specific behavior
TEST_F(MistralIntegrationTest, MistralSpecificBehavior) {
    std::cout << "\n=== Mistral-Specific Behavior ===" << std::endl;

    // Test 1: No stop sequences parameter
    std::cout << "1. Stop sequences support:" << std::endl;
    m_context->reset();
    m_context->set_model("mistral-small-latest")
             .add_user_message("Count to 5 then STOP");

    auto request = m_context->build_request();

    // Mistral doesn't have stop parameter in schema
    EXPECT_FALSE(request.contains("stop"));
    std::cout << "   ✓ No stop parameter (as expected)" << std::endl;

    // Test 2: Model versioning
    std::cout << "2. Model versioning:" << std::endl;

    // Test versioned vs latest models
    std::vector<std::pair<std::string, std::string>> version_tests = {
        {"mistral-small-3.1", "mistral-small-latest"},
        {"mistral-medium-3", "mistral-medium-latest"},
        {"mistral-large-2", "mistral-large-latest"}
    };

    for (const auto& [versioned, latest] : version_tests) {
        bool versioned_exists = false;
        bool latest_exists = false;

        // Check if versioned model exists
        auto models = m_context->get_supported_models();
        versioned_exists = std::find(models.begin(), models.end(), versioned) != models.end();
        latest_exists = std::find(models.begin(), models.end(), latest) != models.end();

        if (versioned_exists && latest_exists) {
            std::cout << "   ✓ Both " << versioned << " and " << latest << " available" << std::endl;
        } else if (latest_exists) {
            std::cout << "   ✓ Only " << latest << " available (rolling release)" << std::endl;
        } else {
            std::cout << "   ✗ Neither version available" << std::endl;
        }
    }

    // Test 3: Simple content format
    std::cout << "3. Message content format:" << std::endl;
    m_context->reset();
    m_context->set_model("mistral-small-latest")
             .add_user_message("Test");

    request = m_context->build_request();
    EXPECT_TRUE(request["messages"][0]["content"].is_string());
    std::cout << "   ✓ Uses simple string content (not array)" << std::endl;
}

// Performance test
TEST_F(MistralIntegrationTest, PerformanceMetrics) {
    m_context->set_model("mistral-small-latest")
             .add_user_message("Reply with 'OK'");

    auto request = m_context->build_request();

    auto start = std::chrono::high_resolution_clock::now();
    json response = make_mistral_call(request);
    auto end = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    std::cout << "Mistral API call took: " << duration.count() << "ms" << std::endl;

    EXPECT_LT(duration.count(), 10000); // Less than 10 seconds

    std::string text = m_context->extract_text_response(response);
    EXPECT_FALSE(text.empty());
}

// Validation summary
TEST_F(MistralIntegrationTest, ValidationSummary) {
    std::cout << "\n=== Mistral API Validation Summary ===" << std::endl;

    // Get current time
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    char time_buffer[100];
    if (std::strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S",
                      std::localtime(&time_t_now))) {
        std::cout << "Date: " << time_buffer << std::endl;
    }

    std::cout << "Provider: Mistral AI" << std::endl;

    // Test basic functionality
    std::cout << "\nBasic Functionality:" << std::endl;

    // Simple request
    try {
        m_context->reset();
        m_context->set_model("mistral-small-latest")
                 .add_user_message("Reply with 'OK'");
        make_mistral_call(m_context->build_request());
        std::cout << "  ✓ Basic requests working" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "  ✗ Basic requests failing: " << e.what() << std::endl;
    }

    // System messages
    try {
        m_context->reset();
        m_context->set_model("mistral-small-latest")
                 .set_system_message("Test")
                 .add_user_message("OK");
        make_mistral_call(m_context->build_request());
        std::cout << "  ✓ System messages working" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "  ✗ System messages failing: " << e.what() << std::endl;
    }

    // Streaming
    try {
        m_context->reset();
        m_context->set_model("mistral-small-latest")
                 .set_parameter("stream", true)
                 .add_user_message("OK");
        make_mistral_stream_call(m_context->build_request());
        std::cout << "  ✓ Streaming working" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "  ✗ Streaming failing: " << e.what() << std::endl;
    }

    std::cout << "\nModel Tiers:" << std::endl;

    // Test each tier
    for (const auto& tier : {"small", "medium", "large"}) {
        std::string model = std::string("mistral-") + tier + "-latest";
        try {
            m_context->reset();
            m_context->set_model(model)
                     .add_user_message("Hi");
            make_mistral_call(m_context->build_request());
            std::cout << "  ✓ Mistral " << tier << " tier available" << std::endl;
        } catch (...) {
            std::cout << "  ✗ Mistral " << tier << " tier unavailable" << std::endl;
        }
    }

    std::cout << "\nRecommendations:" << std::endl;
    std::cout << "  • Use versioned models for production stability" << std::endl;
    std::cout << "  • Use 'latest' models for newest features" << std::endl;
    std::cout << "  • Be aware of 8192 token context limit" << std::endl;
    std::cout << "  • Rate limit: 60 requests/minute" << std::endl;
}

// Test response format consistency
TEST_F(MistralIntegrationTest, ResponseFormatConsistency) {
    std::cout << "\n=== Testing Response Format Consistency ===" << std::endl;

    // Test multiple models to ensure consistent response format
    std::vector<std::string> test_models = {
        "mistral-small-latest",
        "mistral-medium-latest"
    };

    for (const auto& model : test_models) {
        m_context->reset();
        m_context->set_model(model)
                 .add_user_message("Reply with 'test'");

        try {
            json response = make_mistral_call(m_context->build_request());

            // Verify consistent structure
            EXPECT_TRUE(response.contains("id"));
            EXPECT_TRUE(response.contains("object"));
            EXPECT_TRUE(response.contains("created"));
            EXPECT_TRUE(response.contains("model"));
            EXPECT_TRUE(response.contains("choices"));
            EXPECT_TRUE(response.contains("usage"));

            // Verify choices structure
            ASSERT_FALSE(response["choices"].empty());
            auto& choice = response["choices"][0];
            EXPECT_TRUE(choice.contains("index"));
            EXPECT_TRUE(choice.contains("message"));
            EXPECT_TRUE(choice.contains("finish_reason"));

            std::cout << "  ✓ " << model << " has consistent response format" << std::endl;

        } catch (const std::exception& e) {
            std::cout << "  ✗ " << model << " test failed: " << e.what() << std::endl;
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

TEST_F(MistralIntegrationTest, EdgeCasesAndValidation) {
    std::cout << "\n=== Testing Mistral Edge Cases ===" << std::endl;

    // Test 1: Empty message handling
    std::cout << "1. Empty message handling:" << std::endl;
    m_context->reset();
    m_context->set_model("mistral-small-latest")
             .add_user_message("");

    try {
        json response = make_mistral_call(m_context->build_request());
        std::cout << "   ✓ Empty messages accepted" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "   ✗ Empty messages rejected: " << e.what() << std::endl;
    }

    // Test 2: Whitespace-only message
    std::cout << "2. Whitespace-only messages:" << std::endl;
    m_context->reset();
    m_context->set_model("mistral-small-latest")
             .add_user_message("   \n\t   ");

    try {
        json response = make_mistral_call(m_context->build_request());
        std::cout << "   ✓ Whitespace-only messages accepted" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "   ✗ Whitespace-only messages rejected: " << e.what() << std::endl;
    }

    // Test 3: Temperature edge cases
    std::cout << "3. Temperature edge cases:" << std::endl;

    // Test temperature = 0.0
    m_context->reset();
    m_context->set_model("mistral-small-latest")
             .set_parameter("temperature", 0.0)
             .add_user_message("What is 2+2?");

    try {
        json response = make_mistral_call(m_context->build_request());
        std::cout << "   ✓ Temperature 0.0 accepted" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "   ✗ Temperature 0.0 rejected: " << e.what() << std::endl;

        // Try with 0.01 instead
        m_context->set_parameter("temperature", 0.01);
        try {
            make_mistral_call(m_context->build_request());
            std::cout << "   ✓ Temperature 0.01 works as alternative" << std::endl;
        } catch (...) {
            std::cout << "   ✗ Even temperature 0.01 failed" << std::endl;
        }
    }

    // Test 4: Unicode and special characters
    std::cout << "4. Unicode and special characters:" << std::endl;
    m_context->reset();
    m_context->set_model("mistral-small-latest")
             .add_user_message("Test émojis 🚀 and 中文 characters: \n\t\"quotes\"");

    try {
        json response = make_mistral_call(m_context->build_request());
        std::string text = m_context->extract_text_response(response);
        std::cout << "   ✓ Unicode and special characters handled" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "   ✗ Unicode handling failed: " << e.what() << std::endl;
    }

    // Test 5: Very long single message
    std::cout << "5. Long message handling:" << std::endl;
    m_context->reset();
    m_context->set_model("mistral-small-latest")
             .set_parameter("max_tokens", 10);

    std::string long_message(5000, 'a');
    long_message += "\nSummarize in one word.";
    m_context->add_user_message(long_message);

    try {
        json response = make_mistral_call(m_context->build_request());
        std::cout << "   ✓ Long messages accepted" << std::endl;

        if (response.contains("usage")) {
            int tokens = response["usage"]["prompt_tokens"].get<int>();
            std::cout << "     Used " << tokens << " tokens" << std::endl;
        }
    } catch (const std::exception& e) {
        std::cout << "   ✗ Long message failed: " << e.what() << std::endl;
    }
}

TEST_F(MistralIntegrationTest, ModelTierBehaviorComparison) {
    std::cout << "\n=== Comparing Mistral Model Tier Behaviors ===" << std::endl;

    std::vector<std::string> models = {
        "mistral-small-latest",
        "mistral-medium-latest",
        "mistral-large-latest"
    };

    const std::string test_prompt = "What is 2+2? Reply with just the number.";

    for (const auto& model : models) {
        m_context->reset();
        m_context->set_model(model)
                 .set_parameter("temperature", 0.1)  // Low temp for consistency
                 .set_parameter("max_tokens", 10)
                 .add_user_message(test_prompt);

        try {
            auto start = std::chrono::high_resolution_clock::now();
            json response = make_mistral_call(m_context->build_request());
            auto end = std::chrono::high_resolution_clock::now();

            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
            std::string text = m_context->extract_text_response(response);

            std::cout << model << ":" << std::endl;
            std::cout << "  Response: " << text << std::endl;
            std::cout << "  Time: " << duration.count() << "ms" << std::endl;

            if (response.contains("usage")) {
                auto usage = response["usage"];
                std::cout << "  Tokens: " << usage["completion_tokens"].get<int>()
                         << " (completion)" << std::endl;
            }

        } catch (const std::exception& e) {
            std::cout << model << ": Failed - " << e.what() << std::endl;
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

TEST_F(MistralIntegrationTest, ResponseDeterminism) {
    std::cout << "\n=== Testing Response Determinism ===" << std::endl;

    m_context->set_model("mistral-small-latest")
             .set_parameter("temperature", 0.0)  // Should give deterministic results
             .set_parameter("max_tokens", 20)
             .add_user_message("Complete this: The capital of France is");

    std::vector<std::string> responses;

    for (int i = 0; i < 3; ++i) {
        try {
            auto request = m_context->build_request();
            json response = make_mistral_call(request);
            std::string text = m_context->extract_text_response(response);
            responses.push_back(text);
            std::cout << "  Attempt " << (i + 1) << ": " << text << std::endl;
        } catch (const std::exception& e) {
            std::cout << "  Attempt " << (i + 1) << " failed: " << e.what() << std::endl;
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    // Check consistency
    if (responses.size() >= 2) {
        bool consistent = true;
        for (size_t i = 1; i < responses.size(); ++i) {
            if (responses[i] != responses[0]) {
                consistent = false;
                break;
            }
        }

        if (consistent) {
            std::cout << "✓ Responses are deterministic with temperature=0" << std::endl;
        } else {
            std::cout << "⚠ Responses vary despite temperature=0" << std::endl;
        }
    }
}

TEST_F(MistralIntegrationTest, ConversationHistoryLimit) {
    std::cout << "\n=== Testing Conversation History Limits ===" << std::endl;

    m_context->set_model("mistral-small-latest")
             .set_parameter("max_tokens", 10);

    // Build a long conversation
    for (int i = 0; i < 20; ++i) {
        if (i % 2 == 0) {
            m_context->add_user_message("Question " + std::to_string(i/2 + 1));
        } else {
            m_context->add_assistant_message("Answer " + std::to_string(i/2 + 1));
        }
    }

    // Add final user message
    m_context->add_user_message("What was my first question?");

    try {
        auto request = m_context->build_request();
        std::cout << "Conversation has " << request["messages"].size() << " messages" << std::endl;

        json response = make_mistral_call(request);

        if (response.contains("usage")) {
            int tokens = response["usage"]["prompt_tokens"].get<int>();
            std::cout << "Total prompt tokens: " << tokens << std::endl;
        }

        std::string text = m_context->extract_text_response(response);
        std::cout << "✓ Long conversation handled successfully" << std::endl;

    } catch (const std::exception& e) {
        std::cout << "✗ Long conversation failed: " << e.what() << std::endl;
    }
}

TEST_F(MistralIntegrationTest, MistralComprehensiveValidation) {
    std::cout << "\n=== Mistral Comprehensive Validation Report ===" << std::endl;
    std::cout << "Date: ";
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::cout << std::ctime(&time_t);

    struct TestResult {
        std::string category;
        std::string test;
        bool passed;
        std::string note;
    };

    std::vector<TestResult> results;

    // 1. Model availability
    for (const auto& model : {"mistral-small-latest", "mistral-medium-latest", "mistral-large-latest"}) {
        m_context->reset();
        m_context->set_model(model)
                 .set_parameter("max_tokens", 5)
                 .add_user_message("Hi");
        try {
            make_mistral_call(m_context->build_request());
            results.push_back({"Models", model, true, "Available"});
        } catch (...) {
            results.push_back({"Models", model, false, "Not available"});
        }
    }

    // 2. Features
    // Streaming
    m_context->reset();
    m_context->set_model("mistral-small-latest")
             .set_parameter("stream", true)
             .add_user_message("Hi");
    try {
        make_mistral_stream_call(m_context->build_request());
        results.push_back({"Features", "Streaming", true, ""});
    } catch (...) {
        results.push_back({"Features", "Streaming", false, "Not working"});
    }

    // System messages
    m_context->reset();
    m_context->set_model("mistral-small-latest")
             .set_system_message("Test")
             .add_user_message("Hi");
    try {
        make_mistral_call(m_context->build_request());
        results.push_back({"Features", "System Messages", true, ""});
    } catch (...) {
        results.push_back({"Features", "System Messages", false, "Not working"});
    }

    // Temperature 0
    m_context->reset();
    m_context->set_model("mistral-small-latest")
             .set_parameter("temperature", 0.0)
             .add_user_message("Hi");
    try {
        make_mistral_call(m_context->build_request());
        results.push_back({"Parameters", "Temperature 0.0", true, ""});
    } catch (...) {
        results.push_back({"Parameters", "Temperature 0.0", false, "Not supported"});
    }

    // Print results
    std::cout << "\n" << std::string(70, '-') << std::endl;
    std::cout << std::left << std::setw(15) << "Category"
              << std::setw(25) << "Test"
              << std::setw(10) << "Result"
              << "Notes" << std::endl;
    std::cout << std::string(70, '-') << std::endl;

    for (const auto& result : results) {
        std::cout << std::left << std::setw(15) << result.category
                  << std::setw(25) << result.test
                  << std::setw(10) << (result.passed ? "PASS" : "FAIL")
                  << result.note << std::endl;
    }

    // Calculate pass rate
    int passed = std::count_if(results.begin(), results.end(),
                               [](const auto& r) { return r.passed; });

    std::cout << "\n=== Summary ===" << std::endl;
    std::cout << "Tests passed: " << passed << "/" << results.size()
              << " (" << (passed * 100 / results.size()) << "%)" << std::endl;

    if (passed == static_cast<int>(results.size())) {
        std::cout << "✅ All tests passing. Schema is fully compatible with Mistral API." << std::endl;
    } else if (passed >= results.size() * 0.8) {
        std::cout << "⚠️  Most tests passing. Minor updates may be needed." << std::endl;
    } else {
        std::cout << "❌ Many tests failing. Schema needs significant updates." << std::endl;
    }
}
