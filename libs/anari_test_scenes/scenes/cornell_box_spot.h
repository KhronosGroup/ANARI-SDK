// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "scene.h"

namespace anari {
namespace scenes {

TestScene *sceneCornellBoxSpot(anari::Device d);

struct CornellBoxSpot : public TestScene
{
  CornellBoxSpot(anari::Device d);
  ~CornellBoxSpot();

  anari::World world() override;

  std::vector<ParameterInfo> parameters() override;

  void commit() override;

 private:
  anari::World m_world{nullptr};
};

} // namespace scenes
} // namespace anari