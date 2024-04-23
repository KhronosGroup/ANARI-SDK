// Copyright 2021-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "glTF.h"
// stb_image
#include "stb_image.h"
// std
#include <fstream>
#include <iostream>
#include <iomanip>

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

static uint32_t base64_sextet(char c) {
  if('A' <= c && c <= 'Z') {
    return 0+c-'A';
  } else if('a' <= c && c <= 'z') {
    return 26+c-'a';
  } else if('0' <= c && c <= '9') {
    return 52+c-'0';
  } else if(c == '+') {
    return 62;
  } else if(c == '/') {
    return 63;
  } else if(c == '=') {
    return 0;
  } else {
    return 0xFFFFu;
  }
}

static void debase64(const char *in, size_t N_in, std::vector<char> out) {
  int bitcount = 0;
  uint32_t bits = 0;
  for(size_t i = 0;i<N_in;++i) {
    uint32_t sextet = base64_sextet(in[i]);
    if(sextet<64u) {
      bits = (bits<<6u) | sextet;
      bitcount += 6;
      if(bitcount>=8) {
        bitcount -= 8;
        out.push_back((char)((bits>>bitcount)&255u));
      }
    }
  }
}

static void matmul(float *A, float *B, float *C) {
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      float dot = 0;
      for (int k = 0; k < 4; ++k) {
        dot += B[i + 4 * k] * C[k + 4 * j];
      }
      A[i + 4 * j] = dot;
    }
  }
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
      if(!tex.contains("source")) {
        return nullptr;
      }

      ANARIArray2D source = images.at(tex["source"]);
      ANARISampler sampler = anariNewSampler(device, "image2D");
      anariSetParameter(device, sampler, "image", ANARI_ARRAY2D, &source);
      anariSetParameter(device, sampler, "inAttribute", ANARI_STRING, texcoord_attribute(texspec.value("texCoord", 0)));

      if(tex.contains("sampler")) {
        const auto &sampleparams = gltf["samplers"].at(int(tex["sampler"]));
        anariSetParameter(device, sampler, "filter", ANARI_STRING, filter_mode(sampleparams.value("magFilter", 9729)));
        anariSetParameter(device, sampler, "wrapMode1", ANARI_STRING, wrap_mode(sampleparams.value("wrapS", 10497)));
        anariSetParameter(device, sampler, "wrapMode2", ANARI_STRING, wrap_mode(sampleparams.value("wrapT", 10497)));
      } else {
        anariSetParameter(device, sampler, "filter", ANARI_STRING, "linear");
        anariSetParameter(device, sampler, "wrapMode1", ANARI_STRING, "repeat");
        anariSetParameter(device, sampler, "wrapMode2", ANARI_STRING, "repeat");
      }
      if(swizzle) {
        anariSetParameter(device, sampler, "outTransform", ANARI_FLOAT32_MAT4, swizzle);        
      }
      anariCommitParameters(device, sampler);
      return sampler;
  }

  template<typename T>
  std::vector<char> decode_buffer(const T &buf) {
    std::vector<char> data;

    if(buf.contains("uri")) {
      std::string uri = buf["uri"];
      size_t length = buf["byteLength"];
      // base64 encoded data uri
      if(uri.substr(0, 4) == "data") {
        auto comma = uri.find_first_of(',');
        if(comma != std::string::npos) {
          data.reserve(length);
          debase64(uri.c_str()+comma, uri.size()-comma, data);
        }
      } else {
        // bin file
        data.resize(length);
        std::ifstream buf_in(path + uri, std::ios::in | std::ios::binary);
        if(buf_in) {
          buf_in.read(data.data(), length);
        }          
      }
    }
    return data;
  }

  template<typename T>
  void* decode_image(const T &img, int *width, int *height, int *n) {
    if(img.contains("uri")) {
      std::string uri = img["uri"];
      if(uri.substr(0, 4) == "data") {
        auto comma = uri.find_first_of(',');
        if(comma != std::string::npos) {
          std::vector<char> buffer;
          debase64(uri.c_str()+comma, uri.size()-comma, buffer);
          return stbi_load_from_memory((const stbi_uc*)buffer.data(), buffer.size(), width, height, n, 0);
        }
      } else {
        std::string filename = path + uri;
        return stbi_load(filename.c_str(), width, height, n, 0);
      }        
    } else if(img.contains("bufferView")) {
      const auto &bufferView = gltf["bufferViews"][int(img["bufferView"])];
      const std::vector<char> &buffer = buffers.at(bufferView["buffer"]);
      size_t offset = bufferView.value("byteOffset", 0);
      size_t length = bufferView["byteLength"];
      return stbi_load_from_memory((const stbi_uc*)buffer.data()+offset, length, width, height, n, 0);
    }
    return nullptr;
  }

  void load_assets() {    
    for(const auto &buf : gltf["buffers"]) {
      decode_buffer(buf);
    }

    for(const auto &img : gltf["images"]) {

      int width, height, n;
      void *data = nullptr;

      data = decode_image(img, &width, &height, &n);

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
        if(sampler) {
          anariSetParameter(device, material, "baseColor", ANARI_SAMPLER, &sampler);
          anariRelease(device, sampler);
        }
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
        if(metallicSampler) {
          anariSetParameter(device, material, "metallic", ANARI_SAMPLER, &metallicSampler);
          anariRelease(device, metallicSampler);
        }
        ANARISampler roughnessSampler = configure_sampler(pbr["metallicRoughnessTexture"], roughnessSwizzle);
        if(roughnessSampler) {
          anariSetParameter(device, material, "roughness", ANARI_SAMPLER, &roughnessSampler);
          anariRelease(device, roughnessSampler);
        }
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

        // mode
        // 0 POINTS
        // 1 LINES
        // 2 LINE_LOOP
        // 3 LINE_STRIP
        // 4 TRIANGLES
        // 5 TRIANGLE_STRIP
        // 6 TRIANGLE_FAN

        if(mode != 4) {
          continue;
        }

        ANARIGeometry geometry = anariNewGeometry(device, "triangle");
        for(const auto &attr : prim["attributes"].items()) {

          const char *paramname = "";
          if(attr.key() == "POSITION") {
            paramname = "vertex.position";
          } else if(attr.key() == "NORMAL") {
            paramname = "vertex.normal";
          } else if(attr.key() == "TANGENT") {
            paramname = "vertex.tangent";
          } else if(attr.key() == "COLOR_0") {
            paramname = "vertex.color";
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
    if(gltf.contains("extensionsUsed")) {
      std::cout << "extensions:" << std::endl;
      for(const auto &ext : gltf["extensionsUsed"]) {
        std::cout << ext << std::endl;
      }      
    }
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
  pos = filename.find_last_of('.');
  std::string ext = "";
  if(pos != std::string::npos) {
    ext = filename.substr(pos);
  }

  gltf_context ctx;
  ctx.path = path;
  ctx.device = m_device;
  

  if(ext==".gltf") {
    std::ifstream gltf_in(filename.c_str());
    ctx.gltf = json::parse(gltf_in);
  } else if(ext==".glb") {
    std::ifstream gltf_in(filename.c_str(), std::ios::in | std::ios::binary);
    uint32_t magic;
    uint32_t version;
    uint32_t length;
    gltf_in.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    if(magic != 0x46546C67u) {
      throw std::runtime_error("invalid gltf magic");
    }
    gltf_in.read(reinterpret_cast<char*>(&version), sizeof(version));
    gltf_in.read(reinterpret_cast<char*>(&length), sizeof(length));

    uint32_t chunkLength;
    uint32_t chunkType;
    gltf_in.read(reinterpret_cast<char*>(&chunkLength), sizeof(chunkLength));
    gltf_in.read(reinterpret_cast<char*>(&chunkType), sizeof(chunkType));
    std::vector<uint8_t> json_buf(chunkLength);
    gltf_in.read(reinterpret_cast<char*>(json_buf.data()), chunkLength);
    ctx.gltf = json::parse(json_buf);

    if(gltf_in.read(reinterpret_cast<char*>(&chunkLength), sizeof(chunkLength))) {
      gltf_in.read(reinterpret_cast<char*>(&chunkType), sizeof(chunkType));
      ctx.buffers.emplace_back(chunkLength);
      gltf_in.read(reinterpret_cast<char*>(ctx.buffers.back().data()), chunkLength);      
    }
  } else if(ext==".b3dm") {
    std::ifstream gltf_in(filename.c_str(), std::ios::in | std::ios::binary);
    uint32_t magic;
    uint32_t version;
    uint32_t length;

    gltf_in.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    gltf_in.read(reinterpret_cast<char*>(&version), sizeof(version));
    gltf_in.read(reinterpret_cast<char*>(&length), sizeof(length));

    uint32_t featureTableJSONByteLength;
    uint32_t featureTableBinaryByteLength;

    uint32_t batchTableJSONByteLength;
    uint32_t batchTableBinaryByteLength;

    gltf_in.read(reinterpret_cast<char*>(&featureTableJSONByteLength), sizeof(featureTableJSONByteLength));
    gltf_in.read(reinterpret_cast<char*>(&featureTableBinaryByteLength), sizeof(featureTableBinaryByteLength));
    gltf_in.read(reinterpret_cast<char*>(&batchTableJSONByteLength), sizeof(batchTableJSONByteLength));
    gltf_in.read(reinterpret_cast<char*>(&batchTableBinaryByteLength), sizeof(batchTableBinaryByteLength));
    gltf_in.ignore(featureTableJSONByteLength);
    gltf_in.ignore(featureTableBinaryByteLength);
    gltf_in.ignore(batchTableJSONByteLength);
    gltf_in.ignore(batchTableBinaryByteLength);


    gltf_in.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    if(magic != 0x46546C67u) {
      throw std::runtime_error("invalid gltf magic");
    }
    gltf_in.read(reinterpret_cast<char*>(&version), sizeof(version));
    gltf_in.read(reinterpret_cast<char*>(&length), sizeof(length));

    uint32_t chunkLength;
    uint32_t chunkType;
    gltf_in.read(reinterpret_cast<char*>(&chunkLength), sizeof(chunkLength));
    gltf_in.read(reinterpret_cast<char*>(&chunkType), sizeof(chunkType));
    std::vector<uint8_t> json_buf(chunkLength);
    gltf_in.read(reinterpret_cast<char*>(json_buf.data()), chunkLength);
    ctx.gltf = json::parse(json_buf);

    if(gltf_in.read(reinterpret_cast<char*>(&chunkLength), sizeof(chunkLength))) {
      gltf_in.read(reinterpret_cast<char*>(&chunkType), sizeof(chunkType));
      ctx.buffers.emplace_back(chunkLength);
      gltf_in.read(reinterpret_cast<char*>(ctx.buffers.back().data()), chunkLength);      
    }
  }

  //json gltf = json::parse(gltf_in);


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
