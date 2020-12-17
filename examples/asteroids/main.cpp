
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

class scene_start final : public tiny_scene_base {
public:
    scene_start(tiny_engine& e) :
        renderer(e),
        sprites_texture("assets/sprites.png") {}

    virtual void handle_event(tiny_engine& engine, const SDL_Event& event) override {
        switch (event.type) {
            case SDL_EventType::SDL_KEYDOWN:
                switch (event.key.keysym.scancode) {
                    case SDL_Scancode::SDL_SCANCODE_S:
                        std::cout << "S" << std::endl;
                        break;
                    default:
                        break;
                }
                break;
        }
    }

    virtual void update(tiny_engine& engine) override {}

    virtual void draw(tiny_engine& engine) override {
        renderer.set_camera_size({800,600});
        renderer.set_camera_pos({0,0});

        renderer.draw_sprite({0,0}, {32,32}, sprites_texture, {0,0}, {16, 16});
    }

private:
    tiny_renderer renderer;
    tiny_texture sprites_texture;
};

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
