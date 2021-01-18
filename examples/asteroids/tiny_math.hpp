#pragma once

#include <cassert>
#include <cstdint>

template <typename T = float>
constexpr T tiny_tau = 6.283185307179586476925286766559;

template <std::size_t N>
struct tiny_vec;

template <>
struct tiny_vec<2> {
    float x = 0.f;
    float y = 0.f;

    constexpr float& operator[](std::size_t i) {
        const auto& self = *this;
        return const_cast<float&>(self[i]);
    }

    constexpr const float& operator[](std::size_t i) const {
        assert(i >= 0 && i < 2);
        switch (i) {
            default:
            case 0: return x;
            case 1: return y;
        }
    }
};

template <>
struct tiny_vec<3> {
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;

    constexpr float& operator[](std::size_t i) {
        const auto& self = *this;
        return const_cast<float&>(self[i]);
    }

    constexpr const float& operator[](std::size_t i) const {
        assert(i >= 0 && i < 3);
        switch (i) {
            default:
            case 0: return x;
            case 1: return y;
            case 2: return z;
        }
    }
};

template <>
struct tiny_vec<4> {
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
    float w = 0.f;

    constexpr float& operator[](std::size_t i) {
        const auto& self = *this;
        return const_cast<float&>(self[i]);
    }

    constexpr const float& operator[](std::size_t i) const {
        assert(i >= 0 && i < 4);
        switch (i) {
            default:
            case 0: return x;
            case 1: return y;
            case 2: return z;
            case 3: return w;
        }
    }
};

#define DECL_OP(OP)                                                                 \
    template <std::size_t N>                                                        \
    constexpr tiny_vec<N> operator OP(const tiny_vec<N>& a, const tiny_vec<N>& b) { \
        tiny_vec<N> r;                                                              \
        for (auto i = 0u; i < N; ++i) {                                             \
            r[i] = a[i] OP b[i];                                                    \
        }                                                                           \
        return r;                                                                   \
    }                                                                               \
    template <std::size_t N>                                                        \
    constexpr tiny_vec<N> operator OP(const tiny_vec<N>& a, float b) {              \
        tiny_vec<N> r;                                                              \
        for (auto i = 0u; i < N; ++i) {                                             \
            r[i] = a[i] OP b;                                                       \
        }                                                                           \
        return r;                                                                   \
    }

DECL_OP(+)
DECL_OP(-)
DECL_OP(*)
DECL_OP(/)

#undef DECL_OP

template <std::size_t N>
constexpr auto length(const tiny_vec<N>& vec) -> float {
    auto acc = 0.f;
    for (auto i = 0u; i < N; ++i) {
        acc += vec[i] * vec[i];
    }
    return std::sqrt(acc);
}

template <std::size_t N>
constexpr auto normalize(const tiny_vec<N>& vec) -> tiny_vec<N> {
    return vec / length(vec);
}

constexpr auto rotate(const tiny_vec<2>& vec, float rad) -> tiny_vec<2> {
    auto sin = std::sin(rad);
    auto cos = std::cos(rad);
    return {vec.x * cos - vec.y * sin, vec.x * sin + vec.y * cos};
}

template <std::size_t C, std::size_t R = C>
struct tiny_matrix {
    using col_type = tiny_vec<R>;
    col_type columns[C];

    static constexpr tiny_matrix identity() {
        tiny_matrix m = {};
        for (auto i = 0u; i < C && i < R; ++i) {
            m.columns[i][i] = 1.f;
        }
        return m;
    }

    constexpr col_type& operator[](std::size_t i) {
        assert(i >= 0 && i < C);
        return columns[i];
    }

    constexpr const col_type& operator[](std::size_t i) const {
        assert(i >= 0 && i < C);
        return columns[i];
    }
};

template <std::size_t C, std::size_t R>
const float* value_ptr(const tiny_matrix<C, R>& m) {
    return reinterpret_cast<const float*>(&m);
}

template <std::size_t C, std::size_t R>
constexpr tiny_vec<R> operator*(const tiny_matrix<C, R>& m, const tiny_vec<C>& v) {
    auto r = tiny_vec<R>{};
    for (auto i = 0u; i < C; ++i) {
        r = r + m[i] * v[i];
    }
    return r;
}

template <std::size_t R1, std::size_t C2, std::size_t CR>
constexpr tiny_matrix<C2, R1> operator*(const tiny_matrix<CR, R1>& m1, const tiny_matrix<C2, CR>& m2) {
    auto r = tiny_matrix<C2, R1>{};
    for (auto c = 0u; c < R1; ++c) {
        for (auto i = 0u; i < C2; ++i) {
            r[c] = r[c] + m1[i] * m2[c][i];
        }
    }
    return r;
}
