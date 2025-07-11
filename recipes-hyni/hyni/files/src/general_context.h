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

#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <optional>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <stdexcept>

namespace hyni {

/**
 * @brief Custom exception for schema-related errors
 */
class schema_exception : public std::runtime_error {
public:
    /**
     * @brief Constructs a schema exception with the given message
     * @param msg The error message
     */
    explicit schema_exception(const std::string& msg) : std::runtime_error(msg) {}
};

/**
 * @brief Custom exception for validation-related errors
 */
class validation_exception : public std::runtime_error {
public:
    /**
     * @brief Constructs a validation exception with the given message
     * @param msg The error message
     */
    explicit validation_exception(const std::string& msg) : std::runtime_error(msg) {}
};

/**
 * @brief Configuration structure for additional context options
 */
struct context_config {
    bool enable_streaming_support = false;  ///< Whether to enable streaming support
    bool enable_validation = true;          ///< Whether to enable validation
    bool enable_caching = true;             ///< Whether to enable caching
    std::optional<int> default_max_tokens;  ///< Default maximum tokens for responses
    std::optional<double> default_temperature; ///< Default temperature for responses
    std::unordered_map<std::string, nlohmann::json> custom_parameters; ///< Custom parameters
};

/**
 * @brief Main class for handling LLM context and API interactions
 *
 * This class manages the context for interacting with language model APIs,
 * including message handling, parameter configuration, and request/response processing.
 *
 * @note This class is NOT thread-safe. In multi-threaded environments, each thread
 *       should maintain its own instance. Consider using thread_local storage:
 *       @code
 *       thread_local hyni::general_context context("schema.json");
 *       @endcode
 */
class general_context {
public:

    /**
     * @brief Rule of 5
     */
    general_context(const general_context&) = delete;
    general_context& operator=(const general_context&) = delete;
    general_context(general_context&&) = default;
    general_context& operator=(general_context&&) = default;
    ~general_context() = default;

    /**
     * @brief Constructs a general context with the given schema and configuration
     * @param schema_path Path to the schema file
     * @param config Configuration options
     * @throws schema_exception If the schema is invalid or cannot be loaded
     */
    explicit general_context(const std::string& schema_path, const context_config& config = {});

    /**
     * @brief Constructs a general context with the given pre-loaded schema and configuration
     * @param schema the pre-loaded JSON schema
     * @param config Configuration options
     * @throws schema_exception If the schema is invalid or cannot be loaded
     */
    explicit general_context(const nlohmann::json& schema,
                             const context_config& config = {})
        : m_schema(schema), m_config(config) {
        validate_schema();
        cache_schema_elements();
        apply_defaults();
        build_headers();
    }

    /**
     * @brief Sets the model to use for requests
     * @param model The model name
     * @return Reference to this context for method chaining
     * @throws validation_exception If the model is not supported and validation is enabled
     */
    general_context& set_model(const std::string& model);

    /**
     * @brief Sets the system message for the conversation
     * @param system_text The system message text
     * @return Reference to this context for method chaining
     * @throws validation_exception If system messages are not supported and validation is enabled
     */
    general_context& set_system_message(const std::string& system_text);

    /**
     * @brief Sets a single parameter for the request
     * @param key The parameter key
     * @param value The parameter value
     * @return Reference to this context for method chaining
     * @throws validation_exception If the parameter is invalid and validation is enabled
     */
    general_context& set_parameter(const std::string& key, const nlohmann::json& value);

    /**
     * @brief Sets multiple parameters for the request
     * @param params Map of parameter keys and values
     * @return Reference to this context for method chaining
     * @throws validation_exception If any parameter is invalid and validation is enabled
     */
    general_context& set_parameters(const std::unordered_map<std::string, nlohmann::json>& params);

    /**
     * @brief Sets the API key for authentication
     * @param api_key The API key
     * @return Reference to this context for method chaining
     * @throws validation_exception If the API key is empty
     */
    general_context& set_api_key(const std::string& api_key);

    /**
     * @brief Adds a user message to the conversation
     * @param content The message content
     * @param media_type Optional media type for multimodal content
     * @param media_data Optional media data for multimodal content
     * @return Reference to this context for method chaining
     * @throws validation_exception If the message is invalid and validation is enabled
     */
    general_context& add_user_message(const std::string& content,
                                      const std::optional<std::string>& media_type = {},
                                      const std::optional<std::string>& media_data = {});

    /**
     * @brief Adds an assistant message to the conversation
     * @param content The message content
     * @return Reference to this context for method chaining
     * @throws validation_exception If the message is invalid and validation is enabled
     */
    general_context& add_assistant_message(const std::string& content);

    /**
     * @brief Adds a message with the specified role to the conversation
     * @param role The message role (e.g., "user", "assistant", "system")
     * @param content The message content
     * @param media_type Optional media type for multimodal content
     * @param media_data Optional media data for multimodal content
     * @return Reference to this context for method chaining
     * @throws validation_exception If the message is invalid and validation is enabled
     */
    general_context& add_message(const std::string& role, const std::string& content,
                    const std::optional<std::string>& media_type = {},
                    const std::optional<std::string>& media_data = {});

    /**
     * @brief Builds a request object based on the current context
     * @param streaming Whether to enable streaming for this request
     * @return JSON object representing the request
     */
    [[nodiscard]] nlohmann::json build_request(bool streaming = false);

    /**
     * @brief Extracts the text response from a JSON response
     * @param response The JSON response from the API
     * @return The extracted text
     * @throws std::runtime_error If the text cannot be extracted
     */
    [[nodiscard]] std::string extract_text_response(const nlohmann::json& response);

    /**
     * @brief Extracts the full response content from a JSON response
     * @param response The JSON response from the API
     * @return The extracted content as JSON
     * @throws std::runtime_error If the content cannot be extracted
     */
    [[nodiscard]] nlohmann::json extract_full_response(const nlohmann::json& response);

    /**
     * @brief Extracts an error message from a JSON response
     * @param response The JSON response from the API
     * @return The extracted error message
     */
    [[nodiscard]] std::string extract_error(const nlohmann::json& response);

    /**
     * @brief Resets the context to its initial state
     * @throws nlohmann::json::type_error If the inital content cannot be parsed
     */
    void reset();

    /**
     * @brief Clears all user messages in the context
     */
    void clear_user_messages() noexcept;

    /**
     * @brief Clears system message in the context
     */
    void clear_system_message() noexcept;

    /**
     * @brief Clears all parameters in the context
     */
    void clear_parameters() noexcept;

    /**
     * @brief Checks if an API key has been set
     * @return True if an API key is set, false otherwise
     */
    [[nodiscard]] bool has_api_key() const noexcept { return !m_api_key.empty(); }

    /**
     * @brief Gets the schema used by this context
     * @return The schema as JSON
     */
    [[nodiscard]] const nlohmann::json& get_schema() const noexcept { return m_schema; }

    /**
     * @brief Gets the provider name
     * @return The provider name
     */
    [[nodiscard]] const std::string& get_provider_name() const noexcept { return m_provider_name; }

    /**
     * @brief Gets the API endpoint
     * @return The API endpoint URL
     */
    [[nodiscard]] const std::string& get_endpoint() const noexcept { return m_endpoint; }

    /**
     * @brief Gets the HTTP headers for API requests
     * @return Map of header names to values
     */
    [[nodiscard]] const std::unordered_map<std::string, std::string>& get_headers() const noexcept
    { return m_headers; }

    /**
     * @brief Gets the list of models supported by the provider
     * @return Vector of supported model names
     */
    [[nodiscard]] std::vector<std::string> get_supported_models() const;

    /**
     * @brief Checks if the provider supports multimodal content
     * @return True if multimodal content is supported, false otherwise
     */
    [[nodiscard]] bool supports_multimodal() const noexcept;

    /**
     * @brief Checks if the provider supports streaming
     * @return True if streaming is supported, false otherwise
     */
    [[nodiscard]] bool supports_streaming() const noexcept;

    /**
     * @brief Checks if the provider supports system messages
     * @return True if system messages are supported, false otherwise
     */
    [[nodiscard]] bool supports_system_messages() const noexcept;

    /**
     * @brief Checks if the current context would produce a valid request
     * @return True if the request would be valid, false otherwise
     */
    [[nodiscard]] bool is_valid_request() const;

    /**
     * @brief Gets a list of validation errors for the current context
     * @return Vector of error messages, empty if valid
     */
    [[nodiscard]] std::vector<std::string> get_validation_errors() const;

    /**
     * @brief Gets all parameters in the context
     * @return Map of parameter names to values
     */
    [[nodiscard]] const std::unordered_map<std::string, nlohmann::json>&
    get_parameters() const noexcept { return m_parameters; }

    /**
     * @brief Gets a parameter value by key
     * @param key The parameter key
     * @return The parameter value
     * @throws validation_exception If the parameter does not exist
     */
    [[nodiscard]] nlohmann::json get_parameter(const std::string& key) const;

    /**
     * @brief Gets a parameter value converted to a specific type
     * @tparam T The type to convert the parameter to
     * @param key The parameter key
     * @return The parameter value converted to type T
     * @throws validation_exception If the parameter does not exist or cannot be converted
     */
    template<typename T>
    [[nodiscard]] T get_parameter_as(const std::string& key) const {
        auto param = get_parameter(key);
        try {
            return param.get<T>();
        } catch (const nlohmann::json::exception& e) {
            throw validation_exception("Parameter '" + key +
                                       "' cannot be converted to requested type: " + e.what());
        }
    }

    /**
     * @brief Gets a parameter value converted to a specific type, with a default value
     * @tparam T The type to convert the parameter to
     * @param key The parameter key
     * @param default_value The default value to return if the parameter does not exist
     * @return The parameter value converted to type T, or the default value
     * @throws validation_exception If the parameter exists but cannot be converted
     */
    template<typename T>
    [[nodiscard]] T get_parameter_as(const std::string& key, const T& default_value) const {
        if (!has_parameter(key)) {
            return default_value;
        }
        return get_parameter_as<T>(key);
    }

    /**
     * @brief Checks if a parameter exists
     * @param key The parameter key
     * @return True if the parameter exists, false otherwise
     */
    [[nodiscard]] bool has_parameter(const std::string& key) const noexcept;

    /**
     * @brief Gets all messages in the context
     * @return Vector of message objects
     */
    [[nodiscard]] const std::vector<nlohmann::json>& get_messages() const noexcept
    { return m_messages; }

private:
    void load_schema(const std::string& schema_path);
    void validate_schema();
    void apply_defaults();
    void cache_schema_elements();
    void build_headers();

    nlohmann::json create_message(const std::string& role, const std::string& content,
                                  const std::optional<std::string>& media_type = {},
                                  const std::optional<std::string>& media_data = {});
    nlohmann::json create_text_content(const std::string& text);
    nlohmann::json create_image_content(const std::string& media_type, const std::string& data);

    [[nodiscard]] nlohmann::json resolve_path(const nlohmann::json& json,
                                              const std::vector<std::string>& path) const;
    [[nodiscard]] std::vector<std::string> parse_json_path(const nlohmann::json& path_array) const;

    void validate_message(const nlohmann::json& message) const;
    void validate_parameter(const std::string& key, const nlohmann::json& value) const;

    [[nodiscard]] std::string encode_image_to_base64(const std::string& image_path) const;
    [[nodiscard]] bool is_base64_encoded(const std::string& data) const noexcept;
    void apply_template_values(nlohmann::json& j,
                               const std::unordered_map<std::string, std::string>& replacements);

private:
    nlohmann::json m_schema;
    nlohmann::json m_request_template;
    context_config m_config;

    std::string m_provider_name;
    std::string m_endpoint;
    std::unordered_map<std::string, std::string> m_headers;
    std::string m_model_name;
    std::optional<std::string> m_system_message;
    std::vector<nlohmann::json> m_messages;
    std::unordered_map<std::string, nlohmann::json> m_parameters;
    std::string m_api_key;
    std::unordered_set<std::string> m_valid_roles;

    std::vector<std::string> m_text_path;
    std::vector<std::string> m_error_path;
    nlohmann::json m_message_structure;
    nlohmann::json m_text_content_format;
    nlohmann::json m_image_content_format;
};

} // hyni
