//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "thinFilm.h"
#include "fresnel.h"
#include "mathPrimitives.h"

#include <algorithm>
#include <cmath>

namespace mxcpp {
namespace Bsdf {
namespace detail {

static constexpr int _kThinFilmAiryIterations = 2;

bool
HasThinFilm(float weight, float thickness, float /*ior*/)
{
    return weight > kEpsilon &&
           thickness > kEpsilon;
}





static Vec3f
_F0ToIor(const Vec3f& F0)
{
    const Vec3f sqrtF0 = SqrtVec(Clamp01(Vec3f(
        std::clamp(F0[0], 0.01f, 0.99f),
        std::clamp(F0[1], 0.01f, 0.99f),
        std::clamp(F0[2], 0.01f, 0.99f))));
    return CompDiv(Vec3f(1.0f) + sqrtF0, Vec3f(1.0f) - sqrtF0);
}

Vec2f
FresnelDielectricPolarized(float cosTheta, float ior)
{
    const float cosTheta2 = Clamp01(cosTheta) * Clamp01(cosTheta);
    const float sinTheta2 = 1.0f - cosTheta2;

    const float t0 = std::max(ior * ior - sinTheta2, 0.0f);
    const float t1 = t0 + cosTheta2;
    const float t2 = 2.0f * std::sqrt(t0) * Clamp01(cosTheta);
    const float Rs = (t1 - t2) / std::max(t1 + t2, kEpsilon);

    const float t3 = cosTheta2 * t0 + sinTheta2 * sinTheta2;
    const float t4 = t2 * sinTheta2;
    const float Rp = Rs * (t3 - t4) / std::max(t3 + t4, kEpsilon);

    return Vec2f(Rp, Rs);
}

static void
_FresnelConductorPolarized(
    float cosTheta,
    const Vec3f& n,
    const Vec3f& k,
    Vec3f* Rp,
    Vec3f* Rs)
{
    const float clampedCos = Clamp01(cosTheta);
    const float cosTheta2 = clampedCos * clampedCos;
    const float sinTheta2 = 1.0f - cosTheta2;
    const Vec3f n2 = SquareVec(n);
    const Vec3f k2 = SquareVec(k);

    const Vec3f t0 = n2 - k2 - Vec3f(sinTheta2);
    const Vec3f a2plusb2 = SqrtVec(t0 * t0 + 4.0f * CompMul(n2, k2));
    const Vec3f t1 = a2plusb2 + Vec3f(cosTheta2);
    const Vec3f a = SqrtVec(MaxVec(0.5f * (a2plusb2 + t0), 0.0f));
    const Vec3f t2 = 2.0f * a * clampedCos;
    *Rs = CompDiv(t1 - t2, t1 + t2);

    const Vec3f t3 = a2plusb2 * cosTheta2 + Vec3f(sinTheta2 * sinTheta2);
    const Vec3f t4 = t2 * sinTheta2;
    *Rp = CompMul(*Rs, CompDiv(t3 - t4, t3 + t4));
}

static void
_FresnelConductorPhasePolarized(float cosTheta, float iorIn,
                                const Vec3f& iorOut,
                                const Vec3f& absorptionIndexOut, Vec3f* phiP,
                                Vec3f* phiS)
{
    const Vec3f k2 = CompDiv(absorptionIndexOut, iorOut);
    const Vec3f iorOutSquared = CompMul(iorOut, iorOut);
    const Vec3f sinThetaSqr(1.0f - cosTheta * cosTheta);
    const Vec3f A = iorOutSquared * (Vec3f(1.0f) - CompMul(k2, k2)) -
                    iorIn * iorIn * sinThetaSqr;
    const Vec3f twoIorOutSquaredK2 = 2.0f * CompMul(iorOutSquared, k2);
    const Vec3f B = SqrtVec(CompMul(A, A) +
                             CompMul(twoIorOutSquaredK2, twoIorOutSquaredK2));
    const Vec3f U = SqrtVec((A + B) * 0.5f);
    const Vec3f V = MaxVec(SqrtVec((B - A) * 0.5f), 0.0f);

    *phiS = Vec3f(std::atan2(2.0f * iorIn * V[0] * cosTheta,
                             U[0] * U[0] + V[0] * V[0] -
                                 iorIn * iorIn * cosTheta * cosTheta),
                  std::atan2(2.0f * iorIn * V[1] * cosTheta,
                             U[1] * U[1] + V[1] * V[1] -
                                 iorIn * iorIn * cosTheta * cosTheta),
                  std::atan2(2.0f * iorIn * V[2] * cosTheta,
                             U[2] * U[2] + V[2] * V[2] -
                                 iorIn * iorIn * cosTheta * cosTheta));

    const Vec3f oneMinusK2 = Vec3f(1.0f) - CompMul(k2, k2);
    const Vec3f onePlusK2 = Vec3f(1.0f) + CompMul(k2, k2);
    *phiP =
        Vec3f(std::atan2(2.0f * iorIn * iorOutSquared[0] * cosTheta *
                             (2.0f * k2[0] * U[0] - oneMinusK2[0] * V[0]),
                         iorOutSquared[0] * iorOutSquared[0] * onePlusK2[0] *
                                 onePlusK2[0] * cosTheta * cosTheta -
                             iorIn * iorIn * (U[0] * U[0] + V[0] * V[0])),
              std::atan2(2.0f * iorIn * iorOutSquared[1] * cosTheta *
                             (2.0f * k2[1] * U[1] - oneMinusK2[1] * V[1]),
                         iorOutSquared[1] * iorOutSquared[1] * onePlusK2[1] *
                                 onePlusK2[1] * cosTheta * cosTheta -
                             iorIn * iorIn * (U[1] * U[1] + V[1] * V[1])),
              std::atan2(2.0f * iorIn * iorOutSquared[2] * cosTheta *
                             (2.0f * k2[2] * U[2] - oneMinusK2[2] * V[2]),
                         iorOutSquared[2] * iorOutSquared[2] * onePlusK2[2] *
                                 onePlusK2[2] * cosTheta * cosTheta -
                             iorIn * iorIn * (U[2] * U[2] + V[2] * V[2])));
}

Vec3f
FresnelConductor(
    float cosTheta,
    const Vec3f& ior,
    const Vec3f& extinction)
{
    Vec3f Rp(0.0f), Rs(0.0f);
    _FresnelConductorPolarized(cosTheta, ior, extinction, &Rp, &Rs);
    return Clamp01((Rp + Rs) * 0.5f);
}

static Vec3f
_EvalSensitivity(float opd, const Vec3f& shift)
{
    const float phase = 2.0f * kPi * opd;
    const Vec3f val(5.4856e-13f, 4.4201e-13f, 5.2481e-13f);
    const Vec3f pos(1.6810e+06f, 1.7953e+06f, 2.2084e+06f);
    const Vec3f var(4.3278e+09f, 9.3046e+09f, 6.6121e+09f);
    const Vec3f phaseVec = pos * phase + shift;
    const Vec3f gaussian = ExpVec(-var * phase * phase);
    Vec3f xyz = CompMul(
        CompMul(val, SqrtVec(2.0f * kPi * var)),
        CompMul(CosVec(phaseVec), gaussian));
    xyz[0] += 9.7470e-14f * std::sqrt(2.0f * kPi * 4.5282e+09f) *
        std::cos(2.2399e+06f * phase + shift[0]) *
        std::exp(-4.5282e+09f * phase * phase);
    return xyz * (1.0f / 1.0685e-7f);
}

static Vec3f
_XYZToRGB(const Vec3f& xyz)
{
    const Vec3f redVec(2.3706743f, -0.9000405f, -0.4706338f);
    const Vec3f grnVec(-0.5138850f, 1.4253036f, 0.0885814f);
    const Vec3f bluVec(0.0052982f, -0.0146949f, 1.0093968f);
    return Vec3f(
        std::max(Dot(redVec, xyz), 0.0f),
        std::max(Dot(grnVec, xyz), 0.0f),
        std::max(Dot(bluVec, xyz), 0.0f));
}

static Vec3f
_ThinFilmAiryReflectance(
    float cosTheta,
    float thinFilmThickness,
    float thinFilmIor,
    const ThinFilmParams& params)
{
    const float clampedCos = Clamp01(cosTheta);
    const float iorIn = 1.0f;
    const float iorThinFilm = std::max(thinFilmIor, iorIn);
    const Vec3f iorSubstrate = (params.model == ThinFilmModel::Schlick)
                                   ? _F0ToIor(params.F0)
                                   : params.ior;
    const Vec3f absorptionIndexSubstrate =
        (params.model == ThinFilmModel::Schlick) ? Vec3f(0.0f)
                                                  : params.extinction;

    const float sinTheta2 = std::max(1.0f - clampedCos * clampedCos, 0.0f);
    const float eta = iorIn / iorThinFilm;
    const float cosThetaTSqr = 1.0f - sinTheta2 * eta * eta;
    const float cosThetaT = (cosThetaTSqr > 0.0f) ? std::sqrt(cosThetaTSqr) : 0.0f;

    Vec2f R12 = FresnelDielectricPolarized(clampedCos, iorThinFilm / iorIn);
    if (cosThetaT <= 0.0f) {
        R12 = Vec2f(1.0f, 1.0f);
    }
    const Vec2f T121 = Vec2f(1.0f, 1.0f) - R12;

    Vec3f R23p(0.0f), R23s(0.0f);
    Vec3f phi23p(0.0f), phi23s(0.0f);
    if (params.model == ThinFilmModel::Schlick) {
        const Vec3f fresnel = GeneralizedSchlickFresnel(
            params.F0, params.F82, params.F90, params.exponent, cosThetaT);
        R23p = fresnel * 0.5f;
        R23s = fresnel * 0.5f;
        phi23p = Vec3f(iorSubstrate[0] < iorThinFilm ? kPi : 0.0f,
                       iorSubstrate[1] < iorThinFilm ? kPi : 0.0f,
                       iorSubstrate[2] < iorThinFilm ? kPi : 0.0f);
        phi23s = phi23p;
    } else {
        _FresnelConductorPolarized(
            cosThetaT, CompDiv(iorSubstrate, Vec3f(iorThinFilm)),
            CompDiv(absorptionIndexSubstrate, Vec3f(iorThinFilm)), &R23p,
            &R23s);
        _FresnelConductorPhasePolarized(cosThetaT, iorThinFilm, iorSubstrate,
                                        absorptionIndexSubstrate, &phi23p,
                                        &phi23s);
    }

    const float cosB = std::cos(std::atan(iorThinFilm / iorIn));
    const Vec2f phi21(clampedCos < cosB ? 0.0f : kPi, kPi);
    const Vec3f r123p = SqrtVec(MaxVec(R23p * R12[0], 0.0f));
    const Vec3f r123s = SqrtVec(MaxVec(R23s * R12[1], 0.0f));

    const float distMeters = thinFilmThickness * 1.0e-9f;
    const float opd = 2.0f * iorThinFilm * cosThetaT * distMeters;

    Vec3f I(0.0f);
    Vec3f Rs = (T121[0] * T121[0]) *
        CompDiv(R23p, Vec3f(1.0f) - R12[0] * R23p);
    I += Vec3f(R12[0]) + Rs;

    Vec3f Cm = Rs - Vec3f(T121[0]);
    for (int m = 1; m <= _kThinFilmAiryIterations; ++m) {
        Cm = CompMul(Cm, r123p);
        I += CompMul(
            Cm,
            2.0f * _EvalSensitivity(
                static_cast<float>(m) * opd,
                static_cast<float>(m) * (phi23p + Vec3f(phi21[0]))));
    }

    Vec3f Rp = (T121[1] * T121[1]) *
        CompDiv(R23s, Vec3f(1.0f) - R12[1] * R23s);
    I += Vec3f(R12[1]) + Rp;

    Cm = Rp - Vec3f(T121[1]);
    for (int m = 1; m <= _kThinFilmAiryIterations; ++m) {
        Cm = CompMul(Cm, r123s);
        I += CompMul(
            Cm,
            2.0f * _EvalSensitivity(
                static_cast<float>(m) * opd,
                static_cast<float>(m) * (phi23s + Vec3f(phi21[1]))));
    }

    return Clamp01(CompMul(_XYZToRGB(I * 0.5f), params.tint));
}

Vec3f
ApplyThinFilm(
    const Vec3f& baseReflectance,
    float cosTheta,
    float thinFilmWeight,
    float thinFilmThickness,
    float thinFilmIor,
    const ThinFilmParams& params)
{
    if (!HasThinFilm(thinFilmWeight, thinFilmThickness, thinFilmIor)) {
        return baseReflectance;
    }

    const Vec3f thinFilmReflectance = _ThinFilmAiryReflectance(
        cosTheta, thinFilmThickness, thinFilmIor, params);
    return Clamp01(LerpVec(
        baseReflectance,
        thinFilmReflectance,
        Clamp01(thinFilmWeight)));
}

}  // namespace detail
}  // namespace Bsdf
}  // namespace mxcpp
