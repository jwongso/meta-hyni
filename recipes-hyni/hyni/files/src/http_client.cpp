#include "http_client.h"
#include "logger.h"
#include <sstream>

namespace hyni {

http_client::http_client() {
    LOG_INFO("http_client::http_client()");
    m_curl.reset(curl_easy_init());
    if (!m_curl) {
        LOG_ERROR("Failed to initialize CURL");
        throw std::runtime_error("Failed to initialize CURL");
    }
    setup_common_options();

    curl_version_info_data* ver = curl_version_info(CURLVERSION_NOW);
    LOG_INFO("CURL version: " + std::string(ver->version));
}

http_client::~http_client() {
    if (m_headers) {
        curl_slist_free_all(m_headers);
    }
}

void http_client::setup_common_options() {
    LOG_INFO("http_client::setup_common_options()");
    curl_easy_setopt(m_curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(m_curl.get(), CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(m_curl.get(), CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(m_curl.get(), CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(m_curl.get(), CURLOPT_TIMEOUT_MS, m_timeout_ms);
    curl_easy_setopt(m_curl.get(), CURLOPT_XFERINFOFUNCTION, progress_callback_wrapper);
}

http_client& http_client::set_timeout(long timeout_ms) {
    LOG_INFO("http_client::set_timeout()");
    m_timeout_ms = timeout_ms;
    curl_easy_setopt(m_curl.get(), CURLOPT_TIMEOUT_MS, m_timeout_ms);
    return *this;
}

http_client& http_client::set_headers(const std::unordered_map<std::string, std::string>& headers) {
    LOG_INFO("http_client::set_headers()");
    if (m_headers) {
        curl_slist_free_all(m_headers);
        m_headers = nullptr;
    }

    // Add new headers
    for (const auto& [key, value] : headers) {
        std::string header = key + ": " + value;
        m_headers = curl_slist_append(m_headers, header.c_str());
    }

    curl_easy_setopt(m_curl.get(), CURLOPT_HTTPHEADER, m_headers);
    return *this;
}

http_client& http_client::set_user_agent(const std::string& user_agent) {
    curl_easy_setopt(m_curl.get(), CURLOPT_USERAGENT, user_agent.c_str());
    return *this;
}

http_client& http_client::set_proxy(const std::string& proxy) {
    curl_easy_setopt(m_curl.get(), CURLOPT_PROXY, proxy.c_str());
    return *this;
}

http_response http_client::post(const std::string& url, const nlohmann::json& payload,
                                progress_callback cancel_check) {
    http_response response;
    response.success = false;

    // Validate inputs
    if (url.empty()) {
        response.error_message = "URL cannot be empty";
        LOG_ERROR(response.error_message);
        return response;
    }

    if (!m_curl) {
        response.error_message = "cURL handle is null";
        LOG_ERROR(response.error_message);
        return response;
    }

    // Serialize JSON payload
    std::string payload_str;
    try {
        payload_str = payload.dump();
    } catch (const nlohmann::json::exception& e) {
        response.error_message = std::string("JSON serialization error: ") + e.what();
        LOG_ERROR(response.error_message);
        return response;
    } catch (const std::bad_alloc& e) {
        response.error_message = "Memory allocation failed during JSON serialization";
        LOG_ERROR(response.error_message);
        return response;
    } catch (const std::exception& e) {
        response.error_message = std::string("JSON serialization failed: ") + e.what();
        LOG_ERROR(response.error_message);
        return response;
    } catch (...) {
        response.error_message = "Unknown error during JSON serialization";
        LOG_ERROR(response.error_message);
        return response;
    }

    // Set basic cURL options first
    CURLcode opt_result;

    opt_result = curl_easy_setopt(m_curl.get(), CURLOPT_URL, url.c_str());
    if (opt_result != CURLE_OK) {
        response.error_message = std::string("Failed to set URL: ") + curl_easy_strerror(opt_result);
        LOG_ERROR(response.error_message);
        return response;
    }

    opt_result = curl_easy_setopt(m_curl.get(), CURLOPT_POST, 1L);
    if (opt_result != CURLE_OK) {
        response.error_message = std::string("Failed to set POST mode: ") + curl_easy_strerror(opt_result);
        LOG_ERROR(response.error_message);
        return response;
    }

    opt_result = curl_easy_setopt(m_curl.get(), CURLOPT_POSTFIELDS, payload_str.c_str());
    if (opt_result != CURLE_OK) {
        response.error_message = std::string("Failed to set POST data: ") + curl_easy_strerror(opt_result);
        LOG_ERROR(response.error_message);
        return response;
    }

    opt_result = curl_easy_setopt(m_curl.get(), CURLOPT_POSTFIELDSIZE, payload_str.size());
    if (opt_result != CURLE_OK) {
        response.error_message = std::string("Failed to set POST data size: ") + curl_easy_strerror(opt_result);
        LOG_ERROR(response.error_message);
        return response;
    }

    // Set headers if available
    if (m_headers) {
        opt_result = curl_easy_setopt(m_curl.get(), CURLOPT_HTTPHEADER, m_headers);
        if (opt_result != CURLE_OK) {
            response.error_message = std::string("Failed to set HTTP headers: ") + curl_easy_strerror(opt_result);
            LOG_ERROR(response.error_message);
            return response;
        }
    }

    // Set up callbacks
    opt_result = curl_easy_setopt(m_curl.get(), CURLOPT_WRITEFUNCTION, write_callback);
    if (opt_result != CURLE_OK) {
        response.error_message = std::string("Failed to set write callback: ") + curl_easy_strerror(opt_result);
        LOG_ERROR(response.error_message);
        return response;
    }

    opt_result = curl_easy_setopt(m_curl.get(), CURLOPT_WRITEDATA, &response.body);
    if (opt_result != CURLE_OK) {
        response.error_message = std::string("Failed to set write data: ") + curl_easy_strerror(opt_result);
        LOG_ERROR(response.error_message);
        return response;
    }

    opt_result = curl_easy_setopt(m_curl.get(), CURLOPT_HEADERFUNCTION, header_callback);
    if (opt_result != CURLE_OK) {
        response.error_message = std::string("Failed to set header callback: ") + curl_easy_strerror(opt_result);
        LOG_ERROR(response.error_message);
        return response;
    }

    opt_result = curl_easy_setopt(m_curl.get(), CURLOPT_HEADERDATA, &response.headers);
    if (opt_result != CURLE_OK) {
        response.error_message = std::string("Failed to set header data: ") + curl_easy_strerror(opt_result);
        LOG_ERROR(response.error_message);
        return response;
    }

    // Set progress callback if provided
    if (cancel_check) {
        m_current_progress_callback = cancel_check;

        opt_result = curl_easy_setopt(m_curl.get(), CURLOPT_XFERINFOFUNCTION, progress_callback_wrapper);
        if (opt_result != CURLE_OK) {
            response.error_message = std::string("Failed to set progress callback: ") + curl_easy_strerror(opt_result);
            LOG_ERROR(response.error_message);
            return response;
        }

        opt_result = curl_easy_setopt(m_curl.get(), CURLOPT_XFERINFODATA, this);
        if (opt_result != CURLE_OK) {
            response.error_message = std::string("Failed to set progress data: ") + curl_easy_strerror(opt_result);
            LOG_ERROR(response.error_message);
            return response;
        }

        opt_result = curl_easy_setopt(m_curl.get(), CURLOPT_NOPROGRESS, 0L);
        if (opt_result != CURLE_OK) {
            response.error_message = std::string("Failed to enable progress: ") + curl_easy_strerror(opt_result);
            LOG_ERROR(response.error_message);
            return response;
        }
    } else {
        curl_easy_setopt(m_curl.get(), CURLOPT_NOPROGRESS, 1L);
    }

    // Perform the request - this is where it crashes
    CURLcode res = curl_easy_perform(m_curl.get());

    if (res != CURLE_OK) {
        LOG_ERROR("cURL error code: " + std::to_string(static_cast<int>(res)));

        const char* error_str = curl_easy_strerror(res);
        if (error_str) {
            response.error_message = std::string("cURL error: ") + error_str;
            LOG_ERROR("cURL error details: " + response.error_message);
        } else {
            response.error_message = "cURL error: " + std::to_string(static_cast<int>(res));
        }
        response.success = false;
    } else {
        // Get response code
        long response_code = 0;
        CURLcode info_result = curl_easy_getinfo(m_curl.get(), CURLINFO_RESPONSE_CODE, &response_code);
        if (info_result == CURLE_OK) {
            response.status_code = response_code;
            response.success = (response.status_code >= 200 && response.status_code < 300);
            LOG_INFO("Request completed successfully with status: " + std::to_string(response_code));
        } else {
            response.error_message = std::string("Failed to get response code: ") + curl_easy_strerror(info_result);
            LOG_ERROR(response.error_message);
            response.success = false;
        }
    }

    return response;
}

http_response http_client::get(const std::string& url, progress_callback cancel_check) {
    http_response response;

    curl_easy_setopt(m_curl.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(m_curl.get(), CURLOPT_HTTPGET, 1L);

    // Set up callbacks
    curl_easy_setopt(m_curl.get(), CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(m_curl.get(), CURLOPT_WRITEDATA, &response.body);
    curl_easy_setopt(m_curl.get(), CURLOPT_HEADERFUNCTION, header_callback);
    curl_easy_setopt(m_curl.get(), CURLOPT_HEADERDATA, &response.headers);
    curl_easy_setopt(m_curl.get(), CURLOPT_XFERINFODATA, &cancel_check);
    curl_easy_setopt(m_curl.get(), CURLOPT_NOPROGRESS, 0L);

    CURLcode res = curl_easy_perform(m_curl.get());

    if (res != CURLE_OK) {
        response.error_message = curl_easy_strerror(res);
        response.success = false;
    } else {
        curl_easy_getinfo(m_curl.get(), CURLINFO_RESPONSE_CODE, &response.status_code);
        response.success = (response.status_code >= 200 && response.status_code < 300);
    }

    return response;
}

void http_client::post_stream(const std::string& url, const nlohmann::json& payload,
                              stream_callback on_chunk,
                              completion_callback on_complete,
                              progress_callback cancel_check) {
    // This would typically run in a separate thread
    auto task = [=, this]() {
        http_response response;

        std::string payload_str = payload.dump();
        curl_easy_setopt(m_curl.get(), CURLOPT_URL, url.c_str());
        curl_easy_setopt(m_curl.get(), CURLOPT_POST, 1L);
        curl_easy_setopt(m_curl.get(), CURLOPT_POSTFIELDS, payload_str.c_str());
        curl_easy_setopt(m_curl.get(), CURLOPT_POSTFIELDSIZE, payload_str.size());

        // Custom write function for streaming
        auto stream_writer = [](void* contents, size_t size, size_t nmemb, void* userp) -> size_t {
            auto callback = static_cast<stream_callback*>(userp);
            std::string chunk(static_cast<char*>(contents), size * nmemb);
            (*callback)(chunk);
            return size * nmemb;
        };

        curl_easy_setopt(m_curl.get(), CURLOPT_WRITEFUNCTION, stream_writer);
        curl_easy_setopt(m_curl.get(), CURLOPT_WRITEDATA, &on_chunk);
        curl_easy_setopt(m_curl.get(), CURLOPT_HEADERFUNCTION, header_callback);
        curl_easy_setopt(m_curl.get(), CURLOPT_HEADERDATA, &response.headers);
        curl_easy_setopt(m_curl.get(), CURLOPT_XFERINFODATA, &cancel_check);
        curl_easy_setopt(m_curl.get(), CURLOPT_NOPROGRESS, 0L);

        CURLcode res = curl_easy_perform(m_curl.get());

        if (res != CURLE_OK) {
            response.error_message = curl_easy_strerror(res);
            response.success = false;
        } else {
            curl_easy_getinfo(m_curl.get(), CURLINFO_RESPONSE_CODE, &response.status_code);
            response.success = (response.status_code >= 200 && response.status_code < 300);
        }

        if (on_complete) {
            on_complete(response);
        }
    };

    std::thread(task).detach();
}

std::future<http_response> http_client::post_async(const std::string& url, const nlohmann::json& payload) {
    auto promise = std::make_shared<std::promise<http_response>>();

    auto task = [=, this]() {
        try {
            auto response = post(url, payload);
            promise->set_value(response);
        } catch (...) {
            promise->set_exception(std::current_exception());
        }
    };

    std::thread(task).detach();
    return promise->get_future();
}

size_t http_client::write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    auto* response = static_cast<std::string*>(userp);
    response->append(static_cast<char*>(contents), size * nmemb);
    return size * nmemb;
}

size_t http_client::header_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    auto* headers = static_cast<std::unordered_map<std::string, std::string>*>(userp);
    std::string header_line(static_cast<char*>(contents), size * nmemb);

    size_t separator = header_line.find(':');
    if (separator != std::string::npos) {
        std::string key = header_line.substr(0, separator);
        std::string value = header_line.substr(separator + 1);
        // Trim whitespace
        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t") + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t") + 1);
        (*headers)[key] = value;
    }

    return size * nmemb;
}

int http_client::progress_callback_wrapper(void* clientp, curl_off_t dltotal, curl_off_t dlnow,
                                           curl_off_t ultotal, curl_off_t ulnow) {
    auto* client = static_cast<http_client*>(clientp);
    if (client && client->m_current_progress_callback) {
        return client->m_current_progress_callback() ? 1 : 0;
    }
    return 0;
}

} // namespace hyni
