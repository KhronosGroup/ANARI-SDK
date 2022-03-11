// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "scene.h"

namespace anari {
namespace scenes {

TestScene *sceneCornellBoxQuad(anari::Device d);

struct CornellBoxQuad : public TestScene
{
  CornellBoxQuad(anari::Device d);
  ~CornellBoxQuad();

  anari::World world() override;

  std::vector<ParameterInfo> parameters() override;

  void commit() override;

 private:
  anari::World m_world{nullptr};
};

} // namespace scenes
} // namespace anari