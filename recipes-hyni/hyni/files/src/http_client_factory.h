#pragma once

#include <memory>
#include "http_client.h"
#include "general_context.h"

namespace hyni
{

class http_client_factory {
public:
    static std::unique_ptr<http_client> create_http_client(const general_context& context);

    static std::unique_ptr<http_client> create_with_config(
        const std::unordered_map<std::string, std::string>& headers,
        long timeout_ms = 60000);
};

} // hyni
