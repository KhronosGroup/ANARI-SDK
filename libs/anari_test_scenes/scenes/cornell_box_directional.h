// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "scene.h"

namespace anari {
namespace scenes {

TestScene *sceneCornellBoxDirectional(anari::Device d);

struct CornellBoxDirectional : public TestScene
{
  CornellBoxDirectional(anari::Device d);
  ~CornellBoxDirectional();

  std::vector<ParameterInfo> parameters() override;

  anari::World world() override;

  void commit() override;

 private:
  anari::World m_world{nullptr};
};

} // namespace scenes
} // namespace anari