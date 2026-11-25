// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: LicenseRef-TOST-1.0
//
// The hero-wavelength conversions (HeroWavelength.isph), in their own file
// compiled without fused multiply-adds (--opt=disable-fma, CMakeLists.txt), so
// that they round as the C++ they port does (x86-64 builds have no FMA).
//
// Ported from Typhoon (NVIDIA-Omniverse/OpenUSD typhoon/main at 70c45e83b,
// pxr/imaging/plugin/hdEmbree/renderer/materials/MaterialXCpp/spectral.h:
// mxcpp::Spectral::_LookupIndex(), RgbToSpectralValue() and
// _SpectralValueToRgbUnbalanced() for linear Rec. 709, and SpectralValueToRgb()),
// Copyright 2026 Pixar, Tomorrow Open Source Technology License 1.0; see
// external/typhoon/LICENSE.txt. Its tables, adapted from OSL's libbsdl
// (BSD-3-Clause), are read in place (SpectralTables.cpp). Keep in step with
// spectral.h when the pin in external/typhoon/PROVENANCE.md moves.

#include "renderer/pathtracer/HeroWavelength.isph"

// _LookupIndex(): the table entry nearest 'wavelengthNm', clamped to the
// domain; halfway cases round away from zero, as std::round()
static inline int heroLookupIndex(float wavelengthNm)
{
  const float clamped =
      clamp(wavelengthNm, HERO_LAMBDA_MIN_NM, HERO_LAMBDA_MAX_NM);
  const float shifted = (clamped - HERO_LAMBDA_MIN_NM) / HERO_LAMBDA_STEP_NM;
  // shifted is at least 0 (or NaN, which std::round() keeps and the cast
  // below turns into an unspecified int, clamped)
  float rounded = floor(shifted);
  if (shifted - rounded >= 0.5f)
    rounded = rounded + 1.f;
  return clamp((int)rounded, 0, HERO_LAMBDA_RESOLUTION - 1);
}

static inline float3 heroTable3(const uniform float *uniform table, int i)
{
#pragma ignore warning(perf)
  return make_float3(table[3 * i], table[3 * i + 1], table[3 * i + 2]);
}

float HeroWavelength_rgbToSpectral(float3 rgb, float wavelengthNm)
{
  const float3 basis =
      heroTable3(HeroWavelength_srgbBasisTable, heroLookupIndex(wavelengthNm));
  return max(basis.x * rgb.x + basis.y * rgb.y + basis.z * rgb.z, 0.f);
}

float3 HeroWavelength_spectralToRgb(
    float value, float wavelengthNm, float wavelengthPdf)
{
  if (value <= 0.f || wavelengthPdf <= 0.f)
    return make_float3(0.f);

  const int index = heroLookupIndex(wavelengthNm);
#pragma ignore warning(perf)
  const float illuminant = HeroWavelength_d65Table[index];
  const uniform float invNorm = 1.f / max(*HeroWavelength_d65Luminance, 1.0e-8f);
  const float scale = illuminant * value * invNorm / wavelengthPdf;
  const float3 response = heroTable3(HeroWavelength_xyzResponseTable, index);
  const float x = response.x * scale;
  const float y = response.y * scale;
  const float z = response.z * scale;

  const float r = max(3.2405f * x - 1.5371f * y - 0.4985f * z, 0.f);
  const float g = max(-0.9693f * x + 1.8760f * y + 0.0416f * z, 0.f);
  const float b = max(0.0556f * x - 0.2040f * y + 1.0572f * z, 0.f);
  // balanced, so that a white spectrum averages to white
  return make_float3(r * HeroWavelength_neutralScale[0],
      g * HeroWavelength_neutralScale[1],
      b * HeroWavelength_neutralScale[2]);
}
