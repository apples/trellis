cmake_minimum_required(VERSION 3.16)
project(trellis)

set(OS_LIBS "")
set(OS_DEFINITIONS "")

if(WIN32)
    set(ver ${CMAKE_SYSTEM_VERSION})
    string(REPLACE "." "" ver ${ver})
    string(REGEX REPLACE "([0-9])" "0\\1" ver ${ver})

    list(APPEND OS_LIBS "ws2_32")
    list(APPEND OS_DEFINITIONS "-D_WIN32_WINNT=0x${ver}")
endif()

add_library(trellis INTERFACE)
target_include_directories(trellis INTERFACE
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_SOURCE_DIR}/ext/asio/asio/include")
target_link_libraries(trellis INTERFACE ${OS_LIBS})
target_compile_definitions(trellis INTERFACE ${OS_DEFINITIONS})

add_executable(pingpong_client examples/pingpong/client/main.cpp)
set_property(TARGET pingpong_client PROPERTY CXX_STANDARD 17)
target_link_libraries(pingpong_client trellis)

add_executable(pingpong_server examples/pingpong/server/main.cpp)
set_property(TARGET pingpong_server PROPERTY CXX_STANDARD 17)
target_link_libraries(pingpong_server trellis)
