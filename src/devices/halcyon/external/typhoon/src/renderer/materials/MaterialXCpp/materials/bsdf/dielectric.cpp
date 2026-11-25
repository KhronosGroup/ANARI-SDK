//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "dielectric.h"
#include "thinFilm.h"

#include <renderer/materials/MaterialXCpp/spectral.h>

#include <algorithm>
#include <cmath>
#include <type_traits>
#include <variant>
namespace mxcpp {
namespace Bsdf {
namespace detail {

float
ResolveDielectricIor(
    const Bsdf::DielectricData& data,
    float heroWavelengthNm)
{
    if (data.dispersionAbbe > 0.0f && heroWavelengthNm > 0.0f) {
        return std::max(
            Spectral::CauchyDispersionIOR(
                data.dispersionAbbe, data.ior, heroWavelengthNm),
            1.0f);
    }
    return std::max(data.ior, 1.0f);
}

float
ResolveDielectricIor(
    const Bsdf::DielectricInterfaceData& data,
    float heroWavelengthNm)
{
    if (data.dispersionAbbe > 0.0f && heroWavelengthNm > 0.0f) {
        return std::max(
            Spectral::CauchyDispersionIOR(
                data.dispersionAbbe, data.ior, heroWavelengthNm),
            1.0f);
    }
    return std::max(data.ior, 1.0f);
}

static Vec3f
_ThinWalledWindowReflectance(const Vec3f& frontReflectance)
{
    const Vec3f r(
        Clamp01(frontReflectance[0]),
        Clamp01(frontReflectance[1]),
        Clamp01(frontReflectance[2]));
    return Vec3f(
        (2.0f * r[0]) / (1.0f + r[0]),
        (2.0f * r[1]) / (1.0f + r[1]),
        (2.0f * r[2]) / (1.0f + r[2]));
}

Vec3f
DielectricReflectionFresnelUntinted(
    const Bsdf::DielectricData& data,
    float cosTheta,
    float effectiveIor)
{
    float F0 = (effectiveIor - 1.0f) / (effectiveIor + 1.0f);
    F0 *= F0;
    const Vec3f baseReflectance = SchlickFresnel(
        Vec3f(F0),
        cosTheta);
    ThinFilmParams thinFilm;
    thinFilm.model = ThinFilmModel::Dielectric;
    thinFilm.ior = Vec3f(std::max(effectiveIor, 1.0f));
    thinFilm.tint = Vec3f(1.0f);
    return ApplyThinFilm(
        baseReflectance,
        cosTheta,
        data.thinFilmWeight,
        data.thinFilmThickness,
        data.thinFilmIor,
        thinFilm);
}

Vec3f
DielectricInterfaceReflectanceUntinted(
    const Bsdf::DielectricInterfaceData& data,
    float cosTheta,
    float effectiveIor,
    bool backside)
{
    // The interface models MaterialX dielectric_bsdf, whose reference
    // implementation evaluates the exact dielectric Fresnel equations
    // (mx_fresnel_dielectric).  Schlick fits the outside curve but badly
    // underestimates reflectance from inside the medium near the critical
    // angle, so pick the relative eta by side.  Thin-walled sheets respond
    // symmetrically, so they always use the outside eta.
    const float safeIor = std::max(effectiveIor, 1.0f + 1.0e-4f);
    const float eta = (backside && !data.thinWalled)
        ? 1.0f / safeIor
        : safeIor;
    const Vec2f polarized = FresnelDielectricPolarized(cosTheta, eta);
    const Vec3f baseReflectance(
        Clamp01(0.5f * (polarized[0] + polarized[1])));
    ThinFilmParams thinFilm;
    thinFilm.model = ThinFilmModel::Dielectric;
    thinFilm.ior = Vec3f(std::max(effectiveIor, 1.0f));
    thinFilm.tint = Vec3f(1.0f);
    const Vec3f reflectance = ApplyThinFilm(
        baseReflectance,
        cosTheta,
        data.thinFilmWeight,
        data.thinFilmThickness,
        data.thinFilmIor,
        thinFilm);
    return data.thinWalled
        ? _ThinWalledWindowReflectance(reflectance)
        : reflectance;
}

static bool
_IsCoupledTransmissionInterfaceForStraightShadow(
    const Bsdf::DielectricInterfaceData& data)
{
    const bool hasThinFilm =
        data.thinFilmWeight > kEpsilon &&
        data.thinFilmThickness > kEpsilon;
    return data.compensateCoupledDielectric &&
        !data.thinWalled &&
        data.reflectionWeight > 0.0f &&
        data.transmissionWeight > 0.0f &&
        !hasThinFilm;
}

static bool
_CollectCoupledTransmissionInterfaceForStraightShadow(
    const Bsdf::ClosureTree& tree,
    Bsdf::NodeId nodeId,
    const Bsdf::DielectricInterfaceData** result)
{
    const Bsdf::Node* const node = tree.Get(nodeId);
    if (!node) {
        return true;
    }

    return std::visit(
        [&](const auto& data) -> bool {
            using T = std::decay_t<decltype(data)>;
            if constexpr (std::is_same_v<T, Bsdf::DielectricInterfaceData>) {
                if (!_IsCoupledTransmissionInterfaceForStraightShadow(
                        data)) {
                    return true;
                }
                if (*result) {
                    return false;
                }
                *result = &data;
                return true;
            } else if constexpr (std::is_same_v<T, Bsdf::MixData>) {
                if (data.mix <= kEpsilon) {
                    return _CollectCoupledTransmissionInterfaceForStraightShadow(
                        tree, data.bg, result);
                }
                if (data.mix >= 1.0f - kEpsilon) {
                    return _CollectCoupledTransmissionInterfaceForStraightShadow(
                        tree, data.fg, result);
                }
                return _CollectCoupledTransmissionInterfaceForStraightShadow(
                           tree, data.fg, result) &&
                    _CollectCoupledTransmissionInterfaceForStraightShadow(
                        tree, data.bg, result);
            } else if constexpr (std::is_same_v<T, Bsdf::LayerData>) {
                return _CollectCoupledTransmissionInterfaceForStraightShadow(
                           tree, data.top, result) &&
                    _CollectCoupledTransmissionInterfaceForStraightShadow(
                        tree, data.base, result);
            } else if constexpr (std::is_same_v<T, Bsdf::AddData>) {
                return _CollectCoupledTransmissionInterfaceForStraightShadow(
                           tree, data.in1, result) &&
                    _CollectCoupledTransmissionInterfaceForStraightShadow(
                        tree, data.in2, result);
            } else if constexpr (std::is_same_v<T, Bsdf::MultiplyData>) {
                return _CollectCoupledTransmissionInterfaceForStraightShadow(
                    tree, data.input, result);
            } else {
                return true;
            }
        },
        node->data);
}

const Bsdf::DielectricInterfaceData*
FindCoupledTransmissionInterfaceForStraightShadow(
    const SurfaceClosure& closure)
{
    if (!closure.HasBsdfTree()) {
        return nullptr;
    }
    const Bsdf::DielectricInterfaceData* result = nullptr;
    return _CollectCoupledTransmissionInterfaceForStraightShadow(
               closure.bsdfTree, closure.bsdfTree.root, &result)
        ? result
        : nullptr;
}

float
GetCoupledDielectricCompensation(const Bsdf::DielectricInterfaceData& data,
                                  float effectiveIor,
                                  bool backside,
                                  const Vec3f& normalShdLobeWldOut,
                                  const Vec3f& omegaOutWld)
{
    if (!UsesCoupledRoughDielectricSampling(data)) {
        return 0.0f;
    }
    return BsdlCoupledDielectricCompensation(
        std::max(std::abs(Dot(normalShdLobeWldOut, omegaOutWld)), kEpsilon),
        BsdlLayerRoughnessFromAlpha(data.roughness), effectiveIor,
        backside);
}

Vec3f
EvalCoupledRoughDielectricTransmission(
    const Bsdf::DielectricInterfaceData& data, float effectiveIor,
    bool backside, const Vec3f& normalShdLobeWldOut,
    const Vec3f& omegaInWld, const Vec3f& omegaOutWld)
{
    const Frame frame =
        Frame::FromNormalAndTangent(normalShdLobeWldOut, data.tangent);
    const Vec3f omegaOutLocal = frame.ToLocal(omegaOutWld);
    const Vec3f omegaInLocal = frame.ToLocal(omegaInWld);
    const float cosThetaO = omegaOutLocal[2];
    const float cosThetaI = -omegaInLocal[2];
    if (cosThetaO <= kEpsilon || cosThetaI <= kEpsilon) {
        return Vec3f(0.0f);
    }

    // The Walter half-vector convention uses transmitted/incident IOR.
    const float etaPbrt = backside ? 1.0f / std::max(effectiveIor, kEpsilon)
                                   : std::max(effectiveIor, kEpsilon);
    Vec3f wmLocal = omegaInLocal * etaPbrt + omegaOutLocal;
    if (wmLocal.length() < kEpsilon) {
        return Vec3f(0.0f);
    }
    wmLocal.normalize();
    if (wmLocal[2] < 0.0f) {
        wmLocal = -wmLocal;
    }

    const float cosMO = Dot(omegaOutLocal, wmLocal);
    const float cosMI = Dot(omegaInLocal, wmLocal);
    if (cosMO <= kEpsilon || cosMI >= -kEpsilon) {
        return Vec3f(0.0f);
    }
    const float denom = cosMI + cosMO / etaPbrt;
    const float denom2 = denom * denom;
    if (denom2 <= kEpsilon) {
        return Vec3f(0.0f);
    }

    const Vec2f alpha = ClampAlpha(data.roughness);
    if (!GGXBoundedVNDFContainsNormal_Anisotropic(
            omegaOutLocal, wmLocal, alpha)) {
        return Vec3f(0.0f);
    }
    const float D = GGX_D_Anisotropic(alpha, wmLocal);
    const Vec3f omegaInReflectionSideWld(omegaInLocal[0], omegaInLocal[1],
                                         -omegaInLocal[2]);
    const float G =
        GGX_G_Anisotropic(alpha, omegaOutLocal, omegaInReflectionSideWld);
    const Vec3f transmission =
        DielectricInterfaceTransmissionCoefficient(
            data, cosMO, effectiveIor, backside);
    const float scale = D * G * std::abs(cosMO * cosMI) /
        std::max(cosThetaO * cosThetaI * denom2, kEpsilon);
    return SafeVec(transmission * scale);
}

bool
CoupledRoughDielectricDirectionInSampleSupport(
    const Bsdf::DielectricInterfaceData& data,
    const Vec3f& normalShdLobeWldOut,
    const Vec3f& omegaInWld,
    const Vec3f& omegaOutWld)
{
    const Frame frame =
        Frame::FromNormalAndTangent(normalShdLobeWldOut, data.tangent);
    const Vec3f omegaOutLocal = frame.ToLocal(omegaOutWld);
    const Vec3f omegaInLocal = frame.ToLocal(omegaInWld);
    Vec3f wmLocal = omegaOutLocal + omegaInLocal;
    if (omegaOutLocal[2] <= 0.0f || wmLocal.length() < kEpsilon) {
        return false;
    }
    wmLocal.normalize();
    if (wmLocal[2] < 0.0f) {
        wmLocal = -wmLocal;
    }
    return GGXBoundedVNDFContainsNormal_Anisotropic(
        omegaOutLocal, wmLocal, ClampAlpha(data.roughness));
}

float
PdfCoupledRoughDielectric(const Bsdf::DielectricInterfaceData& data,
                           float effectiveIor,
                           bool backside,
                           const Vec3f& normalShdLobeWldOut,
                           const Vec3f& omegaInWld, const Vec3f& omegaOutWld)
{
    const Frame frame =
        Frame::FromNormalAndTangent(normalShdLobeWldOut, data.tangent);
    const Vec3f omegaOutLocal = frame.ToLocal(omegaOutWld);
    const Vec3f omegaInLocal = frame.ToLocal(omegaInWld);
    if (omegaOutLocal[2] <= 0.0f || std::abs(omegaInLocal[2]) <= kEpsilon) {
        return 0.0f;
    }

    const bool reflection =
        IsSameSide(normalShdLobeWldOut, omegaInWld, omegaOutWld);
    // The Walter half-vector convention uses transmitted/incident IOR.
    const float etaPbrt = backside ? 1.0f / std::max(effectiveIor, kEpsilon)
                                   : std::max(effectiveIor, kEpsilon);

    Vec3f wmLocal = reflection ? omegaInLocal + omegaOutLocal
                               : omegaInLocal * etaPbrt + omegaOutLocal;
    if (wmLocal.length() < kEpsilon) {
        return 0.0f;
    }
    wmLocal.normalize();
    if (wmLocal[2] < 0.0f) {
        wmLocal = -wmLocal;
    }

    const float cosMO = std::abs(Dot(omegaOutLocal, wmLocal));
    if (cosMO <= kEpsilon) {
        return 0.0f;
    }
    const DielectricInterfaceSelection selection =
        DielectricInterfaceSelectionProbabilities(
            data, cosMO, effectiveIor, backside);
    const Vec2f alpha = ClampAlpha(data.roughness);
    if (!GGXBoundedVNDFContainsNormal_Anisotropic(
            omegaOutLocal, wmLocal, alpha)) {
        return 0.0f;
    }
    const float D = GGX_D_Anisotropic(alpha, wmLocal);
    const float boundedD =
        GGXBoundedVNDF_DRatio_Anisotropic(
            omegaOutLocal, wmLocal, alpha) * D;
    float specularPdf = 0.0f;
    if (reflection) {
        specularPdf = selection.reflection * boundedD /
            (4.0f * omegaOutLocal[2]);
    } else if (Dot(omegaInLocal, wmLocal) * Dot(omegaOutLocal, wmLocal) <
               0.0f) {
        const float denom =
            Dot(omegaInLocal, wmLocal) + Dot(omegaOutLocal, wmLocal) / etaPbrt;
        const float denom2 = denom * denom;
        if (denom2 > kEpsilon) {
            const float jacobian =
                -Dot(omegaInLocal, wmLocal) * cosMO /
                (omegaOutLocal[2] * denom2);
            specularPdf = selection.transmission * boundedD * jacobian;
        }
    }

    return specularPdf;
}

Bsdf::BsdfSample
SampleCoupledRoughDielectric(const Bsdf::DielectricInterfaceData& data,
                              float effectiveIor,
                              bool backside,
                              const Vec3f& normalShdLobeWldOut,
                              const Vec3f& omegaOutWld, float u1, float u2,
                              float uChoice)
{
    const Frame frame =
        Frame::FromNormalAndTangent(normalShdLobeWldOut, data.tangent);
    const Vec3f omegaOutLocal = frame.ToLocal(omegaOutWld);
    if (omegaOutLocal[2] <= 0.0f) {
        return Bsdf::BsdfSample{
            Vec3f(0.0f), Vec3f(0.0f), 0.0f, false};
    }

    const Vec2f alpha = ClampAlpha(data.roughness);
    const Vec3f wmLocal =
        SampleGGXBoundedVNDF_Anisotropic(omegaOutLocal, alpha, u1, u2);
    const float cosMO = std::abs(Dot(omegaOutLocal, wmLocal));
    const DielectricInterfaceSelection selection =
        DielectricInterfaceSelectionProbabilities(
            data, cosMO, effectiveIor, backside);
    if (selection.reflection + selection.transmission <= 0.0f) {
        return Bsdf::BsdfSample{
            Vec3f(0.0f), Vec3f(0.0f), 0.0f, false};
    }

    const float eta = backside
        ? std::max(effectiveIor, kEpsilon)
        : 1.0f / std::max(effectiveIor, kEpsilon);
    const float sin2T = eta * eta *
        std::max(0.0f, 1.0f - cosMO * cosMO);
    const bool totalInternalReflection = sin2T >= 1.0f;
    const bool chooseReflection = totalInternalReflection ||
        uChoice < selection.reflection;

    Vec3f omegaInLocal;
    if (chooseReflection) {
        omegaInLocal =
            2.0f * Dot(omegaOutLocal, wmLocal) * wmLocal - omegaOutLocal;
        if (omegaInLocal[2] <= 0.0f) {
            return Bsdf::BsdfSample{
                Vec3f(0.0f), Vec3f(0.0f), 0.0f, false};
        }
    } else {
        float cosT = std::sqrt(std::max(0.0f, 1.0f - sin2T));
        omegaInLocal = -eta * omegaOutLocal + (eta * cosMO - cosT) * wmLocal;
        if (omegaInLocal.length() < kEpsilon) {
            return Bsdf::BsdfSample{
                Vec3f(0.0f), Vec3f(0.0f), 0.0f, false};
        }
        omegaInLocal.normalize();
        if (omegaInLocal[2] >= 0.0f) {
            return Bsdf::BsdfSample{
                Vec3f(0.0f), Vec3f(0.0f), 0.0f, false};
        }
    }

    Bsdf::BsdfSample sample{frame.ToWorld(omegaInLocal), Vec3f(0.0f), 1.0f,
                            false};
    sample.isTransmission = !chooseReflection;
    sample.eta = chooseReflection ? 1.0f : eta;
    return sample;
}

Bsdf::BsdfSample
SampleDeltaTransmission(float ior, const Vec3f& tint, float weight,
                         const Vec3f& normalShdWldOut,
                         const Vec3f& omegaOutWld, bool backside)
{
    const float cosI = std::abs(Dot(normalShdWldOut, omegaOutWld));
    const float eta = backside ? ior : 1.0f / ior;
    const Vec3f normalIncidentWldOut =
        Dot(normalShdWldOut, omegaOutWld) >= 0.0f
        ? normalShdWldOut
        : -normalShdWldOut;

    float sin2T = eta * eta * (1.0f - cosI * cosI);
    if (sin2T >= 1.0f) {
        return SampleDeltaTotalInternalReflection(weight, normalShdWldOut,
                                                   omegaOutWld);
    }

    float cosT = std::sqrt(1.0f - sin2T);
    Vec3f omegaInWld =
        -eta * omegaOutWld +
        (eta * cosI - cosT) * normalIncidentWldOut;
    omegaInWld.normalize();
    float fresnel = SchlickFresnelScalar(ior, cosI);
    Bsdf::BsdfSample sample{omegaInWld, tint * ((1.0f - fresnel) * weight),
                            1.0f, true};
    sample.isTransmission = true;
    sample.eta = eta;
    return sample;
}

Bsdf::BsdfSample
SampleDeltaDielectricTransmission(
    const Bsdf::DielectricData& data,
    float effectiveIor,
    float fresnelCos,
    const Vec3f& normalShdLobeWldOut,
    const Vec3f& omegaOutWld,
    bool backside,
    const Vec3f& luminanceCoefficients)
{
    if (WouldTotalInternalReflect(effectiveIor, normalShdLobeWldOut,
                                   omegaOutWld, backside)) {
        // A transmission-only lobe is paired with a separate reflection lobe
        // that keeps contributing its Schlick reflectance from inside the
        // medium, so a full-weight TIR sample here would double-count
        // reflection and gain energy on every internal bounce.  Carry only
        // the remainder so the reflection/transmission pair totals one.
        const float pairedReflectance = Clamp01(Luminance(
            DielectricReflectionFresnelUntinted(
                data, fresnelCos, effectiveIor),
            luminanceCoefficients));
        return SampleDeltaTotalInternalReflection(
            data.weight * (1.0f - pairedReflectance),
            normalShdLobeWldOut,
            omegaOutWld);
    }

    auto sample = SampleDeltaTransmission(effectiveIor, data.tint, data.weight,
                                           normalShdLobeWldOut, omegaOutWld,
                                           backside);
    const float baseReflectance =
        SchlickFresnelScalar(effectiveIor, fresnelCos);
    sample.bsdfValue =
        CompMul(sample.bsdfValue,
                TransmissionScale(baseReflectance,
                                   DielectricReflectionFresnelUntinted(
                                       data, fresnelCos, effectiveIor)));
    return sample;
}

Bsdf::BsdfSample
SampleDeltaDielectricInterfaceTransmission(
    const Bsdf::DielectricInterfaceData& data, float effectiveIor,
    float fresnelCos, const Vec3f& normalShdLobeWldOut,
    const Vec3f& omegaOutWld, bool backside)
{
    if (data.thinWalled) {
        Vec3f omegaInWld = -omegaOutWld;
        omegaInWld.normalize();
        Bsdf::BsdfSample sample{omegaInWld,
                                DielectricInterfaceTransmissionCoefficient(
                                    data, fresnelCos, effectiveIor, backside),
                                1.0f, true};
        sample.isTransmission = true;
        sample.eta = 1.0f;
        return sample;
    }

    Bsdf::BsdfSample sample =
        WouldTotalInternalReflect(
            effectiveIor, normalShdLobeWldOut, omegaOutWld, backside)
        ? SampleDeltaTotalInternalReflection(
              Clamp01(data.transmissionWeight),
              normalShdLobeWldOut, omegaOutWld)
        : SampleDeltaTransmission(
              effectiveIor, data.transmissionTint,
              Clamp01(data.transmissionWeight), normalShdLobeWldOut,
              omegaOutWld, backside);
    const float baseReflectance =
        SchlickFresnelScalar(effectiveIor, fresnelCos);
    sample.bsdfValue = CompMul(
        sample.bsdfValue,
        TransmissionScale(baseReflectance,
                           DielectricInterfaceReflectanceUntinted(
                               data, fresnelCos, effectiveIor, backside)));
    return sample;
}

}  // namespace detail
}  // namespace Bsdf
}  // namespace mxcpp
