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
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>

using namespace hyni;
using json = nlohmann::json;
namespace fs = std::filesystem;

class OpenAISchemaTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Load OpenAI schema directly
        m_schema_path = "../schemas/openai.json";

        std::ifstream file(m_schema_path);
        if (!file.is_open()) {
            GTEST_SKIP() << "OpenAI schema file not found at: " << m_schema_path;
        }

        try {
            file >> m_schema;
        } catch (const json::parse_error& e) {
            FAIL() << "Failed to parse OpenAI schema: " << e.what();
        }

        // Create context with the schema
        context_config config;
        config.enable_validation = true;

        try {
            m_context = std::make_unique<general_context>(m_schema, config);
        } catch (const std::exception& e) {
            FAIL() << "Failed to create context from schema: " << e.what();
        }
    }

    std::string m_schema_path;
    json m_schema;
    std::unique_ptr<general_context> m_context;
};

// Test schema structure and required fields
TEST_F(OpenAISchemaTest, SchemaStructureValidation) {
    // Test top-level required fields
    EXPECT_TRUE(m_schema.contains("provider"));
    EXPECT_TRUE(m_schema.contains("api"));
    EXPECT_TRUE(m_schema.contains("authentication"));
    EXPECT_TRUE(m_schema.contains("models"));
    EXPECT_TRUE(m_schema.contains("request_template"));
    EXPECT_TRUE(m_schema.contains("message_format"));
    EXPECT_TRUE(m_schema.contains("response_format"));

    // Test provider information
    EXPECT_EQ(m_schema["provider"]["name"], "openai");
    EXPECT_TRUE(m_schema["provider"].contains("display_name"));
    EXPECT_TRUE(m_schema["provider"].contains("version"));

    // Test API configuration
    EXPECT_EQ(m_schema["api"]["endpoint"], "https://api.openai.com/v1/chat/completions");
    EXPECT_EQ(m_schema["api"]["method"], "POST");
    EXPECT_TRUE(m_schema["api"].contains("timeout"));

    // Test authentication
    EXPECT_EQ(m_schema["authentication"]["type"], "header");
    EXPECT_EQ(m_schema["authentication"]["key_name"], "Authorization");
    EXPECT_EQ(m_schema["authentication"]["key_prefix"], "Bearer ");
}

// Test model configuration
TEST_F(OpenAISchemaTest, ModelConfiguration) {
    ASSERT_TRUE(m_schema["models"].contains("available"));
    ASSERT_TRUE(m_schema["models"].contains("default"));

    auto available_models = m_schema["models"]["available"];
    EXPECT_FALSE(available_models.empty());

    // Check for expected OpenAI models
    std::vector<std::string> expected_models = {
        "gpt-4o",
        "gpt-4-turbo",
        "gpt-3.5-turbo"
    };

    for (const auto& model : expected_models) {
        bool found = false;
        for (const auto& available : available_models) {
            if (available.get<std::string>() == model) {
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found) << "Model " << model << " not found in available models";
    }

    // Test default model
    std::string default_model = m_schema["models"]["default"];
    EXPECT_FALSE(default_model.empty());

    // Default model should be in available models
    bool default_found = false;
    for (const auto& available : available_models) {
        if (available.get<std::string>() == default_model) {
            default_found = true;
            break;
        }
    }
    EXPECT_TRUE(default_found) << "Default model not in available models list";
}

// Test request template structure
TEST_F(OpenAISchemaTest, RequestTemplateStructure) {
    auto request_template = m_schema["request_template"];

    // Required fields for OpenAI
    EXPECT_TRUE(request_template.contains("model"));
    EXPECT_TRUE(request_template.contains("messages"));
    EXPECT_TRUE(request_template.contains("max_tokens"));

    // Optional but common fields
    EXPECT_TRUE(request_template.contains("temperature"));
    EXPECT_TRUE(request_template.contains("top_p"));
    EXPECT_TRUE(request_template.contains("stream"));

    // OpenAI-specific fields
    EXPECT_TRUE(request_template.contains("frequency_penalty"));
    EXPECT_TRUE(request_template.contains("presence_penalty"));
    EXPECT_TRUE(request_template.contains("stop"));
    EXPECT_TRUE(request_template.contains("response_format"));

    // Verify default values
    EXPECT_EQ(request_template["messages"], json::array());
    EXPECT_GE(request_template["max_tokens"].get<int>(), 1);
    EXPECT_GE(request_template["temperature"].get<double>(), 0.0);
    EXPECT_LE(request_template["temperature"].get<double>(), 2.0);
}

// Test parameter definitions and validation
TEST_F(OpenAISchemaTest, ParameterValidation) {
    ASSERT_TRUE(m_schema.contains("parameters"));

    // Test temperature parameter
    ASSERT_TRUE(m_schema["parameters"].contains("temperature"));
    auto temp_param = m_schema["parameters"]["temperature"];
    EXPECT_EQ(temp_param["type"], "float");
    EXPECT_EQ(temp_param["min"], 0.0);
    EXPECT_EQ(temp_param["max"], 2.0);

    // Test max_tokens parameter
    ASSERT_TRUE(m_schema["parameters"].contains("max_tokens"));
    auto max_tokens_param = m_schema["parameters"]["max_tokens"];
    EXPECT_EQ(max_tokens_param["type"], "integer");
    EXPECT_GE(max_tokens_param["min"].get<int>(), 1);

    // Test frequency_penalty parameter
    ASSERT_TRUE(m_schema["parameters"].contains("frequency_penalty"));
    auto freq_param = m_schema["parameters"]["frequency_penalty"];
    EXPECT_EQ(freq_param["type"], "float");
    EXPECT_EQ(freq_param["min"], -2.0);
    EXPECT_EQ(freq_param["max"], 2.0);

    // Test presence_penalty parameter
    ASSERT_TRUE(m_schema["parameters"].contains("presence_penalty"));
    auto pres_param = m_schema["parameters"]["presence_penalty"];
    EXPECT_EQ(pres_param["type"], "float");
    EXPECT_EQ(pres_param["min"], -2.0);
    EXPECT_EQ(pres_param["max"], 2.0);
}

// Test message format configuration
TEST_F(OpenAISchemaTest, MessageFormatConfiguration) {
    ASSERT_TRUE(m_schema.contains("message_format"));
    auto message_format = m_schema["message_format"];

    // Test message structure
    ASSERT_TRUE(message_format.contains("structure"));
    auto structure = message_format["structure"];
    EXPECT_TRUE(structure.contains("role"));
    EXPECT_TRUE(structure.contains("content"));

    // Test content types
    ASSERT_TRUE(message_format.contains("content_types"));
    auto content_types = message_format["content_types"];

    // Text content
    ASSERT_TRUE(content_types.contains("text"));
    auto text_format = content_types["text"];
    EXPECT_EQ(text_format["type"], "text");
    EXPECT_TRUE(text_format.contains("text"));

    // Image content (for vision models)
    ASSERT_TRUE(content_types.contains("image"));
    auto image_format = content_types["image"];
    EXPECT_EQ(image_format["type"], "image_url");
    EXPECT_TRUE(image_format.contains("image_url"));
    EXPECT_TRUE(image_format["image_url"].contains("url"));
}

// Test response format configuration
TEST_F(OpenAISchemaTest, ResponseFormatConfiguration) {
    ASSERT_TRUE(m_schema.contains("response_format"));
    auto response_format = m_schema["response_format"];

    // Success response
    ASSERT_TRUE(response_format.contains("success"));
    auto success_format = response_format["success"];

    // Test response paths
    ASSERT_TRUE(success_format.contains("text_path"));
    auto text_path = success_format["text_path"];
    EXPECT_EQ(text_path[0], "choices");
    EXPECT_EQ(text_path[1], 0);
    EXPECT_EQ(text_path[2], "message");
    EXPECT_EQ(text_path[3], "content");

    // Error response
    ASSERT_TRUE(response_format.contains("error"));
    auto error_format = response_format["error"];
    ASSERT_TRUE(error_format.contains("error_path"));
    auto error_path = error_format["error_path"];
    EXPECT_EQ(error_path[0], "error");
    EXPECT_EQ(error_path[1], "message");
}

// Test multimodal support configuration
TEST_F(OpenAISchemaTest, MultimodalConfiguration) {
    ASSERT_TRUE(m_schema.contains("multimodal"));
    auto multimodal = m_schema["multimodal"];

    EXPECT_EQ(multimodal["supported"], true);
    ASSERT_TRUE(multimodal.contains("supported_types"));

    auto supported_types = multimodal["supported_types"];
    EXPECT_EQ(supported_types.size(), 2); // Only text and image
    EXPECT_TRUE(std::find(supported_types.begin(), supported_types.end(), "text") != supported_types.end());
    EXPECT_TRUE(std::find(supported_types.begin(), supported_types.end(), "image") != supported_types.end());

    // Image formats - only the commonly supported ones
    ASSERT_TRUE(multimodal.contains("image_formats"));
    auto image_formats = multimodal["image_formats"];
    EXPECT_EQ(image_formats.size(), 3); // JPEG, PNG, WebP

    std::vector<std::string> expected_formats = {"image/jpeg", "image/png", "image/webp"};
    for (const auto& format : expected_formats) {
        EXPECT_TRUE(std::find(image_formats.begin(), image_formats.end(), format) != image_formats.end())
            << "Missing image format: " << format;
    }

    // GIF should NOT be present
    EXPECT_TRUE(std::find(image_formats.begin(), image_formats.end(), "image/gif") == image_formats.end())
        << "GIF format should not be listed";
}

// Test feature flags
TEST_F(OpenAISchemaTest, FeatureFlags) {
    ASSERT_TRUE(m_schema.contains("features"));
    auto features = m_schema["features"];

    // OpenAI should support these features
    EXPECT_EQ(features["streaming"], true);
    EXPECT_EQ(features["json_mode"], true);
    EXPECT_EQ(features["vision"], true);
    EXPECT_EQ(features["system_messages"], true);
    EXPECT_EQ(features["message_history"], true);

    // These features are NOT currently implemented
    EXPECT_FALSE(features.contains("function_calling")) << "Function calling not implemented";
    EXPECT_FALSE(features.contains("structured_outputs")) << "Structured outputs not implemented";
}

// Test request building with the schema
TEST_F(OpenAISchemaTest, RequestBuilding) {
    // Set up a basic request
    m_context->set_model("gpt-4o")
             .add_user_message("Hello, world!");

    auto request = m_context->build_request();

    // Verify request structure matches OpenAI expectations
    EXPECT_EQ(request["model"], "gpt-4o");
    EXPECT_TRUE(request.contains("messages"));
    EXPECT_EQ(request["messages"].size(), 1);
    EXPECT_EQ(request["messages"][0]["role"], "user");
    EXPECT_EQ(request["messages"][0]["content"][0]["type"], "text");
    EXPECT_EQ(request["messages"][0]["content"][0]["text"], "Hello, world!");

    // Verify default parameters are included
    EXPECT_TRUE(request.contains("temperature"));
    EXPECT_TRUE(request.contains("max_tokens"));
    EXPECT_TRUE(request.contains("stream"));
    EXPECT_EQ(request["stream"], false);
}

// Test system message handling
TEST_F(OpenAISchemaTest, SystemMessageHandling) {
    m_context->set_system_message("You are a helpful assistant.")
             .add_user_message("Hi!");

    auto request = m_context->build_request();

    // OpenAI includes system message as first message in array
    EXPECT_GE(request["messages"].size(), 2);
    EXPECT_EQ(request["messages"][0]["role"], "system");
    EXPECT_EQ(request["messages"][0]["content"], "You are a helpful assistant.");
    EXPECT_EQ(request["messages"][1]["role"], "user");
}

// Test multimodal request building
TEST_F(OpenAISchemaTest, MultimodalRequestBuilding) {
    // Create a small test image
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

    std::ofstream file("test_openai_image.png", std::ios::binary);
    file.write(reinterpret_cast<const char*>(png_data), sizeof(png_data));
    file.close();

    m_context->add_user_message("What's in this image?", "image/png", "test_openai_image.png");

    auto request = m_context->build_request();

    // Verify multimodal message structure
    EXPECT_EQ(request["messages"].size(), 1);
    auto content = request["messages"][0]["content"];
    EXPECT_EQ(content.size(), 2); // Text + Image

    // Text part
    EXPECT_EQ(content[0]["type"], "text");
    EXPECT_EQ(content[0]["text"], "What's in this image?");

    // Image part
    EXPECT_EQ(content[1]["type"], "image_url");
    EXPECT_TRUE(content[1].contains("image_url"));
    EXPECT_TRUE(content[1]["image_url"].contains("url"));

    std::string image_url = content[1]["image_url"]["url"];
    EXPECT_TRUE(image_url.starts_with("data:image/png;base64,"));

    // Clean up
    fs::remove("test_openai_image.png");
}

// Test streaming configuration
TEST_F(OpenAISchemaTest, StreamingConfiguration) {
    m_context->add_user_message("Hello");

    // Test non-streaming request
    auto request1 = m_context->build_request(false);
    EXPECT_EQ(request1["stream"], false);

    // Test streaming request
    auto request2 = m_context->build_request(true);
    EXPECT_EQ(request2["stream"], true);

    // Test explicit parameter setting
    m_context->set_parameter("stream", true);
    auto request3 = m_context->build_request(false); // Function param should be overridden
    EXPECT_EQ(request3["stream"], true);
}

// Test JSON mode configuration
TEST_F(OpenAISchemaTest, JSONModeConfiguration) {
    m_context->add_user_message("Return a JSON object");

    // Test setting response format
    m_context->set_parameter("response_format", json{{"type", "json_object"}});

    auto request = m_context->build_request();
    EXPECT_TRUE(request.contains("response_format"));
    EXPECT_EQ(request["response_format"]["type"], "json_object");
}

// Test parameter validation
TEST_F(OpenAISchemaTest, ParameterValidationRules) {
    m_context->add_user_message("Test");

    // Valid parameters
    EXPECT_NO_THROW(m_context->set_parameter("temperature", 0.5));
    EXPECT_NO_THROW(m_context->set_parameter("temperature", 0.0));
    EXPECT_NO_THROW(m_context->set_parameter("temperature", 2.0));

    // Invalid parameters (out of range)
    EXPECT_THROW(m_context->set_parameter("temperature", -0.1), validation_exception);
    EXPECT_THROW(m_context->set_parameter("temperature", 2.1), validation_exception);

    // Valid frequency penalty
    EXPECT_NO_THROW(m_context->set_parameter("frequency_penalty", 0.0));
    EXPECT_NO_THROW(m_context->set_parameter("frequency_penalty", -2.0));
    EXPECT_NO_THROW(m_context->set_parameter("frequency_penalty", 2.0));

    // Invalid frequency penalty
    EXPECT_THROW(m_context->set_parameter("frequency_penalty", -2.1), validation_exception);
    EXPECT_THROW(m_context->set_parameter("frequency_penalty", 2.1), validation_exception);

    // Valid max_tokens
    EXPECT_NO_THROW(m_context->set_parameter("max_tokens", 100));
    EXPECT_NO_THROW(m_context->set_parameter("max_tokens", 1));
    EXPECT_NO_THROW(m_context->set_parameter("max_tokens", 4096));

    // Invalid max_tokens
    EXPECT_THROW(m_context->set_parameter("max_tokens", 0), validation_exception);
    EXPECT_THROW(m_context->set_parameter("max_tokens", -1), validation_exception);
}

// Test headers configuration
TEST_F(OpenAISchemaTest, HeadersConfiguration) {
    ASSERT_TRUE(m_schema.contains("headers"));
    auto headers = m_schema["headers"];

    ASSERT_TRUE(headers.contains("required"));
    auto required_headers = headers["required"];

    // OpenAI requires Authorization and Content-Type
    EXPECT_TRUE(required_headers.contains("Authorization"));
    EXPECT_TRUE(required_headers.contains("Content-Type"));

    EXPECT_EQ(required_headers["Content-Type"], "application/json");

    // Authorization should have placeholder
    std::string auth_header = required_headers["Authorization"];
    EXPECT_TRUE(auth_header.starts_with("Bearer "));
}

// Test error codes configuration
TEST_F(OpenAISchemaTest, ErrorCodesConfiguration) {
    ASSERT_TRUE(m_schema.contains("error_codes"));
    auto error_codes = m_schema["error_codes"];

    // Standard HTTP error codes
    EXPECT_EQ(error_codes["400"], "invalid_request_error");
    EXPECT_EQ(error_codes["401"], "authentication_error");
    EXPECT_EQ(error_codes["403"], "permission_error");
    EXPECT_EQ(error_codes["404"], "not_found_error");
    EXPECT_EQ(error_codes["429"], "rate_limit_error");
    EXPECT_EQ(error_codes["500"], "server_error");
}

// Test limits configuration
TEST_F(OpenAISchemaTest, LimitsConfiguration) {
    ASSERT_TRUE(m_schema.contains("limits"));
    auto limits = m_schema["limits"];

    // Context length limits
    EXPECT_TRUE(limits.contains("max_context_length"));
    EXPECT_GE(limits["max_context_length"].get<int>(), 4096); // Minimum expected

    // Output token limits
    EXPECT_TRUE(limits.contains("max_output_tokens"));
    EXPECT_GE(limits["max_output_tokens"].get<int>(), 1024);

    // Rate limits
    if (limits.contains("rate_limits")) {
        auto rate_limits = limits["rate_limits"];
        EXPECT_TRUE(rate_limits.contains("requests_per_minute"));
        EXPECT_TRUE(rate_limits.contains("tokens_per_minute"));
    }
}

TEST_F(OpenAISchemaTest, SchemaFreshnessCheck) {
    ASSERT_TRUE(m_schema["provider"].contains("last_validated"));

    std::string last_validated = m_schema["provider"]["last_validated"];

    // Parse date (format: YYYY-MM-DD)
    std::tm tm = {};
    std::istringstream ss(last_validated);
    ss >> std::get_time(&tm, "%Y-%m-%d");

    auto last_validated_time = std::mktime(&tm);
    auto now = std::time(nullptr);

    double days_old = std::difftime(now, last_validated_time) / (60 * 60 * 24);

    if (days_old > 30) {
        std::cout << "WARNING: Schema was last validated " << days_old
                  << " days ago. Consider re-validating against current API." << std::endl;
    }

    EXPECT_LT(days_old, 90) << "Schema is more than 90 days old and should be updated";
}
