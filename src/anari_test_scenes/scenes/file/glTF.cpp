// Copyright 2021-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "glTF.h"
// stb_image
#include "stb_image.h"
// std
#include <fstream>
#include <iomanip>
#include <iostream>

#include "gltf2anari.h"

namespace anari {
namespace scenes {

// FileGLTF definitions ////////////////////////////////////////////////////////

FileGLTF::FileGLTF(anari::Device d) : TestScene(d)
{
  m_world = anari::newObject<anari::World>(m_device);
}

FileGLTF::~FileGLTF()
{
  anari::release(m_device, m_world);
}

std::vector<ParameterInfo> FileGLTF::parameters()
{
  return {
      // clang-format off
      {makeParameterInfo("fileName", ".gltf file to open", "")}
      // clang-format on
  };
}

anari::World FileGLTF::world()
{
  return m_world;
}

void FileGLTF::commit()
{
  if (!hasParam("fileName"))
    return;

  std::string filename = getParamString("fileName", "");

  gltf_data ctx(m_device);
  ctx.open_file(filename);

  if (!ctx.instances.empty()) {
    anari::setParameterArray1D(m_device,
        m_world,
        "instance",
        ctx.instances[0].data(),
        ctx.instances[0].size());
  } else {
    uint64_t elementStride = 0;
    auto *mapped = (anari::Instance *)anariMapParameterArray1D(m_device,
        m_world,
        "instance",
        ANARI_INSTANCE,
        ctx.groups.size(),
        &elementStride);
    for (size_t i = 0; i < ctx.groups.size(); ++i) {
      mapped[i] = anari::newObject<anari::Instance>(m_device, "transform");
      anari::setParameter(m_device, mapped[i], "group", ctx.groups[i]);
      anari::commitParameters(m_device, mapped[i]);
    }
    anariUnmapParameterArray(m_device, m_world, "instance");
  }

  anari::commitParameters(m_device, m_world);
}

TestScene *sceneFileGLTF(anari::Device d)
{
  return new FileGLTF(d);
}

} // namespace scenes
} // namespace anari
