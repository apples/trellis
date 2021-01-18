
#include "channels.hpp"
#include "message.hpp"
#include "overload.hpp"
#include "tiny_engine.hpp"
#include "client.hpp"
#include "server.hpp"

#include <asio.hpp>

#include <cstddef>
#include <cstring>
#include <memory>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage:\n"
            << "    asteroids_example --server\n"
            << "    asteroids_example --client <server_addr>" << std::endl;
        return EXIT_FAILURE;
    }

    auto io = asio::io_context();

    if (std::string(argv[1]) == "--server") {
        std::cout << "Starting server." << std::endl;
        run_server(io, std::atoi(argv[2]));
    } else if (std::string(argv[1]) == "--client") {
        std::cout << "Starting client." << std::endl;
        run_client(io, std::string(argv[2]), std::atoi(argv[3]));
    }

    std::cout << "Done." << std::endl;
}
