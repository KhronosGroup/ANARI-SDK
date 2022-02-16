// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "scene.h"

namespace anari {
namespace scenes {

TestScene *sceneCornellBoxHDRI(anari::Device d);

struct CornellBoxHDRI : public TestScene
{
  CornellBoxHDRI(anari::Device d);
  ~CornellBoxHDRI();

  anari::World world() override;

  void commit() override;

 private:
  anari::World m_world{nullptr};
};

} // namespace scenes
} // namespace anari