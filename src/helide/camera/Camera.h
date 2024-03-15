// Copyright 2022-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../Object.h"

namespace helide {

struct Camera : public Object
{
  Camera(HelideGlobalState *s);
  ~Camera() override = default;

  virtual void commit() override;

  static Camera *createInstance(
      std::string_view type, HelideGlobalState *state);

  virtual Ray createRay(const float2 &screen) const = 0;

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

} // namespace helide

HELIDE_ANARI_TYPEFOR_SPECIALIZATION(helide::Camera *, ANARI_CAMERA);
