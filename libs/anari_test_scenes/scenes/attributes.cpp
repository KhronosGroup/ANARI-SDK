// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "attributes.h"

#include "glm/ext/matrix_transform.hpp"

namespace anari {
namespace scenes {

static float vertices[] = {
    1.f, 1.f, 0.f,
    1.f, -1.f, 0.f,
    -1.f, -1.f, 0.f,
    -1.f, 1.f, 0.f,
};

static uint32_t indices[] = {
    0, 1, 2,
    0, 2, 3,
};

static float colors4f[] = {
    1.f, 0.f, 0.f, 1.f,
    0.f, 1.f, 0.f, 1.f,
    0.f, 0.f, 1.f, 1.f,
    1.f, 1.f, 1.f, 1.f,
};

static float colors3f[] = {
    1.f, 0.f, 0.f,
    0.f, 1.f, 0.f,
    0.f, 0.f, 1.f,
    1.f, 1.f, 1.f,
};


static float colors2f[] = {
    1.f, 0.f,
    0.f, 1.f,
    0.f, 0.f,
    1.f, 1.f,
};

static float colors1f[] = {
    1.f,
    0.f,
    0.f,
    1.f,
};

static uint32_t colors4u[] = {
    UINT32_MAX, UINT32_C(0), UINT32_C(0), UINT32_MAX,
    UINT32_C(0), UINT32_MAX, UINT32_C(0), UINT32_MAX,
    UINT32_C(0), UINT32_C(0), UINT32_MAX, UINT32_MAX,
    UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX,
};

static uint32_t colors3u[] = {
    UINT32_MAX, UINT32_C(0), UINT32_C(0),
    UINT32_C(0), UINT32_MAX, UINT32_C(0),
    UINT32_C(0), UINT32_C(0), UINT32_MAX,
    UINT32_MAX, UINT32_MAX, UINT32_MAX,
};


static uint32_t colors2u[] = {
    UINT32_MAX, UINT32_C(0),
    UINT32_C(0), UINT32_MAX,
    UINT32_C(0), UINT32_C(0),
    UINT32_MAX, UINT32_MAX,
};

static uint32_t colors1u[] = {
    UINT32_MAX,
    UINT32_C(0),
    UINT32_C(0),
    UINT32_MAX,
};

static uint16_t colors4us[] = {
    UINT16_MAX, UINT16_C(0), UINT16_C(0), UINT16_MAX,
    UINT16_C(0), UINT16_MAX, UINT16_C(0), UINT16_MAX,
    UINT16_C(0), UINT16_C(0), UINT16_MAX, UINT16_MAX,
    UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX,
};

static uint16_t colors3us[] = {
    UINT16_MAX, UINT16_C(0), UINT16_C(0),
    UINT16_C(0), UINT16_MAX, UINT16_C(0),
    UINT16_C(0), UINT16_C(0), UINT16_MAX,
    UINT16_MAX, UINT16_MAX, UINT16_MAX,
};


static uint16_t colors2us[] = {
    UINT16_MAX, UINT16_C(0),
    UINT16_C(0), UINT16_MAX,
    UINT16_C(0), UINT16_C(0),
    UINT16_MAX, UINT16_MAX,
};

static uint16_t colors1us[] = {
    UINT16_MAX,
    UINT16_C(0),
    UINT16_C(0),
    UINT16_MAX,
};

static uint8_t colors4ub[] = {
    UINT8_MAX,  UINT8_C(0),  UINT8_C(0), UINT8_MAX,
    UINT8_C(0), UINT8_MAX, UINT8_C(0), UINT8_MAX,
    UINT8_C(0), UINT8_C(0), UINT8_MAX, UINT8_MAX,
    UINT8_MAX, UINT8_MAX, UINT8_MAX, UINT8_MAX,
};

static uint8_t colors3ub[] = {
    UINT8_MAX, UINT8_C(0), UINT8_C(0),
    UINT8_C(0), UINT8_MAX, UINT8_C(0),
    UINT8_C(0), UINT8_C(0), UINT8_MAX,
    UINT8_MAX, UINT8_MAX, UINT8_MAX,
};


static uint8_t colors2ub[] = {
    UINT8_MAX, UINT8_C(0),
    UINT8_C(0), UINT8_MAX,
    UINT8_C(0), UINT8_C(0),
    UINT8_MAX, UINT8_MAX,
};

static uint8_t colors1ub[] = {
    UINT8_MAX,
    UINT8_C(0),
    UINT8_C(0),
    UINT8_MAX,
};

void *arrays[] = {
  colors4f, colors3f, colors2f, colors1f,
  colors4u, colors3u, colors2u, colors1u,
  colors4us, colors3us, colors2us, colors1us,
  colors4ub, colors3ub, colors2ub, colors1ub,
};

ANARIDataType types[] = {
  ANARI_FLOAT32_VEC4, ANARI_FLOAT32_VEC3, ANARI_FLOAT32_VEC2, ANARI_FLOAT32,
  ANARI_UFIXED32_VEC4, ANARI_UFIXED32_VEC3, ANARI_UFIXED32_VEC2, ANARI_UFIXED32,
  ANARI_UFIXED16_VEC4, ANARI_UFIXED16_VEC3, ANARI_UFIXED16_VEC2, ANARI_UFIXED16,
  ANARI_UFIXED8_VEC4, ANARI_UFIXED8_VEC3, ANARI_UFIXED8_VEC2, ANARI_UFIXED8,
};


// CornelBox definitions //////////////////////////////////////////////////////

Attributes::Attributes(anari::Device d) : TestScene(d)
{
  m_world = anari::newObject<anari::World>(m_device);
}

Attributes::~Attributes()
{
  anari::release(m_device, m_world);
}

anari::World Attributes::world()
{
  return m_world;
}

void Attributes::commit()
{
  auto d = m_device;

  auto vertexArray = anariNewArray1D(d, vertices, 0, 0, ANARI_FLOAT32_VEC3, 4);
  auto indexArray = anariNewArray1D(d, indices, 0, 0, ANARI_UINT32_VEC3, 6);

  ANARIGeometry geometries[16];
  for(int i = 0;i<16;++i) {
    geometries[i] = anari::newObject<anari::Geometry>(d, "triangle");
    anari::setParameter(d, geometries[i], "vertex.position", vertexArray);
    anari::setParameter(d, geometries[i], "primitive.index", indexArray);
    anari::setAndReleaseParameter(d,
        geometries[i],
        "primitive.color",
        anariNewArray1D(d, arrays[i], 0, 0, types[i], 2));
/*
    anari::setAndReleaseParameter(d,
        geometries[i],
        "vertex.color",
        anariNewArray1D(d, arrays[i], 0, 0, types[i], 4));
*/

    anari::commitParameters(d, geometries[i]);
  }

  auto mat = anari::newObject<anari::Material>(d, "matte");
  anari::setParameter(d, mat, "color", "color");
  anari::commitParameters(d, mat);



  std::vector<ANARIInstance> instances;

  for(int i = 0;i<16;++i) {

    int x = i%4;
    int y = i/4;

    auto inst = anari::newObject<anari::Instance>(d);
    auto tl = glm::translate(glm::mat4(1.f), 4.f * glm::vec3(x-1.5f, y-1.5f, 0.f));

    { // NOTE: exercise anari::setParameter with C-array type
      glm::mat4 _xfm = tl;
      float xfm[16];
      std::memcpy(xfm, &_xfm, sizeof(_xfm));
      anari::setParameter(d, inst, "transform", xfm);
    }


    auto surface = anari::newObject<anari::Surface>(d);
    anari::setParameter(d, surface, "geometry", geometries[i]);
    anari::setParameter(d, surface, "material", mat);
    anari::commitParameters(d, surface);

    auto group = anari::newObject<anari::Group>(d);
    anari::setAndReleaseParameter(
        d, group, "surface", anari::newArray1D(d, &surface));
    anari::commitParameters(d, group);
    anari::release(d, surface);

    anari::setParameter(d, inst, "group", group);
    anari::commitParameters(d, inst);
    instances.push_back(inst);
  }

  anari::setAndReleaseParameter(d,
      m_world,
      "instance",
      anari::newArray1D(d, instances.data(), instances.size()));

  for (auto i : instances)
    anari::release(d, i);

  auto light = anari::newObject<anari::Light>(d, "directional");
  anari::setParameter(d, light, "direction", glm::vec3(0, 0, 1));
  anari::setParameter(d, light, "irradiance", 1.f);
  anari::commitParameters(d, light);
  anari::setAndReleaseParameter(
      d, m_world, "light", anari::newArray1D(d, &light));
  anari::release(d, light);

  anari::commitParameters(d, m_world);
}

TestScene *sceneAttributes(anari::Device d)
{
  return new Attributes(d);
}

} // namespace scenes
} // namespace anari
