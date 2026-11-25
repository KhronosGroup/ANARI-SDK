// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: LicenseRef-TOST-1.0
//
// Homogeneous media (Medium.isph), in their own file compiled without fused
// multiply-adds and with the system's math library (--opt=disable-fma
// --math-lib=system, CMakeLists.txt), so that they round as the C++ they port
// does (x86-64 builds have no FMA, and call the same expf(), logf(), sinf()
// and cosf()): free flights and phase samples decide where a path goes.
//
// Ported from Typhoon (NVIDIA-Omniverse/OpenUSD typhoon/main at 70c45e83b,
// pxr/imaging/plugin/hdEmbree/renderer/integrator/: medium.cpp, and the
// weights of ty::Renderer::_TraceVolumeTransmission() in volumeTransport.cpp),
// Copyright 2018-2026 Pixar, Tomorrow Open Source Technology License 1.0;
// see external/typhoon/LICENSE.txt. Keep in step with those files when the
// pin in external/typhoon/PROVENANCE.md moves.
//
// Not ported: Typhoon's Adobe OpenPBR volume transport (its
// MediumTransportModel::AdobeOpenPBR, only with its useAdobeOpenPBR setting,
// which halcyon never sets). The weights here are RGB; a path in
// hero-wavelength mode takes them at its wavelength (HeroWavelength.isph).

#include "renderer/pathtracer/CppRounding.isph"
#include "renderer/pathtracer/Medium.isph"

// medium.cpp's _kEpsilon and _kPi, volumeTransport.cpp's _volumePdfEps
#define MEDIUM_EPSILON 1.0e-6f
#define MEDIUM_PI 3.14159265358979323846f
#define MEDIUM_PDF_EPSILON 1.0e-20f

static inline float mediumInfinity()
{
  return floatbits(0x7f800000);
}

static inline float mediumComponent(float3 v, int i)
{
  return i == 0 ? v.x : (i == 1 ? v.y : v.z);
}

// _ClampNonNegative()
static inline float3 mediumClampNonNegative(float3 v)
{
  return make_float3(max(v.x, 0.f), max(v.y, 0.f), max(v.z, 0.f));
}

// _SafeNormalized()
static inline float3 mediumSafeNormalized(float3 v, float3 fallback)
{
  if (cppDot(v, v) <= MEDIUM_EPSILON * MEDIUM_EPSILON)
    return fallback;
  return imathNormalize(v);
}

// _CoordinateSystem()
static inline void mediumCoordinateSystem(float3 zAxis, float3 &x, float3 &y)
{
  const float3 z = mediumSafeNormalized(zAxis, make_float3(0.f, 0.f, 1.f));
  if (abs(z.z) < 0.999f)
    x = make_float3(-z.y, z.x, 0.f);
  else
    x = cppCross(make_float3(0.f, 1.f, 0.f), z);
  if (cppDot(x, x) <= MEDIUM_EPSILON * MEDIUM_EPSILON)
    x = make_float3(1.f, 0.f, 0.f);
  else
    x = imathNormalize(x);
  y = cppCross(z, x);
  if (cppDot(y, y) <= MEDIUM_EPSILON * MEDIUM_EPSILON)
    y = make_float3(0.f, 1.f, 0.f);
  else
    y = imathNormalize(y);
}

// PhaseHG()
static inline float mediumPhaseHG(float cosTheta, float anisotropy)
{
  const float g = clamp(anisotropy, -0.999f, 0.999f);
  const float denominator = 1.f + g * g + 2.f * g * cosTheta;
  return (1.f - g * g) / (4.f * MEDIUM_PI * denominator * sqrt(denominator));
}

float3 Medium_albedo(const varying HomogeneousMedium &m)
{
  const float3 extinction = Medium_extinction(m);
  float3 albedo = make_float3(0.f);
  if (extinction.x > 1.0e-6f)
    albedo.x = clamp(m.scattering.x / extinction.x, 0.f, 1.f);
  if (extinction.y > 1.0e-6f)
    albedo.y = clamp(m.scattering.y / extinction.y, 0.f, 1.f);
  if (extinction.z > 1.0e-6f)
    albedo.z = clamp(m.scattering.z / extinction.z, 0.f, 1.f);
  return albedo;
}

float3 Medium_evalBeerTransmittance(
    const varying HomogeneousMedium &m, float dist)
{
  if (dist <= 0.f || Medium_isVacuum(m))
    return make_float3(1.f);
  const float3 extinction = Medium_extinction(m);
  return make_float3(exp(-extinction.x * dist),
      exp(-extinction.y * dist),
      exp(-extinction.z * dist));
}

float Medium_sampleFreeFlightChannel(
    const varying HomogeneousMedium &m, int channel, float u)
{
  const float extinction =
      mediumComponent(Medium_extinction(m), clamp(channel, 0, 2));
  if (extinction <= MEDIUM_EPSILON)
    return mediumInfinity();
  const float uClamped = clamp(u, MEDIUM_EPSILON, 1.f - MEDIUM_EPSILON);
  return -log(1.f - uClamped) / extinction;
}

int Medium_channelMIS(
    float3 throughput, float3 weights, float u1, varying float3 &pdfChannel)
{
  const float3 raw = make_float3(max(throughput.x * weights.x, 0.f),
      max(throughput.y * weights.y, 0.f),
      max(throughput.z * weights.z, 0.f));
  const float sum = raw.x + raw.y + raw.z;
  if (sum <= 1.0e-12f) {
    pdfChannel = make_float3(1.f / 3.f);
    const float scaled = clamp(u1 * 3.f, 0.f, 3.f - 1.0e-6f);
    return (int)scaled;
  }
  pdfChannel = raw * (1.f / sum);
  const float u01 = clamp(u1, 0.f, 1.f - 1.0e-6f);
  if (u01 < pdfChannel.x)
    return 0;
  if (u01 < pdfChannel.x + pdfChannel.y)
    return 1;
  return 2;
}

float3 Medium_sampleHenyeyGreenstein(
    float3 wo, float anisotropy, float u1, float u2)
{
  const float g = clamp(anisotropy, -0.999f, 0.999f);
  const float sampleU1 = clamp(u1, MEDIUM_EPSILON, 1.f - MEDIUM_EPSILON);
  const float sampleU2 = clamp(u2, 0.f, 1.f);

  float cosTheta = 0.f;
  if (abs(g) < 1.0e-3f) {
    cosTheta = 1.f - 2.f * sampleU1;
  } else {
    const float sqrTerm = (1.f - g * g) / (1.f - g + 2.f * g * sampleU1);
    cosTheta = (1.f + g * g - sqrTerm * sqrTerm) / (2.f * g);
    cosTheta = clamp(cosTheta, -1.f, 1.f);
  }

  const float sinTheta = sqrt(max(0.f, 1.f - cosTheta * cosTheta));
  const float phi = 2.f * MEDIUM_PI * sampleU2;
  const float sinPhi = sin(phi);
  const float cosPhi = cos(phi);

  const float3 zAxis = mediumSafeNormalized(-wo, make_float3(0.f, 0.f, 1.f));
  float3 xAxis, yAxis;
  mediumCoordinateSystem(zAxis, xAxis, yAxis);

  const float3 wi = xAxis * (sinTheta * cosPhi) + yAxis * (sinTheta * sinPhi)
      + zAxis * cosTheta;
  if (cppDot(wi, wi) <= MEDIUM_EPSILON * MEDIUM_EPSILON)
    return zAxis;
  return imathNormalize(wi);
}

float Medium_pdfHenyeyGreenstein(float3 wi, float3 wo, float anisotropy)
{
  const float3 a = mediumSafeNormalized(wi, make_float3(0.f, 0.f, 1.f));
  const float3 b = mediumSafeNormalized(wo, make_float3(0.f, 0.f, 1.f));
  return mediumPhaseHG(cppDot(a, b), anisotropy);
}

// _TraceVolumeTransmission() /////////////////////////////////////////////////

void Medium_beginTrack(const varying HomogeneousMedium &m,
    float3 throughput,
    float uChannel,
    float uFreeFlight,
    varying MediumTrack &track)
{
  track.absorbingOnly = Medium_isAbsorbingOnly(m);
  track.channel = 0;
  track.channelPdf = make_float3(0.f);
  track.collision = mediumInfinity();
  if (track.absorbingOnly)
    return;
  // Chiang's channel MIS: track one RGB channel, weigh every channel against
  // the mixture pdf
  track.channel = Medium_channelMIS(
      throughput, Medium_albedo(m), uChannel, track.channelPdf);
  track.collision =
      Medium_sampleFreeFlightChannel(m, track.channel, uFreeFlight);
}

float3 Medium_scatterWeight(const varying HomogeneousMedium &m,
    const varying MediumTrack &track,
    float dist)
{
  const float3 transmittance = Medium_evalBeerTransmittance(m, dist);
  const float3 pdf = Medium_extinction(m) * transmittance;
  const float3 contribution = m.scattering * transmittance;
  const float denominator = cppDot(track.channelPdf, pdf);
  if (!isfinite(denominator) || denominator <= MEDIUM_PDF_EPSILON)
    return make_float3(0.f);
  return contribution * (1.f / denominator);
}

float3 Medium_transmittanceWeight(const varying HomogeneousMedium &m,
    const varying MediumTrack &track,
    float dist)
{
  const float3 transmittance = Medium_evalBeerTransmittance(m, dist);
  if (track.absorbingOnly)
    return transmittance;
  const float denominator = cppDot(track.channelPdf, transmittance);
  if (!isfinite(denominator) || denominator <= MEDIUM_PDF_EPSILON)
    return make_float3(0.f);
  return transmittance * (1.f / denominator);
}
