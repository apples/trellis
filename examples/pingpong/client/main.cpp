
#include <trellis/trellis.hpp>

#include <asio.hpp>

int main() {
    asio::io_context io;
    auto client_endpoint = trellis::client_context<>::endpoint(asio::ip::udp::v4(), 0);
    auto server_endpoint = trellis::client_context<>::endpoint(asio::ip::make_address_v4("127.0.0.1"), 6969);
    trellis::client_context client (io, client_endpoint, server_endpoint);

    client.send("ping");
    client.receive();

    io.run();
}
