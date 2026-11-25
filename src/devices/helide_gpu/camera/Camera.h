// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../HelideGPUMath.h"
#include "../Object.h"
#include "ClipRange.h"
// std
#include <optional>

namespace helide_gpu {

struct Camera : public Object
{
  Camera(HelideGPUDeviceGlobalState *s);
  ~Camera() override = default;
  static Camera *createInstance(
      std::string_view type, HelideGPUDeviceGlobalState *state);

  // the parameters common to both cameras: position, direction, up, near, far
  void commitParameters() override;

  // whether the projection is perspective (else orthographic)
  virtual bool perspective() const = 0;
  mat4 viewMatrix() const;
  // 'sceneBounds': the world-space bounds of everything the frame draws, to
  // which the near and far clip planes are fitted (fitClipRange())
  virtual mat4 projMatrix(float frameAspect, const box3 &sceneBounds) const = 0;

 protected:
  // the clip range fitted to 'sceneBounds'
  ClipRange clipRange(const box3 &sceneBounds) const;

  vec3 m_position{0.f, 0.f, 0.f};
  vec3 m_direction{0.f, 0.f, -1.f};
  vec3 m_up{0.f, 1.f, 0.f};
  // the application's clip distances, unset when it gave none (or invalid
  // ones)
  std::optional<float> m_near;
  std::optional<float> m_far;
};

} // namespace helide_gpu

HELIDE_GPU_ANARI_TYPEFOR_SPECIALIZATION(helide_gpu::Camera *, ANARI_CAMERA);
