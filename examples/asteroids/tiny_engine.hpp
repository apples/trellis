#pragma once

#include "tiny_math.hpp"

#include "stb/stb_image.h"

#include <SDL2/SDL.h>
#include <glad/glad.h>

#include <asio.hpp>

#include <atomic>
#include <cassert>
#include <cstdint>
#include <chrono>
#include <memory>
#include <string>

class tiny_engine;

enum class tiny_vetrex_attrib : GLuint {
    POSITION = 0,
    TEXCOORD = 1,
};

/// Scene abstract base class
class tiny_scene_base {
public:
    virtual ~tiny_scene_base() = 0;
    virtual void handle_event(tiny_engine& engine, const SDL_Event& event) = 0;
    virtual void update(tiny_engine& engine) = 0;
    virtual void draw(tiny_engine& engine) = 0;
};

inline tiny_scene_base::~tiny_scene_base() = default;

/// 2D Texture
class tiny_texture {
public:
    tiny_texture(const std::string& fname) : handle(0), x(0), y(0) {
        auto n = 0;
        auto data = stbi_load(fname.c_str(), &x, &y, &n, 4);

        if (!data) {
            std::cerr << "Failed to load image \"" << fname << "\": " << stbi_failure_reason() << std::endl;
            std::terminate();
        }

        glGenTextures(1, &handle);
        glBindTexture(GL_TEXTURE_2D, handle);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, x, y, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);

        stbi_image_free(data);
    }

    ~tiny_texture() {
        assert(handle != 0);
        glDeleteTextures(1, &handle);
    }

    void bind(int i = 0) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, handle);
    }

    auto get() const -> GLuint {
        return handle;
    }

    auto width() const -> int {
        return x;
    }

    auto height() const -> int {
        return y;
    }

private:
    GLuint handle;
    int x;
    int y;
};

struct tiny_vertex {
    GLfloat x;
    GLfloat y;
    GLfloat z;
    GLfloat u;
    GLfloat v;
};
static_assert(sizeof(tiny_vertex) == sizeof(GLfloat) * 5);
static_assert(alignof(tiny_vertex) == alignof(GLfloat));

struct tiny_triangle {
    GLuint v[3];
};
static_assert(sizeof(tiny_triangle) == sizeof(GLuint) * 3);
static_assert(alignof(tiny_triangle) == alignof(GLuint));

/// Mesh
class tiny_mesh {
public:
    tiny_mesh(const tiny_vertex* verts, std::size_t n_verts, const tiny_triangle* tris, std::size_t n_tris) :
        vao(0),
        vertex_buffer(0),
        element_buffer(0),
        count(0) {
            glGenVertexArrays(1, &vao);
            glBindVertexArray(vao);

            glGenBuffers(1, &vertex_buffer);
            glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer);
            glBufferData(GL_ARRAY_BUFFER, sizeof(tiny_vertex) * n_verts, verts, GL_STATIC_DRAW);

            glGenBuffers(1, &element_buffer);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, element_buffer);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(tiny_triangle) * n_tris, tris, GL_STATIC_DRAW);

            glEnableVertexAttribArray(static_cast<GLuint>(tiny_vetrex_attrib::POSITION));
            glVertexAttribPointer(
                static_cast<GLuint>(tiny_vetrex_attrib::POSITION),
                3,
                GL_FLOAT,
                GL_FALSE,
                sizeof(tiny_vertex),
                reinterpret_cast<const void*>(0));

            glEnableVertexAttribArray(static_cast<GLuint>(tiny_vetrex_attrib::TEXCOORD));
            glVertexAttribPointer(
                static_cast<GLuint>(tiny_vetrex_attrib::TEXCOORD),
                2,
                GL_FLOAT,
                GL_FALSE,
                sizeof(tiny_vertex),
                reinterpret_cast<const void*>(sizeof(GLfloat) * 3));

            glBindVertexArray(0);

            count = n_tris * 3;
        }

    ~tiny_mesh() {
        glDeleteBuffers(1, &element_buffer);
        glDeleteBuffers(1, &vertex_buffer);
        glDeleteVertexArrays(1, &vao);
    }

    void draw() {
        glBindVertexArray(vao);
        glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, reinterpret_cast<const void*>(0));
        glBindVertexArray(0);
    }

private:
    GLuint vao;
    GLuint vertex_buffer;
    GLuint element_buffer;
    GLsizei count;
};

/// Shader program
class tiny_shader {
public:
    tiny_shader(const char* vert_source, const char* frag_source) : handle(0) {
        auto compile_shader = [](GLenum type, const char* shader_source) {
            auto shader = glCreateShader(type);
            glShaderSource(shader, 1, &shader_source, nullptr);
            glCompileShader(shader);

            auto status = GLint{GL_FALSE};
            glGetShaderiv(shader, GL_COMPILE_STATUS, &status);

            if (status == GL_FALSE) {
                auto len = GLint{0};
                glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
                auto str = std::string(len, '\0');
                glGetShaderInfoLog(shader, len, &len, str.data());
                str.resize(len);
                std::cerr << "Failed to compile shader: " << str << "\nSource:\n" << shader_source << std::endl;
                std::terminate();
            }

            return shader;
        };

        auto vert = compile_shader(GL_VERTEX_SHADER, vert_source);
        auto frag = compile_shader(GL_FRAGMENT_SHADER, frag_source);

        handle = glCreateProgram();
        glAttachShader(handle, vert);
        glAttachShader(handle, frag);
        glLinkProgram(handle);

        auto status = GLint{GL_FALSE};
        glGetProgramiv(handle, GL_LINK_STATUS, &status);

        if (status == GL_FALSE) {
            auto len = GLint{0};
            glGetProgramiv(handle, GL_INFO_LOG_LENGTH, &len);
            auto str = std::string(len, '\0');
            glGetProgramInfoLog(handle, len, &len, str.data());
            str.resize(len);
            std::cerr << "Failed to link program: " << str << std::endl;
            std::terminate();
        }

        glDeleteShader(vert);
        glDeleteShader(frag);

        auto num_uniforms = GLint{0};
        auto max_len = GLint{0};
        glGetProgramiv(handle, GL_ACTIVE_UNIFORMS, &num_uniforms);
        glGetProgramiv(handle, GL_ACTIVE_UNIFORM_MAX_LENGTH, &max_len);
        auto buffer = std::string(max_len, '\0');

        for (auto i = 0; i < num_uniforms; ++i) {
            auto len = GLint{};
            auto size = GLint{};
            auto type = GLenum{};
            glGetActiveUniform(handle, i, max_len, &len, &size, &type, buffer.data());
            auto name = std::string(buffer.c_str(), len);
            uniforms.emplace(name, uniform{i, size, type});
        }

        glBindAttribLocation(handle, static_cast<GLuint>(tiny_vetrex_attrib::POSITION), "VertexPosition");
        glBindAttribLocation(handle, static_cast<GLuint>(tiny_vetrex_attrib::TEXCOORD), "VertexTexCoord");
        glBindFragDataLocation(handle, 0, "FragColor");
    }

    ~tiny_shader() {
        glDeleteProgram(handle);
    }

    void use() {
        glUseProgram(handle);
    }

    void set_uniform(const std::string& name, GLfloat x) {
        if (auto iter = uniforms.find(name); iter != uniforms.end()) {
            glUniform1f(iter->second.loc, x);
        }
    }

    void set_uniform(const std::string& name, GLint x) {
        if (auto iter = uniforms.find(name); iter != uniforms.end()) {
            glUniform1i(iter->second.loc, x);
        }
    }

    template <std::size_t N>
    void set_uniform(const std::string& name, const tiny_vec<N>& v) {
        if (auto iter = uniforms.find(name); iter != uniforms.end()) {
            if constexpr (N == 2) glUniform2f(iter->second.loc, v.x, v.y);
            else if constexpr (N == 3) glUniform3f(iter->second.loc, v.x, v.y, v.z);
            else if constexpr (N == 4) glUniform4f(iter->second.loc, v.x, v.y, v.z, v.w);
            else static_assert(N != N, "Invalid vector size");
        }
    }

    template <std::size_t C, std::size_t R>
    void set_uniform(const std::string& name, const tiny_matrix<C, R>& m) {
        if (auto iter = uniforms.find(name); iter != uniforms.end()) {
            if constexpr (C == 2) {
                if constexpr (R == 2) glUniformMatrix2fv(iter->second.loc, 1, GL_FALSE, value_ptr(m));
                else if constexpr (R == 3) glUniformMatrix2x3fv(iter->second.loc, 1, GL_FALSE, value_ptr(m));
                else if constexpr (R == 4) glUniformMatrix2x4fv(iter->second.loc, 1, GL_FALSE, value_ptr(m));
                else static_assert(R != R, "Invalid matrix size");
            } else if constexpr (C == 3) {
                if constexpr (R == 2) glUniformMatrix3x2fv(iter->second.loc, 1, GL_FALSE, value_ptr(m));
                else if constexpr (R == 3) glUniformMatrix3fv(iter->second.loc, 1, GL_FALSE, value_ptr(m));
                else if constexpr (R == 4) glUniformMatrix3x4fv(iter->second.loc, 1, GL_FALSE, value_ptr(m));
                else static_assert(R != R, "Invalid matrix size");
            } else if constexpr (C == 4) {
                if constexpr (R == 2) glUniformMatrix4x2fv(iter->second.loc, 1, GL_FALSE, value_ptr(m));
                else if constexpr (R == 3) glUniformMatrix4x3fv(iter->second.loc, 1, GL_FALSE, value_ptr(m));
                else if constexpr (R == 4) glUniformMatrix4fv(iter->second.loc, 1, GL_FALSE, value_ptr(m));
                else static_assert(R != R, "Invalid matrix size");
            } else if constexpr (C == 4) {
                static_assert(C != C, "Invalid matrix size");
            }
        }
    }

private:
    struct uniform {
        GLint loc;
        GLint size;
        GLenum type;
    };

    GLuint handle;
    std::unordered_map<std::string, uniform> uniforms;
};

inline const char* const tiny_vert_shader = R"(#version 430 core
in vec3 VertexPosition;
in vec2 VertexTexCoord;

uniform mat4 MVP;
uniform mat3 TexCoordMat;

out vec2 texcoord;

void main() {
    gl_Position = MVP * vec4(VertexPosition, 1.0);
    texcoord = vec2(TexCoordMat * vec3(VertexTexCoord, 1.0));
}
)";

inline const char* const tiny_frag_shader = R"(#version 430 core
in vec2 texcoord;

uniform sampler2D DiffuseTex;

out vec4 FragColor;

void main() {
    FragColor = texture(DiffuseTex, texcoord);
    if (FragColor.a == 0.0) discard;
}
)";

inline constexpr tiny_vertex tiny_sprite_vertices[] = {
    {-0.5f, 0.5f, 0.f, 0.f, 0.f},
    {0.5f, 0.5f, 0.f, 1.f, 0.f},
    {0.5f, -0.5f, 0.f, 1.f, 1.f},
    {-0.5f, -0.5f, 0.f, 0.f, 1.f},
};

inline constexpr tiny_triangle tiny_sprite_tris[] = {
    {{0, 1, 2}},
    {{2, 3, 0}},
};

/// 2D graphics routines
class tiny_renderer {
public:
    tiny_renderer(tiny_engine&) :
        shader(tiny_vert_shader, tiny_frag_shader),
        sprite_mesh(tiny_sprite_vertices, 4, tiny_sprite_tris, 2),
        proj_mat(tiny_matrix<4>::identity()),
        view_mat(tiny_matrix<4>::identity()) {}

    void set_camera_size(tiny_vec<2> size) {
        auto zrange = 10.f;
        proj_mat = tiny_matrix<4>::identity();
        proj_mat[0][0] = 2.f / size.x;
        proj_mat[1][1] = 2.f / size.y;
        proj_mat[2][2] = - 2.f / zrange;
    }

    void set_camera_pos(tiny_vec<2> pos) {
        view_mat = tiny_matrix<4>::identity();
        view_mat[3][0] = -pos.x;
        view_mat[3][1] = -pos.y;
    }

    void draw_sprite(tiny_vec<2> pos, tiny_vec<2> size, tiny_texture& texture, tiny_vec<2> px_origin, tiny_vec<2> px_size) {
        auto wh = tiny_vec<2>{float(texture.width()), float(texture.height())};
        auto uv_origin = px_origin / wh;
        auto uv_size = px_size / wh;

        auto uv_mat = tiny_matrix<3>::identity();
        uv_mat[0][0] = uv_size.x;
        uv_mat[1][1] = uv_size.y;
        uv_mat[2][0] = uv_origin.x;
        uv_mat[2][1] = uv_origin.y;

        auto model_mat = tiny_matrix<4>::identity();
        model_mat[0][0] = size.x;
        model_mat[1][1] = size.y;
        model_mat[3][0] = pos.x;
        model_mat[3][1] = pos.y;

        auto mvp = proj_mat * view_mat * model_mat;

        shader.use();
        shader.set_uniform("MVP", mvp);
        shader.set_uniform("TexCoordMat", uv_mat);
        shader.set_uniform("DiffuseTex", 0);

        texture.bind(0);
        sprite_mesh.draw();
    }

private:
    tiny_shader shader;
    tiny_mesh sprite_mesh;
    tiny_matrix<4> proj_mat;
    tiny_matrix<4> view_mat;
};

/// Main engine
class tiny_engine {
public:
    using clock = std::chrono::steady_clock;

    tiny_engine(asio::io_context& io, const std::string& name) :
        io(&io),
        window{},
        gl_context{},
        running{false} {
            // Require OpenGL 4.3 Core profile
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);

            window = SDL_CreateWindow(
                name.c_str(),
                SDL_WINDOWPOS_CENTERED,
                SDL_WINDOWPOS_CENTERED,
                800,
                600,
                SDL_WINDOW_OPENGL);

            if (!window) {
                std::cerr << "Failed to open window: " << SDL_GetError() << std::endl;
                std::terminate();
            }

            gl_context = SDL_GL_CreateContext(window);

            if (!gl_context) {
                std::cerr << "Failed to create GL context: " << SDL_GetError() << std::endl;
                std::terminate();
            }

            if (!gladLoadGLLoader(SDL_GL_GetProcAddress)) {
                std::cerr << "Failed to load GL functions." << std::endl;
                std::terminate();
            }

            int gl_major;
            int gl_minor;
            SDL_GL_GetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, &gl_major);
            SDL_GL_GetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, &gl_minor);

            std::cout << "GL Vendor: " << glGetString(GL_VENDOR) << std::endl;
            std::cout << "GL Renderer: " << glGetString(GL_RENDERER) << std::endl;
            std::cout << "GL Version: " << glGetString(GL_VERSION) << std::endl;
            std::cout << "GL Context: " << gl_major << "." << gl_minor << std::endl;

            SDL_GL_SetSwapInterval(1); // vsync
        }

    ~tiny_engine() {
        SDL_GL_DeleteContext(gl_context);
        SDL_DestroyWindow(window);
    }

    auto get_io() -> asio::io_context& {
        assert(io);
        return *io;
    }

    template <typename S, typename... Ts>
    void queue_scene(Ts&&... ts) {
        queued_scene = [=](tiny_engine& e) {
            e.scene.reset();
            e.scene = std::make_unique<S>(e, ts...);
        };
    }

    void main_loop() {
        running = true;

        auto work_guard = asio::make_work_guard(*io);

        // IO thread for networking
        auto io_thread = std::thread([this]{
            io->run();
        });

        // Rendering "thread"
        [&]{
            while (running) {
                // Window event polling
                for (auto event = SDL_Event{}; running && SDL_PollEvent(&event);) {
                    switch (event.type) {
                        case SDL_QUIT:
                            running = false;
                            return; // breaks out of lambda
                        default:
                            if (scene) {
                                scene->handle_event(*this, event);
                            }
                            break;
                    }
                }

                {
                    // Scene update and render
                    if (scene) {
                        scene->update(*this);

                        glClearColor(0, 0, 0, 1);
                        glClear(GL_COLOR_BUFFER_BIT);
                        scene->draw(*this);
                    }

                    // Queued scene transition
                    if (queued_scene) {
                        queued_scene(*this);
                        queued_scene = {};
                    }
                }

                // Swap buffers (vsync)
                SDL_GL_SwapWindow(window);
            }
        }();

        // Stop IO thread
        io->stop();
        io_thread.join();

        // Destroy scene
        scene.reset();
    }

    void stop() {
        running = false;
    }

private:
    asio::io_context* io;
    SDL_Window* window;
    SDL_GLContext gl_context;
    std::unique_ptr<tiny_scene_base> scene;
    std::function<void(tiny_engine& e)> queued_scene;
    std::atomic<bool> running;
};
