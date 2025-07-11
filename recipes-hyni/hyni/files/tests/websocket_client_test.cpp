#include "../src/websocket_client.h"
#include <gtest/gtest.h>
#include <boost/beast.hpp>
#include <boost/asio.hpp>
#include <thread>
#include <atomic>
#include <condition_variable>

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace asio = boost::asio;
using tcp = boost::asio::ip::tcp;

class MockWebSocketServer {
public:
    MockWebSocketServer(asio::io_context& ioc, unsigned short port)
        : acceptor_(ioc, {tcp::v4(), port}),
        ws_(asio::make_strand(ioc)) {
        run();
    }

    ~MockWebSocketServer() {
        stop();
    }

    void stop() {
        if (running_) {
            running_ = false;
            acceptor_.cancel();
            if (ws_.is_open()) {
                ws_.close(websocket::close_code::normal);
            }
        }
    }

private:
    void run() {
        running_ = true;
        acceptor_.async_accept(
            asio::make_strand(acceptor_.get_executor()),
            [this](beast::error_code ec, tcp::socket socket) {
                if (ec || !running_) return;

                // Move the socket into a new WebSocket stream
                auto ws = std::make_shared<websocket::stream<tcp::socket>>(std::move(socket));

                ws->async_accept([this, ws](beast::error_code ec) {
                    if (ec || !running_) return;
                    do_read(ws);
                });
            });
    }

    void do_read(std::shared_ptr<websocket::stream<tcp::socket>> ws) {
        auto buffer = std::make_shared<beast::flat_buffer>();
        ws->async_read(*buffer, [this, ws, buffer](beast::error_code ec, size_t) {
            if (ec == websocket::error::closed || !running_) return;

            if (!ec) {
                auto msg = beast::buffers_to_string(buffer->data());
                buffer->consume(buffer->size());

                ws->text(true);
                ws->async_write(asio::buffer(msg), [this, ws, buffer](beast::error_code ec, size_t) {
                    if (!ec && running_) do_read(ws);
                });
            }
        });
    }

    tcp::acceptor acceptor_;
    websocket::stream<tcp::socket> ws_;
    beast::flat_buffer buffer_;
    std::atomic<bool> running_{false};
};

class WebSocketClientTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Start mock server
        server_ = std::make_unique<MockWebSocketServer>(ioc_, 8080);

        // Create client
        client_ = std::make_shared<hyni_websocket_client>(ioc_, "127.0.0.1", "8080");

        // Set up handlers
        client_->set_message_handler([this](const std::string& msg) {
            std::lock_guard<std::mutex> lock(mutex_);
            received_messages_.push_back(msg);
            cv_.notify_all();
        });

        client_->set_connection_handler([this](bool connected) {
            std::lock_guard<std::mutex> lock(mutex_);
            connected_ = connected;
            cv_.notify_all();
        });

        // Run IO context in background
        io_thread_ = std::thread([this]() { ioc_.run(); });
    }

    void TearDown() override {
        client_->shutdown();
        server_->stop();
        ioc_.stop();
        if (io_thread_.joinable()) io_thread_.join();
    }

    bool wait_for_connection(bool expected, std::chrono::milliseconds timeout = std::chrono::milliseconds(100)) {
        std::unique_lock<std::mutex> lock(mutex_);
        return cv_.wait_for(lock, timeout, [this, expected]() {
            return connected_ == expected;
        });
    }

    bool wait_for_message(std::chrono::milliseconds timeout = std::chrono::milliseconds(100)) {
        std::unique_lock<std::mutex> lock(mutex_);
        return cv_.wait_for(lock, timeout, [this]() {
            return !received_messages_.empty();
        });
    }

    asio::io_context ioc_;
    std::unique_ptr<MockWebSocketServer> server_;
    std::shared_ptr<hyni_websocket_client> client_;
    std::thread io_thread_;

    // Synchronization
    std::mutex mutex_;
    std::condition_variable cv_;
    bool connected_ = false;
    std::vector<std::string> received_messages_;
};

TEST_F(WebSocketClientTest, SuccessfulConnection) {
    client_->connect();
    EXPECT_TRUE(wait_for_connection(true));
    EXPECT_TRUE(client_->is_connected());
}

TEST_F(WebSocketClientTest, MessageExchange) {
    client_->connect();
    ASSERT_TRUE(wait_for_connection(true));

    const std::string test_msg = "Hello WebSocket";
    client_->send(test_msg);

    ASSERT_TRUE(wait_for_message());
    EXPECT_EQ(received_messages_.back(), test_msg);
}

TEST_F(WebSocketClientTest, Disconnection) {
    std::promise<void> disconnected_promise;
    std::atomic<bool> disconnected{false};

    client_->set_close_handler([&](boost::beast::error_code ec) {
        disconnected = true;
        disconnected_promise.set_value();
    });

    client_->connect();
    EXPECT_TRUE(wait_for_connection(true));
    EXPECT_TRUE(client_->is_connected());

    client_->disconnect();

    // Wait for the disconnect handler
    auto future = disconnected_promise.get_future();
    EXPECT_EQ(future.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    EXPECT_TRUE(disconnected);
    EXPECT_FALSE(client_->is_connected());
}


TEST_F(WebSocketClientTest, MessageQueueing) {
    client_->connect();
    ASSERT_TRUE(wait_for_connection(true));

    // Send multiple messages rapidly
    client_->send("Message 1");
    client_->send("Message 2");
    client_->send("Message 3");

    // Wait for all messages to be received
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait_for(lock, std::chrono::milliseconds(200), [this]() {
        return received_messages_.size() >= 3;
    });

    EXPECT_EQ(received_messages_.size(), 3);
    EXPECT_EQ(received_messages_[0], "Message 1");
    EXPECT_EQ(received_messages_[1], "Message 2");
    EXPECT_EQ(received_messages_[2], "Message 3");
}
