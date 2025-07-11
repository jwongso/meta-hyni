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

#include <memory>
#include <optional>
#include "http_client.h"
#include "general_context.h"

namespace hyni
{

class chat_api_error : public std::runtime_error {
    using std::runtime_error::runtime_error;
};

class streaming_not_supported_error : public chat_api_error {
public:
    streaming_not_supported_error()
        : chat_api_error("Streaming is not supported by this provider") {}
};

class no_user_message_error : public chat_api_error {
public:
    no_user_message_error()
        : chat_api_error("No user message found in context") {}
};

class failed_api_response : public chat_api_error {
public:
    failed_api_response(const std::string& message)
        : chat_api_error("Failed to parse API response: " + message) {}
};

/**
 * @brief Main class for interacting with LLM chat APIs
 *
 * This class provides methods for sending messages to LLM APIs,
 * with support for synchronous, asynchronous, and streaming interactions.
 *
 * Thread Safety: This class is NOT thread-safe. Concurrent calls to any
 * methods may result in undefined behavior. For concurrent usage, create
 * separate instances or use external synchronization.
 */
class chat_api : public std::enable_shared_from_this<chat_api> {
public:
    // Add move constructor and move assignment
    chat_api(chat_api&&) = default;
    chat_api& operator=(chat_api&&) = default;

    // Delete copy operations to prevent accidental copies
    chat_api(const chat_api&) = delete;
    chat_api& operator=(const chat_api&) = delete;

    // Add virtual destructor if this might be a base class
    ~chat_api() = default;

    /**
     * @brief Constructs a chat API with the given context
     * @param context The general context to use for API interactions
     */
    explicit chat_api(std::unique_ptr<general_context> context);

    /**
     * @brief Sends a message and waits for a response
     * @param message The message to send
     * @param cancel_check Optional callback to check if the operation should be cancelled
     * @return The response text
     * @throws std::runtime_error If the request fails or the response cannot be parsed
     */
    [[nodiscard]] std::string send_message(const std::string& message, progress_callback cancel_check = nullptr);

    /**
     * @brief Sends a message and streams the response
     * @param message The message to send
     * @param on_chunk Callback function to handle each chunk of the response
     * @param on_complete Optional callback function to handle completion
     * @param cancel_check Optional callback to check if the operation should be cancelled
     * @throws std::runtime_error If streaming is not supported or the request fails
     */
    void send_message_stream(const std::string& message,
                             stream_callback on_chunk,
                             completion_callback on_complete = nullptr,
                             progress_callback cancel_check = nullptr);

    /**
     * @brief Sends a message asynchronously
     * @param message The message to send
     * @return A future containing the response text
     */
    [[nodiscard]] std::future<std::string> send_message_async(const std::string& message);

    /**
     * @brief Sends the current context as a message and waits for a response
     * @param cancel_check Optional callback to check if the operation should be cancelled
     * @return The response text
     * @throws std::runtime_error If the request fails or the response cannot be parsed
     */
    [[nodiscard]] std::string send_message(progress_callback cancel_check = nullptr);

    /**
     * @brief Sends the current context as a message and streams the response
     * @param on_chunk Callback function to handle each chunk of the response
     * @param on_complete Optional callback function to handle completion
     * @param cancel_check Optional callback to check if the operation should be cancelled
     * @throws std::runtime_error If streaming is not supported or the request fails
     */
    void send_message_stream(stream_callback on_chunk,
                             completion_callback on_complete = nullptr,
                             progress_callback cancel_check = nullptr);

    /**
     * @brief Sends the current context as a message asynchronously
     * @return A future containing the response text
     */
    [[nodiscard]] std::future<std::string> send_message_async();

    /**
     * @brief Gets the underlying context for advanced usage
     * @return Reference to the general context
     */
    [[nodiscard]] general_context& get_context() noexcept { return *m_context; }

    /**
     * @brief Gets the underlying context for advanced usage
     * @return Reference to the general context
     */
    [[nodiscard]] const general_context& get_context() const noexcept { return *m_context; }

private:
    /**
     * @brief Parses a streaming response chunk and extracts content
     *
     * This method handles Server-Sent Events (SSE) format responses from streaming APIs.
     * It processes lines starting with "data: " and extracts the actual content
     * from the JSON payload.
     *
     * @param chunk Raw chunk received from the HTTP stream
     * @param on_chunk Callback to invoke with extracted content
     *
     * @note Exceptions are caught and suppressed to prevent callback interruption
     */
    void parse_stream_chunk(const std::string& chunk, const stream_callback& on_chunk);

    /**
     * @brief Ensures that the HTTP client is initialized
     *
     * Lazily creates the HTTP client if it doesn't exist, using the factory
     * to create an appropriate client based on the context configuration.
     *
     * @throws std::runtime_error If HTTP client creation fails
     */
    void ensure_http_client();

    /**
     * @brief Sends a request to the API
     *
     * @param request The JSON request payload to send
     * @param cancel_check Optional callback to check if the operation should be cancelled
     * @return The HTTP response
     *
     * @throws std::runtime_error If the HTTP request fails
     */
    [[nodiscard]] http_response send_request(const nlohmann::json& request,
                                             progress_callback cancel_check = nullptr);

private:
    std::unique_ptr<general_context> m_context;
    std::unique_ptr<http_client> m_http_client;  
};

struct needs_schema {};
struct has_schema {};

template<typename SchemaState = needs_schema>
class chat_api_builder {
private:
    std::string m_schema_path;
    context_config m_config;
    std::string m_api_key;
    std::chrono::milliseconds m_timeout{30000};
    int m_max_retries{3};

    template<typename T>
    friend class chat_api_builder;

public:
    static chat_api_builder create() {
        return chat_api_builder();
    }

    template<typename T = SchemaState>
    auto schema(const std::string& path) -> std::enable_if_t<std::is_same_v<T, needs_schema>, chat_api_builder<has_schema>> {
        chat_api_builder<has_schema> next;
        next.m_schema_path = path;
        next.m_config = m_config;
        next.m_api_key = m_api_key;
        next.m_timeout = m_timeout;
        next.m_max_retries = m_max_retries;
        return next;
    }

    auto config(const context_config& cfg) -> chat_api_builder& {
        m_config = cfg;
        return *this;
    }

    auto api_key(const std::string& key) -> chat_api_builder& {
        m_api_key = key;
        return *this;
    }

    auto timeout(std::chrono::milliseconds timeout) -> chat_api_builder& {
        m_timeout = timeout;
        return *this;
    }

    auto max_retries(int retries) -> chat_api_builder& {
        m_max_retries = retries;
        return *this;
    }

    template<typename T = SchemaState>
    auto build() -> std::enable_if_t<std::is_same_v<T, has_schema>, std::unique_ptr<chat_api>> {
        auto context = std::make_unique<general_context>(m_schema_path, m_config);
        if (!m_api_key.empty()) {
            context->set_api_key(m_api_key);
        }
        auto api = std::make_unique<chat_api>(std::move(context));
        return api;
    }
};

/*
Usage:
auto api = chat_api_builder<>()
               .with_schema("schemas/claude.json")  // Required first!
               .with_api_key(key)                   // Optional, any order
               .with_config(config)                 // Optional, any order
               .build();                            // Only compiles if schema was set!

This won't compile:
auto api = chat_api_builder<>()
            .with_api_key(key)
            .build();  // Error: build() not available without schema!
*/

} // hyni
