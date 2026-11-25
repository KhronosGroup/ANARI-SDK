//
// MaterialXCpp math types and basic math helpers — pxr-independent, Imath-based.
//
#ifndef MXCPP_MATH_TYPES_H
#define MXCPP_MATH_TYPES_H

#include <Imath/ImathMatrix.h>
#include <Imath/ImathVec.h>

namespace mxcpp {

using Vec2f = Imath::V2f;
using Vec3f = Imath::V3f;
using Vec4f = Imath::V4f;
using Mat3f = Imath::M33f;
using Mat4f = Imath::M44f;

/// Component-wise multiplication (scalars use operator*, vectors use
/// per-element multiplication).
template<typename T>
inline T CompMul(const T& a, const T& b) { return a * b; }

template<>
inline Vec2f CompMul<Vec2f>(const Vec2f& a, const Vec2f& b)
{
    return Vec2f(a[0] * b[0], a[1] * b[1]);
}

template<>
inline Vec3f CompMul<Vec3f>(const Vec3f& a, const Vec3f& b)
{
    return Vec3f(a[0] * b[0], a[1] * b[1], a[2] * b[2]);
}

template<>
inline Vec4f CompMul<Vec4f>(const Vec4f& a, const Vec4f& b)
{
    return Vec4f(a[0] * b[0], a[1] * b[1], a[2] * b[2], a[3] * b[3]);
}

/// Component-wise division.
template<typename T>
inline T CompDiv(const T& a, const T& b) {
    return (b != T(0)) ? a / b : T(0);
}

template<>
inline Vec2f CompDiv<Vec2f>(const Vec2f& a, const Vec2f& b)
{
    return Vec2f(b[0] != 0.0f ? a[0] / b[0] : 0.0f,
                 b[1] != 0.0f ? a[1] / b[1] : 0.0f);
}

template<>
inline Vec3f CompDiv<Vec3f>(const Vec3f& a, const Vec3f& b)
{
    return Vec3f(b[0] != 0.0f ? a[0] / b[0] : 0.0f,
                 b[1] != 0.0f ? a[1] / b[1] : 0.0f,
                 b[2] != 0.0f ? a[2] / b[2] : 0.0f);
}

template<>
inline Vec4f CompDiv<Vec4f>(const Vec4f& a, const Vec4f& b)
{
    return Vec4f(b[0] != 0.0f ? a[0] / b[0] : 0.0f,
                 b[1] != 0.0f ? a[1] / b[1] : 0.0f,
                 b[2] != 0.0f ? a[2] / b[2] : 0.0f,
                 b[3] != 0.0f ? a[3] / b[3] : 0.0f);
}

}  // namespace mxcpp

#endif  // MXCPP_MATH_TYPES_H
