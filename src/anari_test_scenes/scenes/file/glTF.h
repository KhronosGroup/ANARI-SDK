// Copyright 2021-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../scene.h"

namespace anari {
namespace scenes {

TestScene *sceneFileGLTF(anari::Device d);

struct FileGLTF : public TestScene
{
  FileGLTF(anari::Device d);
  ~FileGLTF();

  std::vector<ParameterInfo> parameters() override;

  anari::World world() override;

  void commit() override;

 private:
  anari::World m_world{nullptr};
};

} // namespace scenes
} // namespace anari
