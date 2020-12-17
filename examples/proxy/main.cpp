
#include "channels.hpp"
#include "message.hpp"
#include "overload.hpp"

#include <cereal/archives/binary.hpp>
#include <cereal/types/string.hpp>
#include <trellis/trellis.hpp>
#include <trellis/proxy_context.hpp>

#include <asio.hpp>

using client_context = apply_channels<trellis::client_context>;
using server_context = apply_channels<trellis::server_context>;
using proxy_context = trellis::proxy_context;

inline const char* important_message =
    "In 1982, Taeko Okajima is 27 years old, unmarried, has lived her whole life in Tokyo and now works at a company there. "
    "She decides to take another trip to visit the family of the elder brother of her brother-in-law in the rural countryside to help with the safflower harvest and get away from city life. "
    "While traveling at night on a sleeper train to Yamagata, she begins to recall memories of herself as a schoolgirl in 1966, and her intense desire to go on holiday like her classmates, all of whom have family outside of the big city. "
    "At the arrival train station, she is surprised to find out that her brother in law's second cousin Toshio, whom she barely knows, is the one who came to pick her up. "
    "During her stay in Yamagata, she finds herself increasingly nostalgic and wistful for her childhood self, while simultaneously wrestling with adult issues of career and love. "
    "The trip dredges up forgotten memories (not all of them good ones) — the first stirrings of childish romance, puberty and growing up, the frustrations of math and boys. "
    "In lyrical switches between the present and the past, Taeko wonders if she has been true to the dreams of her childhood self. "
    "In doing so, she begins to realize that Toshio has helped her along the way. "
    "Finally, Taeko faces her own true self, how she views the world and the people around her. "
    "Taeko chooses to stay in the countryside instead of returning to Tokyo. "
    "It is implied that she and Toshio begin a relationship.";

int main() {
    asio::io_context io;

    auto client = client_context(io);
    auto server = server_context(io);
    auto proxy = proxy_context(io);

    auto responses = std::array<bool, 100>{};
    auto response_order = std::vector<int>{};
    response_order.reserve(100);

    auto timer = asio::steady_timer(io, std::chrono::seconds{15});

    timer.async_wait([&]([[maybe_unused]] asio::error_code ec) {
        for (auto i = 0u; i < responses.size(); ++i) {
            std::cout << "Response " << std::setw(2) << ": " << (responses[i] ? "YES" : "NO") << "\n";
        }

        std::cout << "Response order:\n";
        for (auto i = 0u; i < response_order.size(); ++i) {
            std::cout << "  " << response_order[i] << "\n";
        }

        std::cout << "Proxy stats:\n";
        std::cout << "  Client messages: " << proxy.get_stats().client_messages << " (" << proxy.get_stats().client_messages_dropped << " dropped)\n";
        std::cout << "  Server messages: " << proxy.get_stats().server_messages << " (" << proxy.get_stats().server_messages_dropped << " dropped)\n";

        std::cout << std::flush;

        client.stop();
        server.stop();
        proxy.stop();
    });

    std::cout << "Connecting..." << std::endl;

    server.on_receive<channel_numbers>([](server_context&, const server_context::connection_ptr& conn, std::istream& packet) {
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
    client.on_connect([&](client_context&, const client_context::connection_ptr& conn) {
        std::cout << "Connection success" << std::endl;
        std::cout << "Sending message_numbers..." << std::endl;

        for (auto n = 0; n < int(responses.size()); ++n) {
            auto ostream = trellis::opacketstream(*conn);

            {
                auto archive = cereal::BinaryOutputArchive(ostream);
                auto msg = message_numbers{n, important_message};
                archive(msg);
            }

            ostream.send<channel_numbers>();
        }
    });

    std::cout << "client_endpoint: " << client.get_endpoint() << std::endl;

    client.on_receive<channel_numbers>([&](client_context&, const client_context::connection_ptr&, std::istream& packet) {
        auto message = message_numbers{};

        {
            auto archive = cereal::BinaryInputArchive(packet);
            archive(message);
        }

        std::cout << "Client received message " << message.number << ", important message " << (message.padding == important_message ? "survived." : "was lost.") << std::endl;

        assert(!responses[message.number]);

        responses[message.number] = true;
        response_order.push_back(message.number);

        if (response_order.size() == responses.size()) {
            timer.cancel();
        }
    });

    io.run();

    std::cout << "Finished." << std::endl;
}
