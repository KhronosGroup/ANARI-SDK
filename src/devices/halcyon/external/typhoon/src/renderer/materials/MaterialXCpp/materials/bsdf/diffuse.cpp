//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "diffuse.h"

namespace mxcpp {
namespace Bsdf {
namespace detail {

static constexpr float _kFonConstantA = 0.5f - 2.0f / (3.0f * kPi);

static constexpr float _kFonConstantB =
    2.0f / 3.0f - 28.0f / (15.0f * kPi);

static float
_FonDirectionalAlbedoApprox(float mu, float roughness)
{
    const float clampedMu = Clamp01(mu);
    const float muComp = 1.0f - clampedMu;
    constexpr float g1 = 0.0571085289f;
    constexpr float g2 = 0.491881867f;
    constexpr float g3 = -0.332181442f;
    constexpr float g4 = 0.0714429953f;
    const float gOverPi =
        muComp * (g1 + muComp * (g2 + muComp * (g3 + muComp * g4)));
    return (1.0f + roughness * gOverPi) /
           (1.0f + _kFonConstantA * roughness);
}

Vec3f
EvalEonDiffuse(
    const Vec3f& color,
    float roughness,
    float NdotV,
    float NdotL,
    float LdotV)
{
    const float r = Clamp01(roughness);
    if (r <= 0.0f) {
        return color * kInvPi;
    }

    const float muI = std::max(NdotL, kEpsilon);
    const float muO = std::max(NdotV, kEpsilon);
    const float s = LdotV - muI * muO;
    const float sOverF = (s > 0.0f) ? s / std::max(muI, muO) : s;
    const float aF = 1.0f / (1.0f + _kFonConstantA * r);
    const Vec3f fSS = color * (kInvPi * aF * (1.0f + r * sOverF));

    const float eFi = _FonDirectionalAlbedoApprox(muI, r);
    const float eFo = _FonDirectionalAlbedoApprox(muO, r);
    const float avgEF = aF * (1.0f + _kFonConstantB * r);
    const Vec3f rhoMS = CompDiv(
        CompMul(color, color) * avgEF,
        Vec3f(1.0f) - color * (1.0f - avgEF));
    const float fMSScale =
        kInvPi *
        std::max(kEpsilon, 1.0f - eFi) *
        std::max(kEpsilon, 1.0f - eFo) /
        std::max(kEpsilon, 1.0f - avgEF);

    return fSS + rhoMS * fMSScale;
}

}  // namespace detail
}  // namespace Bsdf
}  // namespace mxcpp
