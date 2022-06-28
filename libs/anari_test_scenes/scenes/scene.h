// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../anari_test_scenes.h"
// anari
#include "anari/anari_cpp/ext/std.h"
#include "anari/backend/utilities/ParameterizedObject.h"

namespace anari {
namespace scenes {

using box3 = std::array<glm::vec3, 2>; // bounds_lower, bounds_upper;

struct TestScene : public ParameterizedObject
{
  virtual anari::World world() = 0;
  virtual box3 bounds();
  virtual std::vector<Camera> cameras();
  virtual std::vector<ParameterInfo> parameters();

  virtual void commit() = 0;

  virtual bool animated() const;
  virtual void computeNextFrame();

  virtual ~TestScene();

 protected:
  TestScene(anari::Device device);

  Camera createDefaultCameraFromWorld(anari::World);
  void setDefaultLight(anari::World);

  anari::Device m_device{nullptr};
};

} // namespace scenes

ANARI_TYPEFOR_SPECIALIZATION(scenes::box3, ANARI_FLOAT32_BOX3);

} // namespace anari