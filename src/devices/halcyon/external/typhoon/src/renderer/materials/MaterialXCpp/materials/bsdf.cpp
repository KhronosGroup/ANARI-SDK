//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "bsdf.h"
#include "adobeOpenPbr.h"

#include <renderer/materials/MaterialXCpp/materials/bsdf/closureTraversal.h>
#include <renderer/materials/MaterialXCpp/materials/bsdf/dielectric.h>
#include <renderer/materials/MaterialXCpp/materials/bsdf/diffuse.h>
#include <renderer/materials/MaterialXCpp/materials/bsdf/energyCompensation.h>
#include <renderer/materials/MaterialXCpp/materials/bsdf/fresnel.h>
#include <renderer/materials/MaterialXCpp/materials/bsdf/legacySurface.h>
#include <renderer/materials/MaterialXCpp/materials/bsdf/mathPrimitives.h>
#include <renderer/materials/MaterialXCpp/materials/bsdf/microfacet.h>
#include <renderer/materials/MaterialXCpp/materials/bsdf/reflectionOnlyInterfaces.h>
#include <renderer/materials/MaterialXCpp/materials/bsdf/shadingFrame.h>
#include <renderer/materials/MaterialXCpp/materials/bsdf/sheen.h>
#include <renderer/materials/MaterialXCpp/materials/bsdf/thinFilm.h>

#include <algorithm>
#include <cmath>

namespace mxcpp {

void
Bsdf::SetDielectricLayerThroughputMode(DielectricLayerThroughputMode mode)
{
    detail::SetDielectricThroughputModeState(mode);
}

Bsdf::DielectricLayerThroughputMode
Bsdf::GetDielectricLayerThroughputMode()
{
    return detail::GetDielectricThroughputModeState();
}

Vec3f
Bsdf::EvalLambertian(const Vec3f& baseColor, const Vec3f& normalShdWldOut,
                     const Vec3f& omegaInWld, const Vec3f& omegaOutWld)
{
    // Lambertian reflection: both omegaInWld and omegaOutWld must lie on the
    // front side of normalShdWldOut. Previously this function ignored
    // normalShdWldOut/omegaInWld/omegaOutWld entirely and returned baseColor/pi
    // unconditionally, causing back-face hits (e.g. viewing an opaque ground
    // from below, or secondary rays landing on the inside of a closed mesh) to
    // "leak" light through the surface. The SSS exit path post-flip was
    // especially vulnerable to this because the flipped ray routinely crosses
    // surfaces at grazing angles.
    if (Dot(normalShdWldOut, omegaInWld) <= 0.0f ||
        Dot(normalShdWldOut, omegaOutWld) <= 0.0f) {
        return Vec3f(0.0f);
    }
    return baseColor * kInvPi;
}

Vec3f
Bsdf::EvalGGXSpecular(float roughness, float /*ior*/,
                      const Vec3f& specularColor, const Vec3f& normalShdWldOut,
                      const Vec3f& omegaInWld, const Vec3f& omegaOutWld)
{
    float alpha = detail::RoughnessToAlpha(roughness);
    float NdotL = std::max(Dot(normalShdWldOut, omegaInWld), 0.0f);
    float NdotV = std::max(Dot(normalShdWldOut, omegaOutWld), detail::kEpsilon);
    if (NdotL <= 0.0f || NdotV <= 0.0f) {
        return Vec3f(0.0f);
    }
    Vec3f H = (omegaInWld + omegaOutWld).normalized();
    float NdotH = std::max(Dot(normalShdWldOut, H), 0.0f);
    float VdotH = std::max(Dot(omegaOutWld, H), 0.0f);
    float D = detail::GGX_D(alpha, NdotH);
    float G = detail::GGX_G(alpha, NdotV, NdotL);
    Vec3f F = detail::SchlickFresnel(specularColor, VdotH);
    Vec3f compensatedF = CompMul(
        F,
        detail::TurquinMicrofacetMsScale(alpha, NdotV, F));
    return detail::SafeVec(CompMul(
        compensatedF,
        Vec3f(D * G / std::max(4.0f * NdotL * NdotV, detail::kEpsilon))));
}

Vec3f
Bsdf::EvalGGXTransmission(float roughness, float ior,
                          const Vec3f& transmissionColor,
                          const Vec3f& normalShdWldOut, const Vec3f& omegaInWld,
                          const Vec3f& omegaOutWld, bool backside)
{
    float alpha = detail::RoughnessToAlpha(roughness);

    detail::Frame frame = detail::Frame::FromNormal(normalShdWldOut);
    Vec3f omegaOutLocal = frame.ToLocal(omegaOutWld);
    Vec3f omegaInLocal = frame.ToLocal(omegaInWld);

    float cosThetaO = std::abs(omegaOutLocal[2]);
    float cosThetaI = std::abs(omegaInLocal[2]);
    if (cosThetaO < detail::kEpsilon || cosThetaI < detail::kEpsilon) {
        return Vec3f(0.0f);
    }

    // The Walter/pbrt convention is eta_t / eta_i, the reciprocal of this
    // renderer's eta convention.
    float etaPbrt = backside ? (1.0f / ior) : ior;

    // Generalized half-vector for refraction (Walter et al. 2007)
    Vec3f wm = (omegaInLocal * etaPbrt + omegaOutLocal);
    if (wm.length() < detail::kEpsilon) {
        return Vec3f(0.0f);
    }
    wm.normalize();
    if (wm[2] < 0.0f) {
        wm = -wm;
    }

    // Reject if omegaInWld and omegaOutWld are on the same side of the
    // microfacet
    if (Dot(omegaInLocal, wm) * Dot(omegaOutLocal, wm) > 0.0f) {
        return Vec3f(0.0f);
    }

    float NdotH = std::abs(wm[2]);
    float VdotH = std::abs(Dot(omegaOutLocal, wm));
    float LdotH = std::abs(Dot(omegaInLocal, wm));

    float fresnel = detail::SchlickFresnelScalar(ior, VdotH);
    float T = 1.0f - fresnel;

    float D = detail::GGX_D(alpha, NdotH);
    float G = detail::SmithG1(alpha, cosThetaO) * detail::SmithG1(alpha, cosThetaI);

    // Jacobian denominator uses signed dot products (Walter et al.)
    float denom = Dot(omegaInLocal, wm) + Dot(omegaOutLocal, wm) / etaPbrt;
    denom *= denom;
    if (denom < detail::kEpsilon) {
        return Vec3f(0.0f);
    }

    float btdf = T * D * G * VdotH * LdotH /
        (cosThetaO * cosThetaI * denom);

    return detail::SafeVec(transmissionColor * std::abs(btdf));
}

Vec3f
Bsdf::EvalSheen(const Vec3f& sheenColor, float roughness,
                const Vec3f& normalShdWldOut, const Vec3f& omegaInWld,
                const Vec3f& omegaOutWld)
{
    float alpha = detail::ClampRoughness(roughness);
    float NdotL = std::max(Dot(normalShdWldOut, omegaInWld), 0.0f);
    float NdotV = std::max(Dot(normalShdWldOut, omegaOutWld), detail::kEpsilon);
    if (NdotL <= 0.0f || NdotV <= 0.0f) {
        return Vec3f(0.0f);
    }
    Vec3f H = (omegaInWld + omegaOutWld).normalized();
    float NdotH = std::max(Dot(normalShdWldOut, H), 0.0f);
    float D = detail::Charlie_D(alpha, NdotH);
    float V = detail::Ashikhmin_V(NdotV, NdotL);
    return detail::SafeVec(sheenColor * (D * V));
}

Vec3f
Bsdf::EvalCoat(float coatWeight, float coatRoughness, float coatIor,
               const Vec3f& normalShdWldOut, const Vec3f& omegaInWld,
               const Vec3f& omegaOutWld)
{
    if (coatWeight <= 0.0f) {
        return Vec3f(0.0f);
    }

    float alpha = detail::RoughnessToAlpha(coatRoughness);
    float NdotL = std::max(Dot(normalShdWldOut, omegaInWld), 0.0f);
    float NdotV = std::max(Dot(normalShdWldOut, omegaOutWld), detail::kEpsilon);
    if (NdotL <= 0.0f || NdotV <= 0.0f) {
        return Vec3f(0.0f);
    }
    Vec3f H = (omegaInWld + omegaOutWld).normalized();
    float NdotH = std::max(Dot(normalShdWldOut, H), 0.0f);
    float VdotH = std::max(Dot(omegaOutWld, H), 0.0f);
    float D = detail::GGX_D(alpha, NdotH);
    float G = detail::GGX_G(alpha, NdotV, NdotL);
    float F = detail::SchlickFresnelScalar(coatIor, VdotH);
    return Vec3f(
        coatWeight * D * G * F /
        std::max(4.0f * NdotL * NdotV, detail::kEpsilon));
}

Vec3f
Bsdf::EvalSurface(const SurfaceClosure& closure,
                  const SurfaceInteraction& interaction,
                  const Vec3f& omegaInWld)
{
    Vec3f bsdfValue =
        closure.HasBsdfTree()
            ? detail::EvalNode(closure.bsdfTree, closure.bsdfTree.root,
                               interaction, omegaInWld,
                               detail::BumpShadowingContext::Evaluation)
            : detail::EvalLegacySurface(
                  closure, interaction, omegaInWld,
                  detail::BumpShadowingContext::Evaluation);
    return detail::SafeVec(bsdfValue * closure.presence);
}

Vec3f
Bsdf::EvalSurfaceCosine(
    const SurfaceClosure& closure, const SurfaceInteraction& interaction,
    const Vec3f& omegaInWld)
{
    Vec3f bsdfValueCosine =
        closure.HasBsdfTree()
            ? detail::EvalNodeCosine(
                  closure.bsdfTree, closure.bsdfTree.root,
                  interaction, omegaInWld,
                  detail::BumpShadowingContext::Evaluation)
            : detail::EvalLegacySurface(
                  closure, interaction, omegaInWld,
                  detail::BumpShadowingContext::Evaluation) *
                  std::abs(Dot(interaction.normalShdWldOut, omegaInWld));
    return detail::SafeVec(bsdfValueCosine * closure.presence);
}

Bsdf::BsdfSample
Bsdf::SampleLambertian(const Vec3f& baseColor, const Vec3f& normalShdWldOut,
                       const Vec3f& /*omegaOutWld*/, float u1, float u2)
{
    detail::Frame frame = detail::Frame::FromNormal(normalShdWldOut);
    Vec3f omegaInLocal = detail::SampleCosineHemisphere(u1, u2);
    Vec3f omegaInWld = frame.ToWorld(omegaInLocal);
    float pdfSolidAngle = detail::CosineHemispherePdf(omegaInLocal[2]);
    BsdfSample sample{omegaInWld, baseColor * kInvPi, pdfSolidAngle, false};
    sample.isDiffuseLike = true;
    return sample;
}

float
Bsdf::PdfLambertian(const Vec3f& normalShdWldOut, const Vec3f& omegaInWld)
{
    return detail::CosineHemispherePdf(Dot(normalShdWldOut, omegaInWld));
}

Bsdf::BsdfSample
Bsdf::SampleGGXSpecular(float roughness, float /*ior*/,
                        const Vec3f& specularColor,
                        const Vec3f& normalShdWldOut, const Vec3f& omegaOutWld,
                        float u1, float u2)
{
    float alpha = detail::RoughnessToAlpha(roughness);

    detail::Frame frame = detail::Frame::FromNormal(normalShdWldOut);
    Vec3f omegaOutLocal = frame.ToLocal(omegaOutWld);
    if (omegaOutLocal[2] <= 0.0f) {
        return BsdfSample{Vec3f(0.0f), Vec3f(0.0f), 0.0f, false};
    }

    Vec3f wmLocal = detail::SampleGGX_VNDF(omegaOutLocal, alpha, u1, u2);
    const Vec3f omegaInLocal =
        2.0f * Dot(omegaOutLocal, wmLocal) * wmLocal - omegaOutLocal;
    if (omegaInLocal[2] <= 0.0f) {
        return BsdfSample{Vec3f(0.0f), Vec3f(0.0f), 0.0f, false};
    }

    const Vec3f omegaInWld = frame.ToWorld(omegaInLocal);
    const float NdotL = omegaInLocal[2];
    const float NdotV = omegaOutLocal[2];
    const float NdotH = wmLocal[2];
    const float VdotH = std::max(Dot(omegaOutLocal, wmLocal), 0.0f);
    const float D = detail::GGX_D(alpha, NdotH);
    const float G = detail::GGX_G(alpha, NdotV, NdotL);
    const Vec3f F = detail::SchlickFresnel(specularColor, VdotH);
    const Vec3f compensatedF = CompMul(
        F, detail::TurquinMicrofacetMsScale(alpha, NdotV, F));
    const Vec3f bsdfValue = CompMul(
        compensatedF,
        Vec3f(
            D * G /
            std::max(4.0f * NdotL * NdotV, detail::kEpsilon)));
    const float pdfSolidAngle =
        detail::PdfGGX_VNDF(omegaOutLocal, wmLocal, alpha);
    return BsdfSample{
        omegaInWld, detail::SafeVec(bsdfValue),
        std::max(pdfSolidAngle, 0.0f), false};
}

float
Bsdf::PdfGGXSpecular(float roughness, const Vec3f& normalShdWldOut,
                     const Vec3f& omegaInWld, const Vec3f& omegaOutWld)
{
    float alpha = detail::RoughnessToAlpha(roughness);
    detail::Frame frame = detail::Frame::FromNormal(normalShdWldOut);
    Vec3f omegaOutLocal = frame.ToLocal(omegaOutWld);
    Vec3f omegaInLocal = frame.ToLocal(omegaInWld);

    if (omegaOutLocal[2] <= 0.0f || omegaInLocal[2] <= 0.0f) {
        return 0.0f;
    }

    Vec3f wmLocal = (omegaOutLocal + omegaInLocal).normalized();
    if (wmLocal[2] <= 0.0f) {
        return 0.0f;
    }

    return detail::PdfGGX_VNDF(omegaOutLocal, wmLocal, alpha);
}

float
Bsdf::GgxDirectionalMissingEnergy(
    float cosTheta,
    float alphaRoughness)
{
    return detail::LookupGgxMissingEnergy(cosTheta, alphaRoughness);
}

float
Bsdf::GgxDirectionalSingleScatterEnergy(
    float cosTheta,
    float alphaRoughness)
{
    return 1.0f - detail::LookupGgxMissingEnergy(cosTheta, alphaRoughness);
}

float
Bsdf::CoupledRoughDielectricDirectionalTransmissionAlbedo(
    float cosTheta,
    const Vec2f& roughness,
    float ior,
    bool backfacing,
    bool compensateMultipleScattering)
{
    const float alpha = std::clamp(
        std::max(roughness[0], roughness[1]), 0.0f, 1.0f);
    const float safeIor = std::clamp(
        ior,
        detail::kBsdlDielectricIorMin,
        detail::kBsdlDielectricIorMax);
    const float relativeEta = backfacing ? 1.0f / safeIor : safeIor;
    const float exactAlbedo = 1.0f - detail::MaterialXDielectricFresnel(
        cosTheta, relativeEta);
    if (alpha <= detail::kTransmissionExactFresnelMaxAlpha) {
        return exactAlbedo;
    }

    const float perceptualRoughness =
        detail::BsdlLayerRoughnessFromAlpha(roughness);
    float albedo = detail::LookupBsdlDielectricTransmissionSingleScatterAlbedo(
        cosTheta, perceptualRoughness, ior, backfacing);
    if (compensateMultipleScattering) {
        const float missingEnergy = detail::BsdlCoupledDielectricCompensation(
            cosTheta, perceptualRoughness, ior, backfacing);
        albedo /= std::max(0.01f, 1.0f - missingEnergy);
    }
    if (alpha < detail::kTransmissionFresnelBlendMaxAlpha) {
        float t = (alpha - detail::kTransmissionExactFresnelMaxAlpha) /
            (detail::kTransmissionFresnelBlendMaxAlpha -
             detail::kTransmissionExactFresnelMaxAlpha);
        t = t * t * (3.0f - 2.0f * t);
        albedo = exactAlbedo * (1.0f - t) + albedo * t;
    }
    return Clamp01(albedo);
}

float
Bsdf::StraightShadowDielectricTransmission(
    const SurfaceClosure& closure,
    float signedCosTheta)
{
    const float cosTheta = Clamp01(std::abs(signedCosTheta));
    const float safeIor = std::max(closure.specularIor, 1.0f);
    const auto* interface =
        detail::FindCoupledTransmissionInterfaceForStraightShadow(closure);
    if (!interface) {
        return Clamp01(
            1.0f - detail::SchlickFresnelScalar(safeIor, cosTheta));
    }
    return CoupledRoughDielectricDirectionalTransmissionAlbedo(
        cosTheta,
        interface->roughness,
        interface->ior,
        signedCosTheta > 0.0f,
        interface->compensateCoupledDielectric);
}

Bsdf::BsdfSample
Bsdf::SampleGGXTransmission(float roughness, float ior,
                            const Vec3f& transmissionColor,
                            const Vec3f& normalShdWldOut,
                            const Vec3f& omegaOutWld, float u1, float u2,
                            bool backside)
{
    float alpha = detail::RoughnessToAlpha(roughness);

    detail::Frame frame = detail::Frame::FromNormal(normalShdWldOut);
    Vec3f omegaOutLocal = frame.ToLocal(omegaOutWld);
    if (std::abs(omegaOutLocal[2]) < detail::kEpsilon) {
        return BsdfSample{Vec3f(0.0f), Vec3f(0.0f), 0.0f, false};
    }

    // Flip to upper hemisphere for VNDF sampling
    bool flipN = omegaOutLocal[2] < 0.0f;
    Vec3f omegaOutFlippedWld = flipN ? -omegaOutLocal : omegaOutLocal;

    Vec3f wmLocal = detail::SampleGGX_VNDF(omegaOutFlippedWld, alpha, u1, u2);
    if (flipN) {
        wmLocal = -wmLocal;
    }

    // Determine eta (incident / transmitted) from the geometric interface
    // side rather than the potentially perturbed shading normal.
    float eta = backside ? ior : (1.0f / ior);

    // Refract through the microfacet
    float cosI = Dot(omegaOutLocal, wmLocal);
    float sin2T = eta * eta * (1.0f - cosI * cosI);
    if (sin2T >= 1.0f) {
        // Total internal reflection
        return BsdfSample{Vec3f(0.0f), Vec3f(0.0f), 0.0f, false};
    }

    float cosT = std::sqrt(1.0f - sin2T);
    // Ensure cosT has opposite sign from cosI for transmission
    if (cosI > 0.0f) {
        cosT = -cosT;
    }
    Vec3f omegaInLocal = -eta * omegaOutLocal + (eta * cosI + cosT) * wmLocal;
    omegaInLocal.normalize();

    // Reject if both directions are on the same side
    if (omegaInLocal[2] * omegaOutLocal[2] > 0.0f) {
        return BsdfSample{Vec3f(0.0f), Vec3f(0.0f), 0.0f, false};
    }

    Vec3f omegaInWld = frame.ToWorld(omegaInLocal);

    // Evaluate BTDF and PDF
    Vec3f bsdfValue =
        EvalGGXTransmission(roughness, ior, transmissionColor, normalShdWldOut,
                            omegaInWld, omegaOutWld, backside);
    float pdfSolidAngle = PdfGGXTransmission(roughness, ior, normalShdWldOut,
                                             omegaInWld, omegaOutWld,
                                             backside);

    if (pdfSolidAngle < detail::kEpsilon) {
        return BsdfSample{Vec3f(0.0f), Vec3f(0.0f), 0.0f, false};
    }

    BsdfSample sample{omegaInWld, detail::SafeVec(bsdfValue), pdfSolidAngle, false};
    sample.isTransmission = true;
    sample.eta = eta;
    return sample;
}

float
Bsdf::PdfGGXTransmission(float roughness, float ior,
                         const Vec3f& normalShdWldOut, const Vec3f& omegaInWld,
                         const Vec3f& omegaOutWld, bool backside)
{
    float alpha = detail::RoughnessToAlpha(roughness);

    detail::Frame frame = detail::Frame::FromNormal(normalShdWldOut);
    Vec3f omegaOutLocal = frame.ToLocal(omegaOutWld);
    Vec3f omegaInLocal = frame.ToLocal(omegaInWld);

    // Transmission requires omegaInWld and omegaOutWld on opposite sides
    if (omegaInLocal[2] * omegaOutLocal[2] > 0.0f) {
        return 0.0f;
    }

    float cosThetaO = std::abs(omegaOutLocal[2]);
    if (cosThetaO < detail::kEpsilon) {
        return 0.0f;
    }

    // The Walter/pbrt convention is eta_t / eta_i, the reciprocal of this
    // renderer's eta convention.
    float etaPbrt = backside ? (1.0f / ior) : ior;

    // Generalized half-vector
    Vec3f wm = (omegaInLocal * etaPbrt + omegaOutLocal);
    if (wm.length() < detail::kEpsilon) {
        return 0.0f;
    }
    wm.normalize();
    if (wm[2] < 0.0f) {
        wm = -wm;
    }

    float VdotH = std::abs(Dot(omegaOutLocal, wm));
    float LdotH = std::abs(Dot(omegaInLocal, wm));

    // VNDF PDF for the half-vector
    float G1o = detail::SmithG1(alpha, cosThetaO);
    float NdotH = std::abs(wm[2]);
    float D = detail::GGX_D(alpha, NdotH);
    float pdfMicrofacetNormalSolidAngle = G1o * D * VdotH / cosThetaO;

    // Jacobian for transmission half-vector change of variables
    // Uses signed dot products (Walter et al. 2007)
    float denom = Dot(omegaInLocal, wm) + Dot(omegaOutLocal, wm) / etaPbrt;
    denom *= denom;
    if (denom < detail::kEpsilon) {
        return 0.0f;
    }
    float dwm_dwi = LdotH / denom;

    return std::max(pdfMicrofacetNormalSolidAngle * dwm_dwi, 0.0f);
}

Bsdf::BsdfSample
Bsdf::SampleSurface(const SurfaceClosure& closure,
                    const SurfaceInteraction& interaction,
                    float u1, float u2, float uLobe)
{
    if (closure.HasBsdfTree()) {
        auto sample =
            detail::SampleNode(closure.bsdfTree, closure.bsdfTree.root,
                               interaction, u1, u2, uLobe);
        if (!sample.isSpecular) {
            sample.bsdfValue *= closure.presence;
            sample.bsdfValueCosine *= closure.presence;
        }
        return sample;
    }
    return detail::SampleLegacySurface(
        closure, interaction, u1, u2, uLobe);
}

float
Bsdf::PdfSurface(const SurfaceClosure& closure,
                 const SurfaceInteraction& interaction,
                 const Vec3f& omegaInWld)
{
    if (closure.HasBsdfTree()) {
        return detail::PdfNode(closure.bsdfTree, closure.bsdfTree.root,
                               interaction, omegaInWld);
    }
    return detail::PdfLegacySurface(
        closure, interaction, omegaInWld);
}

SurfaceClosure
Bsdf::PruneCausticClassLobes(const SurfaceClosure& closure)
{
    SurfaceClosure pruned = closure;
    if (closure.HasBsdfTree()) {
        pruned.bsdfTree =
            detail::PruneCausticClassLobes(closure.bsdfTree);
        if (!pruned.HasBsdfTree()) {
            detail::ClearLegacyBsdfSummary(&pruned);
        }
        return pruned;
    }

    pruned.transmission = 0.0f;
    if (detail::IsEffectivelySmoothPerceptualRoughness(pruned.roughness)) {
        pruned.specular = 0.0f;
        pruned.specularColor = Vec3f(0.0f);
    }
    if (detail::IsEffectivelySmoothPerceptualRoughness(pruned.coatRoughness)) {
        pruned.coat = 0.0f;
    }
    return pruned;
}


bool
Bsdf::SampleSubsurfaceEntry(const SurfaceClosure& closure,
                            const Vec3f& normalShdLobeWldOut,
                            const Vec3f& normalGeomWldOut,
                            const Vec3f& omegaOutWld, float u1, float u2,
                            Vec3f& outDirEntryWld)
{
    // Clamp IOR >= 1 to avoid TIR at entry (matches Cycles).
    const float ior = std::max(closure.specularIor, 1.0f);
    const float eta = 1.0f / ior;  // outside -> inside

    const float cosNI = Dot(normalShdLobeWldOut, omegaOutWld);
    if (cosNI <= 0.0f) {
        return false;
    }

    // Smooth surface: deterministic Snell refraction about geometric normal.
    if (closure.roughness < detail::kSmoothRoughnessThreshold) {
        const float sin2T = eta * eta * (1.0f - cosNI * cosNI);
        if (sin2T >= 1.0f) {
            // Shouldn't happen with IOR >= 1 but guard anyway.
            outDirEntryWld = -omegaOutWld;
            return Dot(normalShdLobeWldOut, outDirEntryWld) < 0.0f &&
                Dot(normalGeomWldOut, outDirEntryWld) < 0.0f;
        }
        const float cosT = std::sqrt(std::max(0.0f, 1.0f - sin2T));
        outDirEntryWld = -eta * omegaOutWld +
            (eta * cosNI - cosT) * normalShdLobeWldOut;
        return Dot(normalShdLobeWldOut, outDirEntryWld) < 0.0f &&
            Dot(normalGeomWldOut, outDirEntryWld) < 0.0f;
    }

    // Rough surface: GGX VNDF samples microfacet normal H, then Snell about H.
    const detail::Frame frame =
        detail::Frame::FromNormal(normalShdLobeWldOut);
    const Vec3f omegaOutLocal = frame.ToLocal(omegaOutWld);
    const float alpha = detail::RoughnessToAlpha(closure.roughness);
    const Vec3f hLocal = detail::SampleGGX_VNDF(omegaOutLocal, alpha, u1, u2);
    const Vec3f H = frame.ToWorld(hLocal);

    const float cosHI = Dot(H, omegaOutWld);
    const float sin2T = eta * eta * (1.0f - cosHI * cosHI);
    if (sin2T >= 1.0f) {
        outDirEntryWld = -omegaOutWld;
        return Dot(normalShdLobeWldOut, outDirEntryWld) < 0.0f &&
            Dot(normalGeomWldOut, outDirEntryWld) < 0.0f;
    }
    const float cosT = std::sqrt(std::max(0.0f, 1.0f - sin2T));
    outDirEntryWld =
        -eta * omegaOutWld + (eta * cosHI - cosT) * H;

    // Reject an invalid selected direction rather than changing its sampling
    // distribution with a fallback event.
    return Dot(normalShdLobeWldOut, outDirEntryWld) < 0.0f &&
        Dot(normalGeomWldOut, outDirEntryWld) < 0.0f;
}

}  // namespace mxcpp
