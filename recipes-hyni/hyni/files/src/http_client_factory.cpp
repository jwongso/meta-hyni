#include "http_client_factory.h"

namespace hyni {

std::unique_ptr<http_client> http_client_factory::create_http_client(
    const general_context& context) {

    auto client = std::make_unique<http_client>();
    // Set headers from context
    client->set_headers(context.get_headers());
    return client;
}

std::unique_ptr<http_client> http_client_factory::create_with_config(
    const std::unordered_map<std::string, std::string>& headers,
    long timeout_ms) {

    auto client = std::make_unique<http_client>();
    client->set_headers(headers);
    return client;
}

} // hyni
