#include "websocket_client.h"
#include "logger.h"

hyni_websocket_client::hyni_websocket_client(asio::io_context& io_context,
                                             const std::string& host,
                                             const std::string& port)
    : m_websocket(asio::make_strand(io_context))
    , m_resolver(asio::make_strand(io_context))
    , m_ping_timer(io_context)
    , m_host(host)
    , m_port(port)
    , m_disconnect_timer(io_context)
{}

hyni_websocket_client::~hyni_websocket_client() {
}

void hyni_websocket_client::connect() {
    if (m_connected) return;

    m_resolver.async_resolve(
        m_host,
        m_port,
        beast::bind_front_handler(
            &hyni_websocket_client::on_resolve,
            shared_from_this()));
}

void hyni_websocket_client::disconnect() {
    if (!m_connected || m_shutting_down) return;

    m_shutting_down = true;
    auto weak_self = weak_from_this();
    m_websocket.async_close(
        websocket::close_code::normal,
        [weak_self](beast::error_code ec) {
            if (auto self = weak_self.lock()) {
                self->on_close(ec);
            }
        });
}

void hyni_websocket_client::send(const std::string& message) {
    if (!m_connected) {
        if (m_error_handler) {
            m_error_handler("Not connected to WebSocket server");
        }
        return;
    }

    bool write_in_progress = !m_write_queue.empty();
    m_write_queue.push(message);

    if (!write_in_progress) {
        m_websocket.text(true); // Set text mode
        m_websocket.async_write(
            asio::buffer(m_write_queue.front()),
            beast::bind_front_handler(
                &hyni_websocket_client::on_write,
                shared_from_this()));
    }
}

void hyni_websocket_client::sendAudioBuffer(const std::vector<uint8_t>& audioBuffer) {
    if (!m_connected) {
        if (m_error_handler) {
            m_error_handler("Not connected to WebSocket server");
        }
        return;
    }

    bool write_in_progress = !m_audio_queue.empty();
    m_audio_queue.push(audioBuffer);

    if (!write_in_progress) {
        m_websocket.binary(true); // Set binary mode
        m_websocket.async_write(
            asio::buffer(m_audio_queue.front()),
            beast::bind_front_handler(
                &hyni_websocket_client::on_audio_write,
                shared_from_this()));
    }
}

void hyni_websocket_client::shutdown() {
    m_shutting_down.store(true);
    m_ping_timer.cancel();

    if (m_websocket.is_open()) {
        beast::get_lowest_layer(m_websocket).cancel();
    }

    if (m_connected.load()) {
        auto weak_self = weak_from_this();
        m_websocket.async_close(
            websocket::close_code::going_away,
            [weak_self](beast::error_code ec) {
                if (auto self = weak_self.lock()) {
                    self->handle_final_close(ec);
                }
            });
    }
}

void hyni_websocket_client::handle_final_close(beast::error_code ec) {
    if (!m_shutting_down.exchange(true)) {
        if (ec && ec != beast::errc::operation_canceled) {
            LOG_ERROR("Final close error: " + ec.message());
        }
    }
}

bool hyni_websocket_client::is_connected() const {
    return m_connected;
}

void hyni_websocket_client::set_message_handler(message_handler_t handler) {
    m_message_handler = std::move(handler);
}

void hyni_websocket_client::set_binary_handler(binary_handler_t handler) {
    m_binary_handler = std::move(handler);
}

void hyni_websocket_client::set_connection_handler(connection_handler_t handler) {
    m_connection_handler = std::move(handler);
}

void hyni_websocket_client::set_error_handler(error_handler_t handler) {
    m_error_handler = std::move(handler);
}

void hyni_websocket_client::set_close_handler(close_handler_t handler) {
    m_close_handler = std::move(handler);
}

void hyni_websocket_client::on_resolve(beast::error_code ec, asio::ip::tcp::resolver::results_type results) {
    if (m_shutting_down.load()) return;
    if (ec) {
        if (m_error_handler) m_error_handler("Resolve failed: " + ec.message());
        return;
    }

    beast::get_lowest_layer(m_websocket).expires_after(std::chrono::seconds(30));
    beast::get_lowest_layer(m_websocket).async_connect(
        results,
        beast::bind_front_handler(
            &hyni_websocket_client::on_connect,
            shared_from_this()));
}

void hyni_websocket_client::on_connect(beast::error_code ec, asio::ip::tcp::resolver::results_type::endpoint_type) {
    if (m_shutting_down.load()) return;
    if (ec) {
        if (m_error_handler) m_error_handler("Connect failed: " + ec.message());
        return;
    }

    beast::get_lowest_layer(m_websocket).expires_never();
    m_websocket.set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));

    m_websocket.async_handshake(
        m_host + ":" + m_port,
        "/",
        beast::bind_front_handler(
            &hyni_websocket_client::on_handshake,
            shared_from_this()));
}

void hyni_websocket_client::on_handshake(beast::error_code ec) {
    if (m_shutting_down.load()) return;

    if (ec) {
        if (m_error_handler) m_error_handler("Handshake failed: " + ec.message());
        start_disconnect_timer();
        return;
    }

    m_connected = true;
    m_ping_outstanding = false;
    m_reconnect_attempts = 0;
    stop_disconnect_timer();

    if (m_connection_handler) m_connection_handler(true);

    m_websocket.async_read(
        m_buffer,
        beast::bind_front_handler(
            &hyni_websocket_client::on_read,
            shared_from_this()));

    start_ping();
}

void hyni_websocket_client::on_read(beast::error_code ec, std::size_t bytes_transferred) {
    if (m_shutting_down.load()) return;

    if (ec == websocket::error::closed) {
        m_connected = false;
        if (m_connection_handler) m_connection_handler(false);
        return;
    }

    if (ec) {
        m_connected = false;
        if (m_error_handler) m_error_handler("Read failed: " + ec.message());
        start_disconnect_timer();
        return;
    }

    // Handle binary or text message
    if (m_websocket.got_text()) {
        if (m_message_handler) {
            m_message_handler(beast::buffers_to_string(m_buffer.data()));
        }
    } else {
        if (m_binary_handler) {
            auto data = static_cast<const uint8_t*>(m_buffer.data().data());
            m_binary_handler(data, bytes_transferred);
        }
    }

    m_buffer.consume(bytes_transferred);
    m_websocket.async_read(
        m_buffer,
        beast::bind_front_handler(
            &hyni_websocket_client::on_read,
            shared_from_this()));
}

void hyni_websocket_client::on_write(beast::error_code ec, std::size_t) {
    if (m_shutting_down.load()) return;

    if (ec) {
        m_connected = false;
        if (m_error_handler) m_error_handler("Write failed: " + ec.message());
        return;
    }

    m_write_queue.pop();

    if (!m_write_queue.empty()) {
        m_websocket.text(true);
        m_websocket.async_write(
            asio::buffer(m_write_queue.front()),
            beast::bind_front_handler(
                &hyni_websocket_client::on_write,
                shared_from_this()));
    }
}

void hyni_websocket_client::on_audio_write(beast::error_code ec, std::size_t) {
    if (m_shutting_down.load()) return;

    if (ec) {
        m_connected = false;
        if (m_error_handler) m_error_handler("Audio write failed: " + ec.message());
        return;
    }

    m_audio_queue.pop();

    if (!m_audio_queue.empty()) {
        m_websocket.binary(true);
        m_websocket.async_write(
            asio::buffer(m_audio_queue.front()),
            beast::bind_front_handler(
                &hyni_websocket_client::on_audio_write,
                shared_from_this()));
    }
}

void hyni_websocket_client::on_close(beast::error_code ec) {
    m_connected = false;
    m_ping_timer.cancel();
    stop_disconnect_timer();
    start_disconnect_timer();

    if (ec && m_error_handler) {
        m_error_handler("Close failed: " + ec.message());
    }
    if (m_connection_handler) {
        m_connection_handler(false);
    }
    if (m_close_handler) {
        m_close_handler(ec);
    }
}

void hyni_websocket_client::start_ping() {
    if (m_shutting_down.load()) return;

    m_ping_timer.expires_after(std::chrono::seconds(30));
    m_ping_timer.async_wait(
        beast::bind_front_handler(
            &hyni_websocket_client::on_ping_timer,
            shared_from_this()));
}

void hyni_websocket_client::on_ping_timer(beast::error_code ec) {
    if (ec || m_shutting_down.load()) return;

    if (m_ping_outstanding) {
        if (m_error_handler) m_error_handler("Ping timeout");
        disconnect();
        return;
    }

    m_ping_outstanding = true;
    m_last_ping = std::chrono::steady_clock::now();

    m_websocket.async_ping(
        {},
        beast::bind_front_handler(
            &hyni_websocket_client::on_ping,
            shared_from_this()));
}

void hyni_websocket_client::on_ping(beast::error_code ec) {
    if (ec || m_shutting_down.load()) {
        if (ec && m_error_handler) {
            m_error_handler("Ping failed: " + ec.message());
        }
        return;
    }

    m_ping_outstanding = false;
    start_ping();
}

void hyni_websocket_client::start_disconnect_timer() {
    if (m_connected || m_disconnect_timer_active) return;

    m_disconnect_timer_active = true;
    m_disconnect_timer.expires_after(m_disconnect_timeout);

    auto weak_self = weak_from_this();
    m_disconnect_timer.async_wait(
        [weak_self](beast::error_code ec) {
            if (auto self = weak_self.lock()) {
                self->on_disconnect_timeout(ec);
            }
        });
}

void hyni_websocket_client::stop_disconnect_timer() {
    m_disconnect_timer_active = false;
    m_disconnect_timer.cancel();
}

void hyni_websocket_client::on_disconnect_timeout(beast::error_code ec) {
    if (ec == asio::error::operation_aborted) return;

    m_disconnect_timer_active = false;

    if (!m_connected) {
        m_reconnect_attempts++;
        auto delay = std::min(
            std::chrono::seconds(1 << m_reconnect_attempts),
            std::chrono::seconds(60));
        m_disconnect_timer.expires_after(delay);
        connect(); // Attempt to reconnect
    }
}
