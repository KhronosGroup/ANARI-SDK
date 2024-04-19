// Copyright 2021-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "glTF.h"
// stb_image
#include "stb_image.h"
// std
#include <fstream>
#include <iostream>

#include "nlohmann/json.hpp"
using json = nlohmann::json;

namespace anari {
namespace scenes {

// FileGLTF definitions ////////////////////////////////////////////////////////

FileGLTF::FileGLTF(anari::Device d) : TestScene(d)
{
  m_world = anari::newObject<anari::World>(m_device);
}

FileGLTF::~FileGLTF()
{
  anari::release(m_device, m_world);
}

std::vector<ParameterInfo> FileGLTF::parameters()
{
  return {
      // clang-format off
      {makeParameterInfo("fileName", ".gltf file to open", "")}
      // clang-format on
  };
}

anari::World FileGLTF::world()
{
  return m_world;
}

static ANARIDataType accessor_type(int c) {
  switch(c-5120) {
    case 0: return ANARI_INT8;
    case 1: return ANARI_UINT8;
    case 2: return ANARI_INT16;
    case 3: return ANARI_UINT16;
    case 5: return ANARI_UINT32;
    case 6: return ANARI_FLOAT32;
    default: return ANARI_UNKNOWN;
  }
}

static ANARIDataType accessor_components(const std::string &str) {
  if(str == "SCALAR") {
    return 1;
  } else if(str.length() != 4) {
    return 0;
  } else if(str[0] == 'V') {
    if(str[3]=='2') {
      return 2;
    } else if(str[3]=='3') {
      return 3;
    } else if(str[3]=='4') {
      return 4;
    } else {
      return 0;
    }
  } else if(str[0] == 'M') {
    if(str[3]=='2') {
      return 4;
    } else if(str[3]=='3') {
      return 9;
    } else if(str[3]=='4') {
      return 16;
    } else {
      return 0;
    }
  } else {
    return 0;
  }
}


static ANARIDataType accessor_element_type(int c, const std::string &str) {
  ANARIDataType baseType = accessor_type(c);
  int components = accessor_components(str);
  if(baseType == ANARI_FLOAT32) {
    switch(components) {
      case 1: return ANARI_FLOAT32;
      case 2: return ANARI_FLOAT32_VEC2;
      case 3: return ANARI_FLOAT32_VEC3;
      case 4: return str[0] == 'V' ? ANARI_FLOAT32_VEC4 : ANARI_FLOAT32_MAT2;
      case 9: return ANARI_FLOAT32_MAT3;
      case 16: return ANARI_FLOAT32_MAT4;
      default: return ANARI_UNKNOWN;
    }
  } else if(components == 1) {
    return baseType;
  } else {
    return ANARI_UNKNOWN;
  }
}

static const char* filter_mode(int mode) {
  if(mode == 9728) {
    return "nearest";
  } else {
    return "linear";
  }
}

static const char* wrap_mode(int mode) {
  if(mode == 10497) {
    return "repeat";
  } else if(mode == 33071) {
    return "clampToEdge";
  } else if(mode == 33648) {
    return "mirrorRepeat";
  } else {
    return "repeat";
  }
}

static const char* texcoord_attribute(int idx) {
  switch(idx) {
    case 0: return "attribute0"; 
    case 1: return "attribute1"; 
    case 2: return "attribute2"; 
    case 3: return "attribute3";
    default: return nullptr; 
  }
}


template<typename T>
void memcpy_to_uint32(void *dst0, const void *src0, size_t count) {
  uint32_t *dst = (uint32_t*)dst0;
  const T *src = (const T*)src0;
  std::copy(src, src+count, dst);
}


struct gltf_context {
  json gltf;
  std::string path;

  ANARIDevice device;
  std::vector<std::vector<char>> buffers;
  std::vector<ANARIArray2D> images;
  std::vector<ANARIMaterial> materials;
  std::vector<ANARIGroup> groups;


  const char* unpack_accessor(int index, ANARIDataType &dataType, size_t &count, size_t &stride) {
    const auto &accessor = gltf["accessors"][index];
    size_t accessorOffset = accessor.value("byteOffset", 0);
    count = accessor["count"];

    dataType = accessor_element_type(accessor["componentType"], accessor["type"]);
    size_t dataSize = anari::sizeOf(dataType);

    const auto &bufferView = gltf["bufferViews"][int(accessor["bufferView"])];
    const std::vector<char> &buffer = buffers.at(bufferView["buffer"]);
    size_t viewOffset = bufferView.value("byteOffset", 0);
    stride = bufferView.value("byteStride", dataSize);

    return buffer.data()+viewOffset+accessorOffset;
  }

  template<typename T>
  ANARISampler configure_sampler(const T &texspec, float *swizzle = nullptr) {
      const auto &tex = gltf["textures"].at(int(texspec["index"]));
      const auto &sampleparams = gltf["samplers"].at(int(tex["sampler"]));
      
      ANARISampler sampler = anariNewSampler(device, "image2D");
      ANARIArray2D source = images.at(tex["source"]);
      anariSetParameter(device, sampler, "image", ANARI_ARRAY2D, &source);
      anariSetParameter(device, sampler, "inAttribute", ANARI_STRING, texcoord_attribute(texspec.value("texCoord", 0)));
      anariSetParameter(device, sampler, "filter", ANARI_STRING, filter_mode(sampleparams.value("magFilter", 9729)));
      anariSetParameter(device, sampler, "wrapMode1", ANARI_STRING, wrap_mode(sampleparams.value("wrapS", 10497)));
      anariSetParameter(device, sampler, "wrapMode2", ANARI_STRING, wrap_mode(sampleparams.value("wrapT", 10497)));
      if(swizzle) {
        anariSetParameter(device, sampler, "outTransform", ANARI_FLOAT32_MAT4, swizzle);        
      }
      anariCommitParameters(device, sampler);
      return sampler;
  }

  void load_assets() {
    for(const auto &buf : gltf["buffers"]) {
      if(buf.contains("uri")) {
        std::ifstream buf_in(path + std::string(buf["uri"]), std::ios::in | std::ios::binary);
        if(buf_in) {
          size_t length = buf["byteLength"];
          buffers.emplace_back(length);
          buf_in.read(buffers.back().data(), length);
        } else {
          buffers.emplace_back();
          std::cout << "failed to open " << path << std::string(buf["uri"]) << std::endl;
        }
      } else {
        buffers.emplace_back();
      }
    }

    for(const auto &img : gltf["images"]) {
      std::string filename = path + std::string(img["uri"]);

      int width, height, n;
      //stbi_set_flip_vertically_on_load(1);
      void *data = stbi_load(filename.c_str(), &width, &height, &n, 0);

      if(data) {
        int texelType = ANARI_UFIXED8_VEC4;
        if (n == 3)
          texelType = ANARI_UFIXED8_VEC3;
        else if (n == 2)
          texelType = ANARI_UFIXED8_VEC2;
        else if (n == 1)
          texelType = ANARI_UFIXED8;

        ANARIArray2D array = anariNewArray2D(device, data,
          [](const void*, const void* appMemory){ stbi_image_free((void*)appMemory); }, 
          nullptr, texelType, width, height);
        images.emplace_back(array);
      } else {
        images.emplace_back(nullptr);
        std::cout << "failed to open " << filename << std::endl;
      }
    }
  }

  void load_materials() {
    for(const auto &mat : gltf["materials"]) {

      // ANARIMaterial material = anariNewMaterial(device, "matte");
      // const auto &pbr = mat["pbrMetallicRoughness"];
      // if(pbr.contains("baseColorTexture")) {
      //   ANARISampler sampler = configure_sampler(pbr["baseColorTexture"]);
      //   anariSetParameter(device, material, "color", ANARI_SAMPLER, &sampler);
      //   anariRelease(device, sampler);

      // } else if(pbr.contains("baseColorFactor")) {
      //   const auto &basecolor = pbr["baseColorFactor"];
      //   float color[4];
      //   std::copy(basecolor.begin(), basecolor.begin()+std::min(4, int(basecolor.size())), color);
      //   anariSetParameter(device, material, "color", ANARI_FLOAT32_VEC3, color);
      // }

      ANARIMaterial material = anariNewMaterial(device, "physicallyBased");
      const auto &pbr = mat["pbrMetallicRoughness"];
      if(pbr.contains("baseColorTexture")) {
        float *swizzle = nullptr;
        if(pbr.contains("baseColorFactor")) {
          const auto &basecolor = pbr["baseColorFactor"];
          float colorSwizzle[16] = {
            basecolor[0], 0.0f, 0.0f, 0.0f,
            0.0f, basecolor[1], 0.0f, 0.0f,
            0.0f, 0.0f, basecolor[2], 0.0f,
            0.0f, 0.0f, 0.0f, basecolor[3],
          };
          swizzle = colorSwizzle;
        }

        ANARISampler sampler = configure_sampler(pbr["baseColorTexture"], swizzle);
        anariSetParameter(device, material, "baseColor", ANARI_SAMPLER, &sampler);
        anariRelease(device, sampler);

      } else if(pbr.contains("baseColorFactor")) {
        const auto &basecolor = pbr["baseColorFactor"];
        float color[4] = {0.0f ,0.0f ,0.0f ,1.0f };
        std::copy(basecolor.begin(), basecolor.begin()+std::min(4, int(basecolor.size())), color);
        anariSetParameter(device, material, "baseColor", ANARI_FLOAT32_VEC3, color);
        anariSetParameter(device, material, "opacity", ANARI_FLOAT32, color+3);
      }

      anariSetParameter(device, material, "alphaMode", ANARI_STRING, "blend");


      if(pbr.contains("metallicRoughnessTexture")) {
        float metallic = pbr.value("metallicFactor", 1);
        float roughness = pbr.value("roughnessFactor", 1);
        
        float metallicSwizzle[16] = {
          0.0f, 0.0f, 0.0f, 0.0f,
          0.0f, 0.0f, 0.0f, 0.0f,
          metallic, 0.0f, 0.0f, 0.0f,
          0.0f, 0.0f, 0.0f, 0.0f,
        };
        float roughnessSwizzle[16] = {
          0.0f, 0.0f, 0.0f, 0.0f,
          roughness, 0.0f, 0.0f, 0.0f,
          0.0f, 0.0f, 0.0f, 0.0f,
          0.0f, 0.0f, 0.0f, 0.0f,
        };


        ANARISampler metallicSampler = configure_sampler(pbr["metallicRoughnessTexture"], metallicSwizzle);
        anariSetParameter(device, material, "metallic", ANARI_SAMPLER, &metallicSampler);
        anariRelease(device, metallicSampler);

        ANARISampler roughnessSampler = configure_sampler(pbr["metallicRoughnessTexture"], roughnessSwizzle);
        anariSetParameter(device, material, "roughness", ANARI_SAMPLER, &roughnessSampler);
        anariRelease(device, roughnessSampler);
      } else {
        if(pbr.contains("metallicFactor")) {
          float metallic = pbr["metallicFactor"];
          anariSetParameter(device, material, "metallic", ANARI_FLOAT32, &metallic);
        }

        if(pbr.contains("roughnessFactor")) {
          float roughness = pbr["roughnessFactor"];
          anariSetParameter(device, material, "roughness", ANARI_FLOAT32, &roughness);
        }
      }


      anariCommitParameters(device, material);
      materials.emplace_back(material);
    }
  }

  void load_surfaces() {

    ANARIMaterial defaultMaterial = anariNewMaterial(device, "matte");
    float materialColor[] = {1.0f, 0.0f, 1.0f};
    anariSetParameter(device, defaultMaterial, "color", ANARI_FLOAT32_VEC3, materialColor);
    anariCommitParameters(device, defaultMaterial);


    for(const auto &mesh : gltf["meshes"]) {
      std::vector<ANARISurface> surfaces;

      for(const auto &prim : mesh["primitives"]) {
        int mode = prim.value("mode", 4);
       
        ANARIGeometry geometry = anariNewGeometry(device, "triangle");
        for(const auto &attr : prim["attributes"].items()) {

          const char *paramname = "";
          if(attr.key() == "POSITION") {
            paramname = "vertex.position";
          } else if(attr.key() == "NORMAL") {
            paramname = "vertex.normal";
          } else if(attr.key() == "TANGENT") {
            paramname = "vertex.tangent";
          } else if(attr.key() == "TEXCOORD_0") {
            paramname = "vertex.attribute0";
          } else if(attr.key() == "TEXCOORD_1") {
            paramname = "vertex.attribute1";
          } else if(attr.key() == "TEXCOORD_2") {
            paramname = "vertex.attribute2";
          } else if(attr.key() == "TEXCOORD_3") {
            paramname = "vertex.attribute3";
          } else {
            continue;
          }

          size_t stride;
          size_t count;
          ANARIDataType dataType;
          const char *src = unpack_accessor(attr.value(), dataType, count, stride);
          size_t dataSize = anari::sizeOf(dataType);

          uint64_t elementStride;
          char *dst = (char*)anariMapParameterArray1D(device, geometry, paramname, dataType, count, &elementStride);

          if(elementStride == stride) {
            std::memcpy(dst, src, elementStride*count);
          } else {
            for(size_t i = 0; i<count;++i) {
              std::memcpy(dst+i*elementStride, src+i*stride, dataSize);
            }          
          }
          anariUnmapParameterArray(device, geometry, paramname);

        }

        if(prim.contains("indices")) {
          size_t stride;
          size_t count;
          ANARIDataType dataType;
          const char *src = unpack_accessor(prim["indices"], dataType, count, stride);
          size_t dataSize = anari::sizeOf(dataType);


          uint64_t elementStride;
          // should probably handle strides
          char *dst = (char*)anariMapParameterArray1D(device, geometry, "primitive.index", ANARI_UINT32_VEC3, count/3, &elementStride);
          if(dataType == ANARI_UINT32) {
            memcpy_to_uint32<uint32_t>(dst, src, count);
          } else if(dataType == ANARI_UINT16) {
            memcpy_to_uint32<uint16_t>(dst, src, count);
          } else if(dataType == ANARI_UINT8) {
            memcpy_to_uint32<uint8_t>(dst, src, count);
          } else if(dataType == ANARI_INT16) {
            memcpy_to_uint32<int16_t>(dst, src, count);
          } else if(dataType == ANARI_INT8) {
            memcpy_to_uint32<int8_t>(dst, src, count);
          }
          anariUnmapParameterArray(device, geometry, "primitive.index");
        }

        anariCommitParameters(device, geometry);


        ANARISurface surface = anariNewSurface(device);
        anariSetParameter(device, surface, "geometry", ANARI_GEOMETRY, &geometry);

        if(prim.contains("material")) {
          ANARIMaterial mat = materials.at(prim["material"]);
          anariSetParameter(device, surface, "material", ANARI_MATERIAL, &mat);
        } else {
          anariSetParameter(device, surface, "material", ANARI_MATERIAL, &defaultMaterial);
        }
        anariCommitParameters(device, surface);
        anariRelease(device, geometry);

        surfaces.push_back(surface);
      }

      ANARIGroup group = anariNewGroup(device);
      {
        uint64_t elementStride;
        ANARISurface *mapped = (ANARISurface*)anariMapParameterArray1D(device, group, "surface", ANARI_SURFACE, surfaces.size(), &elementStride);
        std::copy(surfaces.begin(), surfaces.end(), mapped);
        anariUnmapParameterArray(device, group, "surface");
      }
      anariCommitParameters(device, group);
      groups.emplace_back(group);
    }
  }

  void init() {
    load_assets();
    load_materials();
    load_surfaces();
  }

  ~gltf_context() {
    for(auto obj : images) {
      anariRelease(device, obj);
    }
    for(auto obj : groups) {
      anariRelease(device, obj);
    }
    for(auto obj : materials) {
      anariRelease(device, obj);
    }
  }

};


void FileGLTF::commit()
{
  if (!hasParam("fileName"))
    return;

  std::string filename = getParamString("fileName", "");
  std::string path = "";
  auto pos = filename.find_last_of('/');
  if(pos != std::string::npos) {
   path = filename.substr(0, pos+1);
  }

  std::ifstream gltf_in(filename.c_str());
  //json gltf = json::parse(gltf_in);

  gltf_context ctx;
  ctx.device = m_device;
  ctx.path = path;
  ctx.gltf = json::parse(gltf_in);

  ctx.init();

  {
    uint64_t elementStride;
    ANARIInstance *mapped = (ANARIInstance*)anariMapParameterArray1D(m_device, m_world, "instance", ANARI_INSTANCE, ctx.groups.size(), &elementStride);
    for(size_t i = 0;i<ctx.groups.size();++i) {
      mapped[i] = anariNewInstance(m_device, "transform");
      anariSetParameter(m_device, mapped[i], "group", ANARI_GROUP, &ctx.groups[i]);
      anariCommitParameters(m_device, mapped[i]);
    }
    anariUnmapParameterArray(m_device, m_world, "instance");
  }
  anariCommitParameters(m_device, m_world);


}

TestScene *sceneFileGLTF(anari::Device d)
{
  return new FileGLTF(d);
}

} // namespace scenes
} // namespace anari
