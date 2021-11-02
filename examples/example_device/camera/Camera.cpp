// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Camera.h"
// specific types
#include "Perspective.h"

namespace anari {
namespace example_device {

static FactoryMapPtr<Camera> g_cameras;

static void init()
{
  g_cameras = std::make_unique<FactoryMap<Camera>>();

  g_cameras->emplace(
      "perspective", []() -> Camera * { return new Perspective; });
}

Camera *Camera::createInstance(const char *type)
{
  if (g_cameras.get() == nullptr)
    init();

  auto *fcn = (*g_cameras)[type];

  if (fcn)
    return fcn();
  else {
    throw std::runtime_error("could not create camera");
  }
}

void Camera::commit()
{
  m_pos = getParam<vec3>("position", vec3(0.f));
  m_dir = normalize(getParam<vec3>("direction", vec3(0.f, 0.f, 1.f)));
  m_up = normalize(getParam<vec3>("up", vec3(0.f, 1.f, 0.f)));
  markUpdated();
}

} // namespace example_device

ANARI_TYPEFOR_DEFINITION(example_device::Camera *);

} // namespace anari
