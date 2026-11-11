// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Light.h"
#include "array/Array2D.h"
#include "gpu/GPUTexture.h"

namespace helide_gpu {

// The 'hdri' light (KHR_LIGHT_HDRI): an environment map of 'radiance'
// (W/sr/m^2, times 'scale' and 'color') around the scene at infinity, as
// halcyon's 'preview' renderer treats it. It doesn't light surfaces
// directly: its mean radiance over the sphere adds to the uniform ambient
// light. Camera rays that miss see the sum of the 'visible' hdris in place
// of the background (shaders/hdri.frag).
//
// Mapping (halcyon's and VisRTX's, and so what hdAnari sends for a USD dome
// light): 'up' is kept as given, 'direction' is made perpendicular to it,
// and with left = up x direction the equirectangular 'radiance' array of
// width W and height H covers the sphere with column u = x / W turning from
// -direction (u = 0) through left (0.25), 'direction' (0.5, the center of
// the image) and -left (0.75), and row v = y / H from -up (row 0) to 'up'
// (the last row). The map is looked up unfiltered (nearest texel).
//
// An instance transform turns 'up' and 'direction' (as vectors), and the
// frame is built from the turned vectors, so a non-rigid transform still
// gives an orthonormal frame.
struct Hdri : public Light
{
  Hdri(HelideGPUDeviceGlobalState *s);
  ~Hdri() override;

  void commitParameters() override;
  void finalize() override;
  bool isValid() const override;

  // The orthonormal light frame of 'up' and 'direction' (see above): false
  // if one is zero or not finite or they are parallel
  static bool frame(const vec3 &up,
      const vec3 &direction,
      vec3 &left,
      vec3 &upOut,
      vec3 &directionOut);

  // Light-local 'up' and 'direction' as given
  vec3 up() const;
  vec3 hdriDirection() const;
  // 'color' times 'scale'
  vec3 emission() const;
  // The map's mean radiance over the sphere, each texel weighed by its solid
  // angle (not times emission())
  vec3 meanRadiance() const;
  // The map as an RGBA32F 2D texture, rows in the array's order
  const GPUTexture &texture() const;

 private:
  void gpu_finalize();
  void gpu_freeResources();

  vec3 m_up{0.f, 0.f, 1.f};
  vec3 m_direction{1.f, 0.f, 0.f};
  vec3 m_color{1.f};
  float m_scale{1.f};
  helium::ChangeObserverPtr<Array2D> m_radiance{this};
  bool m_radianceSet{false};

  // set on the GPU thread, which alone reads them
  bool m_valid{false};
  vec3 m_meanRadiance{0.f};
  GPUTexture m_texture;
};

} // namespace helide_gpu
