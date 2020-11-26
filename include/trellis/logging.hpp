#pragma once

#ifdef TRELLIS_ENABLE_LOGGING

#include <iostream>
#include <iomanip>

#define TRELLIS_BEGIN_SECTION(NAME)                         \
    ([](auto&& name) {                                      \
        std::clog << "[trellis] >>> " << name << std::endl; \
    }((NAME)))

#define TRELLIS_END_SECTION(NAME)                           \
    ([](auto&& name) {                                      \
        std::clog << "[trellis] <<< " << name << std::endl; \
    }((NAME)))

#define TRELLIS_LOG_ACTION(THING, ID, ...)                               \
    ([](auto&& thing, auto&& id, auto&&... args) {                       \
        std::clog << "[trellis] ACTION (" << thing << ":" << id << ") "; \
        (std::clog << ... << args);                                      \
        std::clog << std::endl;                                          \
    }((THING), (ID), __VA_ARGS__))

#define TRELLIS_LOG_DATAGRAM(NOTE, DGRAM, COUNT)                                \
    ([](auto&& note, auto&& dgram, auto&& count) {                              \
        auto fmt = std::ios(nullptr);                                           \
        fmt.copyfmt(std::clog);                                                 \
        std::clog << "[trellis] DATAGRAM (" << note << ") [";                   \
        std::clog << std::hex;                                                  \
        for (auto i = 0u; i < count; ++i) {                                     \
            std::clog << std::setw(2) << std::uint32_t(std::uint8_t(dgram[i])); \
            if (i != count - 1) {                                               \
                std::clog << ",";                                               \
            }                                                                   \
        }                                                                       \
        std::clog << "]" << std::endl;                                          \
        std::clog.copyfmt(fmt);                                                 \
    }((NOTE), (DGRAM), (COUNT)))

#define TRELLIS_LOG_FRAGMENT(NOTE, N, COUNT)                                                               \
    ([](auto&& note, auto&& n, auto&& count) {                                                             \
        std::clog << "[trellis] FRAGMENT (" << note << ") " << int(n) << " / " << int(count) << std::endl; \
    }((NOTE), (N), (COUNT)))

#else
#define TRELLIS_BEGIN_SECTION(NAME) ((void)0)
#define TRELLIS_END_SECTION(NAME) ((void)0)
#define TRELLIS_LOG_ACTION(THING, ID, ...) ((void)0)
#define TRELLIS_LOG_DATAGRAM(NOTE, DGRAM, COUNT) ((void)0)
#define TRELLIS_LOG_FRAGMENT(NOTE, N, COUNT) ((void)0)
#endif
