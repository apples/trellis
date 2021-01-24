
#include "../channels.hpp"
#include "../message.hpp"
#include "../overload.hpp"

#include <cereal/types/variant.hpp>
#include <cereal/archives/binary.hpp>
#include <trellis/trellis.hpp>

#include <asio.hpp>

using server_context = apply_channels<trellis::server_context>;
using connection_ptr = typename server_context::connection_ptr;

struct pingpong_server {
    void on_connect(const connection_ptr& conn) {
        std::cout << "Connection from " << conn->get_endpoint() << std::endl;
    }

    void on_disconnect(const connection_ptr& conn, asio::error_code ec) {
        std::cout << "Disconnection " << conn->get_endpoint() << ": ";
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
                std::cout << "Sending message_pong..." << std::endl;

                conn->send<channel_pingpong>([&](auto& ostream) {
                    auto archive = cereal::BinaryOutputArchive(ostream);
                    auto pong = any_message{message_pong{}};
                    archive(pong);
                });
            },
            [&](const message_pong&) {
                std::cout << "Received message_pong from " << conn->get_endpoint() << std::endl;
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
                std::cout << "Sending message_pong..." << std::endl;

                conn->send<channel_pingpong>([&](auto& ostream) {
                    auto archive = cereal::BinaryOutputArchive(ostream);
                    auto pong = any_message{message_pong{}};
                    archive(pong);
                });
            },
            [&](const message_pong&) {
                std::cout << "Received reliable message_pong from " << conn->get_endpoint() << std::endl;
            },
        }, message);
    }
};

int main() {
    auto io = asio::io_context();
    
    auto server = server_context(io);
    
    server.listen({server_context::protocol::v4(), 6969});

    auto timer = asio::steady_timer(io);

    auto poll = [&](auto& poll) -> void {
        timer.expires_from_now(std::chrono::milliseconds(10));
        timer.async_wait([&](asio::error_code ec) {
            if (ec || !server.is_running()) return;
            server.poll_events(pingpong_server{});
            poll(poll);
        });
    };

    poll(poll);
    io.run();
}
