
#include "../channels.hpp"
#include "../message.hpp"
#include "../overload.hpp"

#include <cereal/types/variant.hpp>
#include <cereal/archives/binary.hpp>
#include <trellis/trellis.hpp>

#include <asio.hpp>

using client_context = apply_channels<trellis::client_context>;
using connection_type = typename client_context::connection_type;
using connection_ptr = std::shared_ptr<connection_type>;

int main() {
    asio::io_context io;
    auto client_endpoint = client_context::protocol::endpoint(client_context::protocol::v4(), 0);
    auto server_endpoint = client_context::protocol::endpoint(asio::ip::make_address_v4("127.0.0.1"), 6969);
    auto client = client_context(io);

    std::cout << "Connecting..." << std::endl;

    client.connect(client_endpoint, server_endpoint, [&](client_context& context, const connection_ptr& conn) {
        std::cout << "Connection success" << std::endl;
        std::cout << "Sending message_ping..." << std::endl;

        auto ostream = trellis::opacketstream(*conn);

        {
            auto archive = cereal::BinaryOutputArchive(ostream);
            auto ping = any_message{message_ping{}};
            archive(ping);
        }

        ostream.send<channel_pingpong>();
    });

    client.on_receive<channel_pingpong>([](client_context& client, const connection_ptr& conn, std::istream& packet) {
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
    });

    io.run();
}
