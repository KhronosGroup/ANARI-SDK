//
// hdEmbree medium helpers shared by renderer and MaterialXCpp.
//
#include "medium.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace mxcpp {

float PhaseHG(float cosTheta, float anisotropy) noexcept;

namespace {

constexpr float _kEpsilon = 1.0e-6f;
constexpr float _kPi = 3.14159265358979323846f;

Vec3f
_ClampNonNegative(const Vec3f& value) noexcept
{
    return Vec3f(
        std::max(value[0], 0.0f),
        std::max(value[1], 0.0f),
        std::max(value[2], 0.0f));
}

Vec3f
_SafeNormalized(const Vec3f& value, const Vec3f& fallback) noexcept
{
    if (value.length2() <= _kEpsilon * _kEpsilon) {
        return fallback;
    }
    Vec3f normalized = value;
    normalized.normalize();
    return normalized;
}

void
_CoordinateSystem(
    const Vec3f& zAxis,
    Vec3f* xAxis,
    Vec3f* yAxis) noexcept
{
    const Vec3f z = _SafeNormalized(zAxis, Vec3f(0.0f, 0.0f, 1.0f));
    if (std::abs(z[2]) < 0.999f) {
        *xAxis = Vec3f(-z[1], z[0], 0.0f);
    } else {
        *xAxis = Vec3f(0.0f, 1.0f, 0.0f).cross(z);
    }
    if (xAxis->length2() <= _kEpsilon * _kEpsilon) {
        *xAxis = Vec3f(1.0f, 0.0f, 0.0f);
    } else {
        xAxis->normalize();
    }
    *yAxis = z.cross(*xAxis);
    if (yAxis->length2() <= _kEpsilon * _kEpsilon) {
        *yAxis = Vec3f(0.0f, 1.0f, 0.0f);
    } else {
        yAxis->normalize();
    }
}

}  // namespace

Vec3f
EvalBeerTransmittance(const MediumProperties& medium, float distance) noexcept
{
    if (distance <= 0.0f || medium.IsVacuum()) {
        return Vec3f(1.0f);
    }

    const Vec3f extinction = medium.Extinction();
    return Vec3f(std::exp(-extinction[0] * distance),
                 std::exp(-extinction[1] * distance),
                 std::exp(-extinction[2] * distance));
}

float
MajorantExtinction(const MediumProperties& medium) noexcept
{
    const Vec3f extinction = medium.Extinction();
    return std::max({extinction[0], extinction[1], extinction[2], 0.0f});
}

Vec3f
EvalMajorantTransmittanceWeight(
    const MediumProperties& medium,
    float distance) noexcept
{
    if (distance <= 0.0f || medium.IsVacuum()) {
        return Vec3f(1.0f);
    }

    const float extinctionMajorant = MajorantExtinction(medium);
    if (extinctionMajorant <= _kEpsilon) {
        return Vec3f(1.0f);
    }

    const Vec3f extinction = medium.Extinction();
    return Vec3f(std::exp((extinctionMajorant - extinction[0]) * distance),
                 std::exp((extinctionMajorant - extinction[1]) * distance),
                 std::exp((extinctionMajorant - extinction[2]) * distance));
}

Vec3f
EvalFreeFlightScatterWeight(
    const MediumProperties& medium,
    float distance) noexcept
{
    const float extinctionMajorant = MajorantExtinction(medium);
    if (extinctionMajorant <= _kEpsilon) {
        return Vec3f(0.0f);
    }

    const Vec3f transmittanceWeight =
        EvalMajorantTransmittanceWeight(medium, distance);
    const Vec3f extinctionMajorantVector(extinctionMajorant);
    return CompMul(transmittanceWeight,
                   CompDiv(_ClampNonNegative(medium.scattering),
                           extinctionMajorantVector));
}

float
SampleFreeFlight(const MediumProperties& medium, float u1) noexcept
{
    const float extinctionMajorant = MajorantExtinction(medium);
    if (extinctionMajorant <= _kEpsilon) {
        return std::numeric_limits<float>::infinity();
    }

    const float u1Clamped = std::clamp(u1, _kEpsilon, 1.0f - _kEpsilon);
    return -std::log(1.0f - u1Clamped) / extinctionMajorant;
}

float
PhaseHG(float cosTheta, float anisotropy) noexcept
{
    const float anisotropyClamped = std::clamp(anisotropy, -0.999f, 0.999f);
    const float denominator = 1.0f + anisotropyClamped * anisotropyClamped +
                              2.0f * anisotropyClamped * cosTheta;
    return (1.0f - anisotropyClamped * anisotropyClamped) /
           (4.0f * _kPi * denominator * std::sqrt(denominator));
}

Vec3f
SampleHenyeyGreenstein(const Vec3f& omegaOutWld, float anisotropy, float u1,
                       float u2) noexcept
{
    const float anisotropyClamped = std::clamp(anisotropy, -0.999f, 0.999f);
    const float sampleU1 = std::clamp(u1, _kEpsilon, 1.0f - _kEpsilon);
    const float sampleU2 = std::clamp(u2, 0.0f, 1.0f);

    float cosTheta = 0.0f;
    if (std::abs(anisotropyClamped) < 1.0e-3f) {
        cosTheta = 1.0f - 2.0f * sampleU1;
    } else {
        const float sqrTerm =
            (1.0f - anisotropyClamped * anisotropyClamped) /
            (1.0f - anisotropyClamped + 2.0f * anisotropyClamped * sampleU1);
        cosTheta =
            (1.0f + anisotropyClamped * anisotropyClamped - sqrTerm * sqrTerm) /
            (2.0f * anisotropyClamped);
        cosTheta = std::clamp(cosTheta, -1.0f, 1.0f);
    }

    const float sinTheta =
        std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
    const float phi = 2.0f * _kPi * sampleU2;
    const float sinPhi = std::sin(phi);
    const float cosPhi = std::cos(phi);

    const Vec3f zAxis = _SafeNormalized(-omegaOutWld, Vec3f(0.0f, 0.0f, 1.0f));
    Vec3f xAxis(1.0f, 0.0f, 0.0f);
    Vec3f yAxis(0.0f, 1.0f, 0.0f);
    _CoordinateSystem(zAxis, &xAxis, &yAxis);

    Vec3f omegaInWld = xAxis * (sinTheta * cosPhi) +
                       yAxis * (sinTheta * sinPhi) + zAxis * cosTheta;
    if (omegaInWld.length2() <= _kEpsilon * _kEpsilon) {
        return zAxis;
    }
    omegaInWld.normalize();
    return omegaInWld;
}

float
PdfHenyeyGreenstein(const Vec3f& omegaInWld, const Vec3f& omegaOutWld,
                    float anisotropy) noexcept
{
    const Vec3f omegaInWldSafe =
        _SafeNormalized(omegaInWld, Vec3f(0.0f, 0.0f, 1.0f));
    const Vec3f omegaOutWldSafe =
        _SafeNormalized(omegaOutWld, Vec3f(0.0f, 0.0f, 1.0f));
    return PhaseHG(omegaInWldSafe.dot(omegaOutWldSafe), anisotropy);
}

MediumProperties
MakeTransmissionMedium(
    float transmissionWeight,
    const Vec3f& transmissionColor,
    float transmissionDepth,
    const Vec3f& transmissionScatter,
    float transmissionScatterAnisotropy) noexcept
{
    MediumProperties medium;
    if (transmissionWeight <= 0.0f || transmissionDepth <= _kEpsilon) {
        return medium;
    }

    const Vec3f safeColor(
        std::clamp(transmissionColor[0], _kEpsilon, 1.0f),
        std::clamp(transmissionColor[1], _kEpsilon, 1.0f),
        std::clamp(transmissionColor[2], _kEpsilon, 1.0f));
    const Vec3f scatter = _ClampNonNegative(transmissionScatter);

    const Vec3f extinction(
        -std::log(safeColor[0]) / transmissionDepth,
        -std::log(safeColor[1]) / transmissionDepth,
        -std::log(safeColor[2]) / transmissionDepth);
    medium.scattering = scatter / transmissionDepth;
    Vec3f absorption(extinction[0] - medium.scattering[0],
                     extinction[1] - medium.scattering[1],
                     extinction[2] - medium.scattering[2]);

    // Match the MaterialX volume graph: if scattering pushes any absorption
    // channel below zero, shift the full absorption vector so the minimum
    // lands on zero instead of clamping channels independently.
    const float minAbsorption = std::min(
        {absorption[0], absorption[1], absorption[2]});
    if (minAbsorption < 0.0f) {
        absorption -= Vec3f(minAbsorption);
    }

    medium.absorption = _ClampNonNegative(absorption);
    medium.anisotropy = std::clamp(transmissionScatterAnisotropy, -1.0f, 1.0f);
    return medium;
}

int
MinExtinctionChannel(const MediumProperties& medium) noexcept
{
    const Vec3f extinction = medium.Extinction();
    int indexChannelMinimum = 0;
    float extinctionMinimum = extinction[0];
    for (int indexChannel = 1; indexChannel < 3; ++indexChannel) {
        if (extinction[indexChannel] > _kEpsilon &&
            extinction[indexChannel] < extinctionMinimum) {
            extinctionMinimum = extinction[indexChannel];
            indexChannelMinimum = indexChannel;
        }
    }
    return indexChannelMinimum;
}

float
SampleFreeFlightChannel(const MediumProperties& medium, int channel,
                        float u1) noexcept
{
    const float extinction = medium.Extinction()[std::clamp(channel, 0, 2)];
    if (extinction <= _kEpsilon) {
        return std::numeric_limits<float>::infinity();
    }
    const float u1Clamped = std::clamp(u1, _kEpsilon, 1.0f - _kEpsilon);
    return -std::log(1.0f - u1Clamped) / extinction;
}

Vec3f
EvalChannelScatterWeight(
    const MediumProperties& medium,
    int channel,
    float distance) noexcept
{
    const Vec3f extinction = medium.Extinction();
    const float extinctionTracking = extinction[std::clamp(channel, 0, 2)];
    if (extinctionTracking <= _kEpsilon) {
        return Vec3f(0.0f);
    }
    const Vec3f scattering = _ClampNonNegative(medium.scattering);
    return Vec3f(scattering[0] / extinctionTracking *
                     std::exp((extinctionTracking - extinction[0]) * distance),
                 scattering[1] / extinctionTracking *
                     std::exp((extinctionTracking - extinction[1]) * distance),
                 scattering[2] / extinctionTracking *
                     std::exp((extinctionTracking - extinction[2]) * distance));
}

Vec3f
EvalChannelTransmittanceWeight(
    const MediumProperties& medium,
    int channel,
    float distance) noexcept
{
    if (distance <= 0.0f) {
        return Vec3f(1.0f);
    }
    const Vec3f extinction = medium.Extinction();
    const float extinctionTracking = extinction[std::clamp(channel, 0, 2)];
    return Vec3f(std::exp((extinctionTracking - extinction[0]) * distance),
                 std::exp((extinctionTracking - extinction[1]) * distance),
                 std::exp((extinctionTracking - extinction[2]) * distance));
}

int
ChannelMIS(const Vec3f& throughputRgb, const Vec3f& weights, float u1,
           Vec3f* pdfChannel)
{
    Vec3f raw(std::max(throughputRgb[0] * weights[0], 0.0f),
              std::max(throughputRgb[1] * weights[1], 0.0f),
              std::max(throughputRgb[2] * weights[2], 0.0f));
    const float sum = raw[0] + raw[1] + raw[2];

    if (sum <= 1.0e-12f) {
        // Uniform fallback.
        *pdfChannel = Vec3f(1.0f / 3.0f);
        const float scaled = std::clamp(u1 * 3.0f, 0.0f, 3.0f - 1.0e-6f);
        return static_cast<int>(scaled);
    }

    *pdfChannel = raw * (1.0f / sum);
    const float u01 = std::clamp(u1, 0.0f, 1.0f - 1.0e-6f);
    if (u01 < (*pdfChannel)[0])
        return 0;
    if (u01 < (*pdfChannel)[0] + (*pdfChannel)[1])
        return 1;
    return 2;
}

}  // namespace mxcpp
