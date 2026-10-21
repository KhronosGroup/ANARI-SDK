//
// hdEmbree medium helpers shared by renderer and MaterialXCpp.
//
#ifndef PXR_IMAGING_PLUGIN_HD_EMBREE_MEDIUM_H
#define PXR_IMAGING_PLUGIN_HD_EMBREE_MEDIUM_H

#include <renderer/materials/MaterialXCpp/mathTypes.h>

namespace mxcpp {

enum class MediumTransportModel
{
    HdEmbree,
    AdobeOpenPBR
};

struct AdobeOpenPbrVolumeProperties
{
    Vec3f extinctionCoefficient = Vec3f(0.0f);
    Vec3f albedo = Vec3f(0.0f);
    float anisotropy = 0.0f;
    bool valid = false;
};

struct MediumProperties
{
    Vec3f absorption = Vec3f(0.0f);
    Vec3f scattering = Vec3f(0.0f);
    float anisotropy = 0.0f;
    MediumTransportModel transportModel = MediumTransportModel::HdEmbree;
    AdobeOpenPbrVolumeProperties adobeOpenPbrVolume;

    Vec3f
    Extinction() const noexcept
    {
        return absorption + scattering;
    }

    bool IsVacuum() const noexcept {
        const Vec3f extinction = Extinction();
        return extinction[0] <= 0.0f && extinction[1] <= 0.0f &&
               extinction[2] <= 0.0f;
    }

    bool IsAbsorbingOnly() const noexcept {
        return scattering[0] <= 0.0f && scattering[1] <= 0.0f &&
               scattering[2] <= 0.0f && !IsVacuum();
    }
};

Vec3f EvalBeerTransmittance(
    const MediumProperties& medium,
    float distance) noexcept;

float MajorantExtinction(
    const MediumProperties& medium) noexcept;

Vec3f EvalMajorantTransmittanceWeight(
    const MediumProperties& medium,
    float distance) noexcept;

Vec3f EvalFreeFlightScatterWeight(
    const MediumProperties& medium,
    float distance) noexcept;

float SampleFreeFlight(const MediumProperties& medium, float u1) noexcept;

float PhaseHG(
    float cosTheta,
    float anisotropy) noexcept;

Vec3f SampleHenyeyGreenstein(const Vec3f& omegaOutWld, float anisotropy,
                             float u1, float u2) noexcept;

float PdfHenyeyGreenstein(const Vec3f& omegaInWld, const Vec3f& omegaOutWld,
                          float anisotropy) noexcept;

MediumProperties MakeTransmissionMedium(
    float transmissionWeight,
    const Vec3f& transmissionColor,
    float transmissionDepth,
    const Vec3f& transmissionScatter,
    float transmissionScatterAnisotropy) noexcept;

// --- Per-channel spectral tracking for SSS random walks ---
// Using the majorant (max extinction) for free-flight sampling creates extreme
// variance when channels have very different extinction coefficients (e.g.
// skin).  These functions track by a single channel's extinction instead,
// bounding per-channel weights and eliminating the exponential blow-up.

/// Return the channel index with the minimum non-zero extinction.
int MinExtinctionChannel(
    const MediumProperties& medium) noexcept;

/// Sample a free-flight distance using a specific channel's extinction.
float SampleFreeFlightChannel(const MediumProperties& medium, int channel,
                              float u1) noexcept;

/// Per-channel scatter weight when tracking with a specific channel's
/// extinction (ratio tracking).
Vec3f EvalChannelScatterWeight(
    const MediumProperties& medium,
    int channel,
    float distance) noexcept;

/// Per-channel transmittance weight when tracking with a specific
/// channel's extinction.
Vec3f EvalChannelTransmittanceWeight(
    const MediumProperties& medium,
    int channel,
    float distance) noexcept;

/// Chiang balance heuristic channel selection MIS.
///
/// Selects one of 3 RGB channels with probability proportional to
/// `throughputRgb[i] * weights[i]`.  Returns the selected channel index
/// and writes all channel probabilities to `pdfChannel`.
///
/// If all weights sum to zero, falls back to uniform (1/3 each).
///
/// Used in random walk SSS (Phase 2) to importance-sample color channels.
/// Designed to be reusable by transmission scattering (Phase 4, ticket #404).
int ChannelMIS(const Vec3f& throughputRgb, const Vec3f& weights, float u1,
               Vec3f* pdfChannel);

}  // namespace mxcpp

#endif  // PXR_IMAGING_PLUGIN_HD_EMBREE_MEDIUM_H
