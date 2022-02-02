// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "textured_cube.h"

#include "anari/anari_cpp/ext/glm.h"
#include "glm/ext/matrix_transform.hpp"

static void anari_free(void *ptr, void *)
{
  std::free(ptr);
}

namespace anari {
namespace scenes {

static std::vector<glm::vec3> vertices = {
    //
    {-.5f, .5f, 0.f},
    {.5f, .5f, 0.f},
    {-.5f, -.5f, 0.f},
    {.5f, -.5f, 0.f},
    //
};

static std::vector<glm::uvec3> indices = {
    //
    {0, 2, 3},
    {3, 1, 0},
    //
};

static std::vector<glm::vec2> texcoords = {
    //
    {0.f, 1.f},
    {1.f, 1.f},
    {0.f, 0.f},
    {1.f, 0.f},
    //
};

static anari::Array2D makeTextureData(anari::Device d, int dim)
{
  auto *data = new glm::vec3[dim * dim];

  for (int h = 0; h < dim; h++) {
    for (int w = 0; w < dim; w++) {
      bool even = h & 1;
      if (even)
        data[h * dim + w] = w & 1 ? glm::vec3(.8f) : glm::vec3(.2f);
      else
        data[h * dim + w] = w & 1 ? glm::vec3(.2f) : glm::vec3(.8f);
    }
  }

  return anariNewArray2D(
      d, data, &anari_free, nullptr, ANARI_FLOAT32_VEC3, dim, dim);
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
  anari::commit(d, geom);

  auto surface = anari::newObject<anari::Surface>(d);
  anari::setAndReleaseParameter(d, surface, "geometry", geom);

  auto tex = anari::newObject<anari::Sampler>(d, "image2D");
  anari::setAndReleaseParameter(d, tex, "image", makeTextureData(d, 8));
  anari::setParameter(d, tex, "inAttribute", "attribute0");
  anari::setParameter(d, tex, "filter", "nearest");
  anari::commit(d, tex);

  auto mat = anari::newObject<anari::Material>(d, "matte");
  anari::setAndReleaseParameter(d, mat, "color", tex);
  anari::commit(d, mat);
  anari::setAndReleaseParameter(d, surface, "material", mat);
  anari::commit(d, surface);

  auto group = anari::newObject<anari::Group>(d);
  anari::setAndReleaseParameter(
      d, group, "surface", anari::newArray1D(d, &surface));
  anari::commit(d, group);

  anari::release(d, surface);

  std::vector<anari::Instance> instances;

  auto createInstance = [&](float rotation, glm::vec3 axis) {
    auto inst = anari::newObject<anari::Instance>(d);

    auto tl = glm::translate(glm::mat4(1.f), glm::vec3(0, 0, .5f));
    auto rot = glm::rotate(glm::mat4(1.f), rotation, axis);
    glm::mat4x3 xfm = rot * tl;
    anari::setParameter(d, inst, "transform", xfm);
    anari::setParameter(d, inst, "group", group);
    anari::commit(d, inst);
    return inst;
  };

  instances.push_back(createInstance(glm::radians(0.f), glm::vec3(0, 1, 0)));
  instances.push_back(createInstance(glm::radians(180.f), glm::vec3(0, 1, 0)));
  instances.push_back(createInstance(glm::radians(90.f), glm::vec3(0, 1, 0)));
  instances.push_back(createInstance(glm::radians(270.f), glm::vec3(0, 1, 0)));
  instances.push_back(createInstance(glm::radians(90.f), glm::vec3(1, 0, 0)));
  instances.push_back(createInstance(glm::radians(270.f), glm::vec3(1, 0, 0)));

  anari::setAndReleaseParameter(d,
      m_world,
      "instance",
      anari::newArray1D(d, instances.data(), instances.size()));

  anari::release(d, group);
  for (auto i : instances)
    anari::release(d, i);

  setDefaultLight(m_world);

  anari::commit(d, m_world);
}

std::vector<Camera> TexturedCube::cameras()
{
  Camera cam;
  cam.position = glm::vec3(1.25f);
  cam.at = glm::vec3(0.f);
  cam.direction = glm::normalize(cam.at - cam.position);
  cam.up = glm::vec3(0, 1, 0);
  return {cam};
}

TestScene *sceneTexturedCube(anari::Device d)
{
  return new TexturedCube(d);
}

} // namespace scenes
} // namespace anari
