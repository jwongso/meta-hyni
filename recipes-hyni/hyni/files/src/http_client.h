#pragma once

#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <functional>
#include <memory>
#include <future>

namespace hyni {

// Response structure
struct http_response {
    long status_code = 0;
    std::string body;
    std::unordered_map<std::string, std::string> headers;
    bool success = false;
    std::string error_message;
};

// Callback types for different scenarios
using progress_callback = std::function<bool()>; // return true to cancel
using stream_callback = std::function<void(const std::string& chunk)>;
using completion_callback = std::function<void(const http_response&)>;

// HTTP client with RAII and factory pattern
class http_client {
public:
    http_client();
    ~http_client();

    // Non-copyable but movable
    http_client(const http_client&) = delete;
    http_client& operator=(const http_client&) = delete;
    http_client(http_client&&) = default;
    http_client& operator=(http_client&&) = default;

    // Builder pattern for configuration
    http_client& set_timeout(long timeout_ms);
    http_client& set_headers(const std::unordered_map<std::string, std::string>& headers);
    http_client& set_user_agent(const std::string& user_agent);
    http_client& set_proxy(const std::string& proxy);

    // Synchronous requests
    http_response post(const std::string& url, const nlohmann::json& payload,
                       progress_callback cancel_check = nullptr);

    http_response get(const std::string& url, progress_callback cancel_check = nullptr);

    // Streaming request (for real-time responses)
    void post_stream(const std::string& url, const nlohmann::json& payload,
                     stream_callback on_chunk,
                     completion_callback on_complete = nullptr,
                     progress_callback cancel_check = nullptr);

    // Async requests returning futures
    std::future<http_response> post_async(const std::string& url, const nlohmann::json& payload);

private:
    struct curl_global_raii {
        curl_global_raii() { curl_global_init(CURL_GLOBAL_ALL); }
        ~curl_global_raii() { curl_global_cleanup(); }
    };
    static inline curl_global_raii global_curl;

    struct curl_deleter {
        void operator()(CURL* curl) { curl_easy_cleanup(curl); }
    };

    std::unique_ptr<CURL, curl_deleter> m_curl;
    struct curl_slist* m_headers = nullptr;
    long m_timeout_ms = 60000;
    progress_callback m_current_progress_callback;

    void setup_common_options();
    static size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp);
    static size_t header_callback(void* contents, size_t size, size_t nmemb, void* userp);
    static int progress_callback_wrapper(void* clientp, curl_off_t dltotal, curl_off_t dlnow,
                                         curl_off_t ultotal, curl_off_t ulnow);
};

} // hyni
