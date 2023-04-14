// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../scene.h"

namespace anari {
namespace scenes {

TestScene *scenePbrSpheres(anari::Device d);

struct PbrSpheres : public TestScene
{
  PbrSpheres(anari::Device d);
  ~PbrSpheres();

  anari::World world() override;

  void commit() override;

 private:
  anari::World m_world{nullptr};
};

} // namespace scenes
} // namespace anari
