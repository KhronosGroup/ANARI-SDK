// Copyright 2021-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../anari_test_scenes.h"
// anari
#include "anari/anari_cpp/ext/std.h"
// helium
#include "helium/utility/ParameterizedObject.h"
// std
#include <utility>

namespace anari {
namespace scenes {

using box3 = std::array<math::float3, 2>; // bounds_lower, bounds_upper;

struct ANARI_TEST_SCENES_INTERFACE TestScene
    : public helium::ParameterizedObject
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

  Camera createDefaultCameraFromWorld();
  void setDefaultLight(anari::World);

  anari::Device m_device{nullptr};
};

// Inlined helper functions ///////////////////////////////////////////////////

template <typename... Args>
inline helium::ParameterInfo makeParameterInfo(Args &&...args)
{
  return helium::makeParameterInfo(std::forward<Args>(args)...);
}

} // namespace scenes

ANARI_TYPEFOR_SPECIALIZATION(scenes::box3, ANARI_FLOAT32_BOX3);

} // namespace anari