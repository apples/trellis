#pragma once

#ifdef TRELLIS_ENABLE_LOGGING

#include <iostream>
#include <iomanip>

#define TRELLIS_LOG_ACTION(THING, ID, ...)                               \
    ([](auto&& thing, auto&& id, auto&&... args) {                       \
        std::clog << "[trellis] ACTION (" << thing << ":" << id << ") "; \
        (std::clog << ... << args);                                      \
        std::clog << std::endl;                                          \
    }((THING), (ID), __VA_ARGS__))

#define TRELLIS_LOG_DATAGRAM(NOTE, DGRAM, COUNT)                  \
    ([](auto&& note, auto&& dgram, auto&& count) {                \
        auto fmt = std::ios(nullptr);                             \
        fmt.copyfmt(std::clog);                                   \
        std::clog << "[trellis] DATAGRAM (" << note << ") [";     \
        std::clog << std::hex;                                    \
        for (auto i = 0u; i < count; ++i) {                       \
            auto last = i == count - 1;                           \
            std::clog << std::setw(2) << std::uint32_t(dgram[i]); \
            if (i != count - 1) {                                 \
                std::clog << ",";                                 \
            }                                                     \
        }                                                         \
        std::clog << "]" << std::endl;                            \
        std::clog.copyfmt(fmt);                                   \
    }((NOTE), (DGRAM), (COUNT)))

#else
#define TRELLIS_LOG_ACTION(cid, note) ((void)0)
#define TRELLIS_LOG_DATAGRAM(dgram, count) ((void)0)
#endif
