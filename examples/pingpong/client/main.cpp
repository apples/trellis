
#include <trellis/trellis.hpp>

#include <asio.hpp>

int main() {
    asio::io_context io;
    auto client_endpoint = asio::ip::udp::endpoint(asio::ip::udp::v4(), 0);
    auto server_endpoint = asio::ip::udp::endpoint(asio::ip::make_address_v4("127.0.0.1"), 6969);
    trellis::client_context client (io, client_endpoint, server_endpoint);

    auto connection = client.connect().lock();

    connection->send_data("ping");

    io.run();
}
