// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Camera.h"
#include "OrthographicCamera.h"
#include "PerspectiveCamera.h"

namespace helide_gpu {

Camera::Camera(HelideGPUDeviceGlobalState *s) : Object(ANARI_CAMERA, s) {}

Camera *Camera::createInstance(
    std::string_view type, HelideGPUDeviceGlobalState *s)
{
  if (type == "perspective")
    return new PerspectiveCamera(s);
  if (type == "orthographic")
    return new OrthographicCamera(s);
  return (Camera *)new UnknownObject(ANARI_CAMERA, s);
}

} // namespace helide_gpu

HELIDE_GPU_ANARI_TYPEFOR_DEFINITION(helide_gpu::Camera *);
