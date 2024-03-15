// Copyright 2021-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "textured_cube.h"

static void anari_free(const void *ptr, const void *)
{
  std::free(const_cast<void *>(ptr));
}

namespace anari {
namespace scenes {

static std::vector<math::float3> vertices = {
    //
    {-.5f, .5f, 0.f},
    {.5f, .5f, 0.f},
    {-.5f, -.5f, 0.f},
    {.5f, -.5f, 0.f},
    //
};

static std::vector<math::uint3> indices = {
    //
    {0, 2, 3},
    {3, 1, 0},
    //
};

static std::vector<math::float2> texcoords = {
    //
    {0.f, 1.f},
    {1.f, 1.f},
    {0.f, 0.f},
    {1.f, 0.f},
    //
};

static anari::Array2D makeTextureData(anari::Device d, int dim)
{
  auto *data = new math::float3[dim * dim];

  for (int h = 0; h < dim; h++) {
    for (int w = 0; w < dim; w++) {
      bool even = h & 1;
      if (even)
        data[h * dim + w] = w & 1 ? math::float3(.8f) : math::float3(.2f);
      else
        data[h * dim + w] = w & 1 ? math::float3(.2f) : math::float3(.8f);
    }
  }

  return anariNewArray2D(d,
      data,
      &anari_free,
      nullptr,
      ANARI_FLOAT32_VEC3,
      uint64_t(dim),
      uint64_t(dim));
}

// CornelBox definitions //////////////////////////////////////////////////////

TexturedCube::TexturedCube(anari::Device d) : TestScene(d)
{
  m_world = anari::newObject<anari::World>(m_device);
}

TexturedCube::~TexturedCube()
{
  anari::release(m_device, m_world);
}

anari::World TexturedCube::world()
{
  return m_world;
}

void TexturedCube::commit()
{
  anari::Device d = m_device;

  auto geom = anari::newObject<anari::Geometry>(d, "triangle");
  anari::setAndReleaseParameter(d,
      geom,
      "vertex.position",
      anari::newArray1D(d, vertices.data(), vertices.size()));
  anari::setAndReleaseParameter(d,
      geom,
      "vertex.attribute0",
      anari::newArray1D(d, texcoords.data(), texcoords.size()));
  anari::setAndReleaseParameter(d,
      geom,
      "primitive.index",
      anari::newArray1D(d, indices.data(), indices.size()));
  anari::commitParameters(d, geom);

  auto surface = anari::newObject<anari::Surface>(d);
  anari::setAndReleaseParameter(d, surface, "geometry", geom);

  auto tex = anari::newObject<anari::Sampler>(d, "image2D");
  anari::setAndReleaseParameter(d, tex, "image", makeTextureData(d, 8));
  anari::setParameter(d, tex, "inAttribute", "attribute0");
  anari::setParameter(d, tex, "filter", "nearest");
  anari::commitParameters(d, tex);

  auto mat = anari::newObject<anari::Material>(d, "matte");
  anari::setAndReleaseParameter(d, mat, "color", tex);
  anari::commitParameters(d, mat);
  anari::setAndReleaseParameter(d, surface, "material", mat);
  anari::commitParameters(d, surface);

  auto group = anari::newObject<anari::Group>(d);
  anari::setAndReleaseParameter(
      d, group, "surface", anari::newArray1D(d, &surface));
  anari::commitParameters(d, group);

  anari::release(d, surface);

  std::vector<anari::Instance> instances;

  auto createInstance = [&](float rotation, math::float3 axis) {
    auto inst = anari::newObject<anari::Instance>(d, "transform");

    auto tl = math::translation_matrix(math::float3(0, 0, .5f));
    auto rot = math::rotation_matrix(math::rotation_quat(axis, rotation));
    anari::setParameter(d, inst, "transform", math::mul(rot, tl));
    anari::setParameter(d, inst, "group", group);
    anari::commitParameters(d, inst);
    return inst;
  };

  instances.push_back(
      createInstance(anari::radians(0.f), math::float3(0, 1, 0)));
  instances.push_back(
      createInstance(anari::radians(180.f), math::float3(0, 1, 0)));
  instances.push_back(
      createInstance(anari::radians(90.f), math::float3(0, 1, 0)));
  instances.push_back(
      createInstance(anari::radians(270.f), math::float3(0, 1, 0)));
  instances.push_back(
      createInstance(anari::radians(90.f), math::float3(1, 0, 0)));
  instances.push_back(
      createInstance(anari::radians(270.f), math::float3(1, 0, 0)));

  anari::setAndReleaseParameter(d,
      m_world,
      "instance",
      anari::newArray1D(d, instances.data(), instances.size()));

  anari::release(d, group);
  for (auto i : instances)
    anari::release(d, i);

  setDefaultLight(m_world);

  anari::commitParameters(d, m_world);
}

std::vector<Camera> TexturedCube::cameras()
{
  Camera cam;
  cam.position = math::float3(1.25f);
  cam.at = math::float3(0.f);
  cam.direction = math::normalize(cam.at - cam.position);
  cam.up = math::float3(0, 1, 0);
  return {cam};
}

TestScene *sceneTexturedCube(anari::Device d)
{
  return new TexturedCube(d);
}

} // namespace scenes
} // namespace anari
