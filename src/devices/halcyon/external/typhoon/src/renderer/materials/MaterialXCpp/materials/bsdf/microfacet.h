//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef PXR_IMAGING_PLUGIN_HDEMBREE_MATERIALXCPP_BSDF_MICROFACET_H
#define PXR_IMAGING_PLUGIN_HDEMBREE_MATERIALXCPP_BSDF_MICROFACET_H

#include "mathPrimitives.h"
#include "shadingFrame.h"

#include <renderer/materials/MaterialXCpp/materials/bsdf.h>
#include <renderer/materials/MaterialXCpp/nodes/helpers/colorHelpers.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace mxcpp {
namespace Bsdf {
namespace detail {

/// Perceptual roughness below this value uses the public delta path.
inline constexpr float kSmoothRoughnessThreshold = 1.0e-3f;

/// Smallest finite GGX alpha accepted by distribution and sampling helpers.
inline constexpr float kMinMicrofacetAlpha = 1.0e-6f;

/// GGX alpha below this threshold is treated as a discrete delta lobe.
inline constexpr float kEffectivelySmoothMicrofacetAlpha = 1.0e-3f;

/// Converts finite perceptual `roughness` to alpha after clamping roughness to
/// [0.001,1]. Returns finite alpha in [1e-6,1] and cannot fail.
inline float
RoughnessToAlpha(float roughness)
{
    float r = ClampRoughness(roughness);
    return r * r;
}

/// Clamps both finite anisotropic alpha components to
/// [`kMinMicrofacetAlpha`,1]. Returns finite positive alpha; cannot fail.
inline Vec2f
ClampAlpha(const Vec2f& alpha)
{
    return Vec2f(
        std::clamp(alpha[0], kMinMicrofacetAlpha, 1.0f),
        std::clamp(alpha[1], kMinMicrofacetAlpha, 1.0f));
}

/// Returns the geometric mean of finite anisotropic alpha after clamping each
/// component to [`kMinMicrofacetAlpha`,1]. Cannot fail.
inline float
AverageAlphaForEnergy(const Vec2f& alpha)
{
    const float clampedX =
        std::clamp(alpha[0], kMinMicrofacetAlpha, 1.0f);
    const float clampedY =
        std::clamp(alpha[1], kMinMicrofacetAlpha, 1.0f);
    return std::sqrt(clampedX * clampedY);
}

/// Converts `AverageAlphaForEnergy(alpha)` back to perceptual roughness.
/// `alpha` must be finite. Returns finite roughness in [0.001,1]; cannot fail.
inline float
AverageAlphaAsRoughness(const Vec2f& alpha)
{
    return std::sqrt(AverageAlphaForEnergy(alpha));
}

/// Reduces finite anisotropic alpha to the scalar roughness coordinate used by
/// BSDL's layer LUT. Components are clamped to
/// [`kMinMicrofacetAlpha`,1]. Returns finite [0.001,1]; cannot fail.
inline float
BsdlLayerRoughnessFromAlpha(const Vec2f& alpha)
{
    const float alphaX =
        std::clamp(alpha[0], kMinMicrofacetAlpha, 1.0f);
    const float alphaY =
        std::clamp(alpha[1], kMinMicrofacetAlpha, 1.0f);
    return std::sqrt(
        (std::max(alphaX, alphaY) + std::min(alphaX, alphaY)) * 0.5f);
}

/// Returns whether finite roughness components differ by less than 1e-6.
/// Negative values violate the roughness contract but do not cause failure.
inline bool
IsEffectivelyIsotropic(const Vec2f& roughness)
{
    return std::abs(roughness[0] - roughness[1]) < 1.0e-6f;
}

/// Returns whether the largest finite alpha component is below the renderer's
/// discrete-lobe threshold. Alpha components must be non-negative.
inline bool
IsEffectivelyDeltaAlpha(const Vec2f& alpha)
{
    // Match pbrt-v4's TrowbridgeReitzDistribution::EffectivelySmooth().
    // Below this alpha, finite GGX is numerically valid but produces severe
    // near-mirror indirect-light variance in a unidirectional path tracer.
    return std::max(alpha[0], alpha[1]) <
        kEffectivelySmoothMicrofacetAlpha;
}

/// Raises finite unit `normalShdLobeWldOut` toward finite unit
/// `normalGeomWldOut` when reflecting finite unit `omegaOutWld` would send the
/// result below the geometric surface. The geometric normal and outgoing
/// direction must lie on the incident transport side. Returns a finite unit
/// closure normal whose mirror reflection reaches the Cycles visibility
/// threshold. Degenerate correction geometry returns the input normal. Does
/// not throw.
Vec3f EnsureValidSpecularReflection(
    const Vec3f& normalGeomWldOut,
    const Vec3f& omegaOutWld,
    const Vec3f& normalShdLobeWldOut);

/// Evaluates the isotropic GGX normal distribution.
/// `alpha` must be finite and positive and is clamped to
/// [`kMinMicrofacetAlpha`,1]; `NdotH` must be finite and is clamped to [0,1].
/// Returns a finite non-negative projected-area density; a degenerate
/// denominator returns zero.
inline float
GGX_D(float alpha, float NdotH)
{
    const float clampedAlpha =
        std::clamp(alpha, kMinMicrofacetAlpha, 1.0f);
    const float clampedNdotH = std::clamp(NdotH, 0.0f, 1.0f);
    const float a2 = clampedAlpha * clampedAlpha;
    const float nDotH2 = clampedNdotH * clampedNdotH;
    const float denom = (1.0f - nDotH2) + a2 * nDotH2;
    const float denom2 = denom * denom;
    if (!std::isfinite(denom2) || denom2 <= 0.0f) {
        return 0.0f;
    }
    return a2 / (kPi * denom2);
}

/// Evaluates the height-correlated isotropic GGX visibility term.
/// `alpha`, `NdotV`, and `NdotL` must be finite; alpha is positive and both
/// cosines are in [0,1]. Returns a finite positive scale, with grazing
/// denominators regularized by `kEpsilon`.
inline float
GGX_V(float alpha, float NdotV, float NdotL)
{
    float a2 = alpha * alpha;
    float ggxV = NdotL * std::sqrt(NdotV * NdotV * (1.0f - a2) + a2);
    float ggxL = NdotV * std::sqrt(NdotL * NdotL * (1.0f - a2) + a2);
    return 0.5f / (ggxV + ggxL + kEpsilon);
}

/// Evaluates isotropic Smith G1 masking.
/// `alpha` must be finite and positive; `cosTheta` must be finite in [0,1].
/// Returns finite masking in [0,1], with a grazing denominator floor.
inline float
SmithG1(float alpha, float cosTheta)
{
    float a2 = alpha * alpha;
    float cos2 = cosTheta * cosTheta;
    return 2.0f * cosTheta /
        (cosTheta + std::sqrt(a2 + (1.0f - a2) * cos2) + kEpsilon);
}

enum class BumpShadowingContext
{
    Evaluation,
    Sampling
};

/// Returns whether `omegaInWld` has consistent sides in the smooth and exact
/// lobe frames. All inputs must be finite unit vectors.
inline bool
BumpHemisphereAgreement(const Vec3f& normalSrfWldOut,
                        const Vec3f& normalShdLobeWldOut,
                        const Vec3f& omegaInWld)
{
    return Dot(normalSrfWldOut, omegaInWld) *
            Dot(normalSrfWldOut, normalShdLobeWldOut) *
            Dot(normalShdLobeWldOut, omegaInWld) >=
        0.0f;
}

/// Returns whether a sampled direction lies strictly on the side of both the
/// exact lobe and geometric normals identified by its event label. Lobe
/// applicability, including the microfacet outgoing-side precondition, is the
/// leaf's responsibility. All vectors must be finite and unit length.
inline bool
SampledDirectionIsValid(bool isTransmission,
                        const Vec3f& normalShdLobeWldOut,
                        const Vec3f& normalGeomWldOut,
                        const Vec3f& omegaInWld)
{
    const float lobeSide = Dot(normalShdLobeWldOut, omegaInWld);
    const float geomSide = Dot(normalGeomWldOut, omegaInWld);
    return isTransmission
        ? lobeSide < 0.0f && geomSide < 0.0f
        : lobeSide > 0.0f && geomSide > 0.0f;
}

/// Cycles-aligned bump shadowing. Evaluation rejects disagreement for every
/// lobe; sampling rejects and softens only diffuse-family lobes. PDF is
/// unaffected.
inline float
BumpShadowingTerm(const Vec3f& normalSrfWldOut,
                  const Vec3f& normalShdLobeWldOut,
                  const Vec3f& omegaInWld,
                  bool isDiffuseFamily,
                  BumpShadowingContext context)
{
    if (normalSrfWldOut == normalShdLobeWldOut) {
        return 1.0f;
    }
    if (!BumpHemisphereAgreement(
            normalSrfWldOut, normalShdLobeWldOut, omegaInWld) &&
        (context == BumpShadowingContext::Evaluation || isDiffuseFamily)) {
        return 0.0f;
    }
    if (!isDiffuseFamily) {
        return 1.0f;
    }

    const float cosIn = std::abs(Dot(normalSrfWldOut, omegaInWld));
    const float cosDeviation =
        std::abs(Dot(normalSrfWldOut, normalShdLobeWldOut));
    if (cosDeviation >= 1.0f || cosIn >= 1.0f) {
        return 1.0f;
    }
    if (cosIn < 1.0e-6f) {
        return 0.0f;
    }
    const float tanDeviationSquared =
        1.0f / (cosDeviation * cosDeviation) - 1.0f;
    const float alphaSquared =
        std::clamp(0.125f * tanDeviationSquared, 0.0f, 1.0f);
    return SmithG1(std::sqrt(alphaSquared), cosIn);
}

/// Converts `GGX_V` to the isotropic Smith G2 factor.
/// Inputs satisfy `GGX_V`'s invariants. Returns finite non-negative masking.
inline float
GGX_G(float alpha, float NdotV, float NdotL)
{
    return std::max(
        4.0f * NdotV * NdotL * GGX_V(alpha, NdotV, NdotL), 0.0f);
}

/// Samples an isotropic GGX visible-normal distribution in local space.
/// `omegaOutLocal` must be a finite unit direction in the positive
/// hemisphere, `alpha` finite and positive, and `u1`/`u2` finite in [0,1).
/// Returns a finite unit microfacet normal in the positive hemisphere. A
/// degenerate stretched view returns `(0,0,1)`.
inline Vec3f
SampleGGX_VNDF(
    const Vec3f& omegaOutLocal, float alpha, float u1, float u2)
{
    Vec3f wh(
        alpha * omegaOutLocal[0], alpha * omegaOutLocal[1],
        omegaOutLocal[2]);
    float whLen = wh.length();
    if (whLen < kEpsilon) {
        return Vec3f(0.0f, 0.0f, 1.0f);
    }
    wh /= whLen;
    if (wh[2] < 0.0f) {
        wh = -wh;
    }

    Vec3f T1 = (wh[2] < 0.99999f)
        ? Cross(Vec3f(0.0f, 0.0f, 1.0f), wh).normalized()
        : Vec3f(1.0f, 0.0f, 0.0f);
    Vec3f T2 = Cross(wh, T1);

    float r = std::sqrt(u1);
    float phi = 2.0f * kPi * u2;
    float t1 = r * std::cos(phi);
    float t2 = r * std::sin(phi);

    float h = std::sqrt(std::max(0.0f, 1.0f - t1 * t1));
    float blend = (1.0f + wh[2]) * 0.5f;
    t2 = (1.0f - blend) * h + blend * t2;

    float pz = std::sqrt(std::max(0.0f, 1.0f - t1 * t1 - t2 * t2));
    Vec3f nh = T1 * t1 + T2 * t2 + wh * pz;

    Vec3f wm(alpha * nh[0], alpha * nh[1], std::max(1e-6f, nh[2]));
    return wm.normalized();
}

/// Converts an isotropic visible-normal density to reflection solid angle.
/// Both directions must be finite unit local vectors in the positive
/// hemisphere and `alpha` finite and positive. Returns a finite non-negative
/// density; grazing denominators are regularized by `kEpsilon`.
inline float
PdfGGX_VNDF(
    const Vec3f& omegaOutLocal, const Vec3f& wmLocal, float alpha)
{
    float cosThetaO = std::max(omegaOutLocal[2], kEpsilon);
    float G1 = SmithG1(alpha, cosThetaO);
    float NdotH = std::max(wmLocal[2], 0.0f);
    float VdotH = std::max(Dot(omegaOutLocal, wmLocal), kEpsilon);
    float D = GGX_D(alpha, NdotH);
    float pdfMicrofacetNormalSolidAngle = G1 * D * VdotH / cosThetaO;
    return pdfMicrofacetNormalSolidAngle / (4.0f * VdotH);
}

/// Evaluates the anisotropic GGX normal distribution.
/// `alpha` components must be finite and positive; `wmLocal` must be a finite
/// unit local normal. Returns finite non-negative density; non-finite
/// intermediate geometry returns zero.
inline float
GGX_D_Anisotropic(const Vec2f& alpha, const Vec3f& wmLocal)
{
    if (wmLocal[2] <= 0.0f) {
        return 0.0f;
    }

    // This vector form remains finite at grazing angles. The equivalent
    // tan(theta) form loses valid density when cos(theta)^4 approaches the
    // renderer's general geometric epsilon.
    const float scaledX = wmLocal[0] / alpha[0];
    const float scaledY = wmLocal[1] / alpha[1];
    const float sum =
        scaledX * scaledX + scaledY * scaledY + wmLocal[2] * wmLocal[2];
    const float denom = kPi * alpha[0] * alpha[1] * sum * sum;
    return (std::isfinite(denom) && denom > 0.0f) ? 1.0f / denom : 0.0f;
}

/// Evaluates anisotropic GGX's Smith lambda term.
/// `alpha` components must be finite and positive and `wLocal` a finite unit
/// direction. Returns non-negative lambda; an exactly grazing direction
/// returns infinity.
inline float
GGX_Lambda_Anisotropic(const Vec2f& alpha, const Vec3f& wLocal)
{
    const float absCosTheta = std::abs(wLocal[2]);
    if (absCosTheta == 0.0f) {
        return std::numeric_limits<float>::infinity();
    }
    const float stretchedLength = std::sqrt(
        alpha[0] * alpha[0] * wLocal[0] * wLocal[0] +
        alpha[1] * alpha[1] * wLocal[1] * wLocal[1] +
        wLocal[2] * wLocal[2]);
    return std::max(
        0.0f, (stretchedLength / absCosTheta - 1.0f) * 0.5f);
}

/// Evaluates anisotropic Smith G1 for finite positive `alpha` and a finite
/// unit local direction. Returns finite masking in [0,1]; cannot fail.
inline float
GGX_G1_Anisotropic(const Vec2f& alpha, const Vec3f& wLocal)
{
    return 1.0f / (1.0f + GGX_Lambda_Anisotropic(alpha, wLocal));
}

/// Evaluates anisotropic Smith G2 for finite positive `alpha` and finite unit
/// local incident/outgoing directions. Returns finite masking in [0,1].
inline float
GGX_G_Anisotropic(
    const Vec2f& alpha, const Vec3f& omegaOutLocal,
    const Vec3f& omegaInLocal)
{
    return 1.0f / (
        1.0f + GGX_Lambda_Anisotropic(alpha, omegaOutLocal) +
        GGX_Lambda_Anisotropic(alpha, omegaInLocal));
}

/// Samples an anisotropic GGX visible-normal distribution in local space.
/// `omegaOutLocal` must be a finite unit positive-hemisphere direction,
/// `alpha` finite and positive, and `u1`/`u2` finite in [0,1). Returns a finite
/// unit positive-hemisphere normal; degenerate stretching returns `(0,0,1)`.
inline Vec3f
SampleGGX_VNDF_Anisotropic(
    const Vec3f& omegaOutLocal, const Vec2f& alpha, float u1, float u2)
{
    Vec3f wh(
        alpha[0] * omegaOutLocal[0], alpha[1] * omegaOutLocal[1],
        omegaOutLocal[2]);
    const float whLen = wh.length();
    if (whLen < kEpsilon) {
        return Vec3f(0.0f, 0.0f, 1.0f);
    }
    wh /= whLen;
    if (wh[2] < 0.0f) {
        wh = -wh;
    }

    const Vec3f T1 = (wh[2] < 0.99999f)
        ? Cross(Vec3f(0.0f, 0.0f, 1.0f), wh).normalized()
        : Vec3f(1.0f, 0.0f, 0.0f);
    const Vec3f T2 = Cross(wh, T1);

    const float r = std::sqrt(u1);
    const float phi = 2.0f * kPi * u2;
    float p1 = r * std::cos(phi);
    float p2 = r * std::sin(phi);

    const float h = std::sqrt(std::max(0.0f, 1.0f - p1 * p1));
    const float blend = (1.0f + wh[2]) * 0.5f;
    p2 = (1.0f - blend) * h + blend * p2;

    const float pz = std::sqrt(std::max(0.0f, 1.0f - p1 * p1 - p2 * p2));
    const Vec3f nh = T1 * p1 + T2 * p2 + wh * pz;

    Vec3f wm(
        alpha[0] * nh[0], alpha[1] * nh[1], std::max(1.0e-6f, nh[2]));
    wm.normalize();
    return wm;
}

/// Samples the bounded anisotropic GGX visible-normal distribution optimized
/// for reflection (Eto and Tokuyoshi, listings 1 and 2). `omegaOutLocal` must
/// be a finite unit positive-hemisphere direction; `alpha` must be finite and
/// positive; `u1`/`u2` must be finite in [0,1). Returns a finite unit normal.
inline Vec3f
SampleGGXBoundedVNDF_Anisotropic(
    const Vec3f& omegaOutLocal, const Vec2f& alpha, float u1, float u2)
{
    Vec3f omegaOutStandard(
        omegaOutLocal[0] * alpha[0],
        omegaOutLocal[1] * alpha[1],
        omegaOutLocal[2]);
    omegaOutStandard.normalize();

    const float phi = 2.0f * kPi * u1;
    const float minAlpha = std::clamp(
        std::min(alpha[0], alpha[1]), 0.0f, 1.0f);
    const float s = 1.0f + std::sqrt(
        omegaOutLocal[0] * omegaOutLocal[0] +
        omegaOutLocal[1] * omegaOutLocal[1]);
    const float alphaSquared = minAlpha * minAlpha;
    const float sSquared = s * s;
    const float k = (1.0f - alphaSquared) * sSquared /
        (sSquared + alphaSquared *
            omegaOutLocal[2] * omegaOutLocal[2]);
    const float b = k * omegaOutStandard[2];
    const float z = (1.0f - u2) * (1.0f + b) - b;
    const float sinTheta = std::sqrt(std::clamp(
        1.0f - z * z, 0.0f, 1.0f));
    const Vec3f omegaInStandard(
        sinTheta * std::cos(phi), sinTheta * std::sin(phi), z);
    const Vec3f wmStandard = omegaOutStandard + omegaInStandard;
    Vec3f wmLocal(
        wmStandard[0] * alpha[0],
        wmStandard[1] * alpha[1],
        wmStandard[2]);
    wmLocal.normalize();
    return wmLocal;
}

/// Returns the bounded-reflection VNDF density divided by the GGX NDF at
/// `wmLocal`. Inputs satisfy `SampleGGXBoundedVNDF_Anisotropic`. Returns a
/// finite non-negative ratio; cannot fail.
inline float
GGXBoundedVNDF_DRatio_Anisotropic(
    const Vec3f& omegaOutLocal, const Vec3f& wmLocal, const Vec2f& alpha)
{
    const Vec2f stretchedOut(
        omegaOutLocal[0] * alpha[0],
        omegaOutLocal[1] * alpha[1]);
    const float stretchedLength = std::sqrt(
        Dot(stretchedOut, stretchedOut) +
        omegaOutLocal[2] * omegaOutLocal[2]);
    const float minAlpha = std::clamp(
        std::min(alpha[0], alpha[1]), 0.0f, 1.0f);
    const float s = 1.0f + std::sqrt(
        omegaOutLocal[0] * omegaOutLocal[0] +
        omegaOutLocal[1] * omegaOutLocal[1]);
    const float alphaSquared = minAlpha * minAlpha;
    const float sSquared = s * s;
    const float k = (1.0f - alphaSquared) * sSquared /
        (sSquared + alphaSquared *
            omegaOutLocal[2] * omegaOutLocal[2]);
    const float denom = k * omegaOutLocal[2] + stretchedLength;
    if (denom <= kEpsilon || wmLocal[2] <= 0.0f) {
        return 0.0f;
    }
    return 2.0f * omegaOutLocal[2] / denom;
}

/// Returns whether `wmLocal` lies in the spherical-cap support sampled by
/// `SampleGGXBoundedVNDF_Anisotropic`. Inputs satisfy that function's
/// direction and alpha invariants. Cannot fail.
inline bool
GGXBoundedVNDFContainsNormal_Anisotropic(
    const Vec3f& omegaOutLocal, const Vec3f& wmLocal, const Vec2f& alpha)
{
    Vec3f omegaOutStandard(
        omegaOutLocal[0] * alpha[0],
        omegaOutLocal[1] * alpha[1],
        omegaOutLocal[2]);
    omegaOutStandard.normalize();
    Vec3f wmStandard(
        wmLocal[0] / alpha[0],
        wmLocal[1] / alpha[1],
        wmLocal[2]);
    wmStandard.normalize();
    const Vec3f omegaInStandard =
        2.0f * Dot(omegaOutStandard, wmStandard) * wmStandard -
        omegaOutStandard;

    const float minAlpha = std::clamp(
        std::min(alpha[0], alpha[1]), 0.0f, 1.0f);
    const float s = 1.0f + std::sqrt(
        omegaOutLocal[0] * omegaOutLocal[0] +
        omegaOutLocal[1] * omegaOutLocal[1]);
    const float alphaSquared = minAlpha * minAlpha;
    const float sSquared = s * s;
    const float k = (1.0f - alphaSquared) * sSquared /
        (sSquared + alphaSquared *
            omegaOutLocal[2] * omegaOutLocal[2]);
    return omegaInStandard[2] >= -k * omegaOutStandard[2];
}

/// Evaluates the anisotropic visible-normal solid-angle density.
/// `omegaOutLocal` and `wmLocal` must be finite unit vectors and `alpha`
/// finite and positive. Returns a finite non-negative density; a grazing
/// outgoing direction returns zero.
inline float
PdfGGX_VNDF_Anisotropic(
    const Vec3f& omegaOutLocal, const Vec3f& wmLocal, const Vec2f& alpha)
{
    const float cosThetaO = AbsCosTheta(omegaOutLocal);
    if (cosThetaO <= kEpsilon) {
        return 0.0f;
    }

    const float G1 = GGX_G1_Anisotropic(alpha, omegaOutLocal);
    const float D = GGX_D_Anisotropic(alpha, wmLocal);
    const float VdotH =
        std::max(std::abs(Dot(omegaOutLocal, wmLocal)), kEpsilon);
    return G1 * D * VdotH / cosThetaO;
}

/// Evaluates anisotropic GGX reflection.
/// Roughness components and `weight` are finite in [0,1]; `tangent` is finite;
/// normal and directions are finite unit vectors on the reflection hemisphere;
/// `fresnel` is finite and non-negative. Returns finite non-negative RGB, or
/// zero for invalid geometry. Does not throw.
Vec3f EvalMicrofacetReflectionAnisotropic(
    const Vec2f& roughness, const Vec3f& tangent, const Vec3f& fresnel,
    float weight, const Vec3f& normalShdWldOut, const Vec3f& omegaInWld,
    const Vec3f& omegaOutWld, bool compensateMissingEnergy = true);

/// Evaluates isotropic GGX reflection.
/// `alpha` is finite and positive, `fresnel` finite and non-negative, `weight`
/// finite in [0,1], and the normal/directions finite unit vectors on the
/// reflection hemisphere. Returns finite non-negative RGB, or zero for invalid
/// geometry. Does not throw.
Vec3f EvalMicrofacetReflectionIsotropic(
    float alpha, const Vec3f& fresnel, float weight,
    const Vec3f& normalShdWldOut, const Vec3f& omegaInWld,
    const Vec3f& omegaOutWld, bool compensateMissingEnergy = true);

/// Evaluates the anisotropic GGX reflection PDF.
/// Roughness is finite and positive, `tangent` finite, and the normal and
/// directions finite unit vectors. Returns a finite non-negative solid-angle
/// density; opposite hemispheres or degenerate half vectors return zero.
float PdfGGXSpecularAnisotropic(
    const Vec2f& roughness, const Vec3f& tangent,
    const Vec3f& normalShdWldOut, const Vec3f& omegaInWld,
    const Vec3f& omegaOutWld);

/// Samples anisotropic GGX reflection.
/// Roughness is finite and positive, `tangent` finite, normal/outgoing
/// direction finite unit vectors, and `u1`/`u2` finite in [0,1). Returns a
/// valid unit direction and non-negative PDF, or `pdfSolidAngle == 0` when the
/// sampled direction leaves the reflection hemisphere. Callers must test the
/// PDF before division. Does not throw.
Bsdf::BsdfSample SampleGGXSpecularAnisotropic(
    const Vec2f& roughness, const Vec3f& tangent,
    const Vec3f& normalShdWldOut, const Vec3f& omegaOutWld, float u1,
    float u2);

/// Converts finite perceptual roughness to alpha after clamping to
/// [`kMinMicrofacetAlpha`,1]. Returns finite positive alpha; cannot fail.
inline float
PerceptualRoughnessToAlpha(float roughness)
{
    const float clamped =
        std::clamp(roughness, kMinMicrofacetAlpha, 1.0f);
    return clamped * clamped;
}

/// Returns whether finite perceptual roughness maps below the renderer's
/// discrete-lobe alpha threshold. Cannot fail.
inline bool
IsEffectivelySmoothPerceptualRoughness(float roughness)
{
    return PerceptualRoughnessToAlpha(roughness) <
        kEffectivelySmoothMicrofacetAlpha;
}

}  // namespace detail
}  // namespace Bsdf
}  // namespace mxcpp

#endif  // PXR_IMAGING_PLUGIN_HDEMBREE_MATERIALXCPP_BSDF_MICROFACET_H
