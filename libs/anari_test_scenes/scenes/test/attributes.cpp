// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "attributes.h"

#include <limits>
#include <type_traits>
#include <cmath>

namespace anari {
namespace scenes {

static float vertices[] = {
    1.f, 1.f, 0.f,
    1.f, -1.f, 0.f,
    -1.f, -1.f, 0.f,
    -1.f, 1.f, 0.f,
    0.f, 0.f, 0.f,
};

static float uv[] = {
    1.f, 1.f,
    1.f, 0.f,
    0.f, 0.f,
    0.f, 1.f,
    0.5f, 0.5f,
};

static uint32_t indices[] = {
    0, 1, 4,
    1, 2, 4,
    2, 3, 4,
    3, 0, 4,
};

static float colors4f[] = {
    1.f, 0.f, 0.f, 1.f,
    0.f, 1.f, 0.f, 1.f,
    0.f, 0.f, 1.f, 1.f,
    1.f, 1.f, 1.f, 1.f,
    0.5f, 0.5f, 0.5f, 1.f,

};

static float colors3f[] = {
    1.f, 0.f, 0.f,
    0.f, 1.f, 0.f,
    0.f, 0.f, 1.f,
    1.f, 1.f, 1.f,
    0.5f, 0.5f, 0.5f,
};


static float colors2f[] = {
    1.f, 0.f,
    0.f, 1.f,
    0.f, 0.f,
    1.f, 1.f,
    0.5f, 0.5f,
};

static float colors1f[] = {
    1.f,
    0.f,
    0.f,
    1.f,
    0.5f,
};

static uint32_t colors4u[] = {
    UINT32_MAX, UINT32_C(0), UINT32_C(0), UINT32_MAX,
    UINT32_C(0), UINT32_MAX, UINT32_C(0), UINT32_MAX,
    UINT32_C(0), UINT32_C(0), UINT32_MAX, UINT32_MAX,
    UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX,
    UINT32_MAX/2u, UINT32_MAX/2u, UINT32_MAX/2u, UINT32_MAX,
};

static uint32_t colors3u[] = {
    UINT32_MAX, UINT32_C(0), UINT32_C(0),
    UINT32_C(0), UINT32_MAX, UINT32_C(0),
    UINT32_C(0), UINT32_C(0), UINT32_MAX,
    UINT32_MAX, UINT32_MAX, UINT32_MAX,
    UINT32_MAX/2u, UINT32_MAX/2u, UINT32_MAX/2u,
};


static uint32_t colors2u[] = {
    UINT32_MAX, UINT32_C(0),
    UINT32_C(0), UINT32_MAX,
    UINT32_C(0), UINT32_C(0),
    UINT32_MAX, UINT32_MAX,
    UINT32_MAX/2u, UINT32_MAX/2u,
};

static uint32_t colors1u[] = {
    UINT32_MAX,
    UINT32_C(0),
    UINT32_C(0),
    UINT32_MAX,
    UINT32_MAX/2u,
};

static uint16_t colors4us[] = {
    UINT16_MAX, UINT16_C(0), UINT16_C(0), UINT16_MAX,
    UINT16_C(0), UINT16_MAX, UINT16_C(0), UINT16_MAX,
    UINT16_C(0), UINT16_C(0), UINT16_MAX, UINT16_MAX,
    UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX,
    UINT16_MAX/2u, UINT16_MAX/2u, UINT16_MAX/2u, UINT16_MAX,
};

static uint16_t colors3us[] = {
    UINT16_MAX, UINT16_C(0), UINT16_C(0),
    UINT16_C(0), UINT16_MAX, UINT16_C(0),
    UINT16_C(0), UINT16_C(0), UINT16_MAX,
    UINT16_MAX, UINT16_MAX, UINT16_MAX,
    UINT16_MAX/2u, UINT16_MAX/2u, UINT16_MAX/2u,
};


static uint16_t colors2us[] = {
    UINT16_MAX, UINT16_C(0),
    UINT16_C(0), UINT16_MAX,
    UINT16_C(0), UINT16_C(0),
    UINT16_MAX, UINT16_MAX,
    UINT16_MAX/2u, UINT16_MAX/2u,
};

static uint16_t colors1us[] = {
    UINT16_MAX,
    UINT16_C(0),
    UINT16_C(0),
    UINT16_MAX,
    UINT16_MAX/2u,
};

static uint8_t colors4ub[] = {
    UINT8_MAX,  UINT8_C(0),  UINT8_C(0), UINT8_MAX,
    UINT8_C(0), UINT8_MAX, UINT8_C(0), UINT8_MAX,
    UINT8_C(0), UINT8_C(0), UINT8_MAX, UINT8_MAX,
    UINT8_MAX, UINT8_MAX, UINT8_MAX, UINT8_MAX,
    UINT8_MAX/2u, UINT8_MAX/2u, UINT8_MAX/2u, UINT8_MAX,
};

static uint8_t colors3ub[] = {
    UINT8_MAX, UINT8_C(0), UINT8_C(0),
    UINT8_C(0), UINT8_MAX, UINT8_C(0),
    UINT8_C(0), UINT8_C(0), UINT8_MAX,
    UINT8_MAX, UINT8_MAX, UINT8_MAX,
    UINT8_MAX/2u, UINT8_MAX/2u, UINT8_MAX/2u,
};


static uint8_t colors2ub[] = {
    UINT8_MAX, UINT8_C(0),
    UINT8_C(0), UINT8_MAX,
    UINT8_C(0), UINT8_C(0),
    UINT8_MAX, UINT8_MAX,
    UINT8_MAX/2u, UINT8_MAX/2u,
};

static uint8_t colors1ub[] = {
    UINT8_MAX,
    UINT8_C(0),
    UINT8_C(0),
    UINT8_MAX,
    UINT8_MAX/2u,
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

ANARIMaterial setupSamplerMaterial2D(ANARIDevice d, ANARIArray2D array2d,
  float *inTransform, float *outTransform,
  const char *filter, const char *wrapMode1, const char *wrapMode2)
{
  ANARISampler sampler = anariNewSampler(d, "image2D");
  anari::setParameter(d, sampler, "image", array2d);
  anari::setParameter(d, sampler, "inAttribute", "attribute0");

  if(inTransform) {
    anari::setParameter(d, sampler, "inTransform", ANARI_FLOAT32_MAT4, inTransform);
  }
  if(outTransform) {
    anari::setParameter(d, sampler, "outTransform", ANARI_FLOAT32_MAT4, outTransform);
  }
  if(filter) {
    anari::setParameter(d, sampler, "filter", filter);
  }
  if(wrapMode1) {
    anari::setParameter(d, sampler, "wrapMode1", wrapMode1);
  }
  if(wrapMode2) {
    anari::setParameter(d, sampler, "wrapMode2", wrapMode2);
  }

  anari::commitParameters(d, sampler);

  auto material = anari::newObject<anari::Material>(d, "matte");
  anari::setAndReleaseParameter(d, material, "color", sampler);
  anari::commitParameters(d, material);

  return material;
}

ANARIMaterial setupSamplerMaterial1D(ANARIDevice d, ANARIArray1D array1d,
  float *inTransform, float *outTransform,
  const char *filter, const char *wrapMode1)
{
  ANARISampler sampler = anariNewSampler(d, "image1D");
  anari::setParameter(d, sampler, "image", array1d);
  anari::setParameter(d, sampler, "inAttribute", "attribute0");

  if(inTransform) {
    anari::setParameter(d, sampler, "inTransform", ANARI_FLOAT32_MAT4, inTransform);
  }
  if(outTransform) {
    anari::setParameter(d, sampler, "outTransform", ANARI_FLOAT32_MAT4, outTransform);
  }
  if(filter) {
    anari::setParameter(d, sampler, "filter", filter);
  }
  if(wrapMode1) {
    anari::setParameter(d, sampler, "wrapMode1", wrapMode1);
  }

  anari::commitParameters(d, sampler);

  auto material = anari::newObject<anari::Material>(d, "matte");
  anari::setAndReleaseParameter(d, material, "color", sampler);
  anari::commitParameters(d, material);

  return material;
}

ANARIMaterial setupSamplerMaterial3D(ANARIDevice d, ANARIArray3D array3d,
  float *inTransform, float *outTransform,
  const char *filter, const char *wrapMode1, const char *wrapMode2, const char *wrapMode3)
{
  ANARISampler sampler = anariNewSampler(d, "image3D");
  anari::setParameter(d, sampler, "image", array3d);
  anari::setParameter(d, sampler, "inAttribute", "attribute0");

  if(inTransform) {
    anari::setParameter(d, sampler, "inTransform", ANARI_FLOAT32_MAT4, inTransform);
  }
  if(outTransform) {
    anari::setParameter(d, sampler, "outTransform", ANARI_FLOAT32_MAT4, outTransform);
  }
  if(filter) {
    anari::setParameter(d, sampler, "filter", filter);
  }
  if(wrapMode1) {
    anari::setParameter(d, sampler, "wrapMode1", wrapMode1);
  }
  if(wrapMode2) {
    anari::setParameter(d, sampler, "wrapMode2", wrapMode1);
  }
  if(wrapMode3) {
    anari::setParameter(d, sampler, "wrapMode3", wrapMode1);
  }

  anari::commitParameters(d, sampler);

  auto material = anari::newObject<anari::Material>(d, "matte");
  anari::setAndReleaseParameter(d, material, "color", sampler);
  anari::commitParameters(d, material);

  return material;
}

ANARIMaterial setupSamplerTransform(ANARIDevice d,
  float *transform)
{
  ANARISampler sampler = anariNewSampler(d, "transform");
  anari::setParameter(d, sampler, "inAttribute", "attribute0");

  if(transform) {
    anari::setParameter(d, sampler, "transform", ANARI_FLOAT32_MAT4, transform);
  }

  anari::commitParameters(d, sampler);

  auto material = anari::newObject<anari::Material>(d, "matte");
  anari::setAndReleaseParameter(d, material, "color", sampler);
  anari::commitParameters(d, material);

  return material;
}

ANARIMaterial setupSamplerPrimitive(ANARIDevice d, ANARIArray1D array1d, uint64_t offset)
{
  ANARISampler sampler = anariNewSampler(d, "primitive");
  anari::setParameter(d, sampler, "array", array1d);
  anari::setParameter(d, sampler, "offset", offset);

  anari::commitParameters(d, sampler);

  auto material = anari::newObject<anari::Material>(d, "matte");
  anari::setAndReleaseParameter(d, material, "color", sampler);
  anari::commitParameters(d, material);

  return material;
}


void Attributes::commit()
{
  auto d = m_device;

  auto vertexArray = anariNewArray1D(d, vertices, 0, 0, ANARI_FLOAT32_VEC3, 5);
  auto indexArray = anariNewArray1D(d, indices, 0, 0, ANARI_UINT32_VEC3, 4);

  ANARIGeometry geometries[64] = { };
  for(int i = 0;i<16;++i) {
    geometries[i] = anari::newObject<anari::Geometry>(d, "triangle");
    anari::setParameter(d, geometries[i], "vertex.position", vertexArray);
    anari::setParameter(d, geometries[i], "primitive.index", indexArray);
    anari::setAndReleaseParameter(d,
        geometries[i],
        "primitive.color",
        anariNewArray1D(d, arrays[i], 0, 0, types[i], 4));
    anari::commitParameters(d, geometries[i]);
  }

  for(int i = 0;i<16;++i) {
    geometries[i+16] = anari::newObject<anari::Geometry>(d, "triangle");
    anari::setParameter(d, geometries[i+16], "vertex.position", vertexArray);
    anari::setParameter(d, geometries[i+16], "primitive.index", indexArray);
    anari::setAndReleaseParameter(d,
        geometries[i+16],
        "vertex.color",
        anariNewArray1D(d, arrays[i], 0, 0, types[i], 5));
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
      anariNewArray1D(d, uv, 0, 0, ANARI_FLOAT32_VEC2, 5));
  anari::commitParameters(d, textured);

  ANARIMaterial materials[32] = { };
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


  int N = 32;
  Picture<uint8_t> p(N, N, 4);
  fill_texture(p);
  ANARIArray2D array2d = anariNewArray2D(d, p.data.data(), 0, 0, ANARI_UFIXED8_VEC4, N, N);

  std::vector<uint8_t> data3d(4*N*N*N);
  for(uint64_t z = 0;z<N;++z) {
    for(uint64_t y = 0;y<N;++y) {
      for(uint64_t x = 0;x<N;++x) {
        float r = float(x)/float(N-1);
        float g = float(y)/float(N-1);
        float b = float(z)/float(N-1);
        float m = (x/4 + y/4 + z/4)%2 ? 1.0f : 0.5f;
        uint64_t idx = x + N*y + N*N*z;
        data3d[4*idx+0] = m*r*255u;
        data3d[4*idx+1] = m*g*255u;
        data3d[4*idx+2] = m*b*255u;
        data3d[4*idx+3] = 255u;
      }
    }
  }
  ANARIArray3D array3d = anariNewArray3D(d, data3d.data(), 0, 0, ANARI_UFIXED8_VEC4, N, N, N);

  std::vector<uint8_t> data1d(4*N);
  for(uint64_t i = 0;i<N;++i) {
    float s = float(i)/float(N-1);
    float r = std::fmaxf(1.0f-2.0f*s, 0.0f);
    float g = 1.0f-std::fabs(2.0f*s-1.0f);
    float b = std::fmaxf(2.0f*s-1.0f, 0.0f);
    data1d[4*i+0] = r*255u;
    data1d[4*i+1] = g*255u;
    data1d[4*i+2] = b*255u;
    data1d[4*i+3] = 255u;
  }
  ANARIArray1D array1d = anariNewArray1D(d, data1d.data(), 0, 0, ANARI_UFIXED8_VEC4, N);


  float scale2[16] = {
    2.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 2.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f, 0.0f,
   -0.5f,-0.5f, 0.0f, 1.0f,
  };

  float scalehalf[16] = {
    0.5f, 0.0f, 0.0f, 0.0f,
    0.0f, 0.5f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f,
  };


  float sqr = 2.0f*std::sqrt(0.5f);
  float rotate[16] = {
     sqr, -sqr, 0.0f, 0.0f,
     sqr,  sqr, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f,
  };


  float third = 1.0f/3.0f;
  float grayscale[16] = {
    third, third, third, 0.0f,
    third, third, third, 0.0f,
    third, third, third, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f,
  };

  float complementary[16] = {
    0.0f, 1.0f, 1.0f, 0.0f,
    1.0f, 0.0f, 1.0f, 0.0f,
    1.0f, 1.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f,
  };

  float tex3d[16] = {
    1.0f, 0.0f,-0.5f, 0.0f,
    0.0f, 1.0f,-0.5f, 0.0f,
    0.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f, 1.0f,
  };

  materials[16] = setupSamplerMaterial2D(d, array2d, scale2, nullptr, nullptr, "clampToEdge", "clampToEdge");
  materials[17] = setupSamplerMaterial2D(d, array2d, scale2, nullptr, nullptr, "repeat", nullptr);
  materials[18] = setupSamplerMaterial2D(d, array2d, scale2, nullptr, nullptr, "repeat", "repeat");
  materials[19] = setupSamplerMaterial2D(d, array2d, scale2, nullptr, nullptr, "mirrorRepeat", "mirrorRepeat");

  materials[20] = setupSamplerMaterial2D(d, array2d, rotate, nullptr, nullptr, "repeat", "repeat");
  materials[21] = setupSamplerMaterial2D(d, array2d, scalehalf, nullptr, "linear", "repeat", "repeat");
  materials[22] = setupSamplerMaterial2D(d, array2d, nullptr, grayscale, nullptr, nullptr, nullptr);
  materials[23] = setupSamplerMaterial2D(d, array2d, nullptr, complementary, nullptr, nullptr, nullptr);

  materials[24] = setupSamplerMaterial1D(d, array1d, nullptr, nullptr, "linear", nullptr);
  materials[25] = setupSamplerMaterial1D(d, array1d, rotate, nullptr, "linear", "repeat");
  materials[26] = setupSamplerMaterial3D(d, array3d, nullptr, nullptr, "linear", nullptr, nullptr, nullptr);
  materials[27] = setupSamplerMaterial3D(d, array3d, tex3d, nullptr, "linear", nullptr, nullptr, nullptr);

  materials[28] = setupSamplerTransform(d, nullptr);
  materials[29] = setupSamplerTransform(d, complementary);
  materials[30] = setupSamplerPrimitive(d, array1d, 0);
  materials[31] = setupSamplerPrimitive(d, array1d, 16);

  anari::release(d, array1d);
  anari::release(d, array2d);
  anari::release(d, array3d);

  std::vector<ANARIInstance> instances;

  for(int i = 0;i<32;++i) {

    int x = i%4;
    int y = i/4;

    auto inst = anari::newObject<anari::Instance>(d, "transform");
    auto tl = anari::translation_matrix(2.0f*anari::float3(x-3.5f, y-3.5f, 0.f));

    { // NOTE: exercise anari::setParameter with C-array type
      anari::mat4 _xfm = tl;
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


  for(int i = 0;i<32;++i) {
    if(!materials[i]) {
      continue;
    }

    int x = i%4;
    int y = i/4;

    auto inst = anari::newObject<anari::Instance>(d, "transform");
    auto tl = anari::translation_matrix(2.0f*anari::float3(x+0.5f, y-3.5f, 0.f));

    { // NOTE: exercise anari::setParameter with C-array type
      anari::mat4 _xfm = tl;
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
  anari::setParameter(d, light, "direction", anari::float3(0, 0, 1));
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
