// Copyright 2021-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "instanced_cubes.h"
// std
#include <random>

namespace anari {
namespace scenes {

static std::vector<math::float3> vertices = {
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

static std::vector<math::uint3> indices = {
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

static std::vector<math::float4> colors = {
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

  std::mt19937 rng;
  rng.seed(0);
  std::uniform_real_distribution<float> dist;

  int i = 0;
  for (int x = 1; x < 4; x++) {
    for (int y = 1; y < 4; y++) {
      for (int z = 1; z < 4; z++) {
        auto inst = anari::newObject<anari::Instance>(d, "transform");
        auto tl = math::translation_matrix(4.f * math::float3(x, y, z));
        auto rot_x = math::rotation_matrix(
            math::rotation_quat(math::float3(1, 0, 0), float(x)));
        auto rot_y = math::rotation_matrix(
            math::rotation_quat(math::float3(0, 1, 0), float(y)));
        auto rot_z = math::rotation_matrix(
            math::rotation_quat(math::float3(0, 0, 1), float(z)));

        { // NOTE: exercise anari::setParameter with C-array type
          // math::mat4 _xfm = tl * rot_x * rot_y * rot_z;
          math::mat4 _xfm =
              math::mul(tl, math::mul(rot_x, math::mul(rot_y, rot_z)));
          float xfm[16];
          std::memcpy(xfm, &_xfm, sizeof(_xfm));
          anari::setParameter(d, inst, "transform", xfm);
        }

        if (i++ % 2 == 0) {
          anari::setParameter(d,
              inst,
              "color",
              math::float4(dist(rng), dist(rng), dist(rng), 1.f));
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
