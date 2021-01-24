
#include "../channels.hpp"
#include "../message.hpp"
#include "../overload.hpp"

#include <cereal/types/variant.hpp>
#include <cereal/archives/binary.hpp>
#include <trellis/trellis.hpp>

#include <asio.hpp>

using client_context = apply_channels<trellis::client_context>;
using connection_ptr = typename client_context::connection_ptr;

struct pingpong_client {
    void on_connect(const connection_ptr& conn) {
        std::cout << "Connection success" << std::endl;
        std::cout << "Sending message_ping..." << std::endl;

        conn->send<channel_pingpong>([&](auto& ostream) {
            auto archive = cereal::BinaryOutputArchive(ostream);
            auto ping = any_message{message_ping{}};
            archive(ping);
        });
    }

    void on_disconnect(const connection_ptr& conn, asio::error_code ec) {
        std::cout << "Disconnected from server: ";
        if (ec) {
            std::cout << "Error: " << ec.category().name() << ": " << ec.message() << std::endl;
        } else {
            std::cout << "Disconnected." << std::endl;
        }
    }

    void on_receive(channel_pingpong, const connection_ptr& conn, std::istream& packet) {
        auto message = any_message{};

        {
            auto archive = cereal::BinaryInputArchive(packet);
            archive(message);
        }

        std::visit(overload{
            [&](const message_ping&) {
                std::cout << "Received message_ping from " << conn->get_endpoint() << std::endl;
            },
            [&](const message_pong&) {
                std::cout << "Received message_pong from " << conn->get_endpoint() << std::endl;
                std::cout << "Closing connection..." << std::endl;

                conn->disconnect();
            },
        }, message);
    }
    
    void on_receive(channel_pingpong_r, const connection_ptr& conn, std::istream& packet) {
        auto message = any_message{};

        {
            auto archive = cereal::BinaryInputArchive(packet);
            archive(message);
        }

        std::visit(overload{
            [&](const message_ping&) {
                std::cout << "Received reliable message_ping from " << conn->get_endpoint() << std::endl;
            },
            [&](const message_pong&) {
                std::cout << "Received reliable message_pong from " << conn->get_endpoint() << std::endl;
                std::cout << "Closing connection..." << std::endl;

                conn->disconnect();
            },
        }, message);
    }
};

int main() {
    asio::io_context io;
    auto client_endpoint = client_context::protocol::endpoint(client_context::protocol::v4(), 0);
    auto server_endpoint = client_context::protocol::endpoint(asio::ip::make_address_v4("127.0.0.1"), 6969);
    auto client = client_context(io);

    std::cout << "Connecting..." << std::endl;

    client.connect(client_endpoint, server_endpoint);

    auto timer = asio::steady_timer(io);

    auto poll = [&](auto& poll) -> void {
        timer.expires_from_now(std::chrono::milliseconds(10));
        timer.async_wait([&](asio::error_code ec) {
            if (ec || !client.is_running()) return;
            client.poll_events(pingpong_client{});
            poll(poll);
        });
    };

    poll(poll);
    io.run();
}
