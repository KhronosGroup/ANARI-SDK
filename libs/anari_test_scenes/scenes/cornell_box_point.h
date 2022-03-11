// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "scene.h"

namespace anari {
namespace scenes {

TestScene *sceneCornellBoxPoint(anari::Device d);

struct CornellBoxPoint : public TestScene
{
  CornellBoxPoint(anari::Device d);
  ~CornellBoxPoint();

  anari::World world() override;

  std::vector<ParameterInfo> parameters() override;

  void commit() override;

 private:
  anari::World m_world{nullptr};
};

} // namespace scenes
} // namespace anari