// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "scene.h"

namespace anari {
namespace scenes {

TestScene *sceneCornellBoxCone(anari::Device d);

struct CornellBoxCone : public TestScene
{
  CornellBoxCone(anari::Device d);
  ~CornellBoxCone();

  anari::World world() override;

  void commit() override;

 private:
  anari::World m_world{nullptr};
};

} // namespace scenes
} // namespace anari