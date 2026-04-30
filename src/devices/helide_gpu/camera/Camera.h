// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../HelideGPUMath.h"
#include "../Object.h"

namespace helide_gpu {

struct Camera : public Object
{
  Camera(HelideGPUDeviceGlobalState *s);
  ~Camera() override = default;
  static Camera *createInstance(
      std::string_view type, HelideGPUDeviceGlobalState *state);

  virtual mat4 viewMatrix() const = 0;
  virtual mat4 projMatrix(float frameAspect) const = 0;
};

} // namespace helide_gpu

HELIDE_GPU_ANARI_TYPEFOR_SPECIALIZATION(helide_gpu::Camera *, ANARI_CAMERA);
