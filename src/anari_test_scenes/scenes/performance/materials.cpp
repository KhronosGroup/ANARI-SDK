// Copyright 2021-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "materials.h"

#include "anari/anari_cpp.hpp"
#include "primitives.h"

#include <chrono>
#include <random>

namespace {
constexpr std::uint32_t defaultNumObjects = 1024;
const char *defaultGeometrySubtype = "cone";
constexpr bool defaultCapping = false;
const char *defaultColorBy = "Vertex";
const std::uint32_t defaultMaterialIndex = 0;
const float defaultInterval = 0.01f;

std::vector<std::string> geometrySubtypes(
    {"cone", "curve", "cylinder", "quad", "sphere", "triangle"});

std::uint32_t getVertexCount(const char *subtype)
{
  if (!strcmp(subtype, "cone")) {
    return 2u;
  } else if (!strcmp(subtype, "curve")) {
    return 2u;
  } else if (!strcmp(subtype, "cylinder")) {
    return 2u;
  } else if (!strcmp(subtype, "quad")) {
    return 4u;
  } else if (!strcmp(subtype, "sphere")) {
    return 1u;
  } else if (!strcmp(subtype, "triangle")) {
    return 3u;
  } else {
    return 0u;
  }
}

anari::Array1D make1DCheckerboardVec3f(anari::Device d, int dim)
{
  auto *data = new anari::math::float3[dim];

  for (int w = 0; w < dim; w++) {
    bool even = w & 1;
    if (even)
      data[w] = anari::math::float3(.2f);
    else
      data[w] = anari::math::float3(.8f);
  }

  return anariNewArray1D(
      d,
      data,
      [](const void *ptr, const void *) { std::free(const_cast<void *>(ptr)); },
      nullptr,
      ANARI_FLOAT32_VEC3,
      uint64_t(dim));
}

anari::Array2D make2DCheckerboardVec3f(anari::Device d, int dim)
{
  auto *data = new anari::math::float3[dim * dim];

  for (int h = 0; h < dim; h++) {
    for (int w = 0; w < dim; w++) {
      bool even = h & 1;
      if (even)
        data[h * dim + w] =
            w & 1 ? anari::math::float3(.8f) : anari::math::float3(.2f);
      else
        data[h * dim + w] =
            w & 1 ? anari::math::float3(.2f) : anari::math::float3(.8f);
    }
  }

  return anariNewArray2D(
      d,
      data,
      [](const void *ptr, const void *) { std::free(const_cast<void *>(ptr)); },
      nullptr,
      ANARI_FLOAT32_VEC3,
      uint64_t(dim),
      uint64_t(dim));
}

anari::Array3D make3DCheckerboardVec3f(anari::Device d, int dim)
{
  auto *data = new anari::math::float3[dim * dim * dim];

  for (int l = 0; l < dim; l++) {
    for (int h = 0; h < dim; h++) {
      for (int w = 0; w < dim; w++) {
        bool even = l & 1;
        if (even)
          data[l * dim * dim + h * dim + w] = h & 1
              ? anari::math::float3(.8f)
              : (w & 1 ? anari::math::float3(.8f) : anari::math::float3(.2f));
        else
          data[l * dim * dim + h * dim + w] = h & 1
              ? anari::math::float3(.2f)
              : (w & 1 ? anari::math::float3(.2f) : anari::math::float3(.8f));
      }
    }
  }

  return anariNewArray3D(
      d,
      data,
      [](const void *ptr, const void *) { std::free(const_cast<void *>(ptr)); },
      nullptr,
      ANARI_FLOAT32_VEC3,
      uint64_t(dim),
      uint64_t(dim),
      uint64_t(dim));
}

anari::Array1D make1DCheckerboardf(anari::Device d, int dim)
{
  auto *data = new float[dim];

  for (int w = 0; w < dim; w++) {
    bool even = w & 1;
    if (even)
      data[w] = .2f;
    else
      data[w] = .8f;
  }

  return anariNewArray1D(
      d,
      data,
      [](const void *ptr, const void *) { std::free(const_cast<void *>(ptr)); },
      nullptr,
      ANARI_FLOAT32,
      uint64_t(dim));
}

anari::Array2D make2DCheckerboardf(anari::Device d, int dim)
{
  auto *data = new float[dim * dim];

  for (int h = 0; h < dim; h++) {
    for (int w = 0; w < dim; w++) {
      bool even = h & 1;
      if (even)
        data[h * dim + w] = w & 1 ? .8f : .2f;
      else
        data[h * dim + w] = w & 1 ? .2f : .8f;
    }
  }

  return anariNewArray2D(
      d,
      data,
      [](const void *ptr, const void *) { std::free(const_cast<void *>(ptr)); },
      nullptr,
      ANARI_FLOAT32,
      uint64_t(dim),
      uint64_t(dim));
}

anari::Array3D make3DCheckerboardf(anari::Device d, int dim)
{
  auto *data = new float[dim * dim * dim];

  for (int d = 0; d < dim; d++) {
    for (int h = 0; h < dim; h++) {
      for (int w = 0; w < dim; w++) {
        bool even = d & 1;
        if (even)
          data[d * dim * dim + h * dim + w] = h & 1 ? .8f : (w & 1 ? .8f : .2f);
        else
          data[d * dim * dim + h * dim + w] = h & 1 ? .2f : (w & 1 ? .2f : .8f);
      }
    }
  }

  return anariNewArray3D(
      d,
      data,
      [](const void *ptr, const void *) { std::free(const_cast<void *>(ptr)); },
      nullptr,
      ANARI_FLOAT32,
      uint64_t(dim),
      uint64_t(dim),
      uint64_t(dim));
}

anari::Array1D make1DNoiseVec3f(anari::Device d, int dim)
{
  auto *data = new anari::math::float3[dim];

  std::mt19937 rng;
  rng.seed(0);
  std::uniform_real_distribution<float> val_dist(0.f, 1.f);
  for (int w = 0; w < dim; w++) {
    data[w].x = val_dist(rng);
    data[w].y = val_dist(rng);
    data[w].z = val_dist(rng);
  }

  return anariNewArray1D(
      d,
      data,
      [](const void *ptr, const void *) { std::free(const_cast<void *>(ptr)); },
      nullptr,
      ANARI_FLOAT32_VEC3,
      uint64_t(dim));
}

anari::Array2D make2DNoiseVec3f(anari::Device d, int dim)
{
  auto *data = new anari::math::float3[dim * dim];

  std::mt19937 rng;
  rng.seed(0);
  std::uniform_real_distribution<float> val_dist(0.f, 1.f);
  int index = 0;
  for (int h = 0; h < dim; h++) {
    for (int w = 0; w < dim; w++) {
      data[index].x = val_dist(rng);
      data[index].y = val_dist(rng);
      data[index++].z = val_dist(rng);
    }
  }

  return anariNewArray2D(
      d,
      data,
      [](const void *ptr, const void *) { std::free(const_cast<void *>(ptr)); },
      nullptr,
      ANARI_FLOAT32_VEC3,
      uint64_t(dim),
      uint64_t(dim));
}

anari::Array3D make3DNoiseVec3f(anari::Device d, int dim)
{
  auto *data = new anari::math::float3[dim * dim * dim];

  std::mt19937 rng;
  rng.seed(0);
  std::uniform_real_distribution<float> val_dist(0.f, 1.f);
  int index = 0;
  for (int l = 0; l < dim; l++) {
    for (int h = 0; h < dim; h++) {
      for (int w = 0; w < dim; w++) {
        data[index].x = val_dist(rng);
        data[index].y = val_dist(rng);
        data[index++].z = val_dist(rng);
      }
    }
  }

  return anariNewArray3D(
      d,
      data,
      [](const void *ptr, const void *) { std::free(const_cast<void *>(ptr)); },
      nullptr,
      ANARI_FLOAT32_VEC3,
      uint64_t(dim),
      uint64_t(dim),
      uint64_t(dim));
}

anari::Array1D make1DNoisef(anari::Device d, int dim)
{
  auto *data = new float[dim];

  std::mt19937 rng;
  rng.seed(0);
  std::uniform_real_distribution<float> val_dist(0.f, 1.f);
  for (int w = 0; w < dim; w++) {
    data[w] = val_dist(rng);
  }

  return anariNewArray1D(
      d,
      data,
      [](const void *ptr, const void *) { std::free(const_cast<void *>(ptr)); },
      nullptr,
      ANARI_FLOAT32,
      uint64_t(dim));
}

anari::Array2D make2DNoisef(anari::Device d, int dim)
{
  auto *data = new float[dim * dim];

  std::mt19937 rng;
  rng.seed(0);
  std::uniform_real_distribution<float> val_dist(0.f, 1.f);
  int index = 0;
  for (int h = 0; h < dim; h++) {
    for (int w = 0; w < dim; w++) {
      data[index++] = val_dist(rng);
    }
  }

  return anariNewArray2D(
      d,
      data,
      [](const void *ptr, const void *) { std::free(const_cast<void *>(ptr)); },
      nullptr,
      ANARI_FLOAT32,
      uint64_t(dim),
      uint64_t(dim));
}

anari::Array3D make3DNoisef(anari::Device d, int dim)
{
  auto *data = new float[dim * dim * dim];

  std::mt19937 rng;
  rng.seed(0);
  std::uniform_real_distribution<float> val_dist(0.f, 1.f);
  int index = 0;
  for (int d = 0; d < dim; d++) {
    for (int h = 0; h < dim; h++) {
      for (int w = 0; w < dim; w++) {
        data[index++] = val_dist(rng);
      }
    }
  }

  return anariNewArray3D(
      d,
      data,
      [](const void *ptr, const void *) { std::free(const_cast<void *>(ptr)); },
      nullptr,
      ANARI_FLOAT32,
      uint64_t(dim),
      uint64_t(dim),
      uint64_t(dim));
}

struct MaterialSpec
{
  enum MaterialSubType
  {
    Matte,
    PBR
  };
  enum SourceType
  {
    Vertex, // string
    Primitive, // string
    Uniform, // float32_vec3/float32
    Sampler // sampler
  };
  enum SamplerSubType
  {
    Image1D,
    Image2D,
    Image3D
  };
  enum SamplerImageFunctionType
  {
    Checkerboard,
    Noise
  };
  /**
   * FLOAT32_VEC3 / SAMPLER / STRING
   */
  struct F3SAMPLERSTR
  {
    SourceType m_source;
    anari::math::float3 m_vec3f32;
    SamplerSubType m_sampler_subtype;
    SamplerImageFunctionType m_sampler_image_function_type;
    std::string m_attribute_name;
  };
  /**
   * FLOAT32 / SAMPLER / STRING
   */
  struct FSAMPLERSTR
  {
    SourceType m_source;
    float m_f32;
    SamplerSubType m_sampler_subtype;
    SamplerImageFunctionType m_sampler_image_function_type;
    std::string m_attribute_name;
  };
  MaterialSubType m_subtype;

  F3SAMPLERSTR m_color;

  F3SAMPLERSTR m_base_color;

  FSAMPLERSTR m_opacity;

  std::string m_alpha_mode;
  float m_alpha_cutoff;

  FSAMPLERSTR m_metallic;

  FSAMPLERSTR m_roughness;

  F3SAMPLERSTR m_normal;

  F3SAMPLERSTR m_emissive;

  F3SAMPLERSTR m_occlusion;

  FSAMPLERSTR m_specular;

  F3SAMPLERSTR m_specular_color;

  FSAMPLERSTR m_clear_coat;

  FSAMPLERSTR m_clear_coat_roughness;

  F3SAMPLERSTR m_clear_coat_normal;

  FSAMPLERSTR m_transmission;

  float m_ior;

  FSAMPLERSTR m_thickness;

  float m_attenuation_distance;

  anari::math::float3 m_attenuation_color;

  F3SAMPLERSTR m_sheen_color;

  FSAMPLERSTR m_sheen_roughness;

  FSAMPLERSTR m_iridescence;

  float m_iridescencelor;

  FSAMPLERSTR m_iridescence_thickness;
};

std::vector<MaterialSpec> materialSpecs = {
    // 0
    MaterialSpec{
        .m_subtype = MaterialSpec::Matte,
        .m_color = MaterialSpec::F3SAMPLERSTR{.m_source = MaterialSpec::Uniform,
            .m_vec3f32 = {1.0f, 0.0f, 0.0f}},
        .m_opacity =
            MaterialSpec::FSAMPLERSTR{
                .m_source = MaterialSpec::Uniform,
                .m_f32 = 1.0f,
            },
        .m_alpha_mode = "opaque",
    },
    // 1
    MaterialSpec{
        .m_subtype = MaterialSpec::Matte,
        .m_color = MaterialSpec::F3SAMPLERSTR{.m_source = MaterialSpec::Uniform,
            .m_vec3f32 = {0.0f, 1.0f, 0.0f}},
        .m_opacity =
            MaterialSpec::FSAMPLERSTR{
                .m_source = MaterialSpec::Uniform,
                .m_f32 = 1.0f,
            },
        .m_alpha_mode = "opaque",
    },
    // 2
    MaterialSpec{
        .m_subtype = MaterialSpec::Matte,
        .m_color = MaterialSpec::F3SAMPLERSTR{.m_source = MaterialSpec::Uniform,
            .m_vec3f32 = {0.0f, 0.0f, 1.0f}},
        .m_opacity =
            MaterialSpec::FSAMPLERSTR{
                .m_source = MaterialSpec::Uniform,
                .m_f32 = 1.0f,
            },
        .m_alpha_mode = "opaque",
    },
    // 3
    MaterialSpec{
        .m_subtype = MaterialSpec::Matte,
        .m_color = MaterialSpec::F3SAMPLERSTR{.m_source = MaterialSpec::Uniform,
            .m_vec3f32 = {1.0f, 1.0f, 1.0f}},
        .m_opacity =
            MaterialSpec::FSAMPLERSTR{
                .m_source = MaterialSpec::Uniform,
                .m_f32 = 1.0f,
            },
        .m_alpha_mode = "opaque",
    },
    // 4
    MaterialSpec{
        .m_subtype = MaterialSpec::Matte,
        .m_color = MaterialSpec::F3SAMPLERSTR{.m_source = MaterialSpec::Uniform,
            .m_vec3f32 = {1.0f, 1.0f, 1.0f}},
        .m_opacity =
            MaterialSpec::FSAMPLERSTR{
                .m_source = MaterialSpec::Uniform,
                .m_f32 = 0.5f,
            },
        .m_alpha_mode = "blend",
    },
    // 5
    MaterialSpec{
        .m_subtype = MaterialSpec::Matte,
        .m_color = MaterialSpec::F3SAMPLERSTR{.m_source = MaterialSpec::Uniform,
            .m_vec3f32 = {1.0f, 1.0f, 1.0f}},
        .m_opacity = MaterialSpec::FSAMPLERSTR{.m_source = MaterialSpec::Vertex,
            .m_attribute_name = "attribute1"},
        .m_alpha_mode = "blend",
    },
    // 6
    MaterialSpec{
        .m_subtype = MaterialSpec::Matte,
        .m_color = MaterialSpec::F3SAMPLERSTR{.m_source = MaterialSpec::Uniform,
            .m_vec3f32 = {1.0f, 0.0f, 1.0f}},
        .m_opacity =
            MaterialSpec::FSAMPLERSTR{.m_source = MaterialSpec::Primitive,
                .m_attribute_name = "attribute1"},
        .m_alpha_mode = "blend",
    },
    // 7
    MaterialSpec{
        .m_subtype = MaterialSpec::Matte,
        .m_color = MaterialSpec::F3SAMPLERSTR{.m_source = MaterialSpec::Uniform,
            .m_vec3f32 = {1.0f, 1.0f, 1.0f}},
        .m_opacity =
            MaterialSpec::FSAMPLERSTR{.m_source = MaterialSpec::Primitive,
                .m_attribute_name = "attribute1"},
        .m_alpha_mode = "mask",
        .m_alpha_cutoff = 0.7f,
    },
    // 8
    MaterialSpec{
        .m_subtype = MaterialSpec::Matte,
        .m_color = MaterialSpec::F3SAMPLERSTR{.m_source = MaterialSpec::Vertex,
            .m_attribute_name = "color"},
        .m_opacity =
            MaterialSpec::FSAMPLERSTR{
                .m_source = MaterialSpec::Uniform,
                .m_f32 = 1.0f,
            },
        .m_alpha_mode = "opaque",
    },
    // 9
    MaterialSpec{
        .m_subtype = MaterialSpec::Matte,
        .m_color =
            MaterialSpec::F3SAMPLERSTR{.m_source = MaterialSpec::Primitive,
                .m_attribute_name = "color"},
        .m_opacity =
            MaterialSpec::FSAMPLERSTR{
                .m_source = MaterialSpec::Uniform,
                .m_f32 = 1.0f,
            },
        .m_alpha_mode = "opaque",
    },
    // 10
    MaterialSpec{
        .m_subtype = MaterialSpec::Matte,
        .m_color = MaterialSpec::F3SAMPLERSTR{.m_source = MaterialSpec::Vertex,
            .m_attribute_name = "color"},
        .m_opacity =
            MaterialSpec::FSAMPLERSTR{
                .m_source = MaterialSpec::Uniform,
                .m_f32 = 0.4f,
            },
        .m_alpha_mode = "blend",
    },
    // 11
    MaterialSpec{
        .m_subtype = MaterialSpec::Matte,
        .m_color =
            MaterialSpec::F3SAMPLERSTR{.m_source = MaterialSpec::Primitive,
                .m_attribute_name = "color"},
        .m_opacity =
            MaterialSpec::FSAMPLERSTR{
                .m_source = MaterialSpec::Uniform,
                .m_f32 = 0.4f,
            },
        .m_alpha_mode = "blend",
    },
    // 12
    MaterialSpec{
        .m_subtype = MaterialSpec::Matte,
        .m_color = MaterialSpec::F3SAMPLERSTR{.m_source = MaterialSpec::Sampler,
            .m_sampler_subtype = MaterialSpec::Image1D,
            .m_sampler_image_function_type = MaterialSpec::Noise},
        .m_alpha_mode = "opaque",
    },
    // 13
    MaterialSpec{
        .m_subtype = MaterialSpec::Matte,
        .m_color = MaterialSpec::F3SAMPLERSTR{.m_source = MaterialSpec::Sampler,
            .m_sampler_subtype = MaterialSpec::Image1D,
            .m_sampler_image_function_type = MaterialSpec::Noise},
        .m_opacity =
            MaterialSpec::FSAMPLERSTR{
                .m_source = MaterialSpec::Uniform, .m_f32 = 0.5f},
        .m_alpha_mode = "blend",
    },
    // 14
    MaterialSpec{
        .m_subtype = MaterialSpec::Matte,
        .m_color = MaterialSpec::F3SAMPLERSTR{.m_source = MaterialSpec::Sampler,
            .m_sampler_subtype = MaterialSpec::Image2D,
            .m_sampler_image_function_type = MaterialSpec::Noise},
        .m_alpha_mode = "opaque",
    },
    // 15
    MaterialSpec{
        .m_subtype = MaterialSpec::Matte,
        .m_color = MaterialSpec::F3SAMPLERSTR{.m_source = MaterialSpec::Sampler,
            .m_sampler_subtype = MaterialSpec::Image2D,
            .m_sampler_image_function_type = MaterialSpec::Noise},
        .m_opacity =
            MaterialSpec::FSAMPLERSTR{
                .m_source = MaterialSpec::Uniform, .m_f32 = 0.5f},
        .m_alpha_mode = "blend",
    },
    // 16
    MaterialSpec{
        .m_subtype = MaterialSpec::Matte,
        .m_color = MaterialSpec::F3SAMPLERSTR{.m_source = MaterialSpec::Sampler,
            .m_sampler_subtype = MaterialSpec::Image3D,
            .m_sampler_image_function_type = MaterialSpec::Noise},
        .m_alpha_mode = "opaque",
    },
    // 17
    MaterialSpec{
        .m_subtype = MaterialSpec::Matte,
        .m_color = MaterialSpec::F3SAMPLERSTR{.m_source = MaterialSpec::Sampler,
            .m_sampler_subtype = MaterialSpec::Image3D,
            .m_sampler_image_function_type = MaterialSpec::Noise},
        .m_opacity =
            MaterialSpec::FSAMPLERSTR{
                .m_source = MaterialSpec::Uniform, .m_f32 = 0.5f},
        .m_alpha_mode = "blend",
    },
    // 18
    MaterialSpec{
        .m_subtype = MaterialSpec::Matte,
        .m_color = MaterialSpec::F3SAMPLERSTR{.m_source = MaterialSpec::Uniform,
            .m_vec3f32 = {1.0f, 0.0f, 0.0f}},
        .m_opacity =
            MaterialSpec::FSAMPLERSTR{.m_source = MaterialSpec::Sampler,
                .m_sampler_subtype = MaterialSpec::Image1D,
                .m_sampler_image_function_type = MaterialSpec::Noise},
        .m_alpha_mode = "blend",
    },
    // 19
    MaterialSpec{
        .m_subtype = MaterialSpec::PBR,
        .m_base_color =
            MaterialSpec::F3SAMPLERSTR{.m_source = MaterialSpec::Uniform,
                .m_vec3f32 = {1.0f, 0.0f, 0.0f}},
    },
    // 20
    MaterialSpec{.m_subtype = MaterialSpec::PBR,
        .m_base_color =
            MaterialSpec::F3SAMPLERSTR{
                .m_source = MaterialSpec::Vertex, .m_attribute_name = "color"}},
    // 21
    MaterialSpec{.m_subtype = MaterialSpec::PBR,
        .m_base_color =
            MaterialSpec::F3SAMPLERSTR{.m_source = MaterialSpec::Primitive,
                .m_attribute_name = "color"}},
    // 22
    MaterialSpec{.m_subtype = MaterialSpec::PBR,
        .m_base_color =
            MaterialSpec::F3SAMPLERSTR{.m_source = MaterialSpec::Sampler,
                .m_sampler_subtype = MaterialSpec::Image2D,
                .m_sampler_image_function_type =
                    MaterialSpec::SamplerImageFunctionType::Noise},
        .m_metallic =
            MaterialSpec::FSAMPLERSTR{.m_source = MaterialSpec::Sampler,
                .m_sampler_subtype = MaterialSpec::Image2D,
                .m_sampler_image_function_type =
                    MaterialSpec::SamplerImageFunctionType::Noise},
        .m_roughness =
            MaterialSpec::FSAMPLERSTR{.m_source = MaterialSpec::Sampler,
                .m_sampler_subtype = MaterialSpec::Image2D,
                .m_sampler_image_function_type =
                    MaterialSpec::SamplerImageFunctionType::Noise},
        .m_normal =
            MaterialSpec::F3SAMPLERSTR{.m_source = MaterialSpec::Sampler,
                .m_sampler_subtype = MaterialSpec::Image2D,
                .m_sampler_image_function_type =
                    MaterialSpec::SamplerImageFunctionType::Noise},
        .m_emissive =
            MaterialSpec::F3SAMPLERSTR{.m_source = MaterialSpec::Sampler,
                .m_sampler_subtype = MaterialSpec::Image2D,
                .m_sampler_image_function_type =
                    MaterialSpec::SamplerImageFunctionType::Noise},
        .m_occlusion =
            MaterialSpec::F3SAMPLERSTR{.m_source = MaterialSpec::Sampler,
                .m_sampler_subtype = MaterialSpec::Image2D,
                .m_sampler_image_function_type =
                    MaterialSpec::SamplerImageFunctionType::Noise},
        .m_specular =
            MaterialSpec::FSAMPLERSTR{.m_source = MaterialSpec::Sampler,
                .m_sampler_subtype = MaterialSpec::Image2D,
                .m_sampler_image_function_type =
                    MaterialSpec::SamplerImageFunctionType::Noise},
        .m_specular_color =
            MaterialSpec::F3SAMPLERSTR{.m_source = MaterialSpec::Sampler,
                .m_sampler_subtype = MaterialSpec::Image2D,
                .m_sampler_image_function_type =
                    MaterialSpec::SamplerImageFunctionType::Noise},
        .m_clear_coat =
            MaterialSpec::FSAMPLERSTR{.m_source = MaterialSpec::Sampler,
                .m_sampler_subtype = MaterialSpec::Image2D,
                .m_sampler_image_function_type =
                    MaterialSpec::SamplerImageFunctionType::Noise},
        .m_clear_coat_roughness =
            MaterialSpec::FSAMPLERSTR{.m_source = MaterialSpec::Sampler,
                .m_sampler_subtype = MaterialSpec::Image2D,
                .m_sampler_image_function_type =
                    MaterialSpec::SamplerImageFunctionType::Noise},
        .m_clear_coat_normal =
            MaterialSpec::F3SAMPLERSTR{.m_source = MaterialSpec::Sampler,
                .m_sampler_subtype = MaterialSpec::Image2D,
                .m_sampler_image_function_type =
                    MaterialSpec::SamplerImageFunctionType::Noise},
        .m_transmission =
            MaterialSpec::FSAMPLERSTR{.m_source = MaterialSpec::Sampler,
                .m_sampler_subtype = MaterialSpec::Image2D,
                .m_sampler_image_function_type =
                    MaterialSpec::SamplerImageFunctionType::Noise},
        .m_ior = 1.2f,
        .m_thickness =
            MaterialSpec::FSAMPLERSTR{.m_source = MaterialSpec::Sampler,
                .m_sampler_subtype = MaterialSpec::Image2D,
                .m_sampler_image_function_type =
                    MaterialSpec::SamplerImageFunctionType::Noise},
        .m_attenuation_color = {1.0f, 1.f, 1.f},
        .m_sheen_color =
            MaterialSpec::F3SAMPLERSTR{.m_source = MaterialSpec::Sampler,
                .m_sampler_subtype = MaterialSpec::Image2D,
                .m_sampler_image_function_type =
                    MaterialSpec::SamplerImageFunctionType::Noise},
        .m_sheen_roughness =
            MaterialSpec::FSAMPLERSTR{.m_source = MaterialSpec::Sampler,
                .m_sampler_subtype = MaterialSpec::Image2D,
                .m_sampler_image_function_type =
                    MaterialSpec::SamplerImageFunctionType::Noise},
        .m_iridescence =
            MaterialSpec::FSAMPLERSTR{.m_source = MaterialSpec::Sampler,
                .m_sampler_subtype = MaterialSpec::Image2D,
                .m_sampler_image_function_type =
                    MaterialSpec::SamplerImageFunctionType::Noise},
        .m_iridescencelor = 1.25,
        .m_iridescence_thickness =
            MaterialSpec::FSAMPLERSTR{.m_source = MaterialSpec::Sampler,
                .m_sampler_subtype = MaterialSpec::Image2D,
                .m_sampler_image_function_type =
                    MaterialSpec::SamplerImageFunctionType::Noise}}

};

void setMaterialParameters(anari::Device d,
    anari::Material material,
    const char *parameterName,
    const MaterialSpec::F3SAMPLERSTR &samplerSpec)
{
  switch (samplerSpec.m_source) {
  case MaterialSpec::Vertex:
  case MaterialSpec::Primitive:
    anari::setParameter(
        d, material, parameterName, samplerSpec.m_attribute_name.c_str());
    break;
  case MaterialSpec::Uniform:
    anari::setParameter(d, material, parameterName, samplerSpec.m_vec3f32);
    break;
  case MaterialSpec::Sampler: {
    anari::Sampler sampler;
    switch (samplerSpec.m_sampler_subtype) {
    case MaterialSpec::Image1D:
      sampler = anari::newObject<anari::Sampler>(d, "image1D");
      switch (samplerSpec.m_sampler_image_function_type) {
      case MaterialSpec::Checkerboard:
        anari::setAndReleaseParameter(
            d, sampler, "image", make1DCheckerboardVec3f(d, 8));
        break;
      case MaterialSpec::Noise:
        anari::setAndReleaseParameter(
            d, sampler, "image", make1DNoiseVec3f(d, 8));
        break;
      }
      anari::setParameter(d, sampler, "inAttribute", "attribute0");
      break;
    case MaterialSpec::Image2D:
      sampler = anari::newObject<anari::Sampler>(d, "image2D");
      switch (samplerSpec.m_sampler_image_function_type) {
      case MaterialSpec::Checkerboard:
        anari::setAndReleaseParameter(
            d, sampler, "image", make2DCheckerboardVec3f(d, 8));
        break;
      case MaterialSpec::Noise:
        anari::setAndReleaseParameter(
            d, sampler, "image", make2DNoiseVec3f(d, 8));
        break;
      }
      anari::setParameter(d, sampler, "inAttribute", "attribute1");
      break;
    case MaterialSpec::Image3D:
      sampler = anari::newObject<anari::Sampler>(d, "image3D");
      switch (samplerSpec.m_sampler_image_function_type) {
      case MaterialSpec::Checkerboard:
        anari::setAndReleaseParameter(
            d, sampler, "image", make3DCheckerboardVec3f(d, 8));
        break;
      case MaterialSpec::Noise:
        anari::setAndReleaseParameter(
            d, sampler, "image", make3DNoiseVec3f(d, 8));
        break;
      }
      anari::setParameter(d, sampler, "inAttribute", "attribute2");
      break;
    }
    anari::commitParameters(d, sampler);
    anari::setAndReleaseParameter(d, material, parameterName, sampler);
  } break;
  }
}

void setMaterialParameters(anari::Device d,
    anari::Material material,
    const char *parameterName,
    const MaterialSpec::FSAMPLERSTR &samplerSpec)
{
  switch (samplerSpec.m_source) {
  case MaterialSpec::Vertex:
  case MaterialSpec::Primitive:
    anari::setParameter(
        d, material, parameterName, samplerSpec.m_attribute_name.c_str());
    break;
  case MaterialSpec::Uniform:
    anari::setParameter(d, material, parameterName, samplerSpec.m_f32);
    break;
  case MaterialSpec::Sampler: {
    anari::Sampler sampler;
    switch (samplerSpec.m_sampler_subtype) {
    case MaterialSpec::Image1D:
      sampler = anari::newObject<anari::Sampler>(d, "image1D");
      switch (samplerSpec.m_sampler_image_function_type) {
      case MaterialSpec::Checkerboard:
        anari::setAndReleaseParameter(
            d, sampler, "image", make1DCheckerboardf(d, 8));
        break;
      case MaterialSpec::Noise:
        anari::setAndReleaseParameter(d, sampler, "image", make1DNoisef(d, 8));
        break;
      }
      anari::setParameter(d, sampler, "inAttribute", "attribute0");
      break;
    case MaterialSpec::Image2D:
      sampler = anari::newObject<anari::Sampler>(d, "image2D");
      switch (samplerSpec.m_sampler_image_function_type) {
      case MaterialSpec::Checkerboard:
        anari::setAndReleaseParameter(
            d, sampler, "image", make2DCheckerboardf(d, 8));
        break;
      case MaterialSpec::Noise:
        anari::setAndReleaseParameter(d, sampler, "image", make2DNoisef(d, 8));
        break;
      }
      anari::setParameter(d, sampler, "inAttribute", "attribute1");
      break;
    case MaterialSpec::Image3D:
      sampler = anari::newObject<anari::Sampler>(d, "image3D");
      switch (samplerSpec.m_sampler_image_function_type) {
      case MaterialSpec::Checkerboard:
        anari::setAndReleaseParameter(
            d, sampler, "image", make3DCheckerboardf(d, 8));
        break;
      case MaterialSpec::Noise:
        anari::setAndReleaseParameter(d, sampler, "image", make3DNoisef(d, 8));
        break;
      }
      anari::setParameter(d, sampler, "inAttribute", "attribute2");
      break;
    }
    anari::commitParameters(d, sampler);
    anari::setAndReleaseParameter(d, material, parameterName, sampler);
  } break;
  }
}

anari::Material generateMaterial(anari::Device &d, const MaterialSpec *spec)
{
  anari::Material material{nullptr};
  switch (spec->m_subtype) {
  case MaterialSpec::Matte:
    material = anari::newObject<anari::Material>(d, "matte");
    // color
    ::setMaterialParameters(d, material, "color", spec->m_color);
    break;
  case MaterialSpec::PBR:
    material = anari::newObject<anari::Material>(d, "physicallyBased");
    // baseColor
    ::setMaterialParameters(d, material, "baseColor", spec->m_base_color);
    // metallic
    ::setMaterialParameters(d, material, "metallic", spec->m_metallic);
    // roughness
    ::setMaterialParameters(d, material, "roughness", spec->m_roughness);
    // normal
    ::setMaterialParameters(d, material, "normal", spec->m_normal);
    // emissive
    ::setMaterialParameters(d, material, "emissive", spec->m_emissive);
    // occlusion
    ::setMaterialParameters(d, material, "occlusion", spec->m_occlusion);
    // specular
    ::setMaterialParameters(d, material, "specular", spec->m_specular);
    // specular_color
    ::setMaterialParameters(
        d, material, "specular_color", spec->m_specular_color);
    // clear_coat
    ::setMaterialParameters(d, material, "clear_coat", spec->m_clear_coat);
    // clear_coat_roughness
    ::setMaterialParameters(
        d, material, "clear_coat_roughness", spec->m_clear_coat_roughness);
    // clear_coat_normal
    ::setMaterialParameters(
        d, material, "clear_coat_normal", spec->m_clear_coat_normal);
    // transmission
    ::setMaterialParameters(d, material, "transmission", spec->m_transmission);
    // ior
    anari::setParameter(d, material, "ior", spec->m_ior);
    // thickness
    ::setMaterialParameters(d, material, "thickness", spec->m_thickness);
    // attenuation_color
    anari::setParameter(
        d, material, "attenuation_color", spec->m_attenuation_color);
    // sheen_color
    ::setMaterialParameters(d, material, "sheen_color", spec->m_sheen_color);
    // sheen_roughness
    ::setMaterialParameters(
        d, material, "sheen_roughness", spec->m_sheen_roughness);
    // iridescence
    ::setMaterialParameters(d, material, "iridescence", spec->m_iridescence);
    // iridescencelor
    anari::setParameter(d, material, "iridescencelor", spec->m_iridescencelor);
    // iridescence_thickness
    ::setMaterialParameters(
        d, material, "iridescence_thickness", spec->m_iridescence_thickness);
    break;
  }

  // opacity
  ::setMaterialParameters(d, material, "opacity", spec->m_opacity);
  // alphaMode
  anari::setParameter(d, material, "alphaMode", spec->m_alpha_mode.c_str());
  // alphaCutoff
  anari::setParameter(d, material, "alphaCutoff", spec->m_alpha_cutoff);
  anari::commitParameters(d, material);
  return material;
}

} // namespace

namespace anari {
namespace scenes {
Materials::Materials(anari::Device d)
    : TestScene(d), m_world(anari::newObject<anari::World>(m_device))
{
  m_materials.reserve(10);
  for (auto &spec : ::materialSpecs) {
    m_materials.emplace_back(::generateMaterial(d, &spec));
  }
}

Materials::~Materials()
{
  if (m_geometry) {
    anari::release(m_device, m_geometry);
  }
  while (!m_materials.empty()) {
    auto &material = m_materials.back();
    anari::release(m_device, material);
    m_materials.pop_back();
  }
  if (m_surface) {
    anari::release(m_device, m_surface);
  }
  anari::release(m_device, m_world);
}

anari::World Materials::world()
{
  return m_world;
}

std::vector<ParameterInfo> Materials::parameters()
{
  return {
      // clang-format off
      // param, description, default, value, min, max
      {makeParameterInfo("geometry", "Geometry", defaultGeometrySubtype, geometrySubtypes)},
      {makeParameterInfo("numObjects", "Number of objects", defaultNumObjects, 8u, 1u << 20)},
      {makeParameterInfo("enableCapping", "Capping", defaultCapping)},
      {makeParameterInfo("intervalSeconds", "Interval (seconds)", defaultInterval, 0.01f, 4.0f)},
      {makeParameterInfo("materialIdx", "Material index", defaultMaterialIndex, 0u, static_cast<std::uint32_t>(::materialSpecs.size() - 1))},
      // clang-format on
  };
}

void Materials::commit()
{
  auto &d = m_device;

  // Generate lights
  std::array<math::float3, 6> directions;
  directions[0] = {-1, 0, 0};
  directions[1] = {1, 0, 0};
  directions[2] = {0, -1, 0};
  directions[3] = {0, 1, 0};
  directions[4] = {0, 0, 0 - 1};
  directions[5] = {0, 0, 1};
  std::vector<anari::Light> lights;
  for (int i = 0; i < 6; ++i) {
    auto light = anari::newObject<anari::Light>(m_device, "directional");
    anari::setParameter(m_device, light, "direction", directions[i]);
    anari::setParameter(m_device, light, "irradiance", 1.f);
    anari::commitParameters(m_device, light);
    lights.emplace_back(light);
  }
  anari::setAndReleaseParameter(m_device,
      m_world,
      "light",
      anari::newArray1D(m_device, lights.data(), lights.size()));
  for (auto &l : lights) {
    anari::release(m_device, l);
  }

  const std::uint32_t materialIdx =
      getParam<std::uint32_t>("materialIdx", defaultMaterialIndex)
      % m_materials.size();
  if (m_surface) {
    anari::release(d, m_surface);
  }
  m_surface = anari::newObject<anari::Surface>(d);
  this->generateGeometry();
  this->updateGeometryAttributes(materialIdx);
  anari::setParameter(d, m_surface, "geometry", m_geometry);
  anari::setParameter(d, m_surface, "material", m_materials[materialIdx]);
  anari::commitParameters(d, m_surface);
  anari::setAndReleaseParameter(
      d, m_world, "surface", anari::newArray1D(d, &m_surface));

  anari::commitParameters(d, m_world);
}

void Materials::generateGeometry()
{
  auto &d = m_device;
  const auto numObjects =
      getParam<std::uint32_t>("numObjects", defaultNumObjects);
  const auto geometrySubType =
      getParamString("geometry", defaultGeometrySubtype);
  const bool enableCapping = getParam<bool>("enableCapping", defaultCapping);
  const auto colorBy = getParamString("colorBy", defaultColorBy);

  if (m_geometry) {
    anari::release(d, m_geometry);
  }
  m_geometry = Primitives::generateGeometry(
      d, numObjects, geometrySubType.c_str(), enableCapping);

  std::mt19937 rng;
  rng.seed(0);
  std::uniform_real_distribution<float> val_dist(0.f, 1.f);
  const auto vertexCount = getVertexCount(geometrySubType.c_str());
  m_f32_vec2_array.resize(vertexCount * numObjects);
  for (auto &s : m_f32_vec2_array) {
    s.x = val_dist(rng);
    s.y = val_dist(rng);
  }
  m_f32_vec3_array.resize(vertexCount * numObjects);
  for (auto &s : m_f32_vec3_array) {
    s.x = val_dist(rng);
    s.y = val_dist(rng);
    s.z = val_dist(rng);
  }
  m_f32_vec4_array.resize(vertexCount * numObjects);
  for (auto &s : m_f32_vec4_array) {
    s.x = val_dist(rng);
    s.y = val_dist(rng);
    s.z = val_dist(rng);
    s.w = 1.f;
  }
  m_f32_array.resize(vertexCount * numObjects);
  for (auto &s : m_f32_array) {
    s = val_dist(rng);
  }
}

void Materials::updateGeometryAttributes(std::uint32_t materialIdx)
{
  auto &d = m_device;
  const auto numObjects =
      getParam<std::uint32_t>("numObjects", defaultNumObjects);
  const auto geometrySubType =
      getParamString("geometry", defaultGeometrySubtype);

  anari::unsetParameter(d, m_geometry, "vertex.color");
  anari::unsetParameter(d, m_geometry, "primitive.color");

  // activate vertex/primitive color
  const auto &spec = ::materialSpecs[materialIdx];
  switch (spec.m_subtype) {
  case MaterialSpec::Matte:
    switch (spec.m_color.m_source) {
    case MaterialSpec::Vertex:
      anari::setParameterArray1D(d,
          m_geometry,
          "vertex.color",
          m_f32_vec4_array.data(),
          m_f32_vec4_array.size());
      break;
    case MaterialSpec::Primitive:
      anari::setParameterArray1D(d,
          m_geometry,
          "primitive.color",
          m_f32_vec4_array.data(),
          numObjects);
      break;
    case MaterialSpec::Sampler:
    case MaterialSpec::Uniform:
      break;
    }
    break;
  case MaterialSpec::PBR:
    switch (spec.m_base_color.m_source) {
    case MaterialSpec::Vertex:
      anari::setParameterArray1D(d,
          m_geometry,
          "vertex.color",
          m_f32_vec4_array.data(),
          m_f32_vec4_array.size());
      break;
    case MaterialSpec::Primitive:
      anari::setParameterArray1D(d,
          m_geometry,
          "primitive.color",
          m_f32_vec4_array.data(),
          numObjects);
      break;
    case MaterialSpec::Sampler:
    case MaterialSpec::Uniform:
      break;
    }
    break;
  }

  /**
   * attribute0: f32
   * attribute1: vec2f32
   * attribute2: vec3f32
   */
  anari::setParameterArray1D(d,
      m_geometry,
      "vertex.attribute0",
      m_f32_array.data(),
      m_f32_array.size());
  anari::setParameterArray1D(d,
      m_geometry,
      "vertex.attribute1",
      m_f32_vec2_array.data(),
      m_f32_vec2_array.size());
  anari::setParameterArray1D(d,
      m_geometry,
      "vertex.attribute2",
      m_f32_vec3_array.data(),
      m_f32_vec3_array.size());
  anari::commitParameters(d, m_geometry);
}

void Materials::computeNextFrame()
{
  const auto intervalMS =
      getParam<float>("intervalSeconds", defaultInterval) * 1e3f;
  auto timestamp = std::chrono::duration<float, std::milli>(
      std::chrono::steady_clock::now().time_since_epoch())
                       .count();
  m_elapsed_time += (timestamp - m_timestamp);

  if (m_elapsed_time >= intervalMS) {
    const auto materialIdx =
        getParam<std::uint32_t>("materialIdx", defaultMaterialIndex);
    const std::uint32_t newMaterialIdx = (materialIdx + 1) % m_materials.size();
    setParam<std::uint32_t>("materialIdx", newMaterialIdx);
    this->updateGeometryAttributes(newMaterialIdx);
    anari::setParameter(
        m_device, m_surface, "material", m_materials[newMaterialIdx]);
    anari::commitParameters(m_device, m_surface);
    anari::setParameterArray1D(m_device, m_world, "surface", &m_surface, 1);
    anari::commitParameters(m_device, m_world);
    m_elapsed_time = 0;
  }

  m_timestamp = timestamp;
}

TestScene *sceneMaterials(anari::Device d)
{
  return new Materials(d);
}
} // namespace scenes
} // namespace anari
