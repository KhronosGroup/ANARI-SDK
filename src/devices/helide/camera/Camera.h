// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../Object.h"

namespace helide {

struct RayGenerator
{
  enum class Mode
  {
    PERSPECTIVE,
    ORTHOGRAPHIC
  };

  Ray createRay(const float2 &screen) const;

  Mode mode{Mode::PERSPECTIVE};
  float3 org{0.f, 0.f, 0.f};
  float3 dir{0.f, 0.f, 1.f};
  float3 du{0.f, 0.f, 0.f};
  float3 dv{0.f, 0.f, 0.f};
  float3 ray_00{0.f, 0.f, 0.f};
};

struct Camera : public Object
{
  Camera(HelideGlobalState *s);
  ~Camera() override = default;

  virtual void commitParameters() override;

  static Camera *createInstance(
      std::string_view type, HelideGlobalState *state);

  virtual RayGenerator createRayGenerator(float frameAspect) const = 0;

  float4 imageRegion() const;

 protected:
  float3 m_pos;
  float3 m_dir;
  float3 m_up;
  float4 m_imageRegion;
};

// Inlined definitions ////////////////////////////////////////////////////////

inline float4 Camera::imageRegion() const
{
  return m_imageRegion;
}

inline Ray RayGenerator::createRay(const float2 &screen) const
{
  Ray ray;
  if (mode == Mode::PERSPECTIVE) {
    ray.org = org;
    ray.dir = normalize(ray_00 + screen.x * du + screen.y * dv);
  } else {
    ray.org = ray_00 + screen.x * du + screen.y * dv;
    ray.dir = dir;
  }
  return ray;
}

} // namespace helide

HELIDE_ANARI_TYPEFOR_SPECIALIZATION(helide::Camera *, ANARI_CAMERA);
