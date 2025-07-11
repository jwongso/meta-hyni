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

class MistralSchemaTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Load Mistral schema directly
        m_schema_path = "../schemas/mistral.json";

        std::ifstream file(m_schema_path);
        if (!file.is_open()) {
            GTEST_SKIP() << "Mistral schema file not found at: " << m_schema_path;
        }

        try {
            file >> m_schema;
        } catch (const json::parse_error& e) {
            FAIL() << "Failed to parse Mistral schema: " << e.what();
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
TEST_F(MistralSchemaTest, SchemaStructureValidation) {
    // Test top-level required fields
    EXPECT_TRUE(m_schema.contains("provider"));
    EXPECT_TRUE(m_schema.contains("api"));
    EXPECT_TRUE(m_schema.contains("authentication"));
    EXPECT_TRUE(m_schema.contains("models"));
    EXPECT_TRUE(m_schema.contains("request_template"));
    EXPECT_TRUE(m_schema.contains("message_format"));
    EXPECT_TRUE(m_schema.contains("response_format"));

    // Test provider information
    EXPECT_EQ(m_schema["provider"]["name"], "mistral");
    EXPECT_EQ(m_schema["provider"]["display_name"], "Mistral AI");
    EXPECT_TRUE(m_schema["provider"].contains("version"));

    // Test API configuration
    EXPECT_EQ(m_schema["api"]["endpoint"], "https://api.mistral.ai/v1/chat/completions");
    EXPECT_EQ(m_schema["api"]["method"], "POST");
    EXPECT_TRUE(m_schema["api"].contains("timeout"));

    // Test authentication - Mistral uses Bearer token like OpenAI
    EXPECT_EQ(m_schema["authentication"]["type"], "header");
    EXPECT_EQ(m_schema["authentication"]["key_name"], "Authorization");
    EXPECT_EQ(m_schema["authentication"]["key_prefix"], "Bearer ");
}

// Test model configuration
TEST_F(MistralSchemaTest, ModelConfiguration) {
    ASSERT_TRUE(m_schema["models"].contains("available"));
    ASSERT_TRUE(m_schema["models"].contains("default"));

    auto available_models = m_schema["models"]["available"];
    EXPECT_FALSE(available_models.empty());

    // Check for expected Mistral models
    std::vector<std::string> expected_models = {
        "mistral-small-latest",
        "mistral-medium-latest",
        "mistral-large-latest"
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
    EXPECT_EQ(default_model, "mistral-small-latest");

    // Verify versioned models
    std::vector<std::string> versioned_models = {
        "mistral-small-3.1",
        "mistral-medium-3",
        "mistral-large-2"
    };

    std::cout << "Mistral offers both versioned and 'latest' models:" << std::endl;
    for (const auto& model : available_models) {
        std::string model_name = model.get<std::string>();
        if (model_name.find("latest") != std::string::npos) {
            std::cout << "  • " << model_name << " (rolling release)" << std::endl;
        } else {
            std::cout << "  • " << model_name << " (fixed version)" << std::endl;
        }
    }
}

// Test request template structure
TEST_F(MistralSchemaTest, RequestTemplateStructure) {
    auto request_template = m_schema["request_template"];

    // Required fields for Mistral
    EXPECT_TRUE(request_template.contains("model"));
    EXPECT_TRUE(request_template.contains("messages"));

    // Optional parameters
    EXPECT_TRUE(request_template.contains("temperature"));
    EXPECT_TRUE(request_template.contains("top_p"));
    EXPECT_TRUE(request_template.contains("max_tokens"));
    EXPECT_TRUE(request_template.contains("stream"));

    // Verify defaults
    EXPECT_EQ(request_template["messages"], json::array());
    EXPECT_EQ(request_template["max_tokens"], 1024);
    EXPECT_EQ(request_template["temperature"], 0.7);
    EXPECT_EQ(request_template["stream"], false);

    // Mistral doesn't have frequency_penalty or presence_penalty
    EXPECT_FALSE(request_template.contains("frequency_penalty"));
    EXPECT_FALSE(request_template.contains("presence_penalty"));
}

// Test parameter definitions and validation
TEST_F(MistralSchemaTest, ParameterValidation) {
    ASSERT_TRUE(m_schema.contains("parameters"));

    // Test temperature parameter
    ASSERT_TRUE(m_schema["parameters"].contains("temperature"));
    auto temp_param = m_schema["parameters"]["temperature"];
    EXPECT_EQ(temp_param["type"], "float");
    EXPECT_EQ(temp_param["min"], 0.0);
    EXPECT_EQ(temp_param["max"], 2.0);
    EXPECT_EQ(temp_param["default"], 0.7);

    // Test max_tokens parameter
    ASSERT_TRUE(m_schema["parameters"].contains("max_tokens"));
    auto max_tokens_param = m_schema["parameters"]["max_tokens"];
    EXPECT_EQ(max_tokens_param["type"], "integer");
    EXPECT_EQ(max_tokens_param["min"], 1);
    EXPECT_EQ(max_tokens_param["max"], 8192);
    EXPECT_EQ(max_tokens_param["default"], 1024);

    // Test top_p parameter
    ASSERT_TRUE(m_schema["parameters"].contains("top_p"));
    auto top_p_param = m_schema["parameters"]["top_p"];
    EXPECT_EQ(top_p_param["type"], "float");
    EXPECT_EQ(top_p_param["min"], 0.0);
    EXPECT_EQ(top_p_param["max"], 1.0);

    // Mistral doesn't support stop sequences in parameters
    EXPECT_FALSE(m_schema["parameters"].contains("stop"));
}

// Test message format configuration
TEST_F(MistralSchemaTest, MessageFormatConfiguration) {
    ASSERT_TRUE(m_schema.contains("message_format"));
    auto message_format = m_schema["message_format"];

    // Test message structure
    ASSERT_TRUE(message_format.contains("structure"));
    auto structure = message_format["structure"];
    EXPECT_TRUE(structure.contains("role"));
    EXPECT_TRUE(structure.contains("content"));

    // Mistral uses simple string content
    EXPECT_EQ(structure["content"], "<TEXT_CONTENT>");

    // Test content types
    ASSERT_TRUE(message_format.contains("content_types"));
    auto content_types = message_format["content_types"];

    // Text content only
    ASSERT_TRUE(content_types.contains("text"));
    EXPECT_EQ(content_types.size(), 1); // Only text, no multimodal
}

// Test response format configuration
TEST_F(MistralSchemaTest, ResponseFormatConfiguration) {
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
TEST_F(MistralSchemaTest, MultimodalConfiguration) {
    ASSERT_TRUE(m_schema.contains("multimodal"));
    auto multimodal = m_schema["multimodal"];

    // Mistral does NOT support multimodal
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
TEST_F(MistralSchemaTest, FeatureFlags) {
    ASSERT_TRUE(m_schema.contains("features"));
    auto features = m_schema["features"];

    // Mistral features
    EXPECT_EQ(features["streaming"], true);
    EXPECT_EQ(features["system_messages"], true);
    EXPECT_EQ(features["message_history"], true);

    // Features NOT supported
    EXPECT_EQ(features["function_calling"], false);
    EXPECT_EQ(features["json_mode"], false);
    EXPECT_EQ(features["vision"], false);
}

// Test headers configuration
TEST_F(MistralSchemaTest, HeadersConfiguration) {
    ASSERT_TRUE(m_schema.contains("headers"));
    auto headers = m_schema["headers"];

    ASSERT_TRUE(headers.contains("required"));
    auto required_headers = headers["required"];

    // Mistral uses OpenAI-style headers
    EXPECT_TRUE(required_headers.contains("Authorization"));
    EXPECT_TRUE(required_headers.contains("Content-Type"));

    EXPECT_EQ(required_headers["Content-Type"], "application/json");

    // Authorization should have Bearer prefix
    std::string auth_header = required_headers["Authorization"];
    EXPECT_TRUE(auth_header.starts_with("Bearer "));
}

// Test request building with the schema
TEST_F(MistralSchemaTest, RequestBuilding) {
    // Set up a basic request
    m_context->set_model("mistral-small-latest")
             .add_user_message("Hello, Mistral!");

    auto request = m_context->build_request();

    // Verify request structure matches Mistral expectations
    EXPECT_EQ(request["model"], "mistral-small-latest");
    EXPECT_TRUE(request.contains("messages"));
    EXPECT_EQ(request["messages"].size(), 1);
    EXPECT_EQ(request["messages"][0]["role"], "user");

    // Mistral uses simple string content
    EXPECT_TRUE(request["messages"][0]["content"].is_string());
    EXPECT_EQ(request["messages"][0]["content"], "Hello, Mistral!");

    // Optional parameters should have defaults
    EXPECT_TRUE(request.contains("max_tokens"));
    EXPECT_TRUE(request.contains("temperature"));
}

// Test system message handling
TEST_F(MistralSchemaTest, SystemMessageHandling) {
    m_context->set_system_message("You are a helpful assistant.")
             .add_user_message("Hi!");

    auto request = m_context->build_request();

    // Mistral includes system message in messages array (like OpenAI)
    EXPECT_GE(request["messages"].size(), 2);
    EXPECT_EQ(request["messages"][0]["role"], "system");
    EXPECT_EQ(request["messages"][0]["content"], "You are a helpful assistant.");
    EXPECT_EQ(request["messages"][1]["role"], "user");
    EXPECT_EQ(request["messages"][1]["content"], "Hi!");
}

// Test parameter validation rules
TEST_F(MistralSchemaTest, ParameterValidationRules) {
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
    EXPECT_NO_THROW(m_context->set_parameter("max_tokens", 8192));

    // Invalid max_tokens
    EXPECT_THROW(m_context->set_parameter("max_tokens", 0), validation_exception);
    EXPECT_THROW(m_context->set_parameter("max_tokens", 8193), validation_exception);

    // Test top_p
    EXPECT_NO_THROW(m_context->set_parameter("top_p", 0.5));
    EXPECT_THROW(m_context->set_parameter("top_p", 1.1), validation_exception);
}

// Test streaming configuration
TEST_F(MistralSchemaTest, StreamingConfiguration) {
    m_context->add_user_message("Hello");

    // Test non-streaming request
    auto request1 = m_context->build_request(false);
    EXPECT_EQ(request1["stream"], false);

    // Test streaming request
    auto request2 = m_context->build_request(true);
    EXPECT_EQ(request2["stream"], true);
}

// Test limits configuration
TEST_F(MistralSchemaTest, LimitsConfiguration) {
    ASSERT_TRUE(m_schema.contains("limits"));
    auto limits = m_schema["limits"];

    // Context length - Mistral has smaller context than others
    EXPECT_EQ(limits["max_context_length"], 8192);
    EXPECT_EQ(limits["max_output_tokens"], 8192);

    // Rate limits
    if (limits.contains("rate_limits")) {
        auto rate_limits = limits["rate_limits"];
        EXPECT_TRUE(rate_limits.contains("requests_per_minute"));
        EXPECT_TRUE(rate_limits.contains("tokens_per_minute"));
        EXPECT_EQ(rate_limits["requests_per_minute"], 60);
        EXPECT_EQ(rate_limits["tokens_per_minute"], 60000);
    }
}

// Test message roles
TEST_F(MistralSchemaTest, MessageRoles) {
    ASSERT_TRUE(m_schema.contains("message_roles"));
    auto roles = m_schema["message_roles"];

    // Mistral supports user, assistant, and system
    EXPECT_EQ(roles.size(), 3);
    EXPECT_TRUE(std::find(roles.begin(), roles.end(), "user") != roles.end());
    EXPECT_TRUE(std::find(roles.begin(), roles.end(), "assistant") != roles.end());
    EXPECT_TRUE(std::find(roles.begin(), roles.end(), "system") != roles.end());
}

// Test model naming convention
TEST_F(MistralSchemaTest, ModelNamingConvention) {
    auto models = m_context->get_supported_models();

    std::cout << "\n=== Mistral Model Naming Convention ===" << std::endl;

    // Categorize models
    std::vector<std::string> small_models, medium_models, large_models;

    for (const auto& model : models) {
        if (model.find("small") != std::string::npos) {
            small_models.push_back(model);
        } else if (model.find("medium") != std::string::npos) {
            medium_models.push_back(model);
        } else if (model.find("large") != std::string::npos) {
            large_models.push_back(model);
        }
    }

    // Verify we have all size categories
    EXPECT_FALSE(small_models.empty()) << "Should have small models";
    EXPECT_FALSE(medium_models.empty()) << "Should have medium models";
    EXPECT_FALSE(large_models.empty()) << "Should have large models";

    std::cout << "Small models: ";
    for (const auto& m : small_models) std::cout << m << " ";
    std::cout << "\nMedium models: ";
    for (const auto& m : medium_models) std::cout << m << " ";
    std::cout << "\nLarge models: ";
    for (const auto& m : large_models) std::cout << m << " ";
    std::cout << std::endl;
}

// Test error codes
TEST_F(MistralSchemaTest, ErrorCodesConfiguration) {
    ASSERT_TRUE(m_schema.contains("error_codes"));
    auto error_codes = m_schema["error_codes"];

    // Standard HTTP error codes
    EXPECT_EQ(error_codes["400"], "invalid_request_error");
    EXPECT_EQ(error_codes["401"], "authentication_error");
    EXPECT_EQ(error_codes["403"], "permission_error");
    EXPECT_EQ(error_codes["404"], "not_found_error");
    EXPECT_EQ(error_codes["429"], "rate_limit_error");
    EXPECT_EQ(error_codes["500"], "api_error");
    EXPECT_EQ(error_codes["503"], "service_unavailable_error");
}

// Test schema freshness
TEST_F(MistralSchemaTest, SchemaFreshnessCheck) {
    // Add version tracking to schema if not present
    if (m_schema["provider"].contains("last_validated")) {
        std::string last_validated = m_schema["provider"]["last_validated"];

        // Parse date (format: YYYY-MM-DD)
        std::tm tm = {};
        std::istringstream ss(last_validated);
        ss >> std::get_time(&tm, "%Y-%m-%d");

        auto last_validated_time = std::mktime(&tm);
        auto now = std::time(nullptr);

        double days_old = std::difftime(now, last_validated_time) / (60 * 60 * 24);

        std::cout << "\n=== Mistral Schema Freshness ===" << std::endl;
        std::cout << "Schema last validated: " << last_validated << std::endl;
        std::cout << "Days since validation: " << static_cast<int>(days_old) << std::endl;

        if (days_old > 30) {
            std::cout << "WARNING: Schema was last validated " << static_cast<int>(days_old)
                     << " days ago. Consider re-validating against current API." << std::endl;
        }

        EXPECT_LT(days_old, 90) << "Schema is more than 90 days old and must be updated";
    } else {
        std::cout << "INFO: Schema doesn't include last_validated date" << std::endl;
    }
}

TEST_F(MistralSchemaTest, SchemaCompleteness) {
    std::cout << "\n=== Mistral Schema Completeness Check ===" << std::endl;

    // Essential fields that must exist
    std::vector<std::string> required_fields = {
        "provider", "api", "authentication", "headers", "models",
        "request_template", "parameters", "message_format",
        "response_format", "limits", "features", "validation"
    };

    int missing = 0;
    for (const auto& field : required_fields) {
        if (!m_schema.contains(field)) {
            std::cout << "❌ Missing required field: " << field << std::endl;
            missing++;
        }
    }

    if (missing == 0) {
        std::cout << "✅ All required fields present" << std::endl;
    }

    // Check for recommended fields
    std::vector<std::string> recommended_fields = {
        "error_codes", "message_roles", "system_message", "multimodal"
    };

    for (const auto& field : recommended_fields) {
        if (!m_schema.contains(field)) {
            std::cout << "⚠️  Recommended field missing: " << field << std::endl;
        }
    }

    // Validate models structure
    ASSERT_TRUE(m_schema["models"].contains("available"));
    ASSERT_TRUE(m_schema["models"].contains("default"));

    auto available = m_schema["models"]["available"];
    auto default_model = m_schema["models"]["default"].get<std::string>();

    // Default must be in available
    bool found = false;
    for (const auto& model : available) {
        if (model.get<std::string>() == default_model) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "Default model must be in available models list";

    std::cout << "\nModels configured: " << available.size() << std::endl;
    std::cout << "Default model: " << default_model << std::endl;
}
