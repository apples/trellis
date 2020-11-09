#pragma once

#ifdef TRELLIS_ENABLE_LOGGING
#include <iostream>
#include <iomanip>

#define TRELLIS_LOG_DATAGRAM(note, dgram, count)                    \
    do {                                                            \
        auto fmt = std::ios(nullptr);                               \
        fmt.copyfmt(std::clog);                                     \
        std::clog << "[trellis] DATAGRAM (" << (note) << ") [";     \
        std::clog << std::hex;                                      \
        for (auto i = 0u; i < (count); ++i) {                       \
            auto last = i == (count) - 1;                           \
            std::clog << std::setw(2) << std::uint32_t((dgram)[i]); \
            if (i != (count) - 1) {                                 \
                std::clog << ",";                                   \
            }                                                       \
        }                                                           \
        std::clog << "]" << std::endl;                              \
        std::clog.copyfmt(fmt);                                     \
    } while (false)
#else
#define TRELLIS_LOG_DATAGRAM(dgram, count) do {} while(false)
#endif
