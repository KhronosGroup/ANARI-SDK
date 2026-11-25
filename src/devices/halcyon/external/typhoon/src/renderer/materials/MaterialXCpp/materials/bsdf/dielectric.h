//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef PXR_IMAGING_PLUGIN_HDEMBREE_MATERIALXCPP_BSDF_DIELECTRIC_H
#define PXR_IMAGING_PLUGIN_HDEMBREE_MATERIALXCPP_BSDF_DIELECTRIC_H

#include "energyCompensation.h"
#include "fresnel.h"
#include "mathPrimitives.h"
#include "microfacet.h"
#include "shadingFrame.h"

#include <renderer/materials/MaterialXCpp/materials/bsdf.h>
#include <renderer/materials/MaterialXCpp/nodes/helpers/colorHelpers.h>
#include <renderer/materials/MaterialXCpp/nodes/helpers/mathHelpers.h>

#include <algorithm>
#include <cmath>

namespace mxcpp {
namespace Bsdf {
namespace detail {

/// Roughness thresholds for exact Fresnel, analytic/LUT blending, and LUT-only
/// directional transmission.
inline constexpr float kTransmissionExactFresnelMaxAlpha = 2.0e-3f;
inline constexpr float kTransmissionFresnelBlendMaxAlpha = 7.0e-2f;

/// Resolves the effective absolute IOR of a dielectric closure.
/// `data.ior` must be finite and positive. When dispersion is enabled,
/// `data.dispersionAbbe` and positive `heroWavelengthNm` must be finite.
/// Returns a finite IOR of at least 1; malformed dispersion falls back through
/// the spectral helper's bounded result. Does not throw.
float ResolveDielectricIor(
    const Bsdf::DielectricData& data, float heroWavelengthNm);

/// Resolves the effective absolute IOR of a coupled dielectric interface.
/// The same invariants and guarantees as the `DielectricData` overload apply.
float ResolveDielectricIor(
    const Bsdf::DielectricInterfaceData& data, float heroWavelengthNm);

/// Evaluates untinted reflection Fresnel for a separate dielectric lobe,
/// including optional thin film.
/// `cosTheta` is finite in [0,1], `effectiveIor` finite and at least 1, and
/// all optical fields in `data` finite with non-negative film thickness and
/// positive film IOR. Returns finite RGB reflectance in [0,1]. Cannot fail.
Vec3f DielectricReflectionFresnelUntinted(
    const Bsdf::DielectricData& data, float cosTheta, float effectiveIor);

/// Applies `data.tint` to untinted dielectric reflection.
/// Inputs satisfy `DielectricReflectionFresnelUntinted`; tint must be finite
/// and non-negative. Returns sanitized finite non-negative RGB reflectance.
inline Vec3f
DielectricReflectionFresnel(
    const Bsdf::DielectricData& data, float cosTheta, float effectiveIor)
{
    return CompMul(
        DielectricReflectionFresnelUntinted(data, cosTheta, effectiveIor),
        SafeVec(data.tint));
}

/// Evaluates untinted exact interface Fresnel, optional thin film, and the
/// thin-sheet double-interface correction.
/// `cosTheta` is finite in [0,1], `effectiveIor` finite and at least 1, and
/// all optical fields in `data` finite with valid film parameters. `backside`
/// selects incidence from inside a thick dielectric. Returns finite RGB
/// reflectance in [0,1]. Cannot fail.
Vec3f DielectricInterfaceReflectanceUntinted(
    const Bsdf::DielectricInterfaceData& data, float cosTheta,
    float effectiveIor, bool backside);

/// Returns the interface's tinted, weighted reflection coefficient.
/// Inputs satisfy `DielectricInterfaceReflectanceUntinted`; reflection tint
/// must be finite and non-negative and weight finite. Weight is clamped to
/// [0,1]. Returns finite non-negative RGB and cannot fail.
inline Vec3f
DielectricInterfaceReflectionCoefficient(
    const Bsdf::DielectricInterfaceData& data, float cosTheta,
    float effectiveIor, bool backside)
{
    return CompMul(
        DielectricInterfaceReflectanceUntinted(
            data, cosTheta, effectiveIor, backside),
        SafeVec(data.reflectionTint)) * Clamp01(data.reflectionWeight);
}

/// Returns the interface's tinted, weighted transmission coefficient.
/// Inputs satisfy `DielectricInterfaceReflectanceUntinted`; transmission tint
/// must be finite and non-negative and weight finite. Weight is clamped to
/// [0,1]. Returns finite non-negative RGB and cannot fail.
inline Vec3f
DielectricInterfaceTransmissionCoefficient(
    const Bsdf::DielectricInterfaceData& data, float cosTheta,
    float effectiveIor, bool backside)
{
    return CompMul(
        Vec3f(1.0f) -
            DielectricInterfaceReflectanceUntinted(
                data, cosTheta, effectiveIor, backside),
        SafeVec(data.transmissionTint)) * Clamp01(data.transmissionWeight);
}

/// Normalized probabilities for choosing interface reflection or transmission.
struct DielectricInterfaceSelection
{
    float reflection = 0.0f;
    float transmission = 0.0f;
};

/// Computes luminance-weighted interface selection probabilities.
/// Optical inputs satisfy the coefficient functions. `luminanceCoefficients`
/// must be finite, non-negative, and sum to one. Returns probabilities in
/// [0,1] summing to one, or both zero when neither side contributes.
inline DielectricInterfaceSelection
DielectricInterfaceSelectionProbabilities(
    const Bsdf::DielectricInterfaceData& data, float cosTheta,
    float effectiveIor, bool backside,
    const Vec3f& luminanceCoefficients = DefaultLuminanceCoefficients())
{
    const float reflectionWeight = std::max(
        Luminance(
            DielectricInterfaceReflectionCoefficient(
                data, cosTheta, effectiveIor, backside),
            luminanceCoefficients),
        0.0f);
    const float transmissionWeight = std::max(
        Luminance(
            DielectricInterfaceTransmissionCoefficient(
                data, cosTheta, effectiveIor, backside),
            luminanceCoefficients),
        0.0f);
    const float total = reflectionWeight + transmissionWeight;
    if (total <= 0.0f) {
        return {};
    }
    return {reflectionWeight / total, transmissionWeight / total};
}

/// Returns whether `data` uses coupled rough reflection/transmission sampling.
/// Weights and roughness must be finite. The result is false for thin-walled,
/// single-sided, disabled, or effectively delta interfaces. Cannot fail.
inline bool
UsesCoupledRoughDielectricSampling(
    const Bsdf::DielectricInterfaceData& data)
{
    return data.compensateCoupledDielectric &&
        !data.thinWalled &&
        data.reflectionWeight > 0.0f &&
        data.transmissionWeight > 0.0f &&
        !IsEffectivelyDeltaAlpha(data.roughness);
}

/// Finds the sole coupled transmission interface eligible for exact straight
/// shadows in `closure`.
/// Closure-tree child IDs must be valid. Returns null when no eligible
/// interface exists, when more than one contributes, or when there is no tree.
/// The returned pointer aliases `closure` and remains valid only while its tree
/// is unchanged. Does not throw.
const Bsdf::DielectricInterfaceData*
FindCoupledTransmissionInterfaceForStraightShadow(
    const SurfaceClosure& closure);

/// Computes multiple-scattering compensation for a coupled interface.
/// `effectiveIor` is finite and at least 1; normals and outgoing direction are
/// finite unit vectors; `backside` selects the inside-to-outside interface
/// order; `data` optical fields are finite. Returns zero when coupled sampling
/// is disabled, otherwise bounded compensation. Cannot fail.
float GetCoupledDielectricCompensation(
    const Bsdf::DielectricInterfaceData& data, float effectiveIor,
    bool backside, const Vec3f& normalShdLobeWldOut,
    const Vec3f& omegaOutWld);

/// Evaluates coupled rough dielectric transmission.
/// `effectiveIor` is finite and at least 1; all normals and directions are
/// finite unit vectors; `backside` selects the inside-to-outside interface
/// order; `data` optical fields and positive roughness are finite. Returns
/// finite non-negative RGB, or zero for invalid geometry.
Vec3f EvalCoupledRoughDielectricTransmission(
    const Bsdf::DielectricInterfaceData& data, float effectiveIor,
    bool backside, const Vec3f& normalShdLobeWldOut,
    const Vec3f& omegaInWld, const Vec3f& omegaOutWld);

/// Evaluates the coupled rough dielectric mixture PDF.
/// Inputs and `backside` satisfy `EvalCoupledRoughDielectricTransmission`.
/// Returns a finite non-negative solid-angle density; invalid geometry returns
/// zero.
float PdfCoupledRoughDielectric(
    const Bsdf::DielectricInterfaceData& data, float effectiveIor,
    bool backside, const Vec3f& normalShdLobeWldOut,
    const Vec3f& omegaInWld, const Vec3f& omegaOutWld);

/// Returns whether a coupled rough-dielectric direction pair lies within the
/// bounded reflection-VNDF sampler's support. `data`, normal, and directions
/// satisfy `PdfCoupledRoughDielectric`. Cannot fail.
bool CoupledRoughDielectricDirectionInSampleSupport(
    const Bsdf::DielectricInterfaceData& data,
    const Vec3f& normalShdLobeWldOut,
    const Vec3f& omegaInWld,
    const Vec3f& omegaOutWld);

/// Samples coupled rough dielectric reflection, transmission, or compensation.
/// Optical/geometric inputs satisfy the coupled evaluator; `u1`, `u2`, and
/// `uChoice` are finite in [0,1); `backside` selects the inside-to-outside
/// interface order. Returns a unit incident direction when valid. Failure or
/// invalid geometry returns `pdfSolidAngle == 0`; callers must test the PDF
/// before division. Does not throw.
Bsdf::BsdfSample SampleCoupledRoughDielectric(
    const Bsdf::DielectricInterfaceData& data, float effectiveIor,
    bool backside, const Vec3f& normalShdLobeWldOut,
    const Vec3f& omegaOutWld, float u1, float u2, float uChoice);

/// Samples perfect reflection when refraction is forbidden by TIR.
/// `weight` must be finite and non-negative; `normalShdWldOut` and
/// `omegaOutWld` must be finite unit vectors. Returns a unit specular
/// direction with `pdfSolidAngle == 1` and `eta == 1`. Cannot fail.
inline Bsdf::BsdfSample
SampleDeltaTotalInternalReflection(
    float weight, const Vec3f& normalShdWldOut, const Vec3f& omegaOutWld)
{
    const Vec3f n = Dot(normalShdWldOut, omegaOutWld) >= 0.0f
        ? normalShdWldOut
        : -normalShdWldOut;
    Vec3f omegaInWld = 2.0f * Dot(n, omegaOutWld) * n - omegaOutWld;
    omegaInWld.normalize();
    Bsdf::BsdfSample sample{omegaInWld, Vec3f(weight), 1.0f, true};
    sample.eta = 1.0f;
    return sample;
}

/// Returns whether transmission through an interface would undergo TIR.
/// `ior` must be finite and positive; the normal and outgoing direction must
/// be finite unit vectors; `backside` selects inside-to-outside refraction.
/// Cannot fail for valid inputs.
inline bool
WouldTotalInternalReflect(
    float ior, const Vec3f& normalShdWldOut, const Vec3f& omegaOutWld,
    bool backside)
{
    const float cosI = std::abs(Dot(normalShdWldOut, omegaOutWld));
    const float eta = backside
        ? std::max(ior, kEpsilon)
        : 1.0f / std::max(ior, kEpsilon);
    return eta * eta * (1.0f - cosI * cosI) >= 1.0f;
}

/// Samples ideal dielectric transmission, reflecting under TIR.
/// `ior` must be finite and positive, `tint` and `weight` finite and
/// non-negative, and the incident-facing normal/outgoing direction finite unit
/// vectors. `backside` selects inside-to-outside refraction. Returns a unit
/// direction, `pdfSolidAngle == 1`, and a finite non-negative BSDF value. TIR
/// returns a reflection sample. Cannot fail for valid inputs.
Bsdf::BsdfSample SampleDeltaTransmission(
    float ior, const Vec3f& tint, float weight,
    const Vec3f& normalShdWldOut, const Vec3f& omegaOutWld, bool backside);

/// Samples ideal reflection for a separate dielectric lobe.
/// `data` satisfies the reflection Fresnel invariants; `effectiveIor` is finite
/// and at least 1; the normal/outgoing direction are finite unit vectors.
/// Returns a unit specular sample with `pdfSolidAngle == 1`.
inline Bsdf::BsdfSample
SampleDeltaDielectricReflection(
    const Bsdf::DielectricData& data, float effectiveIor,
    const Vec3f& normalShdLobeWldOut, const Vec3f& omegaOutWld)
{
    const float cosTheta =
        std::max(std::abs(Dot(normalShdLobeWldOut, omegaOutWld)), kEpsilon);
    return SampleDeltaReflection(
        DielectricReflectionFresnel(data, cosTheta, effectiveIor), data.weight,
        normalShdLobeWldOut, omegaOutWld);
}

/// Samples ideal transmission for a separate dielectric lobe.
/// `effectiveIor` is finite and at least 1, `fresnelCos` finite in [0,1],
/// normal/outgoing direction finite unit vectors, and luminance coefficients
/// finite, non-negative, and normalized. `backside` selects inside-to-outside
/// refraction. Returns a finite unit specular sample; TIR carries only energy
/// not already assigned to the paired reflection lobe.
Bsdf::BsdfSample SampleDeltaDielectricTransmission(
    const Bsdf::DielectricData& data, float effectiveIor, float fresnelCos,
    const Vec3f& normalShdLobeWldOut,
    const Vec3f& omegaOutWld, bool backside,
    const Vec3f& luminanceCoefficients = DefaultLuminanceCoefficients());

/// Samples ideal reflection from a coupled dielectric interface.
/// Optical inputs satisfy the interface coefficient invariants; normal and
/// outgoing direction are finite unit vectors and `backside` identifies the
/// active side. Returns a unit specular sample with `pdfSolidAngle == 1`.
inline Bsdf::BsdfSample
SampleDeltaDielectricInterfaceReflection(
    const Bsdf::DielectricInterfaceData& data, float effectiveIor,
    const Vec3f& normalShdLobeWldOut, const Vec3f& omegaOutWld, bool backside)
{
    const float cosTheta =
        std::max(std::abs(Dot(normalShdLobeWldOut, omegaOutWld)), kEpsilon);
    return SampleDeltaReflection(
        DielectricInterfaceReflectionCoefficient(
            data, cosTheta, effectiveIor, backside),
        1.0f, normalShdLobeWldOut, omegaOutWld);
}

/// Samples ideal transmission from a coupled dielectric interface.
/// `effectiveIor` is finite and at least 1, `fresnelCos` finite in [0,1], and
/// normal/outgoing direction finite unit vectors. `backside` selects
/// inside-to-outside refraction. Thin-walled interfaces pass straight through
/// with `eta == 1`; thick interfaces refract or reflect under TIR. Returns a
/// finite unit specular sample. Does not throw.
Bsdf::BsdfSample SampleDeltaDielectricInterfaceTransmission(
    const Bsdf::DielectricInterfaceData& data, float effectiveIor,
    float fresnelCos, const Vec3f& normalShdLobeWldOut,
    const Vec3f& omegaOutWld, bool backside);

}  // namespace detail
}  // namespace Bsdf
}  // namespace mxcpp

#endif  // PXR_IMAGING_PLUGIN_HDEMBREE_MATERIALXCPP_BSDF_DIELECTRIC_H
