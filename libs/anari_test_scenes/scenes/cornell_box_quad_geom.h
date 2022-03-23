// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "scene.h"

namespace anari {
namespace scenes {

TestScene *sceneCornellBoxQuadGeometry(anari::Device d);

struct CornellBoxQuadGeometry : public TestScene
{
  CornellBoxQuadGeometry(anari::Device d);
  ~CornellBoxQuadGeometry();

  anari::World world() override;

  void commit() override;

 private:
  anari::World m_world{nullptr};
};

} // namespace scenes
} // namespace anari