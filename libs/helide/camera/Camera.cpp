// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Camera.h"
// specific types
#include "Orthographic.h"
#include "Perspective.h"

namespace helide {

Camera::Camera(HelideGlobalState *s) : Object(ANARI_CAMERA, s) {}

Camera *Camera::createInstance(std::string_view type, HelideGlobalState *s)
{
  if (type == "perspective")
    return new Perspective(s);
  else if (type == "orthographic")
    return new Orthographic(s);
  else
    return (Camera *)new UnknownObject(ANARI_CAMERA, s);
}

void Camera::commit()
{
  m_pos = getParam<float3>("position", float3(0.f));
  m_dir = normalize(getParam<float3>("direction", float3(0.f, 0.f, 1.f)));
  m_up = normalize(getParam<float3>("up", float3(0.f, 1.f, 0.f)));
  m_imageRegion = float4(0.f, 0.f, 1.f, 1.f);
  getParam("imageRegion", ANARI_FLOAT32_BOX2, &m_imageRegion);
  markUpdated();
}

} // namespace helide

HELIDE_ANARI_TYPEFOR_DEFINITION(helide::Camera *);
