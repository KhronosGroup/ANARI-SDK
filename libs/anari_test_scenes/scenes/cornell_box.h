// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "scene.h"

namespace anari {
namespace scenes {

TestScene *sceneCornellBox(anari::Device d);

struct CornellBox : public TestScene
{
  CornellBox(anari::Device d);
  ~CornellBox();

  anari::World world() override;

  void commit() override;

 private:
  anari::World m_world{nullptr};
};

} // namespace scenes
} // namespace anari
