
#include <trellis/trellis.hpp>

#include <asio.hpp>

int main() {
    asio::io_context io;
    trellis::server_context<> server (io, 6969);

    server.receive();

    io.run();
}
