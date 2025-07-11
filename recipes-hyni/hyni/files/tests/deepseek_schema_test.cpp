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
#include <ctime>
#include <iomanip>

using namespace hyni;
using json = nlohmann::json;
namespace fs = std::filesystem;

class DeepSeekSchemaTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Load DeepSeek schema directly
        m_schema_path = "../schemas/deepseek.json";

        std::ifstream file(m_schema_path);
        if (!file.is_open()) {
            GTEST_SKIP() << "DeepSeek schema file not found at: " << m_schema_path;
        }

        try {
            file >> m_schema;
        } catch (const json::parse_error& e) {
            FAIL() << "Failed to parse DeepSeek schema: " << e.what();
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
TEST_F(DeepSeekSchemaTest, SchemaStructureValidation) {
    // Test top-level required fields
    EXPECT_TRUE(m_schema.contains("provider"));
    EXPECT_TRUE(m_schema.contains("api"));
    EXPECT_TRUE(m_schema.contains("authentication"));
    EXPECT_TRUE(m_schema.contains("models"));
    EXPECT_TRUE(m_schema.contains("request_template"));
    EXPECT_TRUE(m_schema.contains("message_format"));
    EXPECT_TRUE(m_schema.contains("response_format"));

    // Test provider information
    EXPECT_EQ(m_schema["provider"]["name"], "deepseek");
    EXPECT_TRUE(m_schema["provider"].contains("display_name"));
    EXPECT_TRUE(m_schema["provider"].contains("version"));

    // Test API configuration
    EXPECT_EQ(m_schema["api"]["endpoint"], "https://api.deepseek.com/v1/chat/completions");
    EXPECT_EQ(m_schema["api"]["method"], "POST");
    EXPECT_TRUE(m_schema["api"].contains("timeout"));

    // Test authentication - DeepSeek uses Bearer token like OpenAI
    EXPECT_EQ(m_schema["authentication"]["type"], "header");
    EXPECT_EQ(m_schema["authentication"]["key_name"], "Authorization");
    EXPECT_EQ(m_schema["authentication"]["key_prefix"], "Bearer ");
}

// Test model configuration
TEST_F(DeepSeekSchemaTest, ModelConfiguration) {
    ASSERT_TRUE(m_schema["models"].contains("available"));
    ASSERT_TRUE(m_schema["models"].contains("default"));

    auto available_models = m_schema["models"]["available"];
    EXPECT_FALSE(available_models.empty());

    // Check for currently available models
    std::vector<std::string> expected_available = {
        "deepseek-chat",
        "deepseek-coder"
    };

    for (const auto& model : expected_available) {
        bool found = false;
        for (const auto& available : available_models) {
            if (available.get<std::string>() == model) {
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found) << "Model " << model << " not found in available models";
    }

    // Check deprecated models if they exist
    if (m_schema["models"].contains("deprecated")) {
        auto deprecated_models = m_schema["models"]["deprecated"];
        std::vector<std::string> expected_deprecated = {
            "deepseek-math",
            "deepseek-v2",
            "deepseek-v2-light"
        };

        for (const auto& model : expected_deprecated) {
            bool found = false;
            for (const auto& deprecated : deprecated_models) {
                if (deprecated.get<std::string>() == model) {
                    found = true;
                    break;
                }
            }
            EXPECT_TRUE(found) << "Model " << model << " not found in deprecated models";
        }

        std::cout << "Schema includes " << deprecated_models.size()
                  << " deprecated models" << std::endl;
    }

    // Test default model
    std::string default_model = m_schema["models"]["default"];
    EXPECT_FALSE(default_model.empty());
    EXPECT_EQ(default_model, "deepseek-chat");

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
TEST_F(DeepSeekSchemaTest, RequestTemplateStructure) {
    auto request_template = m_schema["request_template"];

    // Required fields for DeepSeek
    EXPECT_TRUE(request_template.contains("model"));
    EXPECT_TRUE(request_template.contains("messages"));

    // Optional parameters
    EXPECT_TRUE(request_template.contains("temperature"));
    EXPECT_TRUE(request_template.contains("top_p"));
    EXPECT_TRUE(request_template.contains("max_tokens"));
    EXPECT_TRUE(request_template.contains("stream"));
    EXPECT_TRUE(request_template.contains("frequency_penalty"));
    EXPECT_TRUE(request_template.contains("presence_penalty"));
    EXPECT_TRUE(request_template.contains("stop"));

    // Verify defaults
    EXPECT_EQ(request_template["messages"], json::array());
    EXPECT_EQ(request_template["max_tokens"], 2048);
    EXPECT_EQ(request_template["temperature"], 0.7);
    EXPECT_EQ(request_template["stream"], false);
}

// Test parameter definitions and validation
TEST_F(DeepSeekSchemaTest, ParameterValidation) {
    ASSERT_TRUE(m_schema.contains("parameters"));

    // Test max_tokens parameter
    ASSERT_TRUE(m_schema["parameters"].contains("max_tokens"));
    auto max_tokens_param = m_schema["parameters"]["max_tokens"];
    EXPECT_EQ(max_tokens_param["type"], "integer");
    EXPECT_EQ(max_tokens_param["required"], false);  // Optional for DeepSeek
    EXPECT_EQ(max_tokens_param["min"], 1);
    EXPECT_EQ(max_tokens_param["max"], 4096);

    // Test temperature parameter
    ASSERT_TRUE(m_schema["parameters"].contains("temperature"));
    auto temp_param = m_schema["parameters"]["temperature"];
    EXPECT_EQ(temp_param["type"], "float");
    EXPECT_EQ(temp_param["min"], 0.0);
    EXPECT_EQ(temp_param["max"], 2.0);  // DeepSeek allows up to 2.0 like OpenAI

    // Test stop parameter
    ASSERT_TRUE(m_schema["parameters"].contains("stop"));
    auto stop_param = m_schema["parameters"]["stop"];
    // DeepSeek allows both string and array for stop
    EXPECT_TRUE(stop_param["type"].is_array() || stop_param["type"].is_string());
}

// Test message format configuration
TEST_F(DeepSeekSchemaTest, MessageFormatConfiguration) {
    ASSERT_TRUE(m_schema.contains("message_format"));
    auto message_format = m_schema["message_format"];

    // Test message structure
    ASSERT_TRUE(message_format.contains("structure"));
    auto structure = message_format["structure"];
    EXPECT_TRUE(structure.contains("role"));
    EXPECT_TRUE(structure.contains("content"));

    // DeepSeek uses simple string content, not array
    EXPECT_EQ(structure["content"], "<TEXT_CONTENT>");

    // Test content types
    ASSERT_TRUE(message_format.contains("content_types"));
    auto content_types = message_format["content_types"];

    // Text content only
    ASSERT_TRUE(content_types.contains("text"));
    EXPECT_EQ(content_types.size(), 1); // Only text, no multimodal
}

// Test response format configuration
TEST_F(DeepSeekSchemaTest, ResponseFormatConfiguration) {
    ASSERT_TRUE(m_schema.contains("response_format"));
    auto response_format = m_schema["response_format"];

    // Success response
    ASSERT_TRUE(response_format.contains("success"));
    auto success_format = response_format["success"];

    // Test response paths - similar to OpenAI
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
}

// Test multimodal support configuration
TEST_F(DeepSeekSchemaTest, MultimodalConfiguration) {
    ASSERT_TRUE(m_schema.contains("multimodal"));
    auto multimodal = m_schema["multimodal"];

    // DeepSeek does NOT support multimodal
    EXPECT_EQ(multimodal["supported"], false);
    ASSERT_TRUE(multimodal.contains("supported_types"));

    auto supported_types = multimodal["supported_types"];
    EXPECT_EQ(supported_types.size(), 1); // Only text
    EXPECT_TRUE(std::find(supported_types.begin(), supported_types.end(), "text") != supported_types.end());

    // No image support
    EXPECT_TRUE(multimodal["image_formats"].empty());
    EXPECT_EQ(multimodal["max_image_size"], 0);
    EXPECT_EQ(multimodal["max_images_per_message"], 0);
}

// Test feature flags
TEST_F(DeepSeekSchemaTest, FeatureFlags) {
    ASSERT_TRUE(m_schema.contains("features"));
    auto features = m_schema["features"];

    // DeepSeek features
    EXPECT_EQ(features["streaming"], true);
    EXPECT_EQ(features["system_messages"], true);
    EXPECT_EQ(features["message_history"], true);

    // Features NOT supported
    EXPECT_EQ(features["function_calling"], false);
    EXPECT_EQ(features["json_mode"], false);
    EXPECT_EQ(features["vision"], false);
}

// Test headers configuration
TEST_F(DeepSeekSchemaTest, HeadersConfiguration) {
    ASSERT_TRUE(m_schema.contains("headers"));
    auto headers = m_schema["headers"];

    ASSERT_TRUE(headers.contains("required"));
    auto required_headers = headers["required"];

    // DeepSeek uses OpenAI-style headers
    EXPECT_TRUE(required_headers.contains("Authorization"));
    EXPECT_TRUE(required_headers.contains("Content-Type"));

    EXPECT_EQ(required_headers["Content-Type"], "application/json");

    // Authorization should have Bearer prefix
    std::string auth_header = required_headers["Authorization"];
    EXPECT_TRUE(auth_header.starts_with("Bearer "));
}

// Test request building with the schema
TEST_F(DeepSeekSchemaTest, RequestBuilding) {
    // Set up a basic request
    m_context->set_model("deepseek-chat")
             .add_user_message("Hello, DeepSeek!");

    auto request = m_context->build_request();

    // Verify request structure matches DeepSeek expectations
    EXPECT_EQ(request["model"], "deepseek-chat");
    EXPECT_TRUE(request.contains("messages"));
    EXPECT_EQ(request["messages"].size(), 1);
    EXPECT_EQ(request["messages"][0]["role"], "user");

    // DeepSeek uses simple string content
    EXPECT_TRUE(request["messages"][0]["content"].is_string());
    EXPECT_EQ(request["messages"][0]["content"], "Hello, DeepSeek!");

    // Optional parameters should have defaults
    EXPECT_TRUE(request.contains("max_tokens"));
    EXPECT_TRUE(request.contains("temperature"));
}

// Test system message handling
TEST_F(DeepSeekSchemaTest, SystemMessageHandling) {
    m_context->set_system_message("You are a helpful assistant.")
             .add_user_message("Hi!");

    auto request = m_context->build_request();

    // DeepSeek includes system message in messages array (like OpenAI)
    EXPECT_GE(request["messages"].size(), 2);
    EXPECT_EQ(request["messages"][0]["role"], "system");
    EXPECT_EQ(request["messages"][0]["content"], "You are a helpful assistant.");
    EXPECT_EQ(request["messages"][1]["role"], "user");
    EXPECT_EQ(request["messages"][1]["content"], "Hi!");
}

// Test parameter validation rules
TEST_F(DeepSeekSchemaTest, ParameterValidationRules) {
    m_context->add_user_message("Test");

    // Valid parameters
    EXPECT_NO_THROW(m_context->set_parameter("temperature", 1.0));
    EXPECT_NO_THROW(m_context->set_parameter("temperature", 0.0));
    EXPECT_NO_THROW(m_context->set_parameter("temperature", 2.0));

    // Invalid parameters
    EXPECT_THROW(m_context->set_parameter("temperature", -0.1), validation_exception);
    EXPECT_THROW(m_context->set_parameter("temperature", 2.1), validation_exception);

    // Valid max_tokens
    EXPECT_NO_THROW(m_context->set_parameter("max_tokens", 100));
    EXPECT_NO_THROW(m_context->set_parameter("max_tokens", 4096));

    // Invalid max_tokens
    EXPECT_THROW(m_context->set_parameter("max_tokens", 0), validation_exception);
    EXPECT_THROW(m_context->set_parameter("max_tokens", 4097), validation_exception);
}

// Test streaming configuration
TEST_F(DeepSeekSchemaTest, StreamingConfiguration) {
    m_context->add_user_message("Hello");

    // Test non-streaming request
    auto request1 = m_context->build_request(false);
    EXPECT_EQ(request1["stream"], false);

    // Test streaming request
    auto request2 = m_context->build_request(true);
    EXPECT_EQ(request2["stream"], true);

    // Test explicit parameter setting
    m_context->set_parameter("stream", true);
    auto request3 = m_context->build_request(false);
    EXPECT_EQ(request3["stream"], true);
}

// Test limits configuration
TEST_F(DeepSeekSchemaTest, LimitsConfiguration) {
    ASSERT_TRUE(m_schema.contains("limits"));
    auto limits = m_schema["limits"];

    // Context length
    EXPECT_EQ(limits["max_context_length"], 128000); // 128k tokens
    EXPECT_EQ(limits["max_output_tokens"], 4096);

    // Rate limits
    if (limits.contains("rate_limits")) {
        auto rate_limits = limits["rate_limits"];
        EXPECT_TRUE(rate_limits.contains("requests_per_minute"));
        EXPECT_TRUE(rate_limits.contains("tokens_per_minute"));
    }
}

// Test message roles
TEST_F(DeepSeekSchemaTest, MessageRoles) {
    ASSERT_TRUE(m_schema.contains("message_roles"));
    auto roles = m_schema["message_roles"];

    // DeepSeek supports user, assistant, and system
    EXPECT_EQ(roles.size(), 3);
    EXPECT_TRUE(std::find(roles.begin(), roles.end(), "user") != roles.end());
    EXPECT_TRUE(std::find(roles.begin(), roles.end(), "assistant") != roles.end());
    EXPECT_TRUE(std::find(roles.begin(), roles.end(), "system") != roles.end());
}

// Test specialized models
TEST_F(DeepSeekSchemaTest, SpecializedModels) {
    // Get the list of available models from schema
    auto available_models = m_context->get_supported_models();

    // Test DeepSeek Coder model (should be available)
    if (std::find(available_models.begin(), available_models.end(), "deepseek-coder") != available_models.end()) {
        m_context->reset();
        EXPECT_NO_THROW(m_context->set_model("deepseek-coder"));
        m_context->add_user_message("Write a Python function");

        auto request = m_context->build_request();
        EXPECT_EQ(request["model"], "deepseek-coder");
        std::cout << "✓ deepseek-coder is available and can be set" << std::endl;
    } else {
        FAIL() << "deepseek-coder should be in available models";
    }

    // Test that deprecated models are NOT in available list
    auto deprecated_models = std::vector<std::string>{
        "deepseek-math", "deepseek-v2", "deepseek-v2-light"
    };

    for (const auto& deprecated : deprecated_models) {
        bool in_available = std::find(available_models.begin(), available_models.end(),
                                      deprecated) != available_models.end();
        EXPECT_FALSE(in_available)
            << "Deprecated model " << deprecated << " should not be in available models";

        // Deprecated models should throw validation exception
        m_context->reset();
        EXPECT_THROW(m_context->set_model(deprecated), validation_exception)
            << "Setting deprecated model " << deprecated << " should throw";
    }

    // Test that non-existent models throw exception
    m_context->reset();
    EXPECT_THROW(m_context->set_model("non-existent-model"), validation_exception);
}

TEST_F(DeepSeekSchemaTest, DeprecatedModelsHandling) {
    ASSERT_TRUE(m_schema["models"].contains("deprecated"))
        << "Schema should have deprecated models list";

    auto deprecated = m_schema["models"]["deprecated"];
    EXPECT_FALSE(deprecated.empty()) << "Deprecated list should not be empty";

    // Verify deprecated models are documented
    if (m_schema["models"].contains("description")) {
        std::string desc = m_schema["models"]["description"];
        EXPECT_TRUE(desc.find("deprecated") != std::string::npos ||
                    desc.find("Deprecated") != std::string::npos)
            << "Description should mention deprecated status";
    }

    // Ensure deprecated models are NOT in available list
    auto available = m_schema["models"]["available"];
    for (const auto& dep_model : deprecated) {
        std::string model_name = dep_model.get<std::string>();
        bool in_available = false;

        for (const auto& avail : available) {
            if (avail.get<std::string>() == model_name) {
                in_available = true;
                break;
            }
        }

        EXPECT_FALSE(in_available)
            << "Model " << model_name << " cannot be both available and deprecated";
    }

    std::cout << "Deprecated models: ";
    for (size_t i = 0; i < deprecated.size(); ++i) {
        std::cout << deprecated[i].get<std::string>();
        if (i < deprecated.size() - 1) std::cout << ", ";
    }
    std::cout << std::endl;
}

// Test schema freshness
TEST_F(DeepSeekSchemaTest, SchemaFreshnessCheck) {
    // Add version tracking to schema
    if (m_schema["provider"].contains("last_validated")) {
        std::string last_validated = m_schema["provider"]["last_validated"];

        // Parse date (format: YYYY-MM-DD)
        std::tm tm = {};
        std::istringstream ss(last_validated);
        ss >> std::get_time(&tm, "%Y-%m-%d");

        auto last_validated_time = std::mktime(&tm);
        auto now = std::time(nullptr);

        double days_old = std::difftime(now, last_validated_time) / (60 * 60 * 24);

        std::cout << "\n=== DeepSeek Schema Freshness ===" << std::endl;
        std::cout << "Schema last validated: " << last_validated << std::endl;
        std::cout << "Days since validation: " << static_cast<int>(days_old) << std::endl;

        if (days_old > 30) {
            std::cout << "WARNING: Schema was last validated " << static_cast<int>(days_old)
                     << " days ago. Consider re-validating against current API." << std::endl;
        }

        EXPECT_LT(days_old, 90) << "Schema is more than 90 days old and must be updated";
    }
}

TEST_F(DeepSeekSchemaTest, StopParameterValidation) {
    m_context->add_user_message("Test");

    // Test 1: String format
    EXPECT_NO_THROW(m_context->set_parameter("stop", "STOP"));

    // Test 2: Array format
    EXPECT_NO_THROW(m_context->set_parameter("stop", json::array({"STOP", "END"})));

    // Test 3: Empty array
    EXPECT_NO_THROW(m_context->set_parameter("stop", json::array()));

    // Test 4: Maximum items (4)
    EXPECT_NO_THROW(m_context->set_parameter("stop",
                    json::array({"A", "B", "C", "D"})));

    // Test 5: Too many items
    EXPECT_THROW(m_context->set_parameter("stop",
                 json::array({"A", "B", "C", "D", "E"})), validation_exception);

    // Test 6: Null value
    EXPECT_NO_THROW(m_context->set_parameter("stop", nullptr));

    // Test 7: Invalid type
    EXPECT_THROW(m_context->set_parameter("stop", 123), validation_exception);
    EXPECT_THROW(m_context->set_parameter("stop", true), validation_exception);

    // Test 8: Build request with each format
    m_context->reset();
    m_context->add_user_message("Test")
             .set_parameter("stop", "STOP");
    auto request1 = m_context->build_request();
    EXPECT_TRUE(request1["stop"].is_string());

    m_context->reset();
    m_context->add_user_message("Test")
             .set_parameter("stop", json::array({"STOP", "END"}));
    auto request2 = m_context->build_request();
    EXPECT_TRUE(request2["stop"].is_array());
    EXPECT_EQ(request2["stop"].size(), 2);
}

// Schema Compatibility Tests
TEST_F(DeepSeekSchemaTest, ApiVersionMatchesProviderSpec) {
    EXPECT_EQ(m_schema["api"]["endpoint"], "https://api.deepseek.com/v1/chat/completions");
    ASSERT_TRUE(m_schema["provider"].contains("api_version"));
    EXPECT_EQ(m_schema["provider"]["api_version"], "v1");
}

TEST_F(DeepSeekSchemaTest, AuthMethodIsBearerToken) {
    EXPECT_EQ(m_schema["authentication"]["type"], "header");
    EXPECT_EQ(m_schema["authentication"]["key_name"], "Authorization");
    EXPECT_EQ(m_schema["authentication"]["key_prefix"], "Bearer ");

    // Verify headers match authentication
    ASSERT_TRUE(m_schema["headers"]["required"].contains("Authorization"));
    std::string auth_header = m_schema["headers"]["required"]["Authorization"];
    EXPECT_TRUE(auth_header.starts_with("Bearer "));
}

TEST_F(DeepSeekSchemaTest, ResponsePathsMatchApiBehavior) {
    auto text_path = m_schema["response_format"]["success"]["text_path"];
    ASSERT_EQ(text_path.size(), 4);
    EXPECT_EQ(text_path[0], "choices");
    EXPECT_EQ(text_path[1], 0);
    EXPECT_EQ(text_path[2], "message");
    EXPECT_EQ(text_path[3], "content");

    // Verify error paths
    auto error_path = m_schema["response_format"]["error"]["error_path"];
    ASSERT_EQ(error_path.size(), 2);
    EXPECT_EQ(error_path[0], "error");
    EXPECT_EQ(error_path[1], "message");
}

TEST_F(DeepSeekSchemaTest, StopSequenceValidation) {
    ASSERT_TRUE(m_schema["parameters"].contains("stop"));
    auto stop_param = m_schema["parameters"]["stop"];

    // Verify multi-type support
    ASSERT_TRUE(stop_param["type"].is_array());
    auto types = stop_param["type"];
    EXPECT_EQ(types.size(), 2);
    EXPECT_TRUE(std::find(types.begin(), types.end(), "string") != types.end());
    EXPECT_TRUE(std::find(types.begin(), types.end(), "array") != types.end());

    // Verify constraints
    if (stop_param.contains("maxItems")) {
        EXPECT_LE(stop_param["maxItems"].get<int>(), 4);
    }

    // Test actual validation
    m_context->add_user_message("Test");

    // Valid cases
    EXPECT_NO_THROW(m_context->set_parameter("stop", "STOP"));
    EXPECT_NO_THROW(m_context->set_parameter("stop", json::array({"STOP", "END"})));
    EXPECT_NO_THROW(m_context->set_parameter("stop", json::array({"A", "B", "C", "D"})));

    // Invalid cases
    if (stop_param.contains("maxItems")) {
        EXPECT_THROW(m_context->set_parameter("stop",
                     json::array({"1", "2", "3", "4", "5"})), validation_exception);
    }
}

TEST_F(DeepSeekSchemaTest, ModelSpecificDefaults) {
    // Check if schema supports model-specific defaults
    if (m_schema.contains("model_specific_defaults")) {
        auto defaults = m_schema["model_specific_defaults"];

        // DeepSeek Coder should have lower temperature for deterministic output
        if (defaults.contains("deepseek-coder")) {
            auto coder_defaults = defaults["deepseek-coder"];
            if (coder_defaults.contains("temperature")) {
                EXPECT_LE(coder_defaults["temperature"].get<double>(), 0.3)
                    << "Coder model should have low temperature for deterministic output";
            }
        }
    } else {
        std::cout << "INFO: Schema doesn't define model-specific defaults" << std::endl;
    }
}

TEST_F(DeepSeekSchemaTest, TokenLimitsConsistency) {
    auto limits = m_schema["limits"];
    auto params = m_schema["parameters"];

    // max_tokens parameter should not exceed max_output_tokens limit
    int max_output = limits["max_output_tokens"].get<int>();
    int param_max = params["max_tokens"]["max"].get<int>();

    EXPECT_LE(param_max, max_output)
        << "Parameter max_tokens limit exceeds output token limit";

    // Context window should be reasonable
    int context_length = limits["max_context_length"].get<int>();
    EXPECT_GE(context_length, 32000) << "Context should be at least 32k tokens";
    EXPECT_LE(context_length, 256000) << "Context claim seems unrealistic";
}

TEST_F(DeepSeekSchemaTest, SchemaCompleteness) {
    // Essential fields that must exist
    std::vector<std::string> required_top_level = {
        "provider", "api", "authentication", "headers", "models",
        "request_template", "parameters", "message_format",
        "response_format", "limits", "features", "validation"
    };

    for (const auto& field : required_top_level) {
        EXPECT_TRUE(m_schema.contains(field))
            << "Missing required top-level field: " << field;
    }

    // Check for future-proofing fields
    std::vector<std::string> recommended_fields = {
        "error_codes", "message_roles", "system_message", "multimodal"
    };

    for (const auto& field : recommended_fields) {
        if (!m_schema.contains(field)) {
            std::cout << "WARNING: Recommended field '" << field
                     << "' is missing from schema" << std::endl;
        }
    }
}
