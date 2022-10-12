

// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "SceneGenerator.h"

namespace cts {

SceneGenerator::SceneGenerator(anari::Device d) : TestScene(d)
{
  m_world = anari::newObject<anari::World>(m_device);
}

SceneGenerator::~SceneGenerator()
{
  anari::release(m_device, m_world);
}

std::vector<anari::scenes::ParameterInfo> SceneGenerator::parameters()
{
  return {
      {"geometrySubtype", ANARI_STRING, "triangle", "Which type of geometry to generate"},
      {"primitveMode", ANARI_STRING, "soup", "How the data is arranged (soup or indexed)"},
      {"primitiveCount", ANARI_UINT32, 1, "How many primtives should be generated"}
      //
  };
}

anari::World SceneGenerator::world()
{
  return m_world;
}

void SceneGenerator::commit()
{
  auto d = m_device;

  std::string geometrySubtype = getParam<std::string>("geometrySubtype", "triangle");
  std::string primitveMode = getParam<std::string>("primitveMode", "soup");
  size_t primitiveCount = getParam<size_t>("primitiveCount", 1);

  // Build this scene top-down to stress commit ordering guarantees

  setDefaultLight(m_world);

  auto surface = anari::newObject<anari::Surface>(d);
  auto geom = anari::newObject<anari::Geometry>(d, geometrySubtype.c_str());
  auto mat = anari::newObject<anari::Material>(d, "matte");
  anari::setParameter(d, mat, "color", "color");
  anari::commitParameters(d, mat);

  anari::setAndReleaseParameter(
      d, m_world, "surface", anari::newArray1D(d, &surface));

  anari::commitParameters(d, m_world);

  anari::setParameter(d, surface, "geometry", geom);
  anari::setParameter(d, surface, "material", mat);


  std::vector<glm::vec3> verticies;
  if (geometrySubtype == "triangle") {
    verticies = generateTriangles(primitveMode, primitiveCount);
  }

  

  anari::setAndReleaseParameter(d,
      geom,
      "vertex.position",
      anari::newArray1D(d, verticies.data(), verticies.size()));


  anari::commitParameters(d, geom);
  anari::commitParameters(d, mat);
  anari::commitParameters(d, surface);

  // cleanup

  anari::release(d, surface);
  anari::release(d, geom);
  anari::release(d, mat);
}

std::vector<glm::vec3> SceneGenerator::generateTriangles(
    const std::string& primitiveMode, size_t primitiveCount)
{
    std::vector<glm::vec3> verticies;

    return verticies;
}

anari::scenes::TestScene *sceneSceneGenerator(anari::Device d)
{
  return new SceneGenerator(d);
}

} // namespace cts