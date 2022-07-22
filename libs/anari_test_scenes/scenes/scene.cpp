// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "scene.h"
// std
#include <cstring>

namespace anari {
namespace scenes {

TestScene::TestScene(anari::Device device) : m_device(device)
{
  anari::retain(m_device, m_device);
}

TestScene::~TestScene()
{
  anari::release(m_device, m_device);
}

Camera TestScene::createDefaultCameraFromWorld(anari::World w)
{
  box3 bounds;

  anari::getProperty(m_device, w, "bounds", bounds);

  const glm::vec3 bounds_size = bounds[1] - bounds[0];
  const glm::vec3 bounds_center = 0.5f * (bounds[0] + bounds[1]);
  const float distance = glm::length(bounds_size) * 0.8f;

  const glm::vec3 eye_pos = bounds_center + glm::vec3(0, 0, -distance);

  Camera cam;

  cam.position = eye_pos;
  cam.direction = glm::normalize(bounds_center - eye_pos);
  cam.at = bounds_center;
  cam.up = glm::vec3(0, 1, 0);

  return cam;
}

box3 TestScene::bounds()
{
  box3 retval;
  anari::getProperty(m_device, world(), "bounds", retval);
  return retval;
}

std::vector<Camera> TestScene::cameras()
{
  return {createDefaultCameraFromWorld(world())};
}

std::vector<ParameterInfo> TestScene::parameters()
{
  return {};
}

bool TestScene::animated() const
{
  return false;
}

void TestScene::computeNextFrame()
{
  // no-op
}

void TestScene::setDefaultLight(anari::World w)
{
  auto light = anari::newObject<anari::Light>(m_device, "directional");
  anari::setParameter(m_device, light, "direction", glm::vec3(0, -1, 0));
  anari::setParameter(m_device, light, "irradiance", 4.f);
  anari::commitParameters(m_device, light);
  anari::setAndReleaseParameter(
      m_device, w, "light", anari::newArray1D(m_device, &light));
  anari::release(m_device, light);
}

} // namespace scenes

ANARI_TYPEFOR_DEFINITION(scenes::box3);

} // namespace anari