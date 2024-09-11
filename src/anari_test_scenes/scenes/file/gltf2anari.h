#include "nlohmann/json.hpp"
using json = nlohmann::json;

#ifdef USE_DRACO
#include "draco/compression/decode.h"
#endif

#ifdef USE_WEBP
#include "webp/decode.h"
#endif

#ifdef USE_KTX
#include <ktx.h>
#endif

#include <fstream>
#include "stb_image.h"
#include <set>
#include <iostream>
#include <tuple>

static anari::DataType accessor_type(int c)
{
  switch (c - 5120) {
  case 0:
    return ANARI_INT8;
  case 1:
    return ANARI_UINT8;
  case 2:
    return ANARI_INT16;
  case 3:
    return ANARI_UINT16;
  case 5:
    return ANARI_UINT32;
  case 6:
    return ANARI_FLOAT32;
  default:
    return ANARI_UNKNOWN;
  }
}

static anari::DataType accessor_components(const std::string &str)
{
  if (str == "SCALAR") {
    return 1;
  } else if (str.length() != 4) {
    return 0;
  } else if (str[0] == 'V') {
    if (str[3] == '2') {
      return 2;
    } else if (str[3] == '3') {
      return 3;
    } else if (str[3] == '4') {
      return 4;
    } else {
      return 0;
    }
  } else if (str[0] == 'M') {
    if (str[3] == '2') {
      return 4;
    } else if (str[3] == '3') {
      return 9;
    } else if (str[3] == '4') {
      return 16;
    } else {
      return 0;
    }
  } else {
    return 0;
  }
}

static anari::DataType accessor_element_type(int c, const std::string &str)
{
  anari::DataType baseType = accessor_type(c);
  int components = accessor_components(str);
  if (baseType == ANARI_FLOAT32) {
    switch (components) {
    case 1:
      return ANARI_FLOAT32;
    case 2:
      return ANARI_FLOAT32_VEC2;
    case 3:
      return ANARI_FLOAT32_VEC3;
    case 4:
      return str[0] == 'V' ? ANARI_FLOAT32_VEC4 : ANARI_FLOAT32_MAT2;
    case 9:
      return ANARI_FLOAT32_MAT3;
    case 16:
      return ANARI_FLOAT32_MAT4;
    default:
      return ANARI_UNKNOWN;
    }
  } else if (components == 1) {
    return baseType;
  } else {
    return ANARI_UNKNOWN;
  }
}

#ifdef USE_DRACO
static anari::DataType draco_type_to_anari(draco::DataType datatype)
{
  switch (datatype) {
  case draco::DT_INT8:
    return ANARI_INT8;
  case draco::DT_UINT8:
    return ANARI_UINT8;
  case draco::DT_INT16:
    return ANARI_INT16;
  case draco::DT_UINT16:
    return ANARI_UINT16;
  case draco::DT_INT32:
    return ANARI_INT32;
  case draco::DT_UINT32:
    return ANARI_UINT32;
  case draco::DT_INT64:
    return ANARI_INT64;
  case draco::DT_UINT64:
    return ANARI_UINT64;
  case draco::DT_FLOAT32:
    return ANARI_FLOAT32;
  case draco::DT_FLOAT64:
    return ANARI_FLOAT64;
  default:
    return ANARI_UNKNOWN;
  }
}
#endif

#ifdef USE_DRACO
static anari::DataType draco_element_type_to_anari(
    draco::DataType datatype, uint8_t components)
{
  anari::DataType baseType = draco_type_to_anari(datatype);
  if (baseType == ANARI_FLOAT32) {
    switch (components) {
    case 1:
      return ANARI_FLOAT32;
    case 2:
      return ANARI_FLOAT32_VEC2;
    case 3:
      return ANARI_FLOAT32_VEC3;
    case 4:
      return ANARI_FLOAT32_VEC4;
    default:
      return ANARI_UNKNOWN;
    }
  } else if (components == 1) {
    return baseType;
  } else {
    return ANARI_UNKNOWN;
  }
}
#endif

static const char *filter_mode(int mode)
{
  if (mode == 9728) {
    return "nearest";
  } else {
    return "linear";
  }
}

static const char *wrap_mode(int mode)
{
  if (mode == 10497) {
    return "repeat";
  } else if (mode == 33071) {
    return "clampToEdge";
  } else if (mode == 33648) {
    return "mirrorRepeat";
  } else {
    return "repeat";
  }
}

static const char *texcoord_attribute(int idx)
{
  switch (idx) {
  case 0:
    return "attribute0";
  case 1:
    return "attribute1";
  case 2:
    return "attribute2";
  case 3:
    return "attribute3";
  default:
    return nullptr;
  }
}

static const char *prim_attribute(const std::string &attr)
{
  if (attr == "POSITION") {
    return "vertex.position";
  } else if (attr == "NORMAL") {
    return "vertex.normal";
  } else if (attr == "TANGENT") {
    return "vertex.tangent";
  } else if (attr == "COLOR_0") {
    return "vertex.color";
  } else if (attr == "TEXCOORD_0") {
    return "vertex.attribute0";
  } else if (attr == "TEXCOORD_1") {
    return "vertex.attribute1";
  } else if (attr == "TEXCOORD_2") {
    return "vertex.attribute2";
  } else if (attr == "TEXCOORD_3") {
    return "vertex.attribute3";
  } else {
    return nullptr;
  }
}

template <typename T>
void memcpy_to_uint32(void *dst0, const void *src0, size_t count)
{
  uint32_t *dst = (uint32_t *)dst0;
  const T *src = (const T *)src0;
  std::copy(src, src + count, dst);
}

static uint32_t base64_sextet(char c)
{
  if ('A' <= c && c <= 'Z') {
    return 0 + c - 'A';
  } else if ('a' <= c && c <= 'z') {
    return 26 + c - 'a';
  } else if ('0' <= c && c <= '9') {
    return 52 + c - '0';
  } else if (c == '+') {
    return 62;
  } else if (c == '/') {
    return 63;
  } else if (c == '=') {
    return 0;
  } else {
    return 0xFFFFu;
  }
}

static void debase64(const char *in, size_t N_in, std::vector<char> out)
{
  int bitcount = 0;
  uint32_t bits = 0;
  for (size_t i = 0; i < N_in; ++i) {
    uint32_t sextet = base64_sextet(in[i]);
    if (sextet < 64u) {
      bits = (bits << 6u) | sextet;
      bitcount += 6;
      if (bitcount >= 8) {
        bitcount -= 8;
        out.push_back((char)((bits >> bitcount) & 255u));
      }
    }
  }
}

static void matmul(float *A, const float *B, const float *C)
{
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

template <typename S, typename R, typename T>
static void srt2mat(float *A, const S &s, const R &r, const T &t)
{
  int i = 0, j = 1, k = 2, w = 3;
  A[0] = s[0] * (1.0f - 2.0f * (r[j] * r[j] + r[k] * r[k]));
  A[1] = s[0] * 2.0f * (r[i] * r[j] + r[k] * r[w]);
  A[2] = s[0] * 2.0f * (r[i] * r[k] - r[j] * r[w]);
  A[3] = 0.0f;

  A[4] = s[1] * 2.0f * (r[i] * r[j] - r[k] * r[w]);
  A[5] = s[1] * (1.0f - 2.0f * (r[i] * r[i] + r[k] * r[k]));
  A[6] = s[1] * 2.0f * (r[j] * r[k] + r[i] * r[w]);
  A[7] = 0.0f;

  A[8] = s[2] * 2.0f * (r[i] * r[k] + r[j] * r[w]);
  A[9] = s[2] * 2.0f * (r[j] * r[k] - r[i] * r[w]);
  A[10] = s[2] * (1.0f - 2.0f * (r[i] * r[i] + r[j] * r[j]));
  A[11] = 0.0f;

  A[12] = t[0];
  A[13] = t[1];
  A[14] = t[2];
  A[15] = 1.0f;
}

struct gltf_data
{
  json gltf;
  std::string path;
  std::string ext;

  ANARIDevice device;
  ANARILibrary library;
  std::vector<std::vector<char>> buffers;
  std::vector<anari::Array2D> images;
  std::vector<anari::Material> materials;
  std::vector<anari::Group> groups;
  std::vector<anari::Camera> cameras;
  std::vector<anari::Light> lights;
  std::vector<std::vector<anari::Instance>> instances;

  const char *unpack_accessor(
      int index, anari::DataType &dataType, size_t &count, size_t &stride)
  {
    const auto &accessor = gltf["accessors"][index];
    size_t accessorOffset = accessor.value("byteOffset", 0);
    count = accessor["count"];

    dataType =
        accessor_element_type(accessor["componentType"], accessor["type"]);
    size_t dataSize = anari::sizeOf(dataType);

    const auto &bufferView = gltf["bufferViews"][int(accessor["bufferView"])];
    const std::vector<char> &buffer = buffers.at(bufferView["buffer"]);
    size_t viewOffset = bufferView.value("byteOffset", 0);
    stride = bufferView.value("byteStride", dataSize);

    return buffer.data() + viewOffset + accessorOffset;
  }

  template <typename T>
  anari::Sampler configure_sampler(const T &texspec, float *swizzle = nullptr)
  {
    const auto &tex = gltf["textures"].at(int(texspec["index"]));

    int source_idx = getImageIndexFromTexture(tex);
    if (source_idx == -1) {
      return nullptr;
    }

    auto sampler = anari::newObject<anari::Sampler>(device, "image2D");
    anari::setParameter(device, sampler, "image", images.at(source_idx));
    anari::setParameter(device,
        sampler,
        "inAttribute",
        texcoord_attribute(texspec.value("texCoord", 0)));

    if (tex.contains("sampler")) {
      const auto &sampleparams = gltf["samplers"].at(int(tex["sampler"]));
      anari::setParameter(device,
          sampler,
          "filter",
          filter_mode(sampleparams.value("magFilter", 9729)));
      anari::setParameter(device,
          sampler,
          "wrapMode1",
          wrap_mode(sampleparams.value("wrapS", 10497)));
      anari::setParameter(device,
          sampler,
          "wrapMode2",
          ANARI_STRING,
          wrap_mode(sampleparams.value("wrapT", 10497)));
    } else {
      anari::setParameter(device, sampler, "filter", "linear");
      anari::setParameter(device, sampler, "wrapMode1", "repeat");
      anari::setParameter(device, sampler, "wrapMode2", "repeat");
    }
    if (swizzle) {
      anari::setParameter(
          device, sampler, "outTransform", ANARI_FLOAT32_MAT4, swizzle);
    }
    anari::commitParameters(device, sampler);
    return sampler;
  }

  template <typename T>
  std::vector<char> decode_buffer(const T &buf)
  {
    std::vector<char> data;

    if (buf.contains("uri")) {
      std::string uri = buf["uri"];
      size_t length = buf["byteLength"];
      // base64 encoded data uri
      if (uri.substr(0, 4) == "data") {
        auto comma = uri.find_first_of(',');
        if (comma != std::string::npos) {
          data.reserve(length);
          debase64(uri.c_str() + comma, uri.size() - comma, data);
        }
      } else {
        // bin file
        data.resize(length);
        std::ifstream buf_in(path + uri, std::ios::in | std::ios::binary);
        if (buf_in) {
          buf_in.read(data.data(), length);
        }
      }
    }
    return data;
  }

  template <typename T, typename F>
  std::tuple<void *, ANARIMemoryDeleter, void *> decode_image_buffer(
      const T &img, std::vector<char> *imageData, F f)
  {
    std::vector<char> buffer;
    size_t length = 0;
    const char *ptr = nullptr;
    if (img.contains("uri")) {
      if (imageData) {
        return f(imageData->data(), imageData->size());
      }
      
      std::string uri = img["uri"];

      if (uri.substr(0, 5) == "data:") {
        auto comma = uri.find_first_of(',');
        if (comma != std::string::npos) {
          debase64(uri.c_str() + comma, uri.size() - comma, buffer);
          length = buffer.size();
          ptr = buffer.data();
        } else {
          return {nullptr, nullptr, nullptr};
        }
      } else {
        std::string filename = path + uri;

        std::ifstream in(
            filename.c_str(), std::ios::in | std::ios::binary | std::ios::ate);
        if (in) {
          length = in.tellg();
          in.seekg(0);
          buffer.resize(length);
          in.read(buffer.data(), buffer.size());
          ptr = buffer.data();
        } else {
          return {nullptr, nullptr, nullptr};
        }
      }
    } else if (img.contains("bufferView")) {
      const auto &bufferView = gltf["bufferViews"][int(img["bufferView"])];
      const std::vector<char> &buffer = buffers.at(bufferView["buffer"]);
      size_t offset = bufferView.value("byteOffset", 0);
      length = bufferView["byteLength"];
      ptr = buffer.data() + offset;
    } else {
      return {nullptr, nullptr, nullptr};
    }

    return f(ptr, length);
  }

  template <typename T>
  std::string mimeType(const T &img)
  {
    if (img.contains("mimeType")) {
      return img["mimeType"];
    } else {
      return std::string();
    }
  }

  template <typename T>
  std::string extension(const T &img)
  {
    if (img.contains("uri")) {
      std::string uri = img["uri"];
      if (uri.substr(0, 5) != "data:") {
        auto pos = uri.find_last_of('.');
        if (pos != std::string::npos) {
          return uri.substr(pos);
        }
      }
    }
    return std::string();
  }

  template <typename T>
  std::tuple<void *, ANARIMemoryDeleter, void*> decode_image(const T &img, std::vector<char> *imageData, int *width, int *height, int *n)
  {
#ifdef USE_WEBP
    if (mimeType(img) == "image/webp" || extension(img) == ".webp") {
      return decode_image_buffer(img, imageData, [&](const char *data, size_t length) {
        *n = 4;
        ANARIMemoryDeleter d = [](const void *, const void *mem) {
            WebPFree(const_cast<void *>(mem));
        };
            return std::make_tuple(
                WebPDecodeRGBA((const uint8_t *)data, length, width, height),
                d,
                nullptr);
      });
    }
#endif
#ifdef USE_KTX
    if (mimeType(img) == "image/ktx2" || extension(img) == ".ktx2") {
      return decode_image_buffer(
          img, imageData, [&](const char *input_data, size_t length) {
            ktxTexture2 *texture;
            KTX_error_code result;
            ktx_size_t offset;
            ktx_uint8_t *data;

            result = ktxTexture2_CreateFromMemory(
                reinterpret_cast<const ktx_uint8_t *>(input_data), length, KTX_TEXTURE_CREATE_NO_FLAGS, &texture);
            if (result != KTX_SUCCESS) {
              printf("failed to load texture: %s\n", ktxErrorString(result));
            }

            khr_df_model_e colorModel = ktxTexture2_GetColorModel_e(texture);

            // check if bc1 (DXT1) compression is usable by the device
            bool bc7 = false;
            const char **device_extensions = nullptr;
            if (anariGetProperty(device,
                    device,
                    "extension",
                    ANARI_STRING_LIST,
                    &device_extensions,
                    sizeof(char **),
                    ANARI_WAIT)) {
              for (int i = 0; device_extensions[i] != nullptr; ++i) {
                if (strncmp("ANARI_EXT_SAMPLER_COMPRESSED_FORMAT_BC67",
                        device_extensions[i],
                        40)
                    == 0) {
                  printf("ANARI_EXT_SAMPLER_COMPRESSED_FORMAT_BC67 TRUE");
                  bc7 = true;
                }
              }
            }
            bool needsTranscoding = ktxTexture2_NeedsTranscoding(texture);
            if (bc7 && needsTranscoding) {
              result = ktxTexture2_TranscodeBasis(texture, KTX_TTF_BC7_RGBA, 0);

              if (result != KTX_SUCCESS) {
                printf("failed to load texture: %s\n",
                    ktxErrorString(result));
              }

              *width = texture->baseWidth;
              *height = texture->baseHeight;
              *n = ktxTexture2_GetNumComponents(texture);
              int fmt = texture->vkFormat;

              result = ktxTexture_GetImageOffset(
                  ktxTexture(texture), 0, 0, 0, &offset);

              if (result != KTX_SUCCESS) {
                printf("failed to load texture: %s\n", ktxErrorString(result));
              }

              data = ktxTexture_GetData(ktxTexture(texture)) + offset;
            } else {
              if (needsTranscoding) {
                result = ktxTexture2_TranscodeBasis(texture, KTX_TTF_RGBA32, 0);
                if (result != KTX_SUCCESS) {
                  printf("failed to load texture: %s\n", ktxErrorString(result));
                }
              }

              int texelType = ANARI_UFIXED8_VEC4;

              *width = texture->baseWidth;
              *height = texture->baseHeight;
              *n = ktxTexture2_GetNumComponents(texture);
              int fmt = texture->vkFormat;

              result = ktxTexture_GetImageOffset(
                  ktxTexture(texture), 0, 0, 0, &offset);

              if (result != KTX_SUCCESS) {
                printf("failed to load texture: %s\n", ktxErrorString(result));
              }
              data = ktxTexture_GetData(ktxTexture(texture)) + offset;
            }
            ANARIMemoryDeleter d = [](const void *usrPtr, const void *mem) {
              void *p = const_cast<void *>(usrPtr); 
              ktxTexture2_Destroy(reinterpret_cast<ktxTexture2*>(p));
            };
            return std::make_tuple(data, d, texture);
          });
    }
#endif // USE_KTX

    return decode_image_buffer(
        img, imageData, [&](const char *data, size_t length) {
          ANARIMemoryDeleter d = [](const void*, const void* mem) {
            stbi_image_free(const_cast<void*>(mem));
          };
    return std::make_tuple(stbi_load_from_memory(
                  (const stbi_uc *)data, length, width, height, n, 0),
              d, nullptr);
    });
  }

  int getImageIndexFromTextureInfo(const nlohmann::json& textureInfo) {
    const int index = textureInfo.value("index", -1);
    if (index != -1 && index < gltf["textures"].size()) {
      const auto &texture = gltf["textures"][index];
      return getImageIndexFromTexture(texture);
    }
    return -1;
  }

  int getImageIndexFromTexture(const nlohmann::json& texture) {
    if (texture.contains("/extensions/EXT_texture_webp/source"_json_pointer)) {
      return texture.at("/extensions/EXT_texture_webp/source"_json_pointer);
    } else if (texture.contains(
                   "/extensions/KHR_texture_basisu/source"_json_pointer)) {
      return texture.at("/extensions/KHR_texture_basisu/source"_json_pointer);
    }
    return texture.value("source", -1);
  }

  void load_assets(std::vector<std::vector<char>>& byteImages)
  {
    if (buffers.empty()) {    
        for (const auto &buf : gltf["buffers"]) {
          buffers.emplace_back(decode_buffer(buf));
        }
    }

    const std::vector<nlohmann::json_pointer<std::string>> sRGBTexturePointers =
    {
        "/emissiveTexture"_json_pointer,
        "/pbrMetallicRoughness/baseColorTexture"_json_pointer,
        "/extensions/KHR_materials_sheen/sheenColorTexture"_json_pointer,
        "/extensions/KHR_materials_specular/specularColorTexture"_json_pointer
    };

    std::set<int> sRGBImages;
    for (const auto &mat : gltf["materials"]) {
      for (const auto &pointer : sRGBTexturePointers) {      
          if (mat.contains(pointer)) {
            sRGBImages.insert(getImageIndexFromTextureInfo(mat.at(pointer)));
          }
      }
    }

    size_t imageFilesIndex = 0;
    size_t imageIndex = 0;
    for (const auto &img : gltf["images"]) {
      int width, height, n;

      std::vector<char> *imageData = nullptr;
      if (img.contains("uri")) {
        if (imageFilesIndex < byteImages.size()) {
          imageData = &byteImages[imageFilesIndex];
          }
        ++imageFilesIndex;      
      }

      auto [data, func, usrptr] = decode_image(img, imageData, &width, &height, &n);

      bool isSRGB = sRGBImages.find(imageIndex) != sRGBImages.end();

      if (data) {
        int texelType = isSRGB ? ANARI_UFIXED8_RGBA_SRGB : ANARI_UFIXED8_VEC4;
        if (n == 3)
          texelType = isSRGB ? ANARI_UFIXED8_RGB_SRGB : ANARI_UFIXED8_VEC3;
        else if (n == 2)
          texelType = isSRGB ? ANARI_UFIXED8_RA_SRGB : ANARI_UFIXED8_VEC2;
        else if (n == 1)
          texelType = isSRGB ? ANARI_UFIXED8_R_SRGB : ANARI_UFIXED8;

        auto array = anari::newArray2D(
            device,
            data,
            func,
            usrptr,
            texelType,
            width,
            height);
        images.emplace_back(array);
      } else {
        images.emplace_back(nullptr);
      }
      ++imageIndex;
    }
  }

  void load_materials()
  {
    for (const auto &mat : gltf["materials"]) {
      auto material =
          anari::newObject<anari::Material>(device, "physicallyBased");
      if (mat.contains("pbrMetallicRoughness")) {
        const auto &pbr = mat["pbrMetallicRoughness"];
        if (pbr.contains("baseColorTexture")) {
          anari::Sampler sampler = nullptr;
          if (pbr.contains("baseColorFactor")) {
            const auto &basecolor = pbr["baseColorFactor"];
            float colorSwizzle[16] = {
                // clang-format off
                basecolor[0], 0.0f, 0.0f, 0.0f,
                0.0f, basecolor[1], 0.0f, 0.0f,
                0.0f, 0.0f, basecolor[2], 0.0f,
                0.0f, 0.0f, 0.0f, basecolor[3],
                // clang-format on
            };
            sampler = configure_sampler(pbr["baseColorTexture"], colorSwizzle);
          } else {
            sampler = configure_sampler(pbr["baseColorTexture"], nullptr);
          }

          if (sampler)
            anari::setAndReleaseParameter(device, material, "baseColor", sampler);
        } else if (pbr.contains("baseColorFactor")) {
          const auto &basecolor = pbr["baseColorFactor"];
          float color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
          std::copy(basecolor.begin(),
              basecolor.begin() + std::min(4, int(basecolor.size())),
              color);
          anari::setParameter(
              device, material, "baseColor", ANARI_FLOAT32_VEC3, &color[0]);
          anari::setParameter(device, material, "opacity", color[3]);
        }

        anari::setParameter(device, material, "alphaMode", "blend");

        if (pbr.contains("metallicRoughnessTexture")) {
          float metallic = pbr.value("metallicFactor", 1);
          float roughness = pbr.value("roughnessFactor", 1);

          float metallicSwizzle[16] = {
              // clang-format off
              0.0f, 0.0f, 0.0f, 0.0f,
              0.0f, 0.0f, 0.0f, 0.0f,
              metallic, 0.0f, 0.0f, 0.0f,
              0.0f, 0.0f, 0.0f, 0.0f,
              // clang-format on
          };
          float roughnessSwizzle[16] = {
              // clang-format off
              0.0f, 0.0f, 0.0f, 0.0f,
              roughness, 0.0f, 0.0f, 0.0f,
              0.0f, 0.0f, 0.0f, 0.0f,
              0.0f, 0.0f, 0.0f, 0.0f,
              // clang-format on
          };

          auto metallicSampler =
              configure_sampler(pbr["metallicRoughnessTexture"], metallicSwizzle);
          if (metallicSampler) {
            anari::setAndReleaseParameter(
                device, material, "metallic", metallicSampler);
          }
          auto roughnessSampler = configure_sampler(
              pbr["metallicRoughnessTexture"], roughnessSwizzle);
          if (roughnessSampler) {
            anari::setAndReleaseParameter(
                device, material, "roughness", roughnessSampler);
          }
        } else {
          if (pbr.contains("metallicFactor")) {
            anari::setParameter<float>(
                device, material, "metallic", pbr["metallicFactor"]);
          }

          if (pbr.contains("roughnessFactor")) {
            anari::setParameter<float>(
                device, material, "roughness", pbr["roughnessFactor"]);
          }
        }
      }
      anari::commitParameters(device, material);
      materials.emplace_back(material);
    }
  }

  void load_surfaces()
  {
    auto defaultMaterial = anari::newObject<anari::Material>(device, "matte");
    // TODO choose correct color
    anari::setParameter(
        device, defaultMaterial, "color", anari::math::float3(0.23f, 0.14f, 0.35f));
    anari::commitParameters(device, defaultMaterial);

    for (const auto &mesh : gltf["meshes"]) {
      std::vector<anari::Surface> surfaces;

      for (const auto &prim : mesh["primitives"]) {
        int mode = prim.value("mode", 4);

        // mode
        // 0 POINTS
        // 1 LINES
        // 2 LINE_LOOP
        // 3 LINE_STRIP
        // 4 TRIANGLES
        // 5 TRIANGLE_STRIP
        // 6 TRIANGLE_FAN

        if (mode != 4) {
          continue;
        }

        auto geometry = anari::newObject<anari::Geometry>(device, "triangle");

        if (prim.contains("extensions")
            && prim["extensions"].contains("KHR_draco_mesh_compression")) {
          const auto &extensions = prim["extensions"];
          const auto &draco = extensions["KHR_draco_mesh_compression"];
#ifdef USE_DRACO
          if (draco.contains("bufferView")) {
            const auto &bufferView =
                gltf["bufferViews"][int(draco["bufferView"])];
            const std::vector<char> &buffer = buffers.at(bufferView["buffer"]);
            size_t offset = bufferView.value("byteOffset", 0);
            size_t length = bufferView["byteLength"];
            draco::DecoderBuffer decoder_buffer;
            decoder_buffer.Init(buffer.data() + offset, length);

            draco::Decoder decoder;
            auto statusor = decoder.DecodeMeshFromBuffer(&decoder_buffer);
            if (!statusor.ok()) {
              throw std::runtime_error(statusor.status().error_msg());
            }
            std::unique_ptr<draco::Mesh> draco_mesh =
                std::move(statusor).value();
            std::cout << "draco attributes " << draco_mesh->num_attributes()
                      << std::endl;

            for (const auto &attr : draco["attributes"].items()) {
              const char *paramname = prim_attribute(attr.key());
              if (paramname == nullptr) {
                continue;
              }
              const auto &att =
                  draco_mesh->GetAttributeByUniqueId(attr.value());
              anari::DataType dataType = draco_element_type_to_anari(
                  att->data_type(), att->num_components());
              uint64_t elementStride;
              char *dst = (char *)anariMapParameterArray1D(device,
                  geometry,
                  paramname,
                  dataType,
                  3 * draco_mesh->num_faces(),
                  &elementStride);

              std::cout << attr.key() << ' ' << paramname << ' '
                        << anari::toString(dataType) << ' ' << att->size()
                        << ' ' << draco_mesh->num_faces() << std::endl;

              // deindex arrays since draco apparently uses per attribute
              // indices
              size_t j = 0;
              for (draco::FaceIndex i(0); i < draco_mesh->num_faces(); ++i) {
                const auto &face = draco_mesh->face(i);
                att->GetMappedValue(face[0], dst + (j++) * elementStride);
                att->GetMappedValue(face[1], dst + (j++) * elementStride);
                att->GetMappedValue(face[2], dst + (j++) * elementStride);
              }

              anariUnmapParameterArray(device, geometry, paramname);
            }
          }
#endif
        } else {
          for (const auto &attr : prim["attributes"].items()) {
            const char *paramname = prim_attribute(attr.key());
            if (paramname == nullptr) {
              continue;
            }

            size_t stride;
            size_t count;
            anari::DataType dataType = ANARI_UNKNOWN;
            const char *src =
                unpack_accessor(attr.value(), dataType, count, stride);
            size_t dataSize = anari::sizeOf(dataType);

            anari::setParameterArray1DStrided(
                device, geometry, paramname, dataType, src, count, stride);
          }

          if (prim.contains("indices")) {
            size_t stride;
            size_t count;
            anari::DataType dataType;
            const char *src =
                unpack_accessor(prim["indices"], dataType, count, stride);
            size_t dataSize = anari::sizeOf(dataType);

            uint64_t elementStride;
            // should probably handle strides
            char *dst = (char *)anariMapParameterArray1D(device,
                geometry,
                "primitive.index",
                ANARI_UINT32_VEC3,
                count / 3,
                &elementStride);
            if (dataType == ANARI_UINT32) {
              memcpy_to_uint32<uint32_t>(dst, src, count);
            } else if (dataType == ANARI_UINT16) {
              memcpy_to_uint32<uint16_t>(dst, src, count);
            } else if (dataType == ANARI_UINT8) {
              memcpy_to_uint32<uint8_t>(dst, src, count);
            } else if (dataType == ANARI_INT16) {
              memcpy_to_uint32<int16_t>(dst, src, count);
            } else if (dataType == ANARI_INT8) {
              memcpy_to_uint32<int8_t>(dst, src, count);
            }
            anariUnmapParameterArray(device, geometry, "primitive.index");
          }
        }

        anari::commitParameters(device, geometry);

        auto surface = anari::newObject<anari::Surface>(device);
        anari::setAndReleaseParameter(device, surface, "geometry", geometry);

        if (prim.contains("material")) {
          anari::setParameter(
              device, surface, "material", materials.at(prim["material"]));
        } else
          anari::setParameter(device, surface, "material", defaultMaterial);
        anari::commitParameters(device, surface);
        surfaces.push_back(surface);
      }

      auto group = anari::newObject<anari::Group>(device);
      anari::setParameterArray1D(
          device, group, "surface", surfaces.data(), surfaces.size());
      for (auto s : surfaces)
        anari::release(device, s);
      anari::commitParameters(device, group);
      groups.emplace_back(group);
    }

    anari::release(device, defaultMaterial);
  }

  template <typename T>
  void traverse_node(const T &node, const float *parent_transform)
  {
    float matrix[16] = {
        // clang-format off
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
        // clang-format on
    };
    if (node.contains("matrix") && node["matrix"].size() == 16) {
      std::copy(node["matrix"].begin(), node["matrix"].end(), matrix);
    } else {
      float s[3] = {1.0f, 1.0f, 1.0f};
      float r[4] = {0.0f, 0.0f, 0.0f, 1.0f};
      float t[3] = {0.0f, 0.0f, 0.0f};
      if (node.contains("scale") && node["scale"].size() == 3) {
        std::copy(node["scale"].begin(), node["scale"].end(), s);
      }
      if (node.contains("rotation") && node["rotation"].size() == 4) {
        std::copy(node["rotation"].begin(), node["rotation"].end(), r);
      }
      if (node.contains("translation") && node["translation"].size() == 3) {
        std::copy(node["translation"].begin(), node["translation"].end(), t);
      }
      srt2mat(matrix, s, r, t);
    }
    float transform[16];
    matmul(transform, parent_transform, matrix);
    anari::math::mat4 transformMat = anari::math::mat4(transform);
    if (node.contains("mesh")) {
      auto instance = anari::newObject<anari::Instance>(device, "transform");
      anari::setParameter(device, instance, "group", groups.at(node["mesh"]));
      anari::setParameter(device, instance, "transform", transform);
      anari::commitParameters(device, instance);
      instances.back().push_back(instance);
    }
    if (node.contains("camera")) {
      auto &glTFCamera = gltf["cameras"].at(int(node["camera"]));
      std::string type = glTFCamera["type"].get<std::string>();

      ANARICamera camera;
      camera = anari::newObject<anari::Camera>(device, type.c_str());
      
      // position
      anari::math::float3 position = anari::math::float3(transform[12], transform[13], transform[14]);
      anari::setParameter(device, camera, "position", position);

      // rotation
      anari::math::float3 direction = anari::math::mul(transformMat, anari::math::float4(0, 0, -1, 0)).xyz();
      anari::math::float3 up = anari::math::mul(transformMat, anari::math::float4(0, 1, 0, 0)).xyz();
      anari::setParameter(device, camera, "direction", direction);
      anari::setParameter(device, camera, "up", up);
      
      // set perspective and orthographic properties
      if (type == "perspective") {
        auto &glTFPerspective = glTFCamera["perspective"];
        anari::setParameter(device, camera, "fovy", glTFPerspective.contains("yfov") ? glTFPerspective["yfov"].get<float>() : 3.141592653589f / 3.0f);
        anari::setParameter(device, camera, "aspect", glTFPerspective.contains("aspectRatio") ? glTFPerspective["aspectRatio"].get<float>() : 1.0f);
        if (glTFPerspective.contains("znear")) {
          anari::setParameter(device, camera, "near", glTFPerspective["znear"].get<float>());
        }
        if (glTFPerspective.contains("zfar")) {
          anari::setParameter(device, camera, "far", glTFPerspective["zfar"].get<float>());
        }
      } else if (type == "orthographic") {
        auto &glTFOrthographic = glTFCamera["orthographic"];
        anari::setParameter(device, camera, "height", glTFOrthographic["ymag"].get<float>());
        anari::setParameter(device, camera, "aspect", glTFOrthographic["xmag"].get<float>() / glTFOrthographic["ymag"].get<float>());
        anari::setParameter(device, camera, "near", glTFOrthographic["znear"].get<float>());
        anari::setParameter(device, camera, "far", glTFOrthographic["zfar"].get<float>());
      }
      
      anari::commitParameters(device, camera);
      cameras.emplace_back(camera);
    }
    if (node.contains("extensions")
        && node["extensions"].contains("KHR_lights_punctual")) {
      const auto &glTFLights = gltf["extensions"]["KHR_lights_punctual"]["lights"];
      const auto &glTFLight = glTFLights.at(
          int(node["extensions"]["KHR_lights_punctual"]["light"]));
      // type
      const std::string type = glTFLight["type"].get<std::string>();
      if (type == "directional" || type == "point" || type == "spot") {
        ANARILight light = anari::newObject<anari::Light>(device, type.c_str());

        // color
        float glTFColor[3] = {1.0f, 1.0f, 1.0f};
        if (glTFLight.contains("color") && glTFLight["color"].size() == 3) {
          std::copy(
              glTFLight["color"].begin(), glTFLight["color"].end(), glTFColor);
        }
        anari::math::float3 color =
            anari::math::float3(glTFColor[0], glTFColor[1], glTFColor[2]);
        anari::setParameter(device, light, "color", color);

        if (type == "point" || type == "spot") {
          // intensity
          float intensity = 1.0f;
          if (glTFLight.contains("intensity")) {
            intensity = glTFLight["intensity"].get<float>();
          }
          anari::setParameter(device, light, "intensity", intensity);

          // position
          anari::math::float3 position =
              anari::math::float3(transform[12], transform[13], transform[14]);
          anari::setParameter(device, light, "position", position);
        }

        if (type == "spot" && type == "directional") {
          // direction
          anari::math::float3 direction =
              anari::math::mul(transformMat, anari::math::float4(0, 0, -1, 0))
                  .xyz();
          anari::setParameter(device, light, "direction", direction);
        }

        if (type == "spot") {
          if (glTFLight.contains("spot")) {
            const auto &glTFSpot = glTFLight["spot"];

            // glTF outer cone angle / ANARI opening angle
            float outerConeAngle = 3.141592653589f;
            if (glTFSpot.contains("outerConeAngle")) {
              outerConeAngle = glTFSpot["outerConeAngle"].get<float>();
            }
            anari::setParameter(
                device, light, "openingAngle", outerConeAngle);

            // glTF inner cone angle / ANARI falloff angle
            float innerConeAngle = 0.1f;
            if (glTFSpot.contains("innerConeAngle")) {
              innerConeAngle = glTFSpot["innerConeAngle"].get<float>();
            }
            anari::setParameter(device, light, "falloffAngle", innerConeAngle);
          }
        }

        if (type == "directional") {
          // TODO
        }

        anari::commitParameters(device, light);
        lights.emplace_back(light);
      }
    }
    if (node.contains("children")) {
      for (int i : node["children"]) {
        const auto &node = gltf["nodes"].at(i);
        traverse_node(node, transform);
      }
    }
  }

  void load_nodes()
  {
    if (!gltf.contains("scenes"))
      return;

    float identity[16] = {
        // clang-format off
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
        // clang-format on
    };

    for (const auto &scene : gltf["scenes"]) {
      instances.emplace_back();
      for (int i : scene["nodes"]) {
        const auto &node = gltf["nodes"].at(i);
        traverse_node(node, identity);
      }
    }
  }

  void parse_glTF(const std::string &jsonText,
      std::vector<std::vector<char>> &sortedBuffers,
      std::vector<std::vector<char>> &sortedImages)
  {
    gltf = json::parse(jsonText);
    buffers = std::move(sortedBuffers);
    load_assets(sortedImages);
    load_materials();
    load_surfaces();
    load_nodes();
  }

  void open_file(const std::string &filename)
  {
    auto pos = filename.find_last_of('/');
    if (pos != std::string::npos) {
      path = filename.substr(0, pos + 1);
    }
    pos = filename.find_last_of('.');
    if (pos != std::string::npos) {
      ext = filename.substr(pos);
    }

    if (ext == ".gltf") {
      std::ifstream gltf_in(filename.c_str());
      gltf = json::parse(gltf_in);
    } else if (ext == ".glb") {
      std::ifstream gltf_in(filename.c_str(), std::ios::in | std::ios::binary);
      uint32_t magic;
      uint32_t version;
      uint32_t length;
      gltf_in.read(reinterpret_cast<char *>(&magic), sizeof(magic));
      if (magic != 0x46546C67u) {
        throw std::runtime_error("invalid gltf magic");
      }
      gltf_in.read(reinterpret_cast<char *>(&version), sizeof(version));
      gltf_in.read(reinterpret_cast<char *>(&length), sizeof(length));

      uint32_t chunkLength;
      uint32_t chunkType;
      gltf_in.read(reinterpret_cast<char *>(&chunkLength), sizeof(chunkLength));
      gltf_in.read(reinterpret_cast<char *>(&chunkType), sizeof(chunkType));
      std::vector<uint8_t> json_buf(chunkLength);
      gltf_in.read(reinterpret_cast<char *>(json_buf.data()), chunkLength);
      gltf = json::parse(json_buf);

      if (gltf_in.read(
              reinterpret_cast<char *>(&chunkLength), sizeof(chunkLength))) {
        gltf_in.read(reinterpret_cast<char *>(&chunkType), sizeof(chunkType));
        buffers.emplace_back(chunkLength);
        gltf_in.read(
            reinterpret_cast<char *>(buffers.back().data()), chunkLength);
      }
    } else if (ext == ".b3dm") {
      std::ifstream gltf_in(filename.c_str(), std::ios::in | std::ios::binary);
      uint32_t magic;
      uint32_t version;
      uint32_t length;

      gltf_in.read(reinterpret_cast<char *>(&magic), sizeof(magic));
      gltf_in.read(reinterpret_cast<char *>(&version), sizeof(version));
      gltf_in.read(reinterpret_cast<char *>(&length), sizeof(length));

      uint32_t featureTableJSONByteLength;
      uint32_t featureTableBinaryByteLength;

      uint32_t batchTableJSONByteLength;
      uint32_t batchTableBinaryByteLength;

      gltf_in.read(reinterpret_cast<char *>(&featureTableJSONByteLength),
          sizeof(featureTableJSONByteLength));
      gltf_in.read(reinterpret_cast<char *>(&featureTableBinaryByteLength),
          sizeof(featureTableBinaryByteLength));
      gltf_in.read(reinterpret_cast<char *>(&batchTableJSONByteLength),
          sizeof(batchTableJSONByteLength));
      gltf_in.read(reinterpret_cast<char *>(&batchTableBinaryByteLength),
          sizeof(batchTableBinaryByteLength));
      gltf_in.ignore(featureTableJSONByteLength);
      gltf_in.ignore(featureTableBinaryByteLength);
      gltf_in.ignore(batchTableJSONByteLength);
      gltf_in.ignore(batchTableBinaryByteLength);

      gltf_in.read(reinterpret_cast<char *>(&magic), sizeof(magic));
      if (magic != 0x46546C67u) {
        throw std::runtime_error("invalid gltf magic");
      }
      gltf_in.read(reinterpret_cast<char *>(&version), sizeof(version));
      gltf_in.read(reinterpret_cast<char *>(&length), sizeof(length));

      uint32_t chunkLength;
      uint32_t chunkType;
      gltf_in.read(reinterpret_cast<char *>(&chunkLength), sizeof(chunkLength));
      gltf_in.read(reinterpret_cast<char *>(&chunkType), sizeof(chunkType));
      std::vector<uint8_t> json_buf(chunkLength);
      gltf_in.read(reinterpret_cast<char *>(json_buf.data()), chunkLength);
      gltf = json::parse(json_buf);

      if (gltf_in.read(
              reinterpret_cast<char *>(&chunkLength), sizeof(chunkLength))) {
        gltf_in.read(reinterpret_cast<char *>(&chunkType), sizeof(chunkType));
        buffers.emplace_back(chunkLength);
        gltf_in.read(
            reinterpret_cast<char *>(buffers.back().data()), chunkLength);
      }
    }

    if (gltf.contains("extensionsUsed")) {
      std::cout << "extensions:" << std::endl;
      for (const auto &ext : gltf["extensionsUsed"]) {
        std::cout << ext << std::endl;
      }
    }
    std::vector<std::vector<char>> empty;
    load_assets(empty);
    load_materials();
    load_surfaces();
    load_nodes();
  } 
  gltf_data(ANARIDevice device) : device(device)
  {
    anariRetain(device, device);
  }

  ~gltf_data()
  {
    for (auto obj : images)
      anari::release(device, obj);
    for (auto obj : groups)
      anari::release(device, obj);
    for (auto obj : materials)
      anari::release(device, obj);
    for (auto obj : cameras)
      anari::release(device, obj);
    for (auto &scene : instances) {
      for (auto obj : scene)
        anari::release(device, obj);
    }
    anari::release(device, device);
  }
};
