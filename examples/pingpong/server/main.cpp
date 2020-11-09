
#include "../channels.hpp"
#include "../message.hpp"
#include "../overload.hpp"

#include <cereal/types/variant.hpp>
#include <cereal/archives/binary.hpp>
#include <trellis/trellis.hpp>

#include <asio.hpp>

using server_context = apply_channels<trellis::server_context>;
using connection_type = typename server_context::connection_type;
using connection_ptr = std::shared_ptr<connection_type>;

int main() {
    auto io = asio::io_context();
    
    auto server = server_context(io, 6969);
    
    server.on_receive<channel_pingpong>([](server_context& server, const connection_ptr& conn, std::istream& packet) {
        auto message = any_message{};

        {
            auto archive = cereal::BinaryInputArchive(packet);
            archive(message);
        }

        std::visit(overload{
            [&](const message_ping&) {
                std::cout << "Received message_ping from " << conn->get_endpoint() << std::endl;
                std::cout << "Sending message_pong..." << std::endl;

                auto ostream = trellis::opacketstream(*conn);

                {
                    auto archive = cereal::BinaryOutputArchive(ostream);
                    auto pong = any_message{message_pong{}};
                    archive(pong);
                }

                ostream.send<channel_pingpong>();
            },
            [&](const message_pong&) {
                std::cout << "Received message_pong from " << conn->get_endpoint() << std::endl;
            },
        }, message);
    });

    server.listen();

    io.run();
}
