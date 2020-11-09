cmake_minimum_required(VERSION 3.16)
project(trellis)

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
