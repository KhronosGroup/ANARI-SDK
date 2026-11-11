// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"

namespace helide_gpu {

// The light types the surface shaders know (shaders/shading.glsl LIGHT_*)
enum class LightType : int32_t
{
  DIRECTIONAL = 0,
  POINT = 1,
  SPOT = 2,
};

// A directional, point or spot light (KHR_LIGHT_DIRECTIONAL, _POINT, _SPOT)
struct Light : public Object
{
  Light(HelideGPUDeviceGlobalState *d, LightType type);
  ~Light() override = default;
  static Light *createInstance(
      std::string_view subtype, HelideGPUDeviceGlobalState *d);

  void commitParameters() override;

  LightType type() const;
  // Light-local position and emission direction (normalized)
  vec3 position() const;
  vec3 direction() const;
  // color times irradiance (directional) or intensity (point, spot)
  vec3 radiance() const;
  // Spot cone: cosines of the half opening angle and of where the falloff
  // starts
  float cosOuterAngle() const;
  float cosInnerAngle() const;

 private:
  LightType m_type{LightType::DIRECTIONAL};
  vec3 m_position{0.f};
  vec3 m_direction{0.f, 0.f, -1.f};
  vec3 m_radiance{1.f};
  float m_cosOuterAngle{-1.f};
  float m_cosInnerAngle{-1.f};
};

} // namespace helide_gpu

HELIDE_GPU_ANARI_TYPEFOR_SPECIALIZATION(helide_gpu::Light *, ANARI_LIGHT);
