//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef PXR_IMAGING_PLUGIN_HDEMBREE_MATERIALXCPP_BSDF_FRESNEL_H
#define PXR_IMAGING_PLUGIN_HDEMBREE_MATERIALXCPP_BSDF_FRESNEL_H

#include "mathPrimitives.h"
#include "shadingFrame.h"

#include <renderer/materials/MaterialXCpp/nodes/helpers/colorHelpers.h>
#include <renderer/materials/MaterialXCpp/nodes/helpers/mathHelpers.h>

#include <algorithm>
#include <cmath>

namespace mxcpp {
namespace Bsdf {
namespace detail {

/// Evaluates the component-wise Schlick approximation between normal and
/// grazing incidence.
///
/// \param F0 Finite normal-incidence reflectance in [0,1] per component.
/// \param cosTheta Finite absolute cosine between the incident direction and
/// the microfacet normal in [0,1].
/// \return Finite reflectance in [F0,1] per component.
/// \note Inputs are not clamped. Values outside their invariant ranges can
/// produce reflectance outside [0,1]; non-finite inputs propagate.
inline Vec3f
SchlickFresnel(const Vec3f& F0, float cosTheta)
{
    float t = 1.0f - cosTheta;
    float t2 = t * t;
    float t5 = t2 * t2 * t;
    return F0 + (Vec3f(1.0f) - F0) * t5;
}

/// Evaluates scalar Schlick dielectric Fresnel from an absolute IOR.
///
/// \param ior Finite transmitted-side absolute IOR greater than zero; callers
/// use values at least 1.
/// \param cosTheta Finite absolute cosine between the incident direction and
/// the microfacet normal in [0,1].
/// \return Finite scalar reflectance between the normal-incidence Fresnel
/// value and 1.
/// \note Inputs are not clamped. `ior == -1` divides by zero, and non-finite or
/// out-of-range inputs can produce non-finite or non-physical reflectance.
inline float
SchlickFresnelScalar(float ior, float cosTheta)
{
    float f0 = (ior - 1.0f) / (ior + 1.0f);
    f0 *= f0;
    float t = 1.0f - cosTheta;
    float t2 = t * t;
    float t5 = t2 * t2 * t;
    return f0 + (1.0f - f0) * t5;
}

/// Evaluates MaterialX's exact unpolarized dielectric Fresnel formula.
///
/// \param cosTheta Finite absolute cosine of incidence. Values outside [0,1]
/// are clamped to that range.
/// \param ior Finite transmitted-side absolute IOR. Values below `kEpsilon`
/// are raised to `kEpsilon`.
/// \return Reflectance clamped to [0,1], or 1 when the supplied angle and IOR
/// imply total internal reflection.
/// \note Non-finite inputs are outside the contract and can propagate NaNs.
inline float
MaterialXDielectricFresnel(float cosTheta, float ior)
{
    const float c = Clamp01(cosTheta);
    const float iorClamped = std::max(ior, kEpsilon);
    const float g2 = iorClamped * iorClamped + c * c - 1.0f;
    if (g2 < 0.0f) {
        return 1.0f;
    }

    const float g = std::sqrt(g2);
    const float sNumer = g - c;
    const float sDenom = std::max(g + c, kEpsilon);
    const float pNumer = (g + c) * c - 1.0f;
    const float pDenom = (g - c) * c + 1.0f;
    const float s = sNumer / sDenom;
    const float p = pNumer / std::copysign(
        std::max(std::abs(pDenom), kEpsilon), pDenom);
    return Clamp01(0.5f * s * s * (1.0f + p * p));
}

/// Evaluates MaterialX generalized Schlick Fresnel constrained by authored
/// reflectance at 0, 82, and 90 degrees.
///
/// \param color0 Finite normal-incidence reflectance per component.
/// \param color82 Finite reflectance at 82 degrees per component.
/// \param color90 Finite grazing-incidence reflectance per component.
/// \param exponent Finite non-negative falloff exponent; negative values are
/// treated as zero.
/// \param cosTheta Finite absolute cosine of incidence; clamped to [0,1].
/// \return Component-wise non-negative reflectance. The result is not clamped
/// above 1 because the authored control colors may intentionally exceed 1.
/// \note Non-finite color, exponent, or cosine inputs can propagate NaNs.
inline Vec3f
GeneralizedSchlickFresnel(
    const Vec3f& color0,
    const Vec3f& color82,
    const Vec3f& color90,
    float exponent,
    float cosTheta)
{
    constexpr float kCosThetaMax = 1.0f / 7.0f;
    const float clampedExponent = std::max(exponent, 0.0f);
    const float x = Clamp01(cosTheta);
    const float oneMinusX = Clamp01(1.0f - x);
    const float baseMix =
        std::pow(1.0f - kCosThetaMax, clampedExponent);
    const float factor = 1.0f /
        (kCosThetaMax * std::pow(1.0f - kCosThetaMax, 6.0f));
    const Vec3f a = CompMul(
        LerpVec(color0, color90, baseMix),
        (Vec3f(1.0f) - color82) * factor);
    const Vec3f result =
        LerpVec(color0, color90, std::pow(oneMinusX, clampedExponent)) -
        a * x * std::pow(oneMinusX, 6.0f);
    return Vec3f(
        std::max(result[0], 0.0f),
        std::max(result[1], 0.0f),
        std::max(result[2], 0.0f));
}

/// Computes normal-incidence conductor reflectance from complex IOR data.
///
/// \param ior Finite real IOR per color channel; negative components are
/// treated as zero.
/// \param extinction Finite extinction coefficient per color channel;
/// negative components are treated as zero.
/// \return Finite non-negative normal-incidence reflectance per component.
/// `kEpsilon` keeps the denominator non-zero for all finite inputs.
/// \note Non-finite components are outside the contract and can propagate.
inline Vec3f
ConductorF0(const Vec3f& ior, const Vec3f& extinction)
{
    Vec3f result(0.0f);
    for (int i = 0; i < 3; ++i) {
        float n = std::max(ior[i], 0.0f);
        float k = std::max(extinction[i], 0.0f);
        float nMinusOne2 = (n - 1.0f) * (n - 1.0f);
        float nPlusOne2 = (n + 1.0f) * (n + 1.0f);
        result[i] = (nMinusOne2 + k * k) / (nPlusOne2 + k * k + kEpsilon);
    }
    return result;
}

/// Resolves the reflection half-vector cosine used by Fresnel evaluation.
///
/// \param omegaInWld Finite unit direction toward the incoming light, on the
/// same side of the interface as `omegaOutWld`.
/// \param omegaOutWld Finite unit direction toward the previous path vertex.
/// \return Non-negative `dot(omegaOutWld, normalize(omegaInWld +
/// omegaOutWld))`.
/// \note Opposing directions have no defined half vector and return the
/// conservative grazing value 1. Non-unit or non-finite inputs violate the
/// contract.
inline float
ReflectionFresnelCosTheta(const Vec3f& omegaInWld, const Vec3f& omegaOutWld)
{
    const Vec3f halfVector = omegaInWld + omegaOutWld;
    if (halfVector.length() < kEpsilon) {
        return 1.0f;
    }
    return std::max(Dot(omegaOutWld, halfVector.normalized()), 0.0f);
}

/// Resolves the transmission half-vector cosine used by Fresnel evaluation.
///
/// \param ior Finite transmitted-side absolute IOR greater than zero.
/// \param normalShdWldOut Finite unit shading normal pointing out of the
/// surface.
/// \param omegaInWld Finite unit transmitted direction, on the opposite side
/// of the interface from `omegaOutWld`.
/// \param omegaOutWld Finite unit direction toward the previous path vertex.
/// \return Absolute cosine between `omegaOutWld` and the Walter transmission
/// half vector.
/// \note A degenerate weighted half vector falls back to
/// `abs(dot(normalShdWldOut, omegaOutWld))`. A non-positive IOR can divide by
/// zero or produce a non-physical half vector; non-finite/non-unit directions
/// violate the contract.
inline float
TransmissionFresnelCosTheta(float ior, const Vec3f& normalShdWldOut,
                             const Vec3f& omegaInWld, const Vec3f& omegaOutWld)
{
    Frame frame = Frame::FromNormal(normalShdWldOut);
    const Vec3f omegaOutLocal = frame.ToLocal(omegaOutWld);
    const Vec3f omegaInLocal = frame.ToLocal(omegaInWld);
    // Walter/pbrt uses transmitted/incident, the reciprocal of this
    // renderer's eta convention.
    const float etaPbrt = (omegaOutLocal[2] > 0.0f) ? ior : (1.0f / ior);

    Vec3f wm = omegaInLocal * etaPbrt + omegaOutLocal;
    if (wm.length() < kEpsilon) {
        return std::max(std::abs(Dot(normalShdWldOut, omegaOutWld)), 0.0f);
    }
    wm.normalize();
    return std::abs(Dot(omegaOutLocal, wm));
}

/// Converts a final Fresnel reflectance into a transmission multiplier relative
/// to a scalar base Fresnel model.
///
/// \param baseReflectance Finite scalar base reflectance in [0,1].
/// \param finalReflectance Finite final reflectance in [0,1] per component.
/// \return Non-negative `(1 - finalReflectance) / (1 - baseReflectance)`.
/// The denominator is floored at `1e-4` near total reflection, and `SafeVec`
/// converts negative or non-finite result components to zero.
/// \note Out-of-range finite inputs are tolerated through the denominator floor
/// and `SafeVec`, but do not represent a physical transmission ratio.
inline Vec3f
TransmissionScale(float baseReflectance, const Vec3f& finalReflectance)
{
    const float baseTransmission = std::max(1.0f - baseReflectance, 1.0e-4f);
    return SafeVec((Vec3f(1.0f) - finalReflectance) *
                    (1.0f / baseTransmission));
}

}  // namespace detail
}  // namespace Bsdf
}  // namespace mxcpp

#endif  // PXR_IMAGING_PLUGIN_HDEMBREE_MATERIALXCPP_BSDF_FRESNEL_H
