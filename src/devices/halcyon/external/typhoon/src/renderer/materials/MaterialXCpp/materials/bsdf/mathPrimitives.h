//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef PXR_IMAGING_PLUGIN_HDEMBREE_MATERIALXCPP_BSDF_MATHPRIMITIVES_H
#define PXR_IMAGING_PLUGIN_HDEMBREE_MATERIALXCPP_BSDF_MATHPRIMITIVES_H

#include <renderer/materials/MaterialXCpp/mathTypes.h>
#include <renderer/materials/MaterialXCpp/nodes/helpers/colorHelpers.h>
#include <renderer/materials/MaterialXCpp/nodes/helpers/mathHelpers.h>

#include <algorithm>
#include <cmath>

namespace mxcpp {
namespace Bsdf {
namespace detail {

/// Positive denominator and vector-length floor used by BSDF numerics.
inline constexpr float kEpsilon = 1.0e-7f;

/// Clamps finite perceptual roughness to the renderer's supported [0.001,1]
/// interval. Non-finite input violates the contract; this operation cannot
/// otherwise fail.
inline float
ClampRoughness(float r)
{
    return std::clamp(r, 0.001f, 1.0f);
}

/// Linearly interpolates componentwise as `a * (1-t) + b * t`.
/// All inputs must be finite; `t` is normally in [0,1] but extrapolation is
/// permitted. Returns signed components and cannot fail.
inline Vec3f
LerpVec(const Vec3f& a, const Vec3f& b, float t)
{
    return a * (1.0f - t) + b * t;
}

/// Computes an RGB dot product with caller-provided luminance coefficients.
/// Both vectors must be finite. Coefficients are expected to be non-negative
/// and sum to one. Returns a finite scalar and cannot fail.
inline float
Luminance(const Vec3f& c, const Vec3f& coefficients)
{
    return coefficients[0] * c[0] +
           coefficients[1] * c[1] +
           coefficients[2] * c[2];
}

/// Returns the renderer's linear Rec.709 luminance coefficients.
/// This operation cannot fail.
inline Vec3f
DefaultLuminanceCoefficients()
{
    return Vec3f(
        0.212639005871510f,
        0.715168678767756f,
        0.072192315360734f);
}

/// Sanitizes a radiometric RGB value.
/// Each negative or non-finite component of `v` becomes zero; other components
/// are preserved. Returns a finite non-negative vector and cannot fail.
inline Vec3f
SafeVec(const Vec3f& v)
{
    Vec3f r = v;
    for (int i = 0; i < 3; ++i) {
        if (!std::isfinite(r[i]) || r[i] < 0.0f) {
            r[i] = 0.0f;
        }
    }
    return r;
}

/// Clamps a finite scalar to [0,1]; non-finite input returns zero.
/// This operation cannot fail.
inline float
ClampFinite01(float x)
{
    return std::isfinite(x) ? Clamp01(x) : 0.0f;
}

/// Computes a componentwise square root after clamping negative components to
/// zero. Components of `v` must be finite. Returns finite non-negative values
/// and cannot otherwise fail.
inline Vec3f
SqrtVec(const Vec3f& v)
{
    return Vec3f(
        std::sqrt(std::max(v[0], 0.0f)),
        std::sqrt(std::max(v[1], 0.0f)),
        std::sqrt(std::max(v[2], 0.0f)));
}

/// Computes componentwise cosine. Components of `v` must be finite.
/// Returns finite values in [-1,1] and cannot fail.
inline Vec3f
CosVec(const Vec3f& v)
{
    return Vec3f(std::cos(v[0]), std::cos(v[1]), std::cos(v[2]));
}

/// Computes componentwise exponential. Components of `v` must be finite and
/// small enough not to overflow. Returns positive finite values for valid
/// input; this operation cannot otherwise fail.
inline Vec3f
ExpVec(const Vec3f& v)
{
    return Vec3f(std::exp(v[0]), std::exp(v[1]), std::exp(v[2]));
}

/// Squares every component of finite `v`. Returns non-negative components;
/// callers must keep inputs small enough to avoid overflow. Cannot otherwise
/// fail.
inline Vec3f
SquareVec(const Vec3f& v)
{
    return CompMul(v, v);
}

/// Clamps every finite component of `v` to at least finite `minimum`.
/// Signed values above the floor are preserved. Cannot fail.
inline Vec3f
MaxVec(const Vec3f& v, float minimum)
{
    return Vec3f(
        std::max(v[0], minimum),
        std::max(v[1], minimum),
        std::max(v[2], minimum));
}

}  // namespace detail
}  // namespace Bsdf
}  // namespace mxcpp

#endif  // PXR_IMAGING_PLUGIN_HDEMBREE_MATERIALXCPP_BSDF_MATHPRIMITIVES_H
