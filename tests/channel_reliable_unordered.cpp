#include "catch.hpp"

#include "context_handler.hpp"

#include <asio.hpp>
#include <trellis/trellis.hpp>
#include <trellis/proxy_context.hpp>

#include <bitset>

using channel_A = trellis::channel_type_reliable_unordered<struct A>;

TEST_CASE("Reliable unordered channel works under perfect conditions", "[channel_reliable_unordered]") {
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
        io.stop();
    });

    auto server_handler = context_handler{
        server,
        [&](const auto& conn_ptr) {
            for (int i = 0; i < COUNT; ++i) {
                conn_ptr->template send<channel_A>([&](std::ostream& ostream) {
                    ostream.write(reinterpret_cast<const char*>(&i), sizeof(i));
                });
            }
        },
        [&](const auto& conn, asio::error_code ec) {},
        [&](channel_A, const auto& conn, std::istream& packet) {},
    };

    auto recvd = std::bitset<COUNT>{};

    auto client_handler = context_handler{
        client,
        [&](const auto& conn_ptr) {},
        [&](const auto& conn, asio::error_code ec) {},
        [&](channel_A, const auto& conn, std::istream& packet) {
            int i;
            packet.read(reinterpret_cast<char*>(&i), sizeof(i));
            REQUIRE(recvd.test(i) == false);
            recvd.set(i);
            if (recvd.count() == COUNT) {
                REQUIRE(timeout.cancel() == 1);
            }
        },
    };

    server_handler.poll();
    client_handler.poll();
    io.run();

    REQUIRE(recvd.count() == COUNT);
}

TEST_CASE("Reliable unordered channel works under unstable conditions", "[channel_reliable_unordered]") {
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
        io.stop();
    });

    auto server_handler = context_handler{
        server,
        [&](const auto& conn_ptr) {
            for (int i = 0; i < COUNT; ++i) {
                conn_ptr->template send<channel_A>([&](std::ostream& ostream) {
                    ostream.write(reinterpret_cast<const char*>(&i), sizeof(i));
                });
            }
        },
        [&](const auto& conn, asio::error_code ec) {},
        [&](channel_A, const auto& conn, std::istream& packet) {},
    };

    auto recvd = std::bitset<COUNT>{};

    auto client_handler = context_handler{
        client,
        [&](const auto& conn_ptr) {},
        [&](const auto& conn, asio::error_code ec) {},
        [&](channel_A, const auto& conn, std::istream& packet) {
            int i;
            packet.read(reinterpret_cast<char*>(&i), sizeof(i));
            REQUIRE(recvd.test(i) == false);
            recvd.set(i);
            if (recvd.count() == COUNT) {
                REQUIRE(timeout.cancel() == 1);
            }
        },
    };

    server_handler.poll();
    client_handler.poll();
    io.run();

    REQUIRE(recvd.count() == COUNT);
}
