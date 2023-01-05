// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "attributes.h"

#include "glm/ext/matrix_transform.hpp"
#include <limits>
#include <type_traits>

namespace anari {
namespace scenes {

static float vertices[] = {
    1.f, 1.f, 0.f,
    1.f, -1.f, 0.f,
    -1.f, -1.f, 0.f,
    -1.f, 1.f, 0.f,
};

static float uv[] = {
    1.f, 1.f,
    1.f, 0.f,
    0.f, 0.f,
    0.f, 1.f,
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

template<typename T>
struct Picture {
  std::vector<T> data;
  const uint64_t width;
  const uint64_t height;
  const uint64_t N;
  Picture(uint64_t w, uint64_t h, uint64_t c)
  : data(c*w*h), width(w), height(h), N(c)
  { }
  void set(uint64_t x, uint64_t y, float r, float g, float b, float a) {
    float v[] = {r, g, b, a};
    uint64_t i = N*(x + y*width);
    for(uint64_t j = 0;j<N;++j) {
      data[i+j] = convert(v[j]);
    }    
  }

  T convert(float x) {
    if(std::is_same<T, float>::value) {
      return x;
    } else if(std::is_integral<T>::value) {
      return double(x)*std::numeric_limits<T>::max();
    }
  }
};

template<typename T>
void fill_texture(T &p) {
  for(uint64_t y = 0;y<p.height;++y) {
    for(uint64_t x = 0;x<p.width;++x) {
      float wx = float(x)/float(p.width-1);
      float wy = float(y)/float(p.height-1);
      float a = wx*wy;
      float b = wx*(1.0f-wy);
      float c = (1.0f-wx)*wy;
      float d = (1.0f-wx)*(1.0f-wy);
      float m = (x/4 + y/4)%2 ? 1.0f : 0.5f;
      p.set(x, y, m*(a+d), m*(b+d), m*(c+d), 1.0f);    
    }    
  }
}


template<typename T>
ANARIMaterial setupTexturedMaterial(ANARIDevice d,
  int width, int height, int components,
  ANARIDataType type, const char *uv)
{
  Picture<T> p(width, height, components);
  fill_texture(p);

  ANARIArray2D array2d = anariNewArray2D(d, p.data.data(), 0, 0, type, width, height);
  ANARISampler sampler = anariNewSampler(d, "image2D");
  anari::setAndReleaseParameter(d, sampler, "image", array2d);
  anari::setParameter(d, sampler, "inAttribute", "attribute0");
  anari::commitParameters(d, sampler);

  auto material = anari::newObject<anari::Material>(d, "matte");
  anari::setAndReleaseParameter(d, material, "color", sampler);
  anari::commitParameters(d, material);

  return material;
}

void Attributes::commit()
{
  auto d = m_device;

  auto vertexArray = anariNewArray1D(d, vertices, 0, 0, ANARI_FLOAT32_VEC3, 4);
  auto indexArray = anariNewArray1D(d, indices, 0, 0, ANARI_UINT32_VEC3, 6);

  ANARIGeometry geometries[64] = { };
  for(int i = 0;i<16;++i) {
    geometries[i] = anari::newObject<anari::Geometry>(d, "triangle");
    anari::setParameter(d, geometries[i], "vertex.position", vertexArray);
    anari::setParameter(d, geometries[i], "primitive.index", indexArray);
    anari::setAndReleaseParameter(d,
        geometries[i],
        "primitive.color",
        anariNewArray1D(d, arrays[i], 0, 0, types[i], 2));
    anari::commitParameters(d, geometries[i]);
  }

  for(int i = 0;i<16;++i) {
    geometries[i+16] = anari::newObject<anari::Geometry>(d, "triangle");
    anari::setParameter(d, geometries[i+16], "vertex.position", vertexArray);
    anari::setParameter(d, geometries[i+16], "primitive.index", indexArray);
    anari::setAndReleaseParameter(d,
        geometries[i+16],
        "vertex.color",
        anariNewArray1D(d, arrays[i], 0, 0, types[i], 4));
    anari::commitParameters(d, geometries[i+16]);
  }

  auto mat = anari::newObject<anari::Material>(d, "matte");
  anari::setParameter(d, mat, "color", "color");
  anari::commitParameters(d, mat);


  ANARIGeometry textured = anari::newObject<anari::Geometry>(d, "triangle");
  anari::setParameter(d, textured, "vertex.position", vertexArray);
  anari::setParameter(d, textured, "primitive.index", indexArray);
  anari::setAndReleaseParameter(d,
      textured,
      "vertex.attribute0",
      anariNewArray1D(d, uv, 0, 0, ANARI_FLOAT32_VEC2, 4));
  anari::commitParameters(d, textured);

  ANARIMaterial materials[16];
  materials[0] = setupTexturedMaterial<float>(d, 32, 32, 4, ANARI_FLOAT32_VEC4, "attribute0");
  materials[1] = setupTexturedMaterial<float>(d, 32, 32, 3, ANARI_FLOAT32_VEC3, "attribute0");
  materials[2] = setupTexturedMaterial<float>(d, 32, 32, 2, ANARI_FLOAT32_VEC2, "attribute0");
  materials[3] = setupTexturedMaterial<float>(d, 32, 32, 1, ANARI_FLOAT32, "attribute0");

  materials[4] = setupTexturedMaterial<uint32_t>(d, 32, 32, 4, ANARI_UFIXED32_VEC4, "attribute0");
  materials[5] = setupTexturedMaterial<uint32_t>(d, 32, 32, 3, ANARI_UFIXED32_VEC3, "attribute0");
  materials[6] = setupTexturedMaterial<uint32_t>(d, 32, 32, 2, ANARI_UFIXED32_VEC2, "attribute0");
  materials[7] = setupTexturedMaterial<uint32_t>(d, 32, 32, 1, ANARI_UFIXED32, "attribute0");

  materials[8] = setupTexturedMaterial<uint16_t>(d, 32, 32, 4, ANARI_UFIXED16_VEC4, "attribute0");
  materials[9] = setupTexturedMaterial<uint16_t>(d, 32, 32, 3, ANARI_UFIXED16_VEC3, "attribute0");
  materials[10] = setupTexturedMaterial<uint16_t>(d, 32, 32, 2, ANARI_UFIXED16_VEC2, "attribute0");
  materials[11] = setupTexturedMaterial<uint16_t>(d, 32, 32, 1, ANARI_UFIXED16, "attribute0");

  materials[12] = setupTexturedMaterial<uint8_t>(d, 32, 32, 4, ANARI_UFIXED8_VEC4, "attribute0");
  materials[13] = setupTexturedMaterial<uint8_t>(d, 32, 32, 3, ANARI_UFIXED8_VEC3, "attribute0");
  materials[14] = setupTexturedMaterial<uint8_t>(d, 32, 32, 2, ANARI_UFIXED8_VEC2, "attribute0");
  materials[15] = setupTexturedMaterial<uint8_t>(d, 32, 32, 1, ANARI_UFIXED8, "attribute0");


  std::vector<ANARIInstance> instances;

  for(int i = 0;i<32;++i) {

    int x = i%4;
    int y = i/4;

    auto inst = anari::newObject<anari::Instance>(d);
    auto tl = glm::translate(glm::mat4(1.f), 2.0f*glm::vec3(x-3.5f, y-3.5f, 0.f));

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

    anari::setAndReleaseParameter(d, inst, "group", group);
    anari::commitParameters(d, inst);
    instances.push_back(inst);
  }


  for(int i = 0;i<16;++i) {

    int x = i%4;
    int y = i/4;

    auto inst = anari::newObject<anari::Instance>(d);
    auto tl = glm::translate(glm::mat4(1.f), 2.0f*glm::vec3(x+0.5f, y-3.5f, 0.f));

    { // NOTE: exercise anari::setParameter with C-array type
      glm::mat4 _xfm = tl;
      float xfm[16];
      std::memcpy(xfm, &_xfm, sizeof(_xfm));
      anari::setParameter(d, inst, "transform", xfm);
    }


    auto surface = anari::newObject<anari::Surface>(d);
    anari::setParameter(d, surface, "geometry", textured);
    anari::setParameter(d, surface, "material", materials[i]);
    anari::commitParameters(d, surface);

    auto group = anari::newObject<anari::Group>(d);
    anari::setAndReleaseParameter(
        d, group, "surface", anari::newArray1D(d, &surface));
    anari::commitParameters(d, group);
    anari::release(d, surface);

    anari::setAndReleaseParameter(d, inst, "group", group);
    anari::commitParameters(d, inst);
    instances.push_back(inst);
  }


  for(auto g : geometries) {
    anari::release(d, g);
  }

  for(auto m : materials) {
    anari::release(d, m);
  }

  anari::release(d, mat);
  anari::release(d, textured);
  anari::release(d, vertexArray);
  anari::release(d, indexArray);

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
