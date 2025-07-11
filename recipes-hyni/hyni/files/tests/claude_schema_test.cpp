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

class ClaudeSchemaTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Load Claude schema directly
        m_schema_path = "../schemas/claude.json";

        std::ifstream file(m_schema_path);
        if (!file.is_open()) {
            GTEST_SKIP() << "Claude schema file not found at: " << m_schema_path;
        }

        try {
            file >> m_schema;
        } catch (const json::parse_error& e) {
            FAIL() << "Failed to parse Claude schema: " << e.what();
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
TEST_F(ClaudeSchemaTest, SchemaStructureValidation) {
    // Test top-level required fields
    EXPECT_TRUE(m_schema.contains("provider"));
    EXPECT_TRUE(m_schema.contains("api"));
    EXPECT_TRUE(m_schema.contains("authentication"));
    EXPECT_TRUE(m_schema.contains("models"));
    EXPECT_TRUE(m_schema.contains("request_template"));
    EXPECT_TRUE(m_schema.contains("message_format"));
    EXPECT_TRUE(m_schema.contains("response_format"));

    // Test provider information
    EXPECT_EQ(m_schema["provider"]["name"], "claude");
    EXPECT_TRUE(m_schema["provider"].contains("display_name"));
    EXPECT_TRUE(m_schema["provider"].contains("version"));

    // Test API configuration
    EXPECT_EQ(m_schema["api"]["endpoint"], "https://api.anthropic.com/v1/messages");
    EXPECT_EQ(m_schema["api"]["method"], "POST");
    EXPECT_TRUE(m_schema["api"].contains("timeout"));

    // Test authentication
    EXPECT_EQ(m_schema["authentication"]["type"], "header");
    EXPECT_EQ(m_schema["authentication"]["key_name"], "x-api-key");
}

// Test model configuration
TEST_F(ClaudeSchemaTest, ModelConfiguration) {
    ASSERT_TRUE(m_schema["models"].contains("available"));
    ASSERT_TRUE(m_schema["models"].contains("default"));

    auto available_models = m_schema["models"]["available"];
    EXPECT_FALSE(available_models.empty());

    // Check for expected Claude models
    std::vector<std::string> expected_models = {
        "claude-3-5-sonnet-20241022",
        "claude-3-5-haiku-20241022",
        "claude-3-opus-20240229"
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
    EXPECT_EQ(default_model, "claude-3-5-sonnet-20241022");
}

// Test request template structure
TEST_F(ClaudeSchemaTest, RequestTemplateStructure) {
    auto request_template = m_schema["request_template"];

    // Required fields for Claude
    EXPECT_TRUE(request_template.contains("model"));
    EXPECT_TRUE(request_template.contains("messages"));
    EXPECT_TRUE(request_template.contains("max_tokens"));

    // Claude-specific fields
    EXPECT_TRUE(request_template.contains("system"));
    EXPECT_TRUE(request_template.contains("stop_sequences"));
    EXPECT_TRUE(request_template.contains("stream"));

    // Optional parameters
    EXPECT_TRUE(request_template.contains("temperature"));
    EXPECT_TRUE(request_template.contains("top_p"));
    EXPECT_TRUE(request_template.contains("top_k"));

    // Verify defaults
    EXPECT_EQ(request_template["messages"], json::array());
    EXPECT_EQ(request_template["max_tokens"], 1024);
    EXPECT_EQ(request_template["stream"], false);
}

// Test parameter definitions and validation
TEST_F(ClaudeSchemaTest, ParameterValidation) {
    ASSERT_TRUE(m_schema.contains("parameters"));

    // Test max_tokens parameter (required for Claude)
    ASSERT_TRUE(m_schema["parameters"].contains("max_tokens"));
    auto max_tokens_param = m_schema["parameters"]["max_tokens"];
    EXPECT_EQ(max_tokens_param["type"], "integer");
    EXPECT_EQ(max_tokens_param["required"], true);  // Claude requires this
    EXPECT_EQ(max_tokens_param["min"], 1);
    EXPECT_EQ(max_tokens_param["max"], 8192);

    // Test temperature parameter
    ASSERT_TRUE(m_schema["parameters"].contains("temperature"));
    auto temp_param = m_schema["parameters"]["temperature"];
    EXPECT_EQ(temp_param["type"], "float");
    EXPECT_EQ(temp_param["min"], 0.0);
    EXPECT_EQ(temp_param["max"], 1.0);  // Claude max is 1.0, not 2.0 like OpenAI

    // Test top_k parameter (Claude-specific)
    ASSERT_TRUE(m_schema["parameters"].contains("top_k"));
    auto top_k_param = m_schema["parameters"]["top_k"];
    EXPECT_EQ(top_k_param["type"], "integer");
    EXPECT_GE(top_k_param["min"].get<int>(), 1);

    // Test stop_sequences parameter
    ASSERT_TRUE(m_schema["parameters"].contains("stop_sequences"));
    auto stop_param = m_schema["parameters"]["stop_sequences"];
    EXPECT_EQ(stop_param["type"], "array");
    EXPECT_EQ(stop_param["max_items"], 4);
}

// Test message format configuration
TEST_F(ClaudeSchemaTest, MessageFormatConfiguration) {
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

    // Image content (Claude format)
    ASSERT_TRUE(content_types.contains("image"));
    auto image_format = content_types["image"];
    EXPECT_EQ(image_format["type"], "image");
    EXPECT_TRUE(image_format.contains("source"));
    EXPECT_EQ(image_format["source"]["type"], "base64");
}

// Test response format configuration
TEST_F(ClaudeSchemaTest, ResponseFormatConfiguration) {
    ASSERT_TRUE(m_schema.contains("response_format"));
    auto response_format = m_schema["response_format"];

    // Success response
    ASSERT_TRUE(response_format.contains("success"));
    auto success_format = response_format["success"];

    // Test response paths
    ASSERT_TRUE(success_format.contains("text_path"));
    auto text_path = success_format["text_path"];
    EXPECT_EQ(text_path[0], "content");
    EXPECT_EQ(text_path[1], 0);
    EXPECT_EQ(text_path[2], "text");

    // Claude-specific paths
    ASSERT_TRUE(success_format.contains("stop_reason_path"));
    auto stop_reason_path = success_format["stop_reason_path"];
    EXPECT_EQ(stop_reason_path[0], "stop_reason");

    // Error response
    ASSERT_TRUE(response_format.contains("error"));
    auto error_format = response_format["error"];
    ASSERT_TRUE(error_format.contains("error_path"));
}

// Test multimodal support configuration
TEST_F(ClaudeSchemaTest, MultimodalConfiguration) {
    ASSERT_TRUE(m_schema.contains("multimodal"));
    auto multimodal = m_schema["multimodal"];

    EXPECT_EQ(multimodal["supported"], true);
    ASSERT_TRUE(multimodal.contains("supported_types"));

    auto supported_types = multimodal["supported_types"];
    EXPECT_EQ(supported_types.size(), 2); // text and image

    // Image formats
    ASSERT_TRUE(multimodal.contains("image_formats"));
    auto image_formats = multimodal["image_formats"];
    EXPECT_EQ(image_formats.size(), 4); // JPEG, PNG, GIF, WebP

    // Claude-specific limits
    EXPECT_EQ(multimodal["max_image_size"], 5242880); // 5MB
    EXPECT_EQ(multimodal["max_images_per_message"], 20);
}

// Test feature flags
TEST_F(ClaudeSchemaTest, FeatureFlags) {
    ASSERT_TRUE(m_schema.contains("features"));
    auto features = m_schema["features"];

    // Claude should support these features
    EXPECT_EQ(features["streaming"], true);
    EXPECT_EQ(features["vision"], true);
    EXPECT_EQ(features["system_messages"], true);
    EXPECT_EQ(features["message_history"], true);

    // Claude does NOT support these
    EXPECT_EQ(features["function_calling"], false);
    EXPECT_EQ(features["json_mode"], false);
}

// Test headers configuration
TEST_F(ClaudeSchemaTest, HeadersConfiguration) {
    ASSERT_TRUE(m_schema.contains("headers"));
    auto headers = m_schema["headers"];

    ASSERT_TRUE(headers.contains("required"));
    auto required_headers = headers["required"];

    // Claude requires specific headers
    EXPECT_TRUE(required_headers.contains("x-api-key"));
    EXPECT_TRUE(required_headers.contains("Anthropic-Version"));
    EXPECT_TRUE(required_headers.contains("Content-Type"));

    EXPECT_EQ(required_headers["Anthropic-Version"], "2023-06-01");
    EXPECT_EQ(required_headers["Content-Type"], "application/json");

    // Optional beta header
    ASSERT_TRUE(headers.contains("optional"));
    EXPECT_TRUE(headers["optional"].contains("Anthropic-Beta"));
}

// Test request building with the schema
TEST_F(ClaudeSchemaTest, RequestBuilding) {
    // Set up a basic request
    m_context->set_model("claude-3-5-sonnet-20241022")
             .add_user_message("Hello, Claude!");

    auto request = m_context->build_request();

    // Verify request structure matches Claude expectations
    EXPECT_EQ(request["model"], "claude-3-5-sonnet-20241022");
    EXPECT_TRUE(request.contains("messages"));
    EXPECT_EQ(request["messages"].size(), 1);
    EXPECT_EQ(request["messages"][0]["role"], "user");

    // Claude requires max_tokens
    EXPECT_TRUE(request.contains("max_tokens"));
    EXPECT_GE(request["max_tokens"].get<int>(), 1);

    // Verify content format
    EXPECT_TRUE(request["messages"][0]["content"].is_array());
    EXPECT_EQ(request["messages"][0]["content"][0]["type"], "text");
    EXPECT_EQ(request["messages"][0]["content"][0]["text"], "Hello, Claude!");
}

// Test system message handling
TEST_F(ClaudeSchemaTest, SystemMessageHandling) {
    m_context->set_system_message("You are a helpful assistant.")
             .add_user_message("Hi!");

    auto request = m_context->build_request();

    // Claude uses separate system field
    EXPECT_TRUE(request.contains("system"));
    EXPECT_EQ(request["system"], "You are a helpful assistant.");

    // System message should NOT be in messages array
    EXPECT_EQ(request["messages"].size(), 1);
    EXPECT_EQ(request["messages"][0]["role"], "user");
}

// Test multimodal request building
TEST_F(ClaudeSchemaTest, MultimodalRequestBuilding) {
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

    std::ofstream file("test_claude_image.png", std::ios::binary);
    file.write(reinterpret_cast<const char*>(png_data), sizeof(png_data));
    file.close();

    m_context->add_user_message("What's in this image?", "image/png", "test_claude_image.png");

    auto request = m_context->build_request();

    // Verify multimodal message structure
    EXPECT_EQ(request["messages"].size(), 1);
    auto content = request["messages"][0]["content"];
    EXPECT_EQ(content.size(), 2); // Text + Image

    // Text part
    EXPECT_EQ(content[0]["type"], "text");
    EXPECT_EQ(content[0]["text"], "What's in this image?");

    // Image part (Claude format)
    EXPECT_EQ(content[1]["type"], "image");
    EXPECT_TRUE(content[1].contains("source"));
    EXPECT_EQ(content[1]["source"]["type"], "base64");
    EXPECT_EQ(content[1]["source"]["media_type"], "image/png");
    EXPECT_TRUE(content[1]["source"].contains("data"));

    // Clean up
    fs::remove("test_claude_image.png");
}

// Test parameter validation
TEST_F(ClaudeSchemaTest, ParameterValidationRules) {
    m_context->add_user_message("Test");

    // Valid parameters
    EXPECT_NO_THROW(m_context->set_parameter("temperature", 0.5));
    EXPECT_NO_THROW(m_context->set_parameter("temperature", 0.0));
    EXPECT_NO_THROW(m_context->set_parameter("temperature", 1.0));

    // Invalid parameters (out of range for Claude)
    EXPECT_THROW(m_context->set_parameter("temperature", -0.1), validation_exception);
    EXPECT_THROW(m_context->set_parameter("temperature", 1.1), validation_exception);

    // Valid max_tokens
    EXPECT_NO_THROW(m_context->set_parameter("max_tokens", 100));
    EXPECT_NO_THROW(m_context->set_parameter("max_tokens", 8192));

    // Invalid max_tokens
    EXPECT_THROW(m_context->set_parameter("max_tokens", 0), validation_exception);
    EXPECT_THROW(m_context->set_parameter("max_tokens", 8193), validation_exception);

    // Claude-specific: top_k
    EXPECT_NO_THROW(m_context->set_parameter("top_k", 10));
    EXPECT_THROW(m_context->set_parameter("top_k", 0), validation_exception);
}

// Test limits configuration
TEST_F(ClaudeSchemaTest, LimitsConfiguration) {
    ASSERT_TRUE(m_schema.contains("limits"));
    auto limits = m_schema["limits"];

    // Claude's impressive context length
    EXPECT_EQ(limits["max_context_length"], 200000); // 200k tokens!
    EXPECT_EQ(limits["max_output_tokens"], 8192);

    // Rate limits
    if (limits.contains("rate_limits")) {
        auto rate_limits = limits["rate_limits"];
        EXPECT_TRUE(rate_limits.contains("requests_per_minute"));
        EXPECT_TRUE(rate_limits.contains("tokens_per_minute"));
    }
}

// Test streaming configuration
TEST_F(ClaudeSchemaTest, StreamingConfiguration) {
    ASSERT_TRUE(m_schema["response_format"].contains("stream"));
    auto stream_format = m_schema["response_format"]["stream"];

    // Claude has more complex streaming events
    ASSERT_TRUE(stream_format.contains("event_types"));
    auto event_types = stream_format["event_types"];

    std::vector<std::string> expected_events = {
        "message_start", "content_block_start", "ping",
        "content_block_delta", "content_block_stop",
        "message_delta", "message_stop"
    };

    for (const auto& event : expected_events) {
        EXPECT_TRUE(std::find(event_types.begin(), event_types.end(), event) != event_types.end())
            << "Missing event type: " << event;
    }
}

// Test message roles
TEST_F(ClaudeSchemaTest, MessageRoles) {
    ASSERT_TRUE(m_schema.contains("message_roles"));
    auto roles = m_schema["message_roles"];

    // Claude only supports user and assistant in messages
    EXPECT_EQ(roles.size(), 2);
    EXPECT_TRUE(std::find(roles.begin(), roles.end(), "user") != roles.end());
    EXPECT_TRUE(std::find(roles.begin(), roles.end(), "assistant") != roles.end());
    // System is separate, not a message role
    EXPECT_TRUE(std::find(roles.begin(), roles.end(), "system") == roles.end());
}

TEST_F(ClaudeSchemaTest, StopSequenceValidation) {
    m_context->add_user_message("Test");

    // Valid stop sequences
    EXPECT_NO_THROW(m_context->set_parameter("stop_sequences", json::array({"STOP"})));
    EXPECT_NO_THROW(m_context->set_parameter("stop_sequences", json::array({"END", "DONE"})));
    EXPECT_NO_THROW(m_context->set_parameter("stop_sequences", json::array()));  // Empty is OK

    // Test max items (4 according to schema)
    EXPECT_NO_THROW(m_context->set_parameter("stop_sequences",
                    json::array({"A", "B", "C", "D"})));

    // Note: We can't test whitespace-only validation at schema level
    // because that's an API-level validation, not a schema validation
    // The API will reject these at runtime:
    // - "\n\n" (whitespace only)
    // - " " (whitespace only)
    // - "\t" (whitespace only)
}

TEST_F(ClaudeSchemaTest, SchemaFreshnessCheck) {
    ASSERT_TRUE(m_schema["provider"].contains("last_validated"));

    std::string last_validated = m_schema["provider"]["last_validated"];

    // Parse date (format: YYYY-MM-DD)
    std::tm tm = {};
    std::istringstream ss(last_validated);
    ss >> std::get_time(&tm, "%Y-%m-%d");

    auto last_validated_time = std::mktime(&tm);
    auto now = std::time(nullptr);

    double days_old = std::difftime(now, last_validated_time) / (60 * 60 * 24);

    std::cout << "\n=== Claude Schema Freshness ===" << std::endl;
    std::cout << "Schema last validated: " << last_validated << std::endl;
    std::cout << "Days since validation: " << static_cast<int>(days_old) << std::endl;

    if (days_old > 30) {
        std::cout << "WARNING: Schema was last validated " << static_cast<int>(days_old)
                  << " days ago. Consider re-validating against current API." << std::endl;
    }

    if (days_old > 60) {
        std::cout << "CRITICAL: Schema is more than 60 days old!" << std::endl;
    }

    EXPECT_LT(days_old, 90) << "Schema is more than 90 days old and must be updated";
}

// Test API version in schema
TEST_F(ClaudeSchemaTest, APIVersionCheck) {
    ASSERT_TRUE(m_schema["provider"].contains("api_version"));

    std::string api_version = m_schema["provider"]["api_version"];
    EXPECT_EQ(api_version, "2023-06-01") << "API version should match current Claude API";

    // Check that headers use the same version
    ASSERT_TRUE(m_schema["headers"]["required"].contains("Anthropic-Version"));
    std::string header_version = m_schema["headers"]["required"]["Anthropic-Version"];
    EXPECT_EQ(header_version, api_version)
        << "Header version should match provider API version";
}

// Test schema completeness
TEST_F(ClaudeSchemaTest, SchemaCompleteness) {
    // Check for all required top-level fields
    std::vector<std::string> required_fields = {
        "provider", "api", "authentication", "headers", "models",
        "request_template", "parameters", "message_roles", "system_message",
        "multimodal", "message_format", "response_format", "limits",
        "features", "error_codes", "validation"
    };

    for (const auto& field : required_fields) {
        EXPECT_TRUE(m_schema.contains(field))
            << "Missing required field: " << field;
    }

    // Check provider metadata
    EXPECT_TRUE(m_schema["provider"].contains("name"));
    EXPECT_TRUE(m_schema["provider"].contains("display_name"));
    EXPECT_TRUE(m_schema["provider"].contains("version"));
    EXPECT_TRUE(m_schema["provider"].contains("api_version"));
    EXPECT_TRUE(m_schema["provider"].contains("last_validated"));
}
