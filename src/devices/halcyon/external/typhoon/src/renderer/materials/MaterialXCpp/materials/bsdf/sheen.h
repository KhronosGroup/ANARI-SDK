//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef PXR_IMAGING_PLUGIN_HDEMBREE_MATERIALXCPP_BSDF_SHEEN_H
#define PXR_IMAGING_PLUGIN_HDEMBREE_MATERIALXCPP_BSDF_SHEEN_H

#include "mathPrimitives.h"

#include <algorithm>
#include <cmath>

namespace mxcpp {
namespace Bsdf {
namespace detail {

/// Evaluates the normalized Charlie sheen normal distribution.
/// `alpha` must be finite and positive; `NdotH` must be finite in [0,1].
/// Returns a finite non-negative projected-area density. Values at or below
/// `kEpsilon` are regularized to `kEpsilon`; this operation cannot fail.
inline float
Charlie_D(float alpha, float NdotH)
{
    float sinTheta2 = 1.0f - NdotH * NdotH;
    float sinTheta = std::sqrt(std::max(0.0f, sinTheta2));
    float invAlpha = 1.0f / std::max(alpha, kEpsilon);
    return (2.0f + invAlpha) * std::pow(sinTheta, invAlpha) * kInvPi * 0.5f;
}

/// Evaluates Ashikhmin's sheen visibility term.
/// `NdotV` and `NdotL` must be finite non-negative surface cosines. Returns a
/// finite positive visibility scale; a degenerate denominator is regularized
/// by `kEpsilon`. This operation cannot fail.
inline float
Ashikhmin_V(float NdotV, float NdotL)
{
    return 1.0f / (4.0f * (NdotL + NdotV - NdotL * NdotV) + kEpsilon);
}

/// Approximates Charlie sheen directional albedo.
/// `NdotV` and `roughness` must be finite values in [0,1]. Returns a finite
/// albedo clamped to [0,1]; the fitted denominator is regularized by
/// `kEpsilon`. This operation cannot fail.
inline float
ApproxSheenDirAlbedo(float NdotV, float roughness)
{
    float x = NdotV;
    float y = roughness;
    float numerator = 13.67300f +
        (-68.78018f * x) +
        (799.08825f * y) +
        (-905.00061f * x * y) +
        (60.28956f * x * x) +
        (1086.96473f * y * y);
    float denominator = 1.0f +
        (61.57746f * x) +
        (442.78211f * y) +
        (2597.49308f * x * y) +
        (121.81241f * x * x) +
        (3045.55075f * y * y);
    return Clamp01(numerator / (denominator + kEpsilon));
}

}  // namespace detail
}  // namespace Bsdf
}  // namespace mxcpp

#endif  // PXR_IMAGING_PLUGIN_HDEMBREE_MATERIALXCPP_BSDF_SHEEN_H
