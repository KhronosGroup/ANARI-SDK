// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Camera.h"
// specific types
#include "Orthographic.h"
#include "Perspective.h"

namespace helide {

Camera::Camera(HelideGlobalState *s) : Object(ANARI_CAMERA, s)
{
  s->objectCounts.cameras++;
}

Camera::~Camera()
{
  deviceState()->objectCounts.cameras--;
}

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
  markUpdated();
}

} // namespace helide

HELIDE_ANARI_TYPEFOR_DEFINITION(helide::Camera *);
