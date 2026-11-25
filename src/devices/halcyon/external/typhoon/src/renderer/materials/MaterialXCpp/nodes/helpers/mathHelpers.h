//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef MXCPP_NODES_MATH_HELPERS_H
#define MXCPP_NODES_MATH_HELPERS_H

#include <renderer/materials/MaterialXCpp/mathTypes.h>

#include <algorithm>
#include <cmath>
#include <type_traits>

namespace mxcpp {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kInvPi = 1.0f / kPi;
constexpr float kTwoPi = 2.0f * kPi;
constexpr float kDegreesToRadians = kPi / 180.0f;

inline float Dot(const Vec2f& a, const Vec2f& b) { return a.dot(b); }
inline float Dot(const Vec3f& a, const Vec3f& b) { return a.dot(b); }
inline float Dot(const Vec4f& a, const Vec4f& b) { return a.dot(b); }

inline Vec3f Cross(const Vec3f& a, const Vec3f& b) { return a.cross(b); }

template<typename T, std::enable_if_t<std::is_arithmetic_v<T>, int> = 0>
inline T
ClampValue(const T& v, const T& lo, const T& hi)
{
    return std::clamp(v, lo, hi);
}

template<typename T, std::enable_if_t<!std::is_arithmetic_v<T>, int> = 0>
inline T
ClampValue(const T& v, const T& lo, const T& hi)
{
    T result(v);
    for (unsigned int i = 0; i < T::dimensions(); ++i) {
        result[i] = std::clamp(v[i], lo[i], hi[i]);
    }
    return result;
}

inline float
Clamp01(float v)
{
    return ClampValue(v, 0.0f, 1.0f);
}

template<typename T, std::enable_if_t<!std::is_arithmetic_v<T>, int> = 0>
inline T
Clamp01(const T& v)
{
    return ClampValue(v, T(0.0f), T(1.0f));
}

template<typename T>
inline T
Mix(const T& bg, const T& fg, float t)
{
    return bg + (fg - bg) * t;
}

inline float
Smoothstep(float lo, float hi, float v)
{
    // Match the generated MaterialX implementation. Testing the upper edge
    // first gives deterministic hard-step behavior for equal or inverted
    // bounds instead of dividing by zero or returning zero everywhere.
    if (v >= hi) {
        return 1.0f;
    }
    if (v <= lo) {
        return 0.0f;
    }
    const float t = (v - lo) / (hi - lo);
    return t * t * (3.0f - 2.0f * t);
}

inline float
PositiveMod(float x, float y = 1.0f)
{
    if (y == 0.0f) {
        return 0.0f;
    }
    return x - y * std::floor(x / y);
}

inline float
Remap(float v, float inLo, float inHi, float outLo, float outHi)
{
    if (inHi == inLo) {
        return outLo;
    }
    const float t = (v - inLo) / (inHi - inLo);
    return outLo + t * (outHi - outLo);
}

template<typename T, std::enable_if_t<std::is_arithmetic_v<T>, int> = 0>
inline T
Fract(const T& value)
{
    return value - std::floor(value);
}

template<typename T, std::enable_if_t<!std::is_arithmetic_v<T>, int> = 0>
inline T
Fract(const T& value)
{
    T result(value);
    for (unsigned int i = 0; i < T::dimensions(); ++i) {
        result[i] = Fract(value[i]);
    }
    return result;
}

inline Vec2f
Rotate2d(const Vec2f& v, float amountDegrees)
{
    const float rad = amountDegrees * kDegreesToRadians;
    const float c = std::cos(rad);
    const float s = std::sin(rad);
    return Vec2f(v[0] * c + v[1] * s,
                 -v[0] * s + v[1] * c);
}

inline Vec3f
Rotate3d(const Vec3f& v, float amountDegrees, const Vec3f& axis)
{
    Vec3f normalizedAxis = axis;
    const float len = normalizedAxis.length();
    if (len < 1.0e-8f) {
        return v;
    }

    normalizedAxis /= len;
    const float rad = amountDegrees * kDegreesToRadians;
    const float c = std::cos(rad);
    const float s = std::sin(rad);
    return v * c +
           Cross(v, normalizedAxis) * s +
           normalizedAxis * Dot(normalizedAxis, v) * (1.0f - c);
}

inline float
Fade(float t)
{
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

inline float
Bilerp(float v0, float v1, float v2, float v3, float s, float t)
{
    const float s1 = 1.0f - s;
    return (1.0f - t) * (v0 * s1 + v1 * s) + t * (v2 * s1 + v3 * s);
}

inline Vec3f
Bilerp(Vec3f v0, Vec3f v1, Vec3f v2, Vec3f v3, float s, float t)
{
    const float s1 = 1.0f - s;
    return (v0 * s1 + v1 * s) * (1.0f - t) + (v2 * s1 + v3 * s) * t;
}

inline float
Trilerp(float v0, float v1, float v2, float v3,
        float v4, float v5, float v6, float v7,
        float s, float t, float r)
{
    const float s1 = 1.0f - s;
    const float t1 = 1.0f - t;
    const float r1 = 1.0f - r;
    return r1 * (t1 * (v0 * s1 + v1 * s) + t * (v2 * s1 + v3 * s)) +
           r  * (t1 * (v4 * s1 + v5 * s) + t * (v6 * s1 + v7 * s));
}

inline Vec3f
Trilerp(Vec3f v0, Vec3f v1, Vec3f v2, Vec3f v3,
        Vec3f v4, Vec3f v5, Vec3f v6, Vec3f v7,
        float s, float t, float r)
{
    const float s1 = 1.0f - s;
    const float t1 = 1.0f - t;
    const float r1 = 1.0f - r;
    return (v0 * s1 + v1 * s) * (t1 * r1) +
           (v2 * s1 + v3 * s) * (t  * r1) +
           (v4 * s1 + v5 * s) * (t1 * r ) +
           (v6 * s1 + v7 * s) * (t  * r );
}

inline float
FloorFrac(float x, int& i)
{
    i = static_cast<int>(std::floor(x));
    return x - float(i);
}

}  // namespace mxcpp

#endif
