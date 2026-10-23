// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Light.h"

namespace halcyon {

// The 'hdri' light (KHR_LIGHT_HDRI): an environment map of 'radiance'
// (W/sr/m^2, times 'scale' and 'color') around the scene at infinity,
// sampled as Typhoon's dome light (renderer/pathtracer/Lights.isph). Both
// renderers show the 'visible' ones where camera rays miss, through the
// same lookup (shading/halcyon_hdri.isph); only the pathtracer is lit by
// them.
//
// Mapping (the ANARI spec leaves it open; this is VisRTX's, and so what
// hdAnari sends for a USD dome light): 'up' is kept as given, 'direction'
// is made perpendicular to it, and with left = up x direction the
// equirectangular 'radiance' array of width W and height H covers the
// sphere with column u = x / W turning from -direction (u = 0) through left
// (0.25), 'direction' (0.5, the center of the image) and -left (0.75), and
// row v = y / H from -up (row 0) to 'up' (the last row). Seen from inside,
// the image is not mirrored: an image's top row goes last (hdAnari flips
// files vertically). This is Typhoon's dome light in its light frame
// (X = left, Y = up, Z = direction), whose texture rows run from +Y down, so
// the texels are copied with their rows reversed (Typhoon's order) and
// Typhoon's code runs on them unchanged.
//
// An instance transform turns 'up' and 'direction' (as vectors), and the
// frame is built from the turned vectors, so a non-rigid transform still
// gives an orthonormal frame (Typhoon applies its inverted transform to
// directions and normalizes).
struct Hdri : public Light
{
  Hdri(HalcyonDeviceGlobalState *s);

  void commitParameters() override;
  void finalize() override;
  bool isValid() const override;

  bool writeWorldRecord(void *record, const mat4 &xfm) const override;

  // The orthonormal light frame of 'up' and 'direction' (see above): false
  // if one is zero or not finite or they are parallel
  static bool frame(const float3 &up,
      const float3 &direction,
      float3 &left,
      float3 &upOut,
      float3 &directionOut);

 private:
  float3 m_up{0.f, 0.f, 1.f};
  float3 m_direction{1.f, 0.f, 0.f};
  float m_scale{1.f};
  helium::ChangeObserverPtr<Array2D> m_radiance{this};
  bool m_valid{false};
  // 'color' times 'scale'
  float3 m_emission{1.f};

  // The texels, rows reversed (row 0 towards 'up'), as packed float3s, and
  // Typhoon's sampling distribution over them
  // (BuildDomeLightSamplingDistribution(), Hdri.cpp): texelWeights has
  // W * H entries, conditionalCdf H * (W + 1), marginalCdf H + 1; all empty
  // and weightSum 0 where the map is black (uniform sampling)
  std::vector<float> m_texels;
  std::vector<float> m_texelWeights;
  std::vector<float> m_conditionalCdf;
  std::vector<float> m_marginalCdf;
  float m_weightSum{0.f};
  uint32_t m_width{0};
  uint32_t m_height{0};
  // whether any texel is above 0
  bool m_emits{false};
};

} // namespace halcyon
