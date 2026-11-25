//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef MXCPP_NODES_HASH_HELPER_H
#define MXCPP_NODES_HASH_HELPER_H

#include <cstdint>
#include <limits>

namespace mxcpp {

struct UVec3
{
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t z = 0;
};

inline uint32_t
Rotl32(uint32_t x, int k)
{
    return (x << k) | (x >> (32 - k));
}

inline void
BJMix(uint32_t& a, uint32_t& b, uint32_t& c)
{
    a -= c; a ^= Rotl32(c, 4); c += b;
    b -= a; b ^= Rotl32(a, 6); a += c;
    c -= b; c ^= Rotl32(b, 8); b += a;
    a -= c; a ^= Rotl32(c,16); c += b;
    b -= a; b ^= Rotl32(a,19); a += c;
    c -= b; c ^= Rotl32(b, 4); b += a;
}

inline uint32_t
BJFinal(uint32_t a, uint32_t b, uint32_t c)
{
    c ^= b; c -= Rotl32(b,14);
    a ^= c; a -= Rotl32(c,11);
    b ^= a; b -= Rotl32(a,25);
    c ^= b; c -= Rotl32(b,16);
    a ^= c; a -= Rotl32(c, 4);
    b ^= a; b -= Rotl32(a,14);
    c ^= b; c -= Rotl32(b,24);
    return c;
}

inline uint32_t
HashInt(int x)
{
    const uint32_t len = 1u;
    const uint32_t seed = 0xdeadbeefu + (len << 2u) + 13u;
    return BJFinal(seed + uint32_t(x), seed, seed);
}

inline uint32_t
HashInt(int x, int y)
{
    const uint32_t len = 2u;
    uint32_t a = 0xdeadbeefu + (len << 2u) + 13u;
    uint32_t b = a;
    uint32_t c = a;
    a += uint32_t(x);
    b += uint32_t(y);
    return BJFinal(a, b, c);
}

inline uint32_t
HashInt(int x, int y, int z)
{
    const uint32_t len = 3u;
    uint32_t a = 0xdeadbeefu + (len << 2u) + 13u;
    uint32_t b = a;
    uint32_t c = a;
    a += uint32_t(x);
    b += uint32_t(y);
    c += uint32_t(z);
    return BJFinal(a, b, c);
}

inline uint32_t
HashInt(int x, int y, int z, int xx)
{
    uint32_t a = 0xdeadbeefu + (4u << 2u) + 13u;
    uint32_t b = a;
    uint32_t c = a;
    a += uint32_t(x);
    b += uint32_t(y);
    c += uint32_t(z);
    BJMix(a, b, c);
    a += uint32_t(xx);
    return BJFinal(a, b, c);
}

inline uint32_t
HashInt(int x, int y, int z, int xx, int yy)
{
    uint32_t a = 0xdeadbeefu + (5u << 2u) + 13u;
    uint32_t b = a;
    uint32_t c = a;
    a += uint32_t(x);
    b += uint32_t(y);
    c += uint32_t(z);
    BJMix(a, b, c);
    a += uint32_t(xx);
    b += uint32_t(yy);
    return BJFinal(a, b, c);
}

inline UVec3
HashVec3(int x, int y)
{
    const uint32_t h = HashInt(x, y);
    return {h & 0xFFu, (h >> 8) & 0xFFu, (h >> 16) & 0xFFu};
}

inline UVec3
HashVec3(int x, int y, int z)
{
    const uint32_t h = HashInt(x, y, z);
    return {h & 0xFFu, (h >> 8) & 0xFFu, (h >> 16) & 0xFFu};
}

inline float
BitsTo01(uint32_t bits)
{
    // Match OSL's oslnoise::bits_to_01: derive the reciprocal in double
    // precision before casting to float, avoiding an intermediate rounded
    // float representation of UINT_MAX.
    constexpr float convertFactor = static_cast<float>(
        static_cast<double>(1.0) /
        static_cast<double>(std::numeric_limits<uint32_t>::max()));
    return static_cast<float>(bits) * convertFactor;
}

}  // namespace mxcpp

#endif
