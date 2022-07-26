// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "instanced_cubes.h"

#include "glm/ext/matrix_transform.hpp"

namespace anari {
namespace scenes {

static std::vector<glm::vec3> vertices = {
    //
    {1.f, 1.f, 1.f},
    {1.f, 1.f, -1.f},
    {1.f, -1.f, 1.f},
    {1.f, -1.f, -1.f},
    {-1.f, 1.f, 1.f},
    {-1.f, 1.f, -1.f},
    {-1.f, -1.f, 1.f},
    {-1.f, -1.f, -1.f}
    //
};

static std::vector<glm::uvec3> indices = {
    // top
    {0, 1, 2},
    {3, 2, 1},
    // right
    {5, 1, 0},
    {0, 4, 5},
    // front
    {0, 2, 6},
    {6, 4, 0},
    // left
    {2, 3, 7},
    {7, 6, 2},
    // back
    {3, 1, 5},
    {5, 7, 3},
    // botton
    {7, 5, 4},
    {4, 6, 7},
    //
};

static std::vector<glm::vec4> colors = {
    //
    {0.f, 1.f, 0.f, 1.f},
    {0.f, 1.f, 1.f, 1.f},
    {1.f, 1.f, 0.f, 1.f},
    {1.f, 1.f, 1.f, 1.f},
    {0.f, 0.f, 0.f, 1.f},
    {0.f, 0.f, 1.f, 1.f},
    {1.f, 0.f, 0.f, 1.f},
    {1.f, 0.f, 1.f, 1.f}
    //
};

// CornelBox definitions //////////////////////////////////////////////////////

InstancedCubes::InstancedCubes(anari::Device d) : TestScene(d)
{
  m_world = anari::newObject<anari::World>(m_device);
}

InstancedCubes::~InstancedCubes()
{
  anari::release(m_device, m_world);
}

anari::World InstancedCubes::world()
{
  return m_world;
}

void InstancedCubes::commit()
{
  auto d = m_device;

  auto geom = anari::newObject<anari::Geometry>(d, "triangle");
  anari::setAndReleaseParameter(d,
      geom,
      "vertex.position",
      anari::newArray1D(d, vertices.data(), vertices.size()));
  anari::setAndReleaseParameter(d,
      geom,
      "vertex.color",
      anari::newArray1D(d, colors.data(), colors.size()));
  anari::setAndReleaseParameter(d,
      geom,
      "primitive.index",
      anari::newArray1D(d, indices.data(), indices.size()));
  anari::commitParameters(d, geom);

  auto surface = anari::newObject<anari::Surface>(d);
  anari::setAndReleaseParameter(d, surface, "geometry", geom);

  auto mat = anari::newObject<anari::Material>(d, "matte");
  anari::setParameter(d, mat, "color", "color");
  anari::commitParameters(d, mat);
  anari::setAndReleaseParameter(d, surface, "material", mat);
  anari::commitParameters(d, surface);

  auto group = anari::newObject<anari::Group>(d);
  anari::setAndReleaseParameter(
      d, group, "surface", anari::newArray1D(d, &surface));
  anari::commitParameters(d, group);

  anari::release(d, surface);

  std::vector<ANARIInstance> instances;

  for (int x = 1; x < 4; x++) {
    for (int y = 1; y < 4; y++) {
      for (int z = 1; z < 4; z++) {
        auto inst = anari::newObject<anari::Instance>(d);
        auto tl = glm::translate(glm::mat4(1.f), 4.f * glm::vec3(x, y, z));
        auto rot_x = glm::rotate(glm::mat4(1.f), float(x), glm::vec3(1, 0, 0));
        auto rot_y = glm::rotate(glm::mat4(1.f), float(y), glm::vec3(0, 1, 0));
        auto rot_z = glm::rotate(glm::mat4(1.f), float(z), glm::vec3(0, 0, 1));

        { // NOTE: exercise anari::setParameter with C-array type
          glm::mat4 _xfm = tl * rot_x * rot_y * rot_z;
          float xfm[16];
          std::memcpy(xfm, &_xfm, sizeof(_xfm));
          anari::setParameter(d, inst, "transform", xfm);
        }

        anari::setParameter(d, inst, "group", group);
        anari::commitParameters(d, inst);
        instances.push_back(inst);
      }
    }
  }

  anari::release(d, group);

  anari::setAndReleaseParameter(d,
      m_world,
      "instance",
      anari::newArray1D(d, instances.data(), instances.size()));

  for (auto i : instances)
    anari::release(d, i);

  setDefaultLight(m_world);

  anari::commitParameters(d, m_world);
}

TestScene *sceneInstancedCubes(anari::Device d)
{
  return new InstancedCubes(d);
}

} // namespace scenes
} // namespace anari
