// Copyright 2024-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Camera.h"

namespace hecore {

Camera::Camera(HeCoreDeviceGlobalState *s) : Object(ANARI_CAMERA, s) {}

Camera *Camera::createInstance(
    std::string_view /*type*/, HeCoreDeviceGlobalState *s)
{
  return (Camera *)new UnknownObject(ANARI_CAMERA, s);
}

} // namespace hecore

HECORE_ANARI_TYPEFOR_DEFINITION(hecore::Camera *);
