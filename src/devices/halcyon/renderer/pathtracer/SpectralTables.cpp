// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: LicenseRef-TOST-1.0

// The vendored spectral data the pathtracer's hero-wavelength transport
// (HeroWavelength.isph) reads, in place, so that its conversions and the
// C++ materials' use the same numbers: MaterialXCpp's spectral.h tables for
// the linear Rec. 709 working space (the CIE 1931 XYZ response, the D65
// illuminant and the sRGB spectral basis, adapted there from OSL's libbsdl,
// BSD-3-Clause; see THIRD-PARTY.md), and the two constants it derives from
// them once: the D65 illuminant's integrated luminance and the neutral round
// trip scale that brings a white's spectral samples back to white.

// typhoon
#include <renderer/materials/MaterialXCpp/spectral.h>

namespace {

namespace spectral = mxcpp::Spectral;

static_assert(spectral::kLambdaResolution == 81,
    "HeroWavelength.isph's HERO_LAMBDA_RESOLUTION is the tables' size");
static_assert(spectral::kLambdaMinNm == 380.f && spectral::kLambdaMaxNm == 780.f
        && spectral::kLambdaStepNm == 5.f,
    "HeroWavelength.isph's HERO_LAMBDA_* are the tables' domain");
static_assert(sizeof(mxcpp::Vec3f) == 3 * sizeof(float),
    "the tables of Vec3f are read as packed float3s");

const float *const d65Y = []() {
  static const float value = spectral::_IntegrateD65Y();
  return &value;
}();

const float *const neutralScale = []() {
  static const mxcpp::Vec3f value = spectral::_GetNeutralRoundTripScale(
      spectral::RgbColorSpace::LinearRec709);
  return &value.x;
}();

} // namespace

extern "C" {
// HeroWavelength.isph's HeroWavelength_* tables
extern const float *const HeroWavelength_xyzResponseTable;
extern const float *const HeroWavelength_d65Table;
extern const float *const HeroWavelength_srgbBasisTable;
extern const float *const HeroWavelength_d65Luminance;
extern const float *const HeroWavelength_neutralScale;
}

const float *const HeroWavelength_xyzResponseTable =
    &spectral::_GetXyzResponse()[0].x;
const float *const HeroWavelength_d65Table =
    spectral::_GetD65Illuminant().data();
const float *const HeroWavelength_srgbBasisTable =
    &spectral::_GetSrgbBasis()[0].x;
const float *const HeroWavelength_d65Luminance = d65Y;
const float *const HeroWavelength_neutralScale = neutralScale;
