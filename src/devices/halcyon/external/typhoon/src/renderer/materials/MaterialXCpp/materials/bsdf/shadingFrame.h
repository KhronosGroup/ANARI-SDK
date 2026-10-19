//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef PXR_IMAGING_PLUGIN_HDEMBREE_MATERIALXCPP_BSDF_SHADINGFRAME_H
#define PXR_IMAGING_PLUGIN_HDEMBREE_MATERIALXCPP_BSDF_SHADINGFRAME_H

#include "mathPrimitives.h"

#include <renderer/materials/MaterialXCpp/materials/bsdf.h>
#include <renderer/materials/MaterialXCpp/nodes/helpers/colorHelpers.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace mxcpp {
namespace Bsdf {
namespace detail {

/// Reflects finite vector `v` across a finite unit `normal`.
/// Preserves vector length within floating-point error and cannot fail.
inline Vec3f
MirrorAcrossSurface(const Vec3f& v, const Vec3f& normal)
{
    return v - 2.0f * Dot(v, normal) * normal;
}

/// Right-handed orthonormal shading frame in world space.
struct Frame {
    Vec3f tangentWld;
    Vec3f bitangentWld;
    Vec3f normalShdWldOut;

    /// Projects finite `directionWld` into this orthonormal frame.
    /// Returns signed local coordinates and cannot fail.
    Vec3f
    ToLocal(const Vec3f& directionWld) const
    {
        return Vec3f(Dot(directionWld, tangentWld),
                     Dot(directionWld, bitangentWld),
                     Dot(directionWld, normalShdWldOut));
    }

    /// Reconstructs a world-space vector from finite local coordinates.
    /// Returns a vector with the same length when this frame is orthonormal;
    /// cannot fail.
    Vec3f
    ToWorld(const Vec3f& directionLocal) const
    {
        return tangentWld * directionLocal[0] +
               bitangentWld * directionLocal[1] +
               normalShdWldOut * directionLocal[2];
    }

    /// Builds a stable orthonormal frame around finite unit
    /// `normalShdWldOut`. Returns a right-handed frame and cannot fail.
    static Frame
    FromNormal(const Vec3f& normalShdWldOut)
    {
        Frame frame;
        frame.normalShdWldOut = normalShdWldOut;
        const Vec3f helper = (std::abs(normalShdWldOut[0]) < 0.9f)
                                 ? Vec3f(1.0f, 0.0f, 0.0f)
                                 : Vec3f(0.0f, 1.0f, 0.0f);
        frame.tangentWld = normalShdWldOut.cross(helper).normalized();
        frame.bitangentWld = normalShdWldOut.cross(frame.tangentWld);
        return frame;
    }

    /// Builds a frame around finite unit `normalShdWldOut`, using finite
    /// `tangentWld` after projection. A zero or parallel tangent falls back to
    /// `FromNormal`. Returns a right-handed orthonormal frame; cannot fail.
    static Frame
    FromNormalAndTangent(const Vec3f& normalShdWldOut, const Vec3f& tangentWld)
    {
        const Vec3f tangentProjectedWld =
            tangentWld - normalShdWldOut * Dot(tangentWld, normalShdWldOut);
        if (tangentProjectedWld.length() < kEpsilon) {
            return FromNormal(normalShdWldOut);
        }

        Frame frame;
        frame.normalShdWldOut = normalShdWldOut;
        frame.tangentWld = tangentProjectedWld.normalized();
        frame.bitangentWld = Cross(frame.normalShdWldOut, frame.tangentWld);
        if (frame.bitangentWld.length() < kEpsilon) {
            return FromNormal(normalShdWldOut);
        }
        frame.bitangentWld.normalize();
        frame.tangentWld = Cross(frame.bitangentWld, frame.normalShdWldOut);
        return frame;
    }
};

/// Maps independent finite samples `u1` and `u2` from [0,1) to a unit
/// direction on the positive-Z cosine hemisphere. Cannot fail.
inline Vec3f
SampleCosineHemisphere(float u1, float u2)
{
    float cosTheta = std::sqrt(u1);
    float sinTheta = std::sqrt(1.0f - u1);
    float phi = 2.0f * kPi * u2;
    return Vec3f(sinTheta * std::cos(phi),
                 sinTheta * std::sin(phi),
                 cosTheta);
}

/// Returns the cosine-hemisphere solid-angle density for finite `cosTheta`.
/// Negative cosines return zero. The result is finite and non-negative;
/// cannot fail.
inline float
CosineHemispherePdf(float cosTheta)
{
    return std::max(cosTheta, 0.0f) * kInvPi;
}

/// Returns the absolute Z cosine of finite local direction `w`.
/// `w` is normally unit length. Cannot fail.
inline float
AbsCosTheta(const Vec3f& w)
{
    return std::abs(w[2]);
}

/// Returns tan(theta)^2 for finite unit local direction `w`.
/// A grazing direction whose squared cosine is at most `kEpsilon` returns
/// positive infinity; otherwise the result is finite and non-negative.
inline float
Tan2Theta(const Vec3f& w)
{
    const float cosTheta2 = w[2] * w[2];
    if (cosTheta2 <= kEpsilon) {
        return std::numeric_limits<float>::infinity();
    }
    return std::max(0.0f, 1.0f - cosTheta2) / cosTheta2;
}

/// Normalizes finite `v`, or returns finite `fallback` when its length is below
/// `kEpsilon`. Callers requiring a unit result must provide a unit fallback.
/// Cannot fail.
inline Vec3f
NormalizeOrFallback(const Vec3f& v, const Vec3f& fallback)
{
    const float length = v.length();
    if (length < kEpsilon) {
        return fallback;
    }
    return v / length;
}

/// Normalizes `candidate` and validates it against `normalShdWldExt`.
///
/// The candidate is accepted only when finite, non-degenerate, and in the
/// same open hemisphere. `outNormalShdLobeWldExt` must be non-null and is
/// written only on success. Does not throw.
inline bool
TryResolveShadingNormal(
    const Vec3f& candidate,
    const Vec3f& normalShdWldExt,
    Vec3f* outNormalShdLobeWldExt)
{
    if (!outNormalShdLobeWldExt) {
        return false;
    }

    const double maximumComponent = std::max({
        std::abs(static_cast<double>(candidate[0])),
        std::abs(static_cast<double>(candidate[1])),
        std::abs(static_cast<double>(candidate[2]))});
    if (!std::isfinite(maximumComponent) || maximumComponent == 0.0) {
        return false;
    }
    const double scaledX =
        static_cast<double>(candidate[0]) / maximumComponent;
    const double scaledY =
        static_cast<double>(candidate[1]) / maximumComponent;
    const double scaledZ =
        static_cast<double>(candidate[2]) / maximumComponent;
    const double length = std::sqrt(
        scaledX * scaledX + scaledY * scaledY + scaledZ * scaledZ);
    if (!std::isfinite(length) || length == 0.0) {
        return false;
    }
    const Vec3f normalized(
        static_cast<float>(scaledX / length),
        static_cast<float>(scaledY / length),
        static_cast<float>(scaledZ / length));
    if (Dot(normalized, normalShdWldExt) <= 0.0f) {
        return false;
    }
    *outNormalShdLobeWldExt = normalized;
    return true;
}

/// Resolves the exterior shading normal stored in `data`.
///
/// `DataT` must expose `hasShadingNormal` and `normal`.
/// `normalShdWldExt` must be a finite unit vector with authored exterior
/// orientation. An authored normal is accepted only when finite,
/// non-degenerate, and in the same open hemisphere; every invalid value falls
/// back to `normalShdWldExt` and is never negated. Returns a finite unit vector
/// and does not throw. This intentionally mirrors renderer-side
/// `ty::TryResolveNormalShdWldExt`; the Gf/mxcpp type boundary prevents sharing
/// the implementation directly.
template<typename DataT>
inline bool
TryResolveShadingNormal(
    const DataT& data,
    const Vec3f& normalShdWldExt,
    Vec3f* outNormalShdLobeWldExt)
{
    if (!outNormalShdLobeWldExt || !data.hasShadingNormal) {
        return false;
    }

    return TryResolveShadingNormal(
        data.normal, normalShdWldExt, outNormalShdLobeWldExt);
}

/// Returns the prepared incident-side normal for a closure leaf. `data.normal`
/// must have been validated before `PrepareShadingNormals` when
/// `data.hasShadingNormal`; an unprepared leaf without a normal inherits
/// finite unit `normalShdWldOut`. Returns a finite unit vector and cannot fail.
template<typename DataT>
inline Vec3f
ResolveShadingNormal(const DataT& data, const Vec3f& normalShdWldOut)
{
    // Preparation faces validated values and installs any reflection-safe
    // correction before traversal. A missing normal is retained only for
    // deliberately unprepared standalone leaf helpers.
    return data.hasShadingNormal ? data.normal : normalShdWldOut;
}

/// Returns whether finite unit directions `omegaInWld` and `omegaOutWld` lie
/// on the same open side of finite unit `normalShdWldOut`. Tangent directions
/// return false. Cannot fail.
inline bool
IsSameSide(const Vec3f& normalShdWldOut, const Vec3f& omegaInWld,
            const Vec3f& omegaOutWld)
{
    return Dot(normalShdWldOut, omegaInWld) *
               Dot(normalShdWldOut, omegaOutWld) >
           0.0f;
}

/// Applies inverse lobe-selection probability to an already weighted discrete
/// specular sample. `selectionProb` must be finite in [0,1]. Non-specular,
/// invalid, or zero-probability samples are returned unchanged. Cannot fail.
inline Bsdf::BsdfSample
ScaleDiscreteSpecularSample(
    Bsdf::BsdfSample sample,
    float selectionProb)
{
    if (sample.isSpecular && sample.pdfSolidAngle > 0.0f &&
        selectionProb > 0.0f) {
        sample.bsdfValue /= selectionProb;
    }
    return sample;
}

/// Samples perfect reflection about finite unit `normalShdLobeWldOut`.
/// `omegaOutWld` must be a finite unit vector; `reflectance` and `weight` must
/// be finite and non-negative. Returns a unit direction, sanitized BSDF value,
/// `pdfSolidAngle == 1`, `isSpecular == true`, and `eta == 1`. Cannot fail.
inline Bsdf::BsdfSample
SampleDeltaReflection(const Vec3f& reflectance, float weight,
                       const Vec3f& normalShdLobeWldOut,
                       const Vec3f& omegaOutWld)
{
    Vec3f omegaInWld =
        2.0f * Dot(normalShdLobeWldOut, omegaOutWld) * normalShdLobeWldOut -
        omegaOutWld;
    omegaInWld.normalize();

    Bsdf::BsdfSample sample{omegaInWld, SafeVec(reflectance * weight), 1.0f,
                            true};
    sample.eta = 1.0f;
    return sample;
}

}  // namespace detail
}  // namespace Bsdf
}  // namespace mxcpp

#endif  // PXR_IMAGING_PLUGIN_HDEMBREE_MATERIALXCPP_BSDF_SHADINGFRAME_H
