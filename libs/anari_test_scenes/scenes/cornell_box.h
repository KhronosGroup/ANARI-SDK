// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "scene.h"

namespace anari {
namespace scenes {

TestScene *sceneCornellBox(ANARIDevice d);

struct CornellBox : public TestScene
{
  CornellBox(ANARIDevice d);
  ~CornellBox();

  ANARIWorld world() override;

  void commit() override;

 private:
  ANARIWorld m_world{nullptr};
};

} // namespace scenes
} // namespace anari
