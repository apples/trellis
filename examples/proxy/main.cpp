
#include "channels.hpp"
#include "message.hpp"
#include "overload.hpp"

#include <cereal/archives/binary.hpp>
#include <trellis/trellis.hpp>
#include <trellis/proxy_context.hpp>

#include <asio.hpp>

using client_context = apply_channels<trellis::client_context>;
using server_context = apply_channels<trellis::server_context>;
using proxy_context = trellis::proxy_context;

int main() {
    asio::io_context io;

    auto client = client_context(io);
    auto server = server_context(io);
    auto proxy = proxy_context(io);

    auto responses = std::array<bool, 20>{};

    auto timer = asio::steady_timer(io, std::chrono::seconds{5});

    timer.async_wait([&](asio::error_code ec) {
        assert(!ec);
        for (int i = 0; i < responses.size(); ++i) {
            std::cout << "Response " << std::setw(2) << ": " << (responses[i] ? "YES" : "NO") << std::endl;
        }
        client.stop();
        server.stop();
        proxy.stop();
    });

    std::cout << "Connecting..." << std::endl;

    server.on_receive<channel_numbers>([](server_context& server, const server_context::connection_ptr& conn, std::istream& packet) {
        auto message = message_numbers{};

        {
            auto archive = cereal::BinaryInputArchive(packet);
            archive(message);
        }

        std::cout << "Server received message " << message.number << std::endl;
        std::cout << "Server responding..." << std::endl;

        auto ostream = trellis::opacketstream(*conn);
        {
            auto archive = cereal::BinaryOutputArchive(ostream);
            archive(message);
        }
        ostream.send<channel_numbers>();
    });

    server.listen({asio::ip::make_address_v4("127.0.0.1"), 0});
    std::cout << "server_endpoint: " << server.get_endpoint() << std::endl;

    proxy.listen({asio::ip::make_address_v4("127.0.0.1"), 0}, server.get_endpoint());
    std::cout << "proxy_endpoint: " << proxy.get_endpoint() << std::endl;

    proxy.set_client_drop_rate(0.5);
    proxy.set_server_drop_rate(0.5);

    client.connect({asio::ip::udp::v4(), 0}, proxy.get_endpoint());
    client.on_connect([&](client_context& context, const client_context::connection_ptr& conn) {
        std::cout << "Connection success" << std::endl;
        std::cout << "Sending message_numbers..." << std::endl;

        for (int n = 0; n < responses.size(); ++n) {
            auto ostream = trellis::opacketstream(*conn);

            {
                auto archive = cereal::BinaryOutputArchive(ostream);
                auto msg = message_numbers{n};
                archive(msg);
            }

            ostream.send<channel_numbers>();
        }
    });

    std::cout << "client_endpoint: " << client.get_endpoint() << std::endl;

    client.on_receive<channel_numbers>([&](client_context& client, const client_context::connection_ptr& conn, std::istream& packet) {
        auto message = message_numbers{};

        {
            auto archive = cereal::BinaryInputArchive(packet);
            archive(message);
        }

        std::cout << "Client received message " << message.number << std::endl;

        responses[message.number] = true;
    });

    io.run();

    std::cout << "Finished." << std::endl;
}
