#include "catch.hpp"

#include <asio.hpp>
#include <trellis/trellis.hpp>
#include <trellis/proxy_context.hpp>

using channel_A = trellis::channel_type_reliable_ordered<struct A>;

TEST_CASE("Reliable ordered channel works under perfect conditions", "[channel_reliable_ordered]") {
    constexpr auto COUNT = 1000;

    asio::io_context io;

    auto server = trellis::server_context<channel_A>(io);
    auto client = trellis::client_context<channel_A>(io);

    server.listen({asio::ip::make_address_v4("127.0.0.1"), 0});
    client.connect({asio::ip::udp::v4(), 0}, server.get_endpoint());

    auto timeout = asio::steady_timer(io, std::chrono::seconds{5});
    timeout.async_wait([&](auto ec) {
        REQUIRE(ec == asio::error::operation_aborted);
        server.stop();
        client.stop();
    });

    server.on_connect([&](auto& server, const auto& conn_ptr) {
        for (int i = 0; i < COUNT; ++i) {
            auto ostream = trellis::opacketstream(*conn_ptr);

            ostream.write(reinterpret_cast<const char*>(&i), sizeof(i));

            ostream.template send<channel_A>();
        }
    });

    int next = 0;

    client.on_receive<channel_A>([&](auto& client, const auto& conn_ptr, std::istream& packet) {
        int i;
        packet.read(reinterpret_cast<char*>(&i), sizeof(i));
        REQUIRE(i == next);
        ++next;
        if (next == COUNT) {
            REQUIRE(timeout.cancel() == 1);
        }
    });

    io.run();

    REQUIRE(next == COUNT);
}

TEST_CASE("Reliable ordered channel works under unstable conditions", "[channel_reliable_ordered]") {
    constexpr auto COUNT = 1000;

    asio::io_context io;

    auto server = trellis::server_context<channel_A>(io);
    auto client = trellis::client_context<channel_A>(io);
    auto proxy = trellis::proxy_context(io);

    server.listen({asio::ip::make_address_v4("127.0.0.1"), 0});
    proxy.listen({asio::ip::make_address_v4("127.0.0.1"), 0}, server.get_endpoint());
    client.connect({asio::ip::udp::v4(), 0}, proxy.get_endpoint());

    proxy.set_client_drop_rate(0.25);
    proxy.set_server_drop_rate(0.25);

    auto timeout = asio::steady_timer(io, std::chrono::seconds{5});
    timeout.async_wait([&](auto ec) {
        REQUIRE(ec == asio::error::operation_aborted);
        server.stop();
        client.stop();
        proxy.stop();
    });

    server.on_connect([&](auto& server, const auto& conn_ptr) {
        for (int i = 0; i < COUNT; ++i) {
            auto ostream = trellis::opacketstream(*conn_ptr);

            ostream.write(reinterpret_cast<const char*>(&i), sizeof(i));

            ostream.template send<channel_A>();
        }
    });

    int next = 0;

    client.on_receive<channel_A>([&](auto& client, const auto& conn_ptr, std::istream& packet) {
        int i;
        packet.read(reinterpret_cast<char*>(&i), sizeof(i));
        REQUIRE(i == next);
        ++next;
        if (next == COUNT) {
            REQUIRE(timeout.cancel() == 1);
        }
    });

    io.run();

    REQUIRE(next == COUNT);
}
