cmake_minimum_required(VERSION 3.16)
project(trellis)

find_package(SDL2)

# ext

set(ASIO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ext/asio/asio" CACHE PATH "Asio directory")
set(CEREAL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ext/cereal" CACHE PATH "Cereal directory")

# os

set(OS_LIBS "")
set(OS_DEFINITIONS "")

if(WIN32)
    set(ver ${CMAKE_SYSTEM_VERSION})
    string(REPLACE "." "" ver ${ver})
    string(REGEX REPLACE "([0-9])" "0\\1" ver ${ver})

    list(APPEND OS_LIBS "ws2_32")
    list(APPEND OS_DEFINITIONS "-D_WIN32_WINNT=0x${ver}")
endif()

# trellis

add_library(trellis INTERFACE)
target_include_directories(trellis INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${ASIO_DIR}/include")
target_link_libraries(trellis INTERFACE ${OS_LIBS})
target_compile_definitions(trellis INTERFACE ${OS_DEFINITIONS})

# tests

add_executable(trellis_tests
    tests/catch.cpp
    tests/catch.hpp
    tests/channel_reliable_ordered.cpp
    tests/channel_reliable_unordered.cpp
    tests/channel_reliable_sequenced.cpp)
set_property(TARGET trellis_tests PROPERTY CXX_STANDARD 17)
target_link_libraries(trellis_tests trellis)

# ping-pong example

set(PINGPONG_COMMON examples/pingpong/channels.hpp)

add_executable(pingpong_client examples/pingpong/client/main.cpp ${PINGPONG_COMMON})
set_property(TARGET pingpong_client PROPERTY CXX_STANDARD 17)
target_link_libraries(pingpong_client trellis)
target_include_directories(pingpong_client PRIVATE
    "${CEREAL_DIR}/include")
target_compile_definitions(pingpong_client PRIVATE TRELLIS_ENABLE_LOGGING)

add_executable(pingpong_server examples/pingpong/server/main.cpp ${PINGPONG_COMMON})
set_property(TARGET pingpong_server PROPERTY CXX_STANDARD 17)
target_link_libraries(pingpong_server trellis)
target_include_directories(pingpong_server PRIVATE
    "${CEREAL_DIR}/include")
target_compile_definitions(pingpong_server PRIVATE TRELLIS_ENABLE_LOGGING)

# proxy example

add_executable(proxy_example
    examples/proxy/main.cpp
    examples/proxy/channels.hpp
    examples/proxy/message.hpp
    examples/proxy/overload.hpp)
set_property(TARGET proxy_example PROPERTY CXX_STANDARD 17)
target_link_libraries(proxy_example trellis)
target_include_directories(proxy_example PRIVATE
    "${CEREAL_DIR}/include")
target_compile_definitions(proxy_example PRIVATE TRELLIS_ENABLE_LOGGING)
target_compile_options(proxy_example PRIVATE -Wall -Wextra)

# Asteroids game example

# if(SDL2_FOUND)
#     add_executable(asteroids_example
#         examples/asteroids/main.cpp
#         examples/asteroids/channels.hpp
#         examples/asteroids/message.hpp
#         examples/asteroids/overload.hpp
#         examples/asteroids/client.hpp
#         examples/asteroids/server.hpp
#         examples/asteroids/tiny_engine.hpp
#         examples/asteroids/tiny_engine.cpp
#         examples/asteroids/tiny_math.hpp
#         examples/asteroids/glad/src/glad.c
#         examples/asteroids/glad/include/glad/glad.h
#         examples/asteroids/glad/include/KHR/khrplatform.h)
#     set_property(TARGET asteroids_example PROPERTY CXX_STANDARD 17)
#     target_link_libraries(asteroids_example
#         trellis
#         SDL2::SDL2)
#     target_include_directories(asteroids_example PRIVATE
#         "${CEREAL_DIR}/include"
#         "${CMAKE_SOURCE_DIR}/examples/asteroids/glad/include")
#     target_compile_options(asteroids_example PRIVATE -Wall)
#     target_compile_definitions(asteroids_example PRIVATE SDL_MAIN_HANDLED)
# else()
#     message(WARNING "SDL2 not found, required by target asteroids_example.")
# endif()
