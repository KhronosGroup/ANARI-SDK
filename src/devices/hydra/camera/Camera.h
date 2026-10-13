// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"
#include "helium/helium_math.h"
#include "scene/Bounds.h"
// pxr
#include <pxr/base/gf/matrix4d.h>

namespace anari_hydra {

// ANARI camera -> camera sprim (SPEC §4.1). v1 slice drives the task
// controller's free camera via view/projection matrices.
struct Camera : public Object
{
  Camera(HydraGlobalState *s);
  ~Camera() override = default;

  static Camera *createInstance(std::string_view subtype, HydraGlobalState *s);

  void commitParameters() override;
  void finalize() override;

  pxr::GfMatrix4d viewMatrix() const;
  pxr::GfMatrix4d projectionMatrix(
      double frameAspect, const Bounds &bounds) const;
  virtual bool isOrthographic() const
  {
    return false;
  }

 protected:
  virtual double imageHeight() const = 0;
  float m_aspect{0.f};
  helium::float2 m_clippingRange{0.f, 0.f};
  helium::float4 m_imageRegion{0.f, 0.f, 1.f, 1.f};
  helium::float3 m_position{0.f, 0.f, 0.f};
  helium::float3 m_direction{0.f, 0.f, -1.f};
  helium::float3 m_up{0.f, 1.f, 0.f};
};

} // namespace anari_hydra

namespace anari {
ANARI_TYPEFOR_SPECIALIZATION(anari_hydra::Camera *, ANARI_CAMERA);
}
