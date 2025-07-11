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

#include "chat_api.h"
#include "http_client.h"
#include "http_client_factory.h"
#include "logger.h"

namespace hyni {

chat_api::chat_api(std::unique_ptr<general_context> context)
    : m_context(std::move(context)) {
    ensure_http_client();
}

std::string chat_api::send_message(const std::string& message, progress_callback cancel_check) {
    LOG_INFO("chat_api::send_message()");

    ensure_http_client();

    m_context->clear_user_messages();
    m_context->add_user_message(message);

    auto request = m_context->build_request();
    m_http_client->set_headers(m_context->get_headers());
    auto response = m_http_client->post(m_context->get_endpoint(), request, cancel_check);

    if (!response.success) {
        LOG_ERROR("API request failed: " + response.error_message);
        throw std::runtime_error("API request failed: " + response.error_message);
    }

    try {
        auto json_response = nlohmann::json::parse(response.body);
        return m_context->extract_text_response(json_response);
    } catch (const std::exception& e) {
        LOG_ERROR("Extract response failed: " + *e.what());
        throw failed_api_response(std::string(e.what()));
    }
}

void chat_api::send_message_stream(const std::string& message,
                                 stream_callback on_chunk,
                                 completion_callback on_complete,
                                 progress_callback cancel_check) {
    ensure_http_client();

    if (!m_context->supports_streaming()) {
        throw streaming_not_supported_error();
    }

    m_context->clear_user_messages();
    m_context->add_user_message(message);

    // Enable streaming in the request
    auto request = m_context->build_request();
    request["stream"] = true;

    m_http_client->set_headers(m_context->get_headers());
    m_http_client->post_stream(
        m_context->get_endpoint(),
        request,
        [on_chunk, this](const std::string& chunk) {
            parse_stream_chunk(chunk, on_chunk);
        },
        on_complete,
        cancel_check
    );
}

void chat_api::parse_stream_chunk(const std::string& chunk, const stream_callback& on_chunk) {
    try {
        std::istringstream stream(chunk);
        std::string line;

        while (std::getline(stream, line)) {
            // Remove potential carriage return
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }

            if (line.empty()) continue;

            static const std::string DATA_PREFIX = "data: ";
            if (line.compare(0, DATA_PREFIX.length(), DATA_PREFIX) == 0) {
                std::string json_str = line.substr(DATA_PREFIX.length());

                if (json_str == "[DONE]") {
                    return;
                }

                try {
                    auto json_chunk = nlohmann::json::parse(json_str);
                    std::string content = m_context->extract_text_response(json_chunk);
                    if (!content.empty()) {
                        on_chunk(content);
                    }
                } catch (const nlohmann::json::exception& e) {
                    // Log parsing error but don't throw
                    // Consider adding a logger
                }
            }
        }
    } catch (const std::exception& e) {
        // Log error but don't throw in callback
    }
}


std::future<std::string> chat_api::send_message_async(const std::string& message) {
    return std::async(std::launch::async, [self = shared_from_this(), message]() {
        return self->send_message(message);
    });
}

std::string chat_api::send_message(progress_callback cancel_check) {
    ensure_http_client();

    // Validate we have at least one user message
    bool has_user_message = false;
    for (const auto& msg : m_context->get_messages()) {
        if (msg["role"] == "user") {
            has_user_message = true;
            break;
        }
    }

    if (!has_user_message) {
        throw no_user_message_error();
    }

    auto request = m_context->build_request();
    m_http_client->set_headers(m_context->get_headers());
    auto response = m_http_client->post(m_context->get_endpoint(), request, cancel_check);

    if (!response.success) {
        throw failed_api_response(response.error_message);
    }

    try {
        auto json_response = nlohmann::json::parse(response.body);
        return m_context->extract_text_response(json_response);
    } catch (const std::exception& e) {
        throw failed_api_response("Failed to parse API response: " + std::string(e.what()));
    }
}

void chat_api::send_message_stream(stream_callback on_chunk,
                                   completion_callback on_complete,
                                   progress_callback cancel_check) {
    ensure_http_client();

    if (!m_context->supports_streaming()) {
        throw streaming_not_supported_error();
    }

    // Validate we have at least one user message
    bool has_user_message = false;
    for (const auto& msg : m_context->get_messages()) {
        if (msg["role"] == "user") {
            has_user_message = true;
            break;
        }
    }

    if (!has_user_message) {
        throw no_user_message_error();
    }

    // Build request with streaming enabled
    auto request = m_context->build_request(true);
    m_http_client->set_headers(m_context->get_headers());

    m_http_client->post_stream(
        m_context->get_endpoint(),
        request,
        [on_chunk, this](const std::string& chunk) {
            parse_stream_chunk(chunk, on_chunk);
        },
        on_complete,
        cancel_check
        );
}

std::future<std::string> chat_api::send_message_async() {
    return std::async(std::launch::async, [self = shared_from_this()]() {
        return self->send_message();
    });
}

void chat_api::ensure_http_client() {
    if (!m_http_client) {
        m_http_client = http_client_factory::create_http_client(*m_context);
    }
}

http_response chat_api::send_request(const nlohmann::json& request, progress_callback cancel_check) {
    return m_http_client->post(m_context->get_endpoint(), request, cancel_check);
}

} // namespace hyni
