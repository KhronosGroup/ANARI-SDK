// Copyright 2021-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "scene.h"
// std
#include <cstring>

namespace anari {
namespace scenes {

constexpr float cameraDistanceFactor = 1.0f;

TestScene::TestScene(anari::Device device) : m_device(device)
{
  anari::retain(m_device, m_device);
}

TestScene::~TestScene()
{
  anari::release(m_device, m_device);
}

Camera TestScene::createDefaultCameraFromWorld()
{
  const box3 b = bounds();
  const math::float3 bounds_size = b[1] - b[0];
  const math::float3 bounds_center = 0.5f * (b[0] + b[1]);
  const float distance = math::length(bounds_size) * cameraDistanceFactor;

  const math::float3 eye_pos = bounds_center + math::float3(0, 0, distance);

  Camera cam;

  cam.position = eye_pos;
  cam.direction = math::normalize(bounds_center - eye_pos);
  cam.at = bounds_center;
  cam.up = math::float3(0, 1, 0);

  return cam;
}

box3 TestScene::bounds()
{
  box3 retval = {math::float3(-1), math::float3(1)};
  anari::getProperty(m_device, world(), "bounds", retval);
  return retval;
}

std::vector<Camera> TestScene::cameras()
{
  return {createDefaultCameraFromWorld()};
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
  anari::setParameter(m_device, light, "direction", math::float3(0, -1, 0));
  anari::setParameter(m_device, light, "irradiance", 4.f);
  anari::commitParameters(m_device, light);
  anari::setAndReleaseParameter(
      m_device, w, "light", anari::newArray1D(m_device, &light));
  anari::release(m_device, light);
}

} // namespace scenes

ANARI_TYPEFOR_DEFINITION(scenes::box3);

} // namespace anari