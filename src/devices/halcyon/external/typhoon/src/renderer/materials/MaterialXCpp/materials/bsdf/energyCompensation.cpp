//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "energyCompensation.h"
#include "dielectricBothLut.h"
#include "dielectricReflFrontLut.h"
#include "dielectricTransmissionLut.h"
#include "fresnel.h"
#include "ggxEnergyLut.h"
#include "mathPrimitives.h"

#include <algorithm>
#include <atomic>
#include <cmath>

namespace mxcpp {
namespace Bsdf {
namespace detail {

static constexpr float _kTurquinMicrofacetMsMinAlpha = 0.04f * 0.04f;

static std::atomic<int> _gDielectricLayerThroughputMode{
    static_cast<int>(Bsdf::DielectricLayerThroughputMode::Bsdl)};

void
SetDielectricThroughputModeState(Bsdf::DielectricLayerThroughputMode mode)
{
    _gDielectricLayerThroughputMode.store(
        static_cast<int>(mode), std::memory_order_relaxed);
}

float
LookupGgxMissingEnergy(float cosTheta, float alphaRoughness)
{
    const float c = Clamp01(cosTheta);
    const float alpha = Clamp01(alphaRoughness);

    const float alphaCoord =
        std::sqrt(alpha) * static_cast<float>(kGgxEnergyAlphaCount - 1);
    const int alpha0 = std::clamp(
        static_cast<int>(alphaCoord), 0, kGgxEnergyAlphaCount - 1);
    const int alpha1 = std::min(alpha0 + 1, kGgxEnergyAlphaCount - 1);
    const float alphaT = alphaCoord - static_cast<float>(alpha0);

    int cos0 = 0;
    while (cos0 + 1 < kGgxEnergyCosThetaCount &&
           c > kGgxEnergyCosTheta[cos0 + 1]) {
        ++cos0;
    }
    const int cos1 = std::min(cos0 + 1, kGgxEnergyCosThetaCount - 1);
    const float cosDenom =
        std::max(kGgxEnergyCosTheta[cos1] - kGgxEnergyCosTheta[cos0],
                 kEpsilon);
    const float cosT =
        std::clamp((c - kGgxEnergyCosTheta[cos0]) / cosDenom, 0.0f, 1.0f);

    const float e00 = kGgxMissingEnergy[alpha0][cos0];
    const float e01 = kGgxMissingEnergy[alpha0][cos1];
    const float e10 = kGgxMissingEnergy[alpha1][cos0];
    const float e11 = kGgxMissingEnergy[alpha1][cos1];
    const float e0 = e00 * (1.0f - cosT) + e01 * cosT;
    const float e1 = e10 * (1.0f - cosT) + e11 * cosT;
    return Clamp01(e0 * (1.0f - alphaT) + e1 * alphaT);
}

static float
_BsdlDielectricReflFrontCosine(int index)
{
    const float t =
        static_cast<float>(index) /
        static_cast<float>(
            bsdf_luts::kBsdlDielectricReflFrontCosThetaCount - 1);
    return std::max(t, 1.0e-6f);
}

float
LookupBsdlDielectricReflFrontFilter(
    float cosTheta,
    float perceptualRoughness,
    float ior)
{
    namespace lut = bsdf_luts;

    const float clampedCosTheta = Clamp01(cosTheta);
    const float roughness = Clamp01(perceptualRoughness);
    const float clampedIor = std::clamp(
        ior,
        kBsdlDielectricIorMin,
        kBsdlDielectricIorMax);

    const float iorIndex = std::sqrt(
        (clampedIor - kBsdlDielectricIorMin) /
        (kBsdlDielectricIorMax - kBsdlDielectricIorMin));
    const float iorCoord =
        iorIndex *
        static_cast<float>(lut::kBsdlDielectricReflFrontIorCount - 1);
    const int ior0 = std::clamp(
        static_cast<int>(iorCoord),
        0,
        lut::kBsdlDielectricReflFrontIorCount - 1);
    const int ior1 = std::min(
        ior0 + 1,
        lut::kBsdlDielectricReflFrontIorCount - 1);
    const float iorT = iorCoord - static_cast<float>(ior0);

    const float roughnessCoord =
        roughness *
        static_cast<float>(lut::kBsdlDielectricReflFrontRoughnessCount - 1);
    const int roughness0 = std::clamp(
        static_cast<int>(roughnessCoord),
        0,
        lut::kBsdlDielectricReflFrontRoughnessCount - 1);
    const int roughness1 = std::min(
        roughness0 + 1,
        lut::kBsdlDielectricReflFrontRoughnessCount - 1);
    const float roughnessT =
        roughnessCoord - static_cast<float>(roughness0);

    int cos0 = 0;
    int cos1 = 0;
    float cosT = 0.0f;
    float prevCos = _BsdlDielectricReflFrontCosine(0);
    if (clampedCosTheta > prevCos) {
        cos0 = lut::kBsdlDielectricReflFrontCosThetaCount - 1;
        cos1 = cos0;
        for (int i = 1; i < lut::kBsdlDielectricReflFrontCosThetaCount; ++i) {
            const float nextCos = _BsdlDielectricReflFrontCosine(i);
            if (clampedCosTheta < nextCos) {
                cos0 = i - 1;
                cos1 = i;
                cosT =
                    (clampedCosTheta - prevCos) / (nextCos - prevCos);
                break;
            }
            prevCos = nextCos;
        }
    }

    const auto lookup = [&](int i, int r, int c0) {
        return lut::kBsdlDielectricReflFrontFilter[i][r][c0];
    };
    const auto lerpCos = [&](int i, int r) {
        return lookup(i, r, cos0) * (1.0f - cosT) +
               lookup(i, r, cos1) * cosT;
    };
    const auto lerpRoughness = [&](int i) {
        return lerpCos(i, roughness0) * (1.0f - roughnessT) +
               lerpCos(i, roughness1) * roughnessT;
    };

    return Clamp01(
        lerpRoughness(ior0) * (1.0f - iorT) +
        lerpRoughness(ior1) * iorT);
}

static float
_LookupBsdlDielectricBothMissingEnergy(
    float cosTheta,
    float perceptualRoughness,
    float ior,
    bool backfacing)
{
    namespace lut = bsdf_luts;

    const float clampedCosTheta = Clamp01(cosTheta);
    const float roughness = Clamp01(perceptualRoughness);
    const float clampedIor = std::clamp(
        ior,
        kBsdlDielectricIorMin,
        kBsdlDielectricIorMax);

    const float iorIndex = std::sqrt(
        (clampedIor - kBsdlDielectricIorMin) /
        (kBsdlDielectricIorMax - kBsdlDielectricIorMin));
    const float iorCoord =
        iorIndex * static_cast<float>(lut::kBsdlDielectricBothIorCount - 1);
    const int ior0 = std::clamp(
        static_cast<int>(iorCoord), 0, lut::kBsdlDielectricBothIorCount - 1);
    const int ior1 =
        std::min(ior0 + 1, lut::kBsdlDielectricBothIorCount - 1);
    const float iorT = iorCoord - static_cast<float>(ior0);

    const float roughnessCoord = roughness * static_cast<float>(
        lut::kBsdlDielectricBothRoughnessCount - 1);
    const int roughness0 = std::clamp(
        static_cast<int>(roughnessCoord),
        0,
        lut::kBsdlDielectricBothRoughnessCount - 1);
    const int roughness1 = std::min(
        roughness0 + 1,
        lut::kBsdlDielectricBothRoughnessCount - 1);
    const float roughnessT =
        roughnessCoord - static_cast<float>(roughness0);

    const float cosCoord = clampedCosTheta * static_cast<float>(
        lut::kBsdlDielectricBothCosThetaCount - 1);
    const int cos0 = std::clamp(
        static_cast<int>(cosCoord),
        0,
        lut::kBsdlDielectricBothCosThetaCount - 1);
    const int cos1 =
        std::min(cos0 + 1, lut::kBsdlDielectricBothCosThetaCount - 1);
    const float cosT = cosCoord - static_cast<float>(cos0);

    const float* values = backfacing
        ? lut::kBsdlDielectricBothBackMissingEnergy
        : lut::kBsdlDielectricBothFrontMissingEnergy;
    const auto lookup = [&](int i, int r, int cosine) {
        const int index =
            (i * lut::kBsdlDielectricBothRoughnessCount + r) *
                lut::kBsdlDielectricBothCosThetaCount +
            cosine;
        return values[index];
    };
    const auto lerpCos = [&](int i, int r) {
        return lookup(i, r, cos0) * (1.0f - cosT) +
            lookup(i, r, cos1) * cosT;
    };
    const auto lerpRoughness = [&](int i) {
        return lerpCos(i, roughness0) * (1.0f - roughnessT) +
            lerpCos(i, roughness1) * roughnessT;
    };

    return Clamp01(
        lerpRoughness(ior0) * (1.0f - iorT) +
        lerpRoughness(ior1) * iorT);
}

float
LookupBsdlDielectricTransmissionSingleScatterAlbedo(
    float cosTheta,
    float perceptualRoughness,
    float ior,
    bool backfacing)
{
    namespace lut = bsdf_luts;

    const float clampedCosTheta = Clamp01(cosTheta);
    const float roughness = Clamp01(perceptualRoughness);
    const float clampedIor = std::clamp(
        ior,
        kBsdlDielectricIorMin,
        kBsdlDielectricIorMax);

    const float iorIndex = std::sqrt(
        (clampedIor - kBsdlDielectricIorMin) /
        (kBsdlDielectricIorMax - kBsdlDielectricIorMin));
    const float iorCoord = iorIndex * static_cast<float>(
        lut::kBsdlDielectricTransmissionIorCount - 1);
    const int ior0 = std::clamp(
        static_cast<int>(iorCoord),
        0,
        lut::kBsdlDielectricTransmissionIorCount - 1);
    const int ior1 = std::min(
        ior0 + 1, lut::kBsdlDielectricTransmissionIorCount - 1);
    const float iorT = iorCoord - static_cast<float>(ior0);

    const float roughnessCoord = roughness * static_cast<float>(
        lut::kBsdlDielectricTransmissionRoughnessCount - 1);
    const int roughness0 = std::clamp(
        static_cast<int>(roughnessCoord),
        0,
        lut::kBsdlDielectricTransmissionRoughnessCount - 1);
    const int roughness1 = std::min(
        roughness0 + 1,
        lut::kBsdlDielectricTransmissionRoughnessCount - 1);
    const float roughnessT =
        roughnessCoord - static_cast<float>(roughness0);

    const float cosCoord = clampedCosTheta * static_cast<float>(
        lut::kBsdlDielectricTransmissionCosThetaCount - 1);
    const int cos0 = std::clamp(
        static_cast<int>(cosCoord),
        0,
        lut::kBsdlDielectricTransmissionCosThetaCount - 1);
    const int cos1 = std::min(
        cos0 + 1,
        lut::kBsdlDielectricTransmissionCosThetaCount - 1);
    const float cosT = cosCoord - static_cast<float>(cos0);

    const float* values = backfacing
        ? lut::kBsdlDielectricTransmissionBackSingleScatterAlbedo
        : lut::kBsdlDielectricTransmissionFrontSingleScatterAlbedo;
    const auto lookup = [&](int i, int r, int cosine) {
        const int index =
            (i * lut::kBsdlDielectricTransmissionRoughnessCount + r) *
                lut::kBsdlDielectricTransmissionCosThetaCount +
            cosine;
        return values[index];
    };
    const auto lerpCos = [&](int i, int r) {
        return lookup(i, r, cos0) * (1.0f - cosT) +
            lookup(i, r, cos1) * cosT;
    };
    const auto lerpRoughness = [&](int i) {
        return lerpCos(i, roughness0) * (1.0f - roughnessT) +
            lerpCos(i, roughness1) * roughnessT;
    };

    return Clamp01(
        lerpRoughness(ior0) * (1.0f - iorT) +
        lerpRoughness(ior1) * iorT);
}

float
BsdlCoupledDielectricCompensation(
    float cosThetaO,
    float perceptualRoughness,
    float ior,
    bool backfacing)
{
    if (perceptualRoughness < std::sqrt(_kTurquinMicrofacetMsMinAlpha)) {
        return 0.0f;
    }

    return _LookupBsdlDielectricBothMissingEnergy(
        cosThetaO,
        perceptualRoughness,
        ior,
        backfacing);
}

Bsdf::DielectricLayerThroughputMode
GetDielectricThroughputModeState()
{
    const auto mode = static_cast<Bsdf::DielectricLayerThroughputMode>(
        _gDielectricLayerThroughputMode.load(std::memory_order_relaxed));
    switch (mode) {
        case Bsdf::DielectricLayerThroughputMode::Bsdl:
        case Bsdf::DielectricLayerThroughputMode::MaterialXGlsl:
            return mode;
    }
    return Bsdf::DielectricLayerThroughputMode::Bsdl;
}

Vec3f
TurquinMicrofacetMsScale(
    float alphaRoughness,
    float cosThetaO,
    const Vec3f& fresnel)
{
    if (alphaRoughness < _kTurquinMicrofacetMsMinAlpha) {
        return Vec3f(1.0f);
    }

    const float missingEnergy =
        LookupGgxMissingEnergy(cosThetaO, alphaRoughness);
    const float singleScatterEnergy = std::max(0.01f, 1.0f - missingEnergy);
    const float missingToSingle = missingEnergy / singleScatterEnergy;

    // Turquin compensation keeps the primary lobe shape and scales it by the
    // outgoing-direction missing energy. This is intentionally not reciprocal.
    return Vec3f(1.0f) +
        Clamp01(fresnel) * missingToSingle;
}

static Vec3f
_TurquinDirectionalReflectance(
    float alphaRoughness,
    float cosThetaO,
    const Vec3f& fresnel)
{
    if (alphaRoughness < _kTurquinMicrofacetMsMinAlpha) {
        return Clamp01(fresnel);
    }

    const float missingEnergy =
        LookupGgxMissingEnergy(cosThetaO, alphaRoughness);
    const float singleScatterEnergy = 1.0f - missingEnergy;
    const Vec3f F = Clamp01(fresnel);
    return Clamp01(
        F * singleScatterEnergy +
        CompMul(F, F) * missingEnergy);
}

Vec3f
LayerThroughputReflectance(
    float alphaRoughness,
    float cosThetaO,
    const Vec3f& fresnel)
{
    return _TurquinDirectionalReflectance(alphaRoughness, cosThetaO, fresnel);
}

static Vec2f
_MaterialXGgxDirAlbedoAnalyticAB(float NdotV, float alpha)
{
    const float x = Clamp01(NdotV);
    const float y = Clamp01(alpha);
    const float x2 = x * x;
    const float y2 = y * y;
    const float xy = x * y;
    const float x2y = x2 * y;
    const float xy2 = x * y2;
    const float x2y2 = x2 * y2;

    const float r0 =
        0.1003f +
        (-0.6303f * x) +
        (9.748f * y) +
        (-2.038f * xy) +
        (29.34f * x2) +
        (-8.245f * y2) +
        (-26.44f * x2y) +
        (19.99f * xy2) +
        (-5.448f * x2y2);
    const float r1 =
        0.9345f +
        (-2.323f * x) +
        (2.229f * y) +
        (-3.748f * xy) +
        (1.424f * x2) +
        (-0.7684f * y2) +
        (1.436f * x2y) +
        (0.2913f * xy2) +
        (0.6286f * x2y2);
    const float r2 =
        1.0f +
        (-1.765f * x) +
        (8.263f * y) +
        (11.53f * xy) +
        (28.96f * x2) +
        (-7.507f * y2) +
        (-36.11f * x2y) +
        (15.86f * xy2) +
        (33.37f * x2y2);
    const float r3 =
        1.0f +
        (0.2281f * x) +
        (15.94f * y) +
        (-55.83f * xy) +
        (13.08f * x2) +
        (41.26f * y2) +
        (54.9f * x2y) +
        (300.2f * xy2) +
        (-285.1f * x2y2);

    const float ab0 = r0 / std::copysign(
        std::max(std::abs(r2), kEpsilon), r2);
    const float ab1 = r1 / std::copysign(
        std::max(std::abs(r3), kEpsilon), r3);
    return Vec2f(ClampFinite01(ab0), ClampFinite01(ab1));
}

static Vec3f
_MaterialXGgxDirAlbedoAnalytic(
    float NdotV,
    float alpha,
    const Vec3f& F0,
    const Vec3f& F90)
{
    const Vec2f ab = _MaterialXGgxDirAlbedoAnalyticAB(NdotV, alpha);
    return F0 * ab[0] + F90 * ab[1];
}

static Vec3f
_MaterialXGgxEnergyCompensation(
    float NdotV,
    float alpha,
    const Vec3f& singleScatterFresnel)
{
    const float Ess = std::max(
        _MaterialXGgxDirAlbedoAnalytic(
            NdotV, alpha, Vec3f(1.0f), Vec3f(1.0f))[0],
        kEpsilon);
    return Vec3f(1.0f) +
        singleScatterFresnel * ((1.0f - Ess) / Ess);
}

Vec3f
MaterialXGlslDielectricLayerReflectance(
    float alphaRoughness,
    float cosThetaO,
    float ior)
{
    float F0 = (ior - 1.0f) / (ior + 1.0f);
    F0 *= F0;
    const Vec3f F(MaterialXDielectricFresnel(cosThetaO, ior));
    return CompMul(
        _MaterialXGgxDirAlbedoAnalytic(
            cosThetaO,
            alphaRoughness,
            Vec3f(F0),
            Vec3f(1.0f)),
        _MaterialXGgxEnergyCompensation(cosThetaO, alphaRoughness, F));
}

}  // namespace detail
}  // namespace Bsdf
}  // namespace mxcpp
