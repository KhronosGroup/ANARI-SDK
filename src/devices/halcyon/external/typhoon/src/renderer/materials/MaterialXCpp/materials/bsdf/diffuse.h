//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef PXR_IMAGING_PLUGIN_HDEMBREE_MATERIALXCPP_BSDF_DIFFUSE_H
#define PXR_IMAGING_PLUGIN_HDEMBREE_MATERIALXCPP_BSDF_DIFFUSE_H

#include "mathPrimitives.h"
#include "shadingFrame.h"

#include <algorithm>
#include <cmath>

namespace mxcpp {
namespace Bsdf {
namespace detail {

/// Evaluates the Oren-Nayar directional factor without the albedo/pi term.
/// `NdotV` and `NdotL` are finite non-negative surface cosines, `LdotV` is a
/// finite direction dot product in [-1,1], and `roughness` is finite in [0,1].
/// Returns a finite non-negative scale. Grazing configurations use zero for
/// the azimuthal correction; this operation cannot otherwise fail.
inline float
OrenNayarFactor(float NdotV, float NdotL, float LdotV, float roughness)
{
    float s = LdotV - NdotL * NdotV;
    float stinv = (s > 0.0f) ? s / std::max(NdotL, NdotV) : 0.0f;
    float sigma2 = roughness * roughness;
    float A = 1.0f - 0.5f * (sigma2 / (sigma2 + 0.33f));
    float B = 0.45f * sigma2 / (sigma2 + 0.09f);
    return A + B * stinv;
}

/// Evaluates the energy-compensated EON diffuse BRDF.
/// `color` must be finite in [0,1], `roughness` finite in [0,1], `NdotV` and
/// `NdotL` finite non-negative cosines, and `LdotV` finite in [-1,1].
/// Returns finite non-negative RGB reflectance; zero-denominator cases are
/// bounded by `kEpsilon`. This operation does not throw.
Vec3f EvalEonDiffuse(
    const Vec3f& color, float roughness, float NdotV, float NdotL,
    float LdotV);

/// Evaluates the Burley diffuse directional factor without the albedo/pi term.
/// `NdotV`, `NdotL`, and `LdotH` are finite values in [0,1]; `roughness` is
/// finite in [0,1]. Returns a finite non-negative scale. Cannot fail.
inline float
BurleyFactor(float NdotV, float NdotL, float LdotH, float roughness)
{
    float F90 = 0.5f + (2.0f * roughness * LdotH * LdotH);
    const auto schlick = [](float cosTheta, float F0, float F90Value) {
        float x = std::pow(Clamp01(1.0f - cosTheta), 5.0f);
        return F0 + (F90Value - F0) * x;
    };
    return schlick(NdotL, 1.0f, F90) * schlick(NdotV, 1.0f, F90);
}

/// Returns the cosine-weighted PDF for transmission below the shading normal.
/// `normalShdWldOut` and `omegaInWld` must be finite unit vectors. Returns
/// zero for the reflection hemisphere and otherwise a finite non-negative
/// solid-angle density. Cannot fail.
inline float
PdfTranslucent(const Vec3f& normalShdWldOut, const Vec3f& omegaInWld)
{
    if (Dot(normalShdWldOut, omegaInWld) >= 0.0f) {
        return 0.0f;
    }
    return CosineHemispherePdf(std::abs(Dot(normalShdWldOut, omegaInWld)));
}

/// Evaluates a Lambertian translucent lobe below the shading normal.
/// `color` must be finite and non-negative, `weight` finite and non-negative,
/// and both vectors finite and unit length. Returns zero outside the
/// transmission hemisphere or when the weight is zero; otherwise returns
/// `color * weight / pi`. Cannot fail.
inline Vec3f
EvalTranslucent(
    const Vec3f& color, float weight, const Vec3f& normalShdWldOut,
    const Vec3f& omegaInWld)
{
    if (Dot(normalShdWldOut, omegaInWld) >= 0.0f || weight <= 0.0f) {
        return Vec3f(0.0f);
    }
    return color * (weight * kInvPi);
}

}  // namespace detail
}  // namespace Bsdf
}  // namespace mxcpp

#endif  // PXR_IMAGING_PLUGIN_HDEMBREE_MATERIALXCPP_BSDF_DIFFUSE_H
