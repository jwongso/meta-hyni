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

#include "general_context.h"
#include "response_utils.h"
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <sstream>

namespace {
void remove_nulls_recursive(nlohmann::json& j) {
    if (j.is_object()) {
        for (auto it = j.begin(); it != j.end(); ) {
            if (it.value().is_null()) {
                it = j.erase(it);
            } else {
                remove_nulls_recursive(it.value());
                ++it;
            }
        }
    } else if (j.is_array()) {
        for (auto& item : j) {
            remove_nulls_recursive(item);
        }
    }
}
} // anonymous namespace

namespace hyni {

general_context::general_context(const std::string& schema_path, const context_config& config)
    : m_config(config) {
    load_schema(schema_path);
    validate_schema();
    cache_schema_elements();
    apply_defaults();
    build_headers();
}

void general_context::load_schema(const std::string& schema_path) {
    std::ifstream file(schema_path);
    if (!file.is_open()) {
        throw schema_exception("Failed to open schema file: " + schema_path);
    }

    try {
        file >> m_schema;
    } catch (const nlohmann::json::parse_error& e) {
        throw schema_exception("Failed to parse schema JSON: " + std::string(e.what()));
    }
}

void general_context::validate_schema() {
    // Check required top-level fields
    std::vector<std::string> required_fields = {"provider", "api", "request_template",
                                                "message_format", "response_format"};

    for (const auto& field : required_fields) {
        if (!m_schema.contains(field)) {
            throw schema_exception("Missing required schema field: " + field);
        }
    }

    // Validate API configuration
    if (!m_schema["api"].contains("endpoint")) {
        throw schema_exception("Missing API endpoint in schema");
    }

    // Validate message format
    if (!m_schema["message_format"].contains("structure") ||
        !m_schema["message_format"].contains("content_types")) {
        throw schema_exception("Invalid message format in schema");
    }

    // Validate response format
    if (!m_schema["response_format"].contains("success") ||
        !m_schema["response_format"]["success"].contains("text_path")) {
        throw schema_exception("Invalid response format in schema");
    }
}

void general_context::cache_schema_elements() {
    // Cache provider info
    m_provider_name = m_schema["provider"]["name"].get<std::string>();
    m_endpoint = m_schema["api"]["endpoint"].get<std::string>();

    if (m_schema.contains("message_roles")) {
        for (const auto& role : m_schema["message_roles"]) {
            m_valid_roles.insert(role.get<std::string>());
        }
    }

    // Cache request template
    m_request_template = m_schema["request_template"];

    // Cache response paths
    m_text_path = parse_json_path(m_schema["response_format"]["success"]["text_path"]);
    if (m_schema["response_format"].contains("error") &&
        m_schema["response_format"]["error"].contains("error_path")) {
        m_error_path = parse_json_path(m_schema["response_format"]["error"]["error_path"]);
    }

    // Cache message formats
    m_message_structure = m_schema["message_format"]["structure"];
    if (m_schema["message_format"]["content_types"].contains("text")) {
        m_text_content_format = m_schema["message_format"]["content_types"]["text"];
    }
    if (m_schema["message_format"]["content_types"].contains("image")) {
        m_image_content_format = m_schema["message_format"]["content_types"]["image"];
    }
}

void general_context::build_headers() {
    m_headers.clear();

    // 1. Process required headers
    if (m_schema.contains("headers") && m_schema["headers"].contains("required")) {
        for (const auto& [key, value] : m_schema["headers"]["required"].items()) {
            std::string header_value = value.get<std::string>();

            if (m_schema.contains("authentication") &&
                m_schema["authentication"].contains("key_placeholder")) {

                const std::string placeholder =
                    m_schema["authentication"]["key_placeholder"].get<std::string>();

                // Just replace the placeholder with the API key
                // The schema should already have the correct format with prefix
                size_t pos = 0;
                while ((pos = header_value.find(placeholder, pos)) != std::string::npos) {
                    header_value.replace(pos, placeholder.length(), m_api_key);
                    pos += m_api_key.length(); // Skip past replacement
                }
            }

            m_headers[key] = header_value;
        }
    }

    // 2. Process optional headers (only if values are provided)
    if (m_schema.contains("headers") && m_schema["headers"].contains("optional")) {
        for (const auto& [key, value] : m_schema["headers"]["optional"].items()) {
            if (!value.is_null() && value.is_string() && !value.get<std::string>().empty()) {
                m_headers[key] = value.get<std::string>();
            }
        }
    }
}

void general_context::apply_defaults() {
    if (m_schema.contains("models") && m_schema["models"].contains("default")) {
        m_model_name = m_schema["models"]["default"].get<std::string>();
    }
}

general_context& general_context::set_model(const std::string& model) {
    // Validate model if available models are specified
    if (m_schema.contains("models") && m_schema["models"].contains("available")) {
        auto available_models = m_schema["models"]["available"];
        bool found = false;
        for (const auto& available_model : available_models) {
            if (available_model.get<std::string>() == model) {
                found = true;
                break;
            }
        }
        if (!found && m_config.enable_validation) {
            throw validation_exception("Model '" + model + "' is not supported by this provider");
        }
    }

    m_model_name = model;
    return *this;
}

general_context& general_context::set_system_message(const std::string& system_text) {
    if (!supports_system_messages() && m_config.enable_validation) {
        throw validation_exception("Provider '" + m_provider_name +
                                   "' does not support system messages");
    }
    m_system_message = system_text;
    return *this;
}

general_context& general_context::set_parameter(const std::string& key,
                                                const nlohmann::json& value) {
    if (m_config.enable_validation) {
        validate_parameter(key, value);
    }
    m_parameters[key] = value;
    return *this;
}

general_context &general_context::set_parameters(
    const std::unordered_map<std::string, nlohmann::json>& params) {
    for (const auto& [key, value] : params) {
        set_parameter(key, value);
    }
    return *this;
}

general_context& general_context::set_api_key(const std::string& api_key) {
    if (api_key.empty()) {
        throw validation_exception("API key cannot be empty");
    }
    m_api_key = api_key;
    build_headers(); // Rebuild headers with new API key

    return *this;
}

general_context &general_context::add_user_message(const std::string& content,
                                      const std::optional<std::string>& media_type,
                                      const std::optional<std::string>& media_data) {
    return add_message("user", content, media_type, media_data);
}

general_context &general_context::add_assistant_message(const std::string& content) {
    return add_message("assistant", content);
}

general_context &general_context::add_message(const std::string& role, const std::string& content,
                                 const std::optional<std::string>& media_type,
                                 const std::optional<std::string>& media_data) {
    auto message = create_message(role, content, media_type, media_data);
    if (m_config.enable_validation) {
        validate_message(message);
    }
    m_messages.push_back(message);
    return *this;
}

nlohmann::json general_context::create_message(const std::string& role, const std::string& content,
                                              const std::optional<std::string>& media_type,
                                              const std::optional<std::string>& media_data) {
    nlohmann::json message;

    // Check if there's a specific structure for this role
    std::string structure_key = role + "_structure";
    if (m_schema.contains("message_format") &&
        m_schema["message_format"].contains(structure_key)) {
        // Use role-specific structure (e.g., system_structure for OpenAI)
        message = m_schema["message_format"][structure_key];

        // Replace placeholders
        if (message.contains("role") && message["role"].is_string() &&
            message["role"] == "<ROLE>") {
            message["role"] = role;
        } else if (!message.contains("role")) {
            message["role"] = role;
        }

        if (message.contains("content") && message["content"].is_string() &&
            message["content"] == "<TEXT>") {
            message["content"] = content;
        }

        // If media is provided and this is not a plain text message, handle it
        if (media_type && media_data && message.contains("content") &&
            message["content"].is_array()) {
            nlohmann::json content_array = nlohmann::json::array();
            content_array.push_back(create_text_content(content));
            content_array.push_back(create_image_content(*media_type, *media_data));
            message["content"] = content_array;
        }
    } else {
        // Use default structure
        message = m_message_structure;

        // Set role
        if (message.contains("role") && message["role"] == "<ROLE>") {
            message["role"] = role;
        } else {
            message["role"] = role;
        }

        // Handle content
        if (message.contains("content") && message["content"].is_array()) {
            // Content is an array (most providers)
            nlohmann::json content_array = nlohmann::json::array();
            content_array.push_back(create_text_content(content));

            // Add image if provided
            if (media_type && media_data) {
                if (!supports_multimodal() && m_config.enable_validation) {
                    throw validation_exception("Provider '" + m_provider_name +
                                               "' does not support multimodal content");
                }
                content_array.push_back(create_image_content(*media_type, *media_data));
            }

            message["content"] = content_array;
        } else if (message.contains("content")) {
            // Content is a simple field
            message["content"] = content;
        }
    }

    return message;
}

nlohmann::json general_context::create_text_content(const std::string& text) {
    nlohmann::json content = m_text_content_format;
    content["text"] = text;
    return content;
}

nlohmann::json general_context::create_image_content(const std::string& media_type,
                                                     const std::string& data) {
    nlohmann::json content = m_image_content_format;

    // Get base64 data
    std::string base64_data;
    if (is_base64_encoded(data)) {
        // Already base64 encoded
        if (data.starts_with("data:")) {
            // Extract just the base64 part from data URI
            size_t comma_pos = data.find(',');
            if (comma_pos != std::string::npos) {
                base64_data = data.substr(comma_pos + 1);
            } else {
                base64_data = data;
            }
        } else {
            base64_data = data;
        }
    } else {
        // Assume it's a file path and encode it
        base64_data = encode_image_to_base64(data);
    }

    // Now apply the format based on the schema template
    apply_template_values(content, {
        {"<IMAGE_URL>", "data:" + media_type + ";base64," + base64_data},
        {"<BASE64_DATA>", base64_data},
        {"<MEDIA_TYPE>", media_type}
    });

    return content;
}

nlohmann::json general_context::build_request(bool streaming) {
    nlohmann::json request = m_request_template;
    nlohmann::json messages_array = nlohmann::json::array();
    for (const auto& msg : m_messages) {
        messages_array.push_back(msg);
    }

    // Set model
    if (!m_model_name.empty()) {
        request["model"] = m_model_name;
    }

    // Set system message if supported
    if (m_system_message && supports_system_messages()) {
        const bool system_in_roles = m_valid_roles.find("system") != m_valid_roles.end();

        if (system_in_roles) {
            // Insert system message at beginning
            messages_array.insert(messages_array.begin(),
                                  create_message("system", *m_system_message));
        } else {
            // Claude style - use separate system field
            request["system"] = *m_system_message;
        }
    }

    // Set messages
    request["messages"] = std::move(messages_array);

    // Apply custom parameters FIRST (so they take precedence)
    for (const auto& [key, value] : m_parameters) {
        request[key] = value;
    }

    // Apply config defaults only if not already set
    if (m_config.default_max_tokens && !request.contains("max_tokens")) {
        request["max_tokens"] = *m_config.default_max_tokens;
    }
    if (m_config.default_temperature && !request.contains("temperature")) {
        request["temperature"] = *m_config.default_temperature;
    }

    // Set streaming: user parameter takes precedence over function parameter
    if (m_parameters.find("stream") == m_parameters.end()) {
        // User hasn't explicitly set stream parameter, use function parameter
        if (streaming && m_schema["features"]["streaming"].get<bool>()) {
            request["stream"] = true;
        } else {
            request["stream"] = false;
        }
    }

    remove_nulls_recursive(request);

    return request;
}

std::string general_context::extract_text_response(const nlohmann::json& response) {
    try {
        nlohmann::json text_node = resolve_path(response, m_text_path);
        return text_node.get<std::string>();
    } catch (const std::exception& e) {
        throw std::runtime_error("Failed to extract text response: " + std::string(e.what()));
    }
}

nlohmann::json general_context::extract_full_response(const nlohmann::json& response) {
    try {
        std::vector<std::string> content_path = parse_json_path(
            m_schema["response_format"]["success"]["content_path"]);
        return resolve_path(response, content_path);
    } catch (const std::exception& e) {
        throw std::runtime_error("Failed to extract full response: " + std::string(e.what()));
    }
}

std::string general_context::extract_error(const nlohmann::json& response) {
    if (m_error_path.empty()) {
        return "Unknown error";
    }

    try {
        nlohmann::json error_node = resolve_path(response, m_error_path);
        return error_node.get<std::string>();
    } catch (const std::exception&) {
        return "Failed to parse error message";
    }
}

nlohmann::json general_context::resolve_path(const nlohmann::json& json,
                                           const std::vector<std::string>& path) const {
    nlohmann::json current = json;

    for (const auto& key : path) {
        if (key.find_first_not_of("0123456789") == std::string::npos) {
            // It's an array index
            int index = std::stoi(key);
            if (!current.is_array() || index >= static_cast<int>(current.size())) {
                throw std::runtime_error("Invalid array access: index " + key);
            }
            current = current[index];
        } else {
            // It's an object key
            if (!current.is_object() || !current.contains(key)) {
                throw std::runtime_error("Invalid object access: key " + key);
            }
            current = current[key];
        }
    }

    return current;
}

std::vector<std::string> general_context::parse_json_path(const nlohmann::json& path_array) const {
    std::vector<std::string> path;
    for (const auto& element : path_array) {
        if (element.is_string()) {
            path.push_back(element.get<std::string>());
        } else if (element.is_number()) {
            path.push_back(std::to_string(element.get<int>()));
        }
    }
    return path;
}

std::vector<std::string> general_context::get_supported_models() const {
    std::vector<std::string> models;
    if (m_schema.contains("models") && m_schema["models"].contains("available")) {
        for (const auto& model : m_schema["models"]["available"]) {
            models.push_back(model.get<std::string>());
        }
    }
    return models;
}

bool general_context::supports_multimodal() const noexcept {
    auto multimodal_it = m_schema.find("multimodal");
    if (multimodal_it != m_schema.end() && multimodal_it->is_object()) {
        auto supported_it = multimodal_it->find("supported");
        if (supported_it != multimodal_it->end() && supported_it->is_boolean()) {
            return supported_it->get<bool>();
        }
    }
    return false;
}

bool general_context::supports_streaming() const noexcept {
    auto features_it = m_schema.find("features");
    if (features_it != m_schema.end() && features_it->is_object()) {
        auto streaming_it = features_it->find("streaming");
        if (streaming_it != features_it->end() && streaming_it->is_boolean()) {
            return streaming_it->get<bool>();
        }
    }
    return false;
}

bool general_context::supports_system_messages() const noexcept {
    auto system_it = m_schema.find("system_message");
    if (system_it != m_schema.end() && system_it->is_object()) {
        auto supported_it = system_it->find("supported");
        if (supported_it != system_it->end() && supported_it->is_boolean()) {
            return supported_it->get<bool>();
        }
    }
    return false;
}

bool general_context::is_valid_request() const {
    return get_validation_errors().empty();
}

std::vector<std::string> general_context::get_validation_errors() const {
    std::vector<std::string> errors;

    // Check required fields
    if (m_model_name.empty()) {
        errors.push_back("Model name is required");
    }

    if (m_messages.empty()) {
        errors.push_back("At least one message is required");
    }

    // Validate message roles
    if (m_schema.contains("validation") && m_schema["validation"].contains("message_validation")) {
        auto validation = m_schema["validation"]["message_validation"];

        if (validation.contains("last_message_role")) {
            std::string required_role = validation["last_message_role"].get<std::string>();
            if (!m_messages.empty()) {
                std::string last_role = m_messages.back()["role"].get<std::string>();
                if (last_role != required_role) {
                    errors.push_back("Last message must be from: " + required_role);
                }
            }
        }
    }

    return errors;
}

bool general_context::has_parameter(const std::string& key) const noexcept {
    return m_parameters.find(key) != m_parameters.end();
}

nlohmann::json general_context::get_parameter(const std::string& key) const {
    auto it = m_parameters.find(key);
    if (it == m_parameters.end()) {
        throw validation_exception("Parameter '" + key + "' not found");
    }
    return it->second;
}

void general_context::validate_message(const nlohmann::json& message) const {
    if (!message.contains("role") || !message.contains("content")) {
        throw validation_exception("Message must contain 'role' and 'content' fields");
    }

    std::string role = message["role"].get<std::string>();
    if (!m_valid_roles.empty() && m_valid_roles.find(role) == m_valid_roles.end()) {
        throw validation_exception("Invalid message role: " + role);
    }
}

void general_context::validate_parameter(const std::string& key,
                                         const nlohmann::json& value) const {
    if (value.is_null()) {
        // Some parameters allow null
        if (m_schema.contains("parameters") &&
            m_schema["parameters"].contains(key) &&
            m_schema["parameters"][key].contains("default") &&
            m_schema["parameters"][key]["default"].is_null()) {
            return; // null is allowed for this parameter
        }
        throw validation_exception("Parameter '" + key + "' cannot be null");
    }

    if (!m_schema.contains("parameters") || !m_schema["parameters"].contains(key)) {
        return; // Parameter not defined in schema
    }

    auto param_def = m_schema["parameters"][key];

    if (param_def.contains("type") && param_def["type"].is_array()) {
        // Multiple types allowed
        bool type_matched = false;
        std::vector<std::string> allowed_types;

        for (const auto& allowed_type : param_def["type"]) {
            std::string type_str = allowed_type.get<std::string>();
            allowed_types.push_back(type_str);

            if ((type_str == "string" && value.is_string()) ||
                (type_str == "array" && value.is_array()) ||
                (type_str == "integer" && value.is_number_integer()) ||
                (type_str == "float" && value.is_number()) ||
                (type_str == "boolean" && value.is_boolean()) ||
                (type_str == "object" && value.is_object())) {
                type_matched = true;
                break;
            }
        }

        if (!type_matched) {
            std::string types_str = "[";
            for (size_t i = 0; i < allowed_types.size(); ++i) {
                types_str += allowed_types[i];
                if (i < allowed_types.size() - 1) types_str += ", ";
            }
            types_str += "]";
            throw validation_exception("Parameter '" + key + "' must be one of types: " + types_str);
        }

        // Additional validation for arrays
        if (value.is_array() && param_def.contains("maxItems")) {
            size_t max_items = param_def["maxItems"].get<size_t>();
            if (value.size() > max_items) {
                throw validation_exception("Parameter '" + key + "' array exceeds maximum of " +
                                         std::to_string(max_items) + " items");
            }
        }

        // Validate array items if specified
        if (value.is_array() && param_def.contains("items")) {
            auto items_def = param_def["items"];
            if (items_def.contains("type")) {
                std::string item_type = items_def["type"].get<std::string>();
                for (const auto& item : value) {
                    if (item_type == "string" && !item.is_string()) {
                        throw validation_exception("Parameter '" + key +
                                                 "' array items must be strings");
                    }
                }
            }
            if (items_def.contains("maxLength")) {
                size_t max_length = items_def["maxLength"].get<size_t>();
                for (const auto& item : value) {
                    if (item.is_string() && item.get<std::string>().length() > max_length) {
                        throw validation_exception("Parameter '" + key +
                                                 "' array item exceeds maximum length of " +
                                                 std::to_string(max_length));
                    }
                }
            }
        }

        return; // Skip other validations since we handled multi-type
    }

    // Add string length validation
    if (value.is_string() && param_def.contains("max_length")) {
        size_t max_len = param_def["max_length"].get<size_t>();
        if (value.get<std::string>().length() > max_len) {
            throw validation_exception("Parameter '" + key + "' exceeds maximum length of " +
                                       std::to_string(max_len));
        }
    }

    // Add enum validation
    if (param_def.contains("enum")) {
        bool found = false;
        for (const auto& allowed : param_def["enum"]) {
            if (value == allowed) {
                found = true;
                break;
            }
        }
        if (!found) {
            throw validation_exception("Parameter '" + key + "' has invalid value");
        }
    }

    // Type validation
    if (param_def.contains("type")) {
        std::string expected_type = param_def["type"].get<std::string>();
        if (expected_type == "integer" && !value.is_number_integer()) {
            throw validation_exception("Parameter '" + key + "' must be an integer");
        } else if (expected_type == "float" && !value.is_number()) {
            throw validation_exception("Parameter '" + key + "' must be a number");
        } else if (expected_type == "string" && !value.is_string()) {
            throw validation_exception("Parameter '" + key + "' must be a string");
        } else if (expected_type == "boolean" && !value.is_boolean()) {
            throw validation_exception("Parameter '" + key + "' must be a boolean");
        } else if (expected_type == "array" && !value.is_array()) {
            throw validation_exception("Parameter '" + key + "' must be an array");
        }
    }

    // Range validation for numbers
    if (value.is_number() && param_def.contains("min")) {
        double min_val = param_def["min"].get<double>();
        if (value.get<double>() < min_val) {
            throw validation_exception("Parameter '" + key + "' must be >= " +
                                       std::to_string(min_val));
        }
    }

    if (value.is_number() && param_def.contains("max")) {
        double max_val = param_def["max"].get<double>();
        if (value.get<double>() > max_val) {
            throw validation_exception("Parameter '" + key + "' must be <= " +
                                       std::to_string(max_val));
        }
    }
}

std::string general_context::encode_image_to_base64(const std::string& image_path) const {
    // Check existence and size first
    std::filesystem::path path(image_path);
    if (!std::filesystem::exists(path)) {
        throw std::runtime_error("Image file does not exist: " + image_path);
    }

    auto file_size = std::filesystem::file_size(path);
    const size_t MAX_IMAGE_SIZE = 10 * 1024 * 1024; // 10MB limit

    if (file_size > MAX_IMAGE_SIZE) {
        throw std::runtime_error("Image file too large: " + std::to_string(file_size) + " bytes");
    }

    std::ifstream file(image_path, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open image file: " + image_path);
    }

    std::vector<char> buffer((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());

    return response_utils::base64_encode(reinterpret_cast<const unsigned char*>(buffer.data()),
                                         buffer.size());
}

bool general_context::is_base64_encoded(const std::string& data) const noexcept {
    if (data.empty()) return false;

    // Check for data URI scheme (e.g., "data:image/png;base64,...")
    if (data.starts_with("data:") && data.find(";base64,") != std::string::npos) {
        return true;
    }

    // Check for valid Base64 characters (ignoring whitespace)
    constexpr std::string_view base64_chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=";

    size_t padding = 0;
    size_t data_len = 0;  // Counts non-whitespace chars

    for (char c : data) {
        if (std::isspace(c)) continue;  // Skip whitespace
        if (base64_chars.find(c) == std::string_view::npos) {
            return false;  // Invalid character
        }
        if (c == '=') {
            if (++padding > 2) return false;  // Max 2 padding chars
        }
        data_len++;
    }

    // Validate length and padding (Base64 length must be divisible by 4)
    return (data_len % 4 == 0) && (padding != 1);  // 1 padding char is invalid
}

void general_context::apply_template_values(nlohmann::json& j,
                                           const std::unordered_map<std::string, std::string>& replacements) {
    if (j.is_string()) {
        std::string str = j.get<std::string>();
        for (const auto& [placeholder, value] : replacements) {
            size_t pos = 0;
            while ((pos = str.find(placeholder, pos)) != std::string::npos) {
                str.replace(pos, placeholder.length(), value);
                pos += value.length();
            }
        }
        j = str;
    } else if (j.is_object()) {
        for (auto& [key, value] : j.items()) {
            apply_template_values(value, replacements);
        }
    } else if (j.is_array()) {
        for (auto& item : j) {
            apply_template_values(item, replacements);
        }
    }
}

void general_context::reset() {
    clear_user_messages();
    clear_system_message();
    clear_parameters();
    m_model_name.clear();
    apply_defaults();
}

void general_context::clear_user_messages() noexcept {
    m_messages.clear();
}

void general_context::clear_system_message() noexcept {
    m_system_message.reset();
}

void general_context::clear_parameters() noexcept {
    m_parameters.clear();
}

} // hyni
