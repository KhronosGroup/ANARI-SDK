// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "instanced_cubes.h"

namespace anari {
namespace scenes {

static std::vector<anari::float3> vertices = {
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

static std::vector<anari::uint3> indices = {
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

static std::vector<anari::float4> colors = {
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
        auto tl = anari::translation_matrix(4.f * anari::float3(x, y, z));
        auto rot_x = anari::rotation_matrix(
            anari::rotation_quat(anari::float3(1, 0, 0), float(x)));
        auto rot_y = anari::rotation_matrix(
            anari::rotation_quat(anari::float3(0, 1, 0), float(y)));
        auto rot_z = anari::rotation_matrix(
            anari::rotation_quat(anari::float3(0, 0, 1), float(z)));

        { // NOTE: exercise anari::setParameter with C-array type
          // anari::mat4 _xfm = tl * rot_x * rot_y * rot_z;
          anari::mat4 _xfm = anari::mul(
              tl, anari::mul(rot_x, anari::mul(rot_y, rot_z)));
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
