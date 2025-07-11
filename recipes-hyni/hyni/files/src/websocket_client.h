#ifndef HYNI_WEBSOCKET_CLIENT_H
#define HYNI_WEBSOCKET_CLIENT_H

#include <string>
#include <functional>
#include <queue>
#include <memory>
#include <atomic>
#include <boost/beast.hpp>
#include <boost/asio.hpp>
#include <nlohmann/json.hpp>

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace asio = boost::asio;
using json = nlohmann::json;

class hyni_websocket_client : public std::enable_shared_from_this<hyni_websocket_client> {
public:
    using message_handler_t = std::function<void(const std::string&)>;
    using binary_handler_t = std::function<void(const uint8_t*, size_t)>;
    using connection_handler_t = std::function<void(bool)>;
    using error_handler_t = std::function<void(const std::string&)>;
    using close_handler_t = std::function<void(boost::beast::error_code)>;

    hyni_websocket_client(asio::io_context& io_context, const std::string& host, const std::string& port);
    ~hyni_websocket_client();

    void connect();
    void disconnect();
    void send(const std::string& message);
    void sendAudioBuffer(const std::vector<uint8_t>& audioBuffer);
    bool is_connected() const;

    void set_message_handler(message_handler_t handler);
    void set_binary_handler(binary_handler_t handler);
    void set_connection_handler(connection_handler_t handler);
    void set_error_handler(error_handler_t handler);
    void set_close_handler(close_handler_t handler);

    void shutdown();

private:
    void on_resolve(beast::error_code ec, asio::ip::tcp::resolver::results_type results);
    void on_connect(beast::error_code ec, asio::ip::tcp::resolver::results_type::endpoint_type);
    void on_handshake(beast::error_code ec);
    void on_read(beast::error_code ec, std::size_t bytes_transferred);
    void on_write(beast::error_code ec, std::size_t bytes_transferred);
    void on_audio_write(beast::error_code ec, std::size_t bytes_transferred);
    void on_close(beast::error_code ec);
    void handle_final_close(beast::error_code ec);
    void start_ping();
    void on_ping(beast::error_code ec);
    void on_ping_timer(beast::error_code ec);
    void start_disconnect_timer();
    void stop_disconnect_timer();
    void on_disconnect_timeout(beast::error_code ec);

    websocket::stream<beast::tcp_stream> m_websocket;
    asio::ip::tcp::resolver m_resolver;
    asio::steady_timer m_ping_timer;
    std::string m_host;
    std::string m_port;

    beast::flat_buffer m_buffer;
    std::queue<std::string> m_write_queue;
    std::queue<std::vector<uint8_t>> m_audio_queue;

    message_handler_t m_message_handler;
    binary_handler_t m_binary_handler;
    connection_handler_t m_connection_handler;
    error_handler_t m_error_handler;
    close_handler_t m_close_handler;

    std::chrono::steady_clock::time_point m_last_ping;
    std::atomic<bool> m_connected{false};
    std::atomic<bool> m_shutting_down{false};
    std::atomic<bool> m_ping_outstanding{false};

    asio::steady_timer m_disconnect_timer;
    std::chrono::seconds m_disconnect_timeout{10};
    bool m_disconnect_timer_active{false};
    int m_reconnect_attempts{0};
};

#endif // HYNI_WEBSOCKET_CLIENT_H
