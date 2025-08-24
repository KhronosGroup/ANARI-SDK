// Copyright 2024-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "textureLoader.h"

#include "../anariTypeHelpers.h"
#include "../debugCodes.h"

#include <anari/frontend/anari_enums.h>
#include <anari/frontend/type_utility.h>
#include <pxr/base/tf/debug.h>

#include <anari/anari_cpp.hpp>
#include <cstddef>
#include <cstdio>
#include <limits>
#include <memory>
#include <type_traits>

PXR_NAMESPACE_OPEN_SCOPE

anari::DataType HdAnariTextureLoader::HioFormatToAnari(HioFormat f)
{
  switch (f) {
  case HioFormatUNorm8:
    return ANARI_UFIXED8;
  case HioFormatUNorm8Vec2:
    return ANARI_UFIXED8_VEC2;
  case HioFormatUNorm8Vec3:
    return ANARI_UFIXED8_VEC3;
  case HioFormatUNorm8Vec4:
    return ANARI_UFIXED8_VEC4;
  case HioFormatSNorm8:
    return ANARI_FIXED8;
  case HioFormatSNorm8Vec2:
    return ANARI_FIXED8_VEC2;
  case HioFormatSNorm8Vec3:
    return ANARI_FIXED8_VEC3;
  case HioFormatSNorm8Vec4:
    return ANARI_FIXED8_VEC4;
  case HioFormatFloat16:
    return ANARI_FLOAT16;
  case HioFormatFloat16Vec2:
    return ANARI_FLOAT16_VEC2;
  case HioFormatFloat16Vec3:
    return ANARI_FLOAT16_VEC3;
  case HioFormatFloat16Vec4:
    return ANARI_FLOAT16_VEC4;
  case HioFormatFloat32:
    return ANARI_FLOAT32;
  case HioFormatFloat32Vec2:
    return ANARI_FLOAT32_VEC2;
  case HioFormatFloat32Vec3:
    return ANARI_FLOAT32_VEC3;
  case HioFormatFloat32Vec4:
    return ANARI_FLOAT32_VEC4;
  case HioFormatDouble64:
    return ANARI_FLOAT64;
  case HioFormatDouble64Vec2:
    return ANARI_FLOAT64_VEC2;
  case HioFormatDouble64Vec3:
    return ANARI_FLOAT64_VEC3;
  case HioFormatDouble64Vec4:
    return ANARI_FLOAT64_VEC4;
  case HioFormatUInt16:
    return ANARI_UINT16;
  case HioFormatUInt16Vec2:
    return ANARI_UINT16_VEC2;
  case HioFormatUInt16Vec3:
    return ANARI_UINT16_VEC3;
  case HioFormatUInt16Vec4:
    return ANARI_UINT16_VEC4;
  case HioFormatInt16:
    return ANARI_INT16;
  case HioFormatInt16Vec2:
    return ANARI_INT16_VEC2;
  case HioFormatInt16Vec3:
    return ANARI_INT16_VEC3;
  case HioFormatInt16Vec4:
    return ANARI_INT16_VEC4;
  case HioFormatUInt32:
    return ANARI_UINT32;
  case HioFormatUInt32Vec2:
    return ANARI_UINT32_VEC2;
  case HioFormatUInt32Vec3:
    return ANARI_UINT32_VEC3;
  case HioFormatUInt32Vec4:
    return ANARI_UINT32_VEC4;
  case HioFormatInt32:
    return ANARI_INT32;
  case HioFormatInt32Vec2:
    return ANARI_INT32_VEC2;
  case HioFormatInt32Vec3:
    return ANARI_INT32_VEC3;
  case HioFormatInt32Vec4:
    return ANARI_INT32_VEC4;

  case HioFormatUNorm8srgb:
    return ANARI_UFIXED8_R_SRGB;
  case HioFormatUNorm8Vec2srgb:
    return ANARI_UFIXED8_RA_SRGB;
  case HioFormatUNorm8Vec3srgb:
    return ANARI_UFIXED8_RGB_SRGB;
  case HioFormatUNorm8Vec4srgb:
    return ANARI_UFIXED8_RGBA_SRGB;

  default: // all other values, including HioFormatInvalid
    break;
  }

  return ANARI_UNKNOWN;
}

ANARIDataType updateWithColorSpace(
    ANARIDataType format, HdAnariTextureLoader::ColorSpace colorSpace)
{
  if (colorSpace == HdAnariTextureLoader::ColorSpace::SRgb) {
    switch (format) {
    case ANARI_UFIXED8:
      return ANARI_UFIXED8_R_SRGB;
    case ANARI_UFIXED8_VEC2:
      return ANARI_UFIXED8_RA_SRGB;
    case ANARI_UFIXED8_VEC3:
      return ANARI_UFIXED8_RGB_SRGB;
    case ANARI_UFIXED8_VEC4:
      return ANARI_UFIXED8_RGBA_SRGB;
    default:
      return format;
    }
  } else {
    switch (format) {
    case ANARI_UFIXED8_R_SRGB:
      return ANARI_UFIXED8;
    case ANARI_UFIXED8_RA_SRGB:
      return ANARI_UFIXED8_VEC2;
    case ANARI_UFIXED8_RGB_SRGB:
      return ANARI_UFIXED8_VEC3;
    case ANARI_UFIXED8_RGBA_SRGB:
      return ANARI_UFIXED8_VEC4;
    default:
      return format;
    }
  }
}

template <typename Tin, typename Tout, std::size_t Nin, std::size_t Nout>
struct Remapper
{
  // static_assert(Nin >= Nout, "Input size must be greater than or equal to
  // output size");

  static constexpr const auto ScaleDown = std::is_integral_v<Tin>;
  static constexpr const auto ScaleUp = std::is_integral_v<Tout>;

  using CommonType = std::conditional_t<
      std::is_integral_v<Tin> && std::is_integral_v<Tout>,
      std::conditional_t<(std::numeric_limits<Tin>::digits <= std::numeric_limits<float>::digits), float, double>,
      std::common_type_t<std::decay_t<Tin>, std::decay_t<Tout>>
    >;

  static void remap(
      Tin const (&dataIn)[][Nin], Tout (&dataOut)[][Nout], std::size_t size)
  {
    for (auto i = 0ul; i < size; ++i) {
      for (auto c = 0ul; c < Nout; ++c) {
        if constexpr (ScaleUp || ScaleDown) {
          CommonType v = dataIn[i][c];
          if constexpr (ScaleDown)
            v /= CommonType(std::numeric_limits<Tin>::max());
          if constexpr (ScaleUp)
            v *= CommonType(std::numeric_limits<Tout>::max());
          dataOut[i][c] = Tout(v);
        } else {
          dataOut[i][c] = Tout(dataIn[i][c]);
        }
      }
    }
  }

  static void linearToSrgb(
      Tin const (&dataIn)[][Nin], Tout (&dataOut)[][Nout], std::size_t size)
  {
    // Only consider the alpha case when input and output have the same number
    // of components.
    constexpr const auto HasAlphaComponent = (Nin & 1) && Nin == Nout;
    constexpr const auto NonAlphaComponentCount =
        HasAlphaComponent ? (Nout - 1) : Nout;

    for (auto i = 0ul; i < size; ++i) {
      for (auto c = 0; c < NonAlphaComponentCount; ++c) {
        CommonType v = dataIn[i][c];
        if constexpr (ScaleDown) {
          v /= CommonType(std::numeric_limits<Tin>::max());
        }
        v = std::pow(v, CommonType(1.0 / 2.2));
        if constexpr (ScaleUp) {
          v *= CommonType(std::numeric_limits<Tout>::max());
        }
        dataOut[i][c] = Tout(v);
      }
      if constexpr (HasAlphaComponent) {
        CommonType v = dataIn[i][Nout - 1];
        if constexpr (ScaleDown) {
          v /= CommonType(std::numeric_limits<Tin>::max());
        }
        if constexpr (ScaleUp) {
          v *= CommonType(std::numeric_limits<Tout>::max());
        }
        dataOut[i][Nout - 1] = Tout(v);
      }
    }
  }

  static void srgbToLinear(
      Tin const (&dataIn)[][Nin], Tout (&dataOut)[][Nout], std::size_t size)
  {
    // Only consider the alpha case when input and output have the same number
    // of components.
    constexpr const auto HasAlphaComponent = (Nin & 1) == 0 && Nin == Nout;
    constexpr const auto NonAlphaComponentCount =
        HasAlphaComponent ? (Nout - 1) : Nout;

    for (auto i = 0ul; i < size; ++i) {
      for (auto c = 0; c < NonAlphaComponentCount; ++c) {
        CommonType v = dataIn[i][c];
        if constexpr (ScaleDown) {
          v /= CommonType(std::numeric_limits<Tin>::max());
        }
        v = std::pow(v, CommonType(2.2));
        if constexpr (ScaleUp) {
          v *= CommonType(std::numeric_limits<Tout>::max());
        }
        dataOut[i][c] = Tout(v);
      }
      if constexpr (HasAlphaComponent) {
        CommonType v = dataIn[i][Nout - 1];
        if constexpr (ScaleDown) {
          v /= CommonType(std::numeric_limits<Tin>::max());
        }
        if constexpr (ScaleUp) {
          v *= CommonType(std::numeric_limits<Tout>::max());
        }
        dataOut[i][Nout - 1] = Tout(v);
      }
    }
  }
};

#define REMAP(                                                                 \
    inputData, outputData, size, Tin, Nin, SrgbIn, Tout, Nout, SrgbOut)        \
  do {                                                                         \
    if constexpr (Nin >= Nout) {                                               \
      using RemapperType = Remapper<Tin, Tout, Nin, Nout>;                     \
      if constexpr (SrgbIn == SrgbOut)                                         \
        RemapperType::remap(*reinterpret_cast<Tin const(*)[][Nin]>(inputData), \
            *reinterpret_cast<Tout(*)[][Nout]>(outputData),                    \
            size);                                                             \
      else if constexpr (SrgbIn && !SrgbOut)                                   \
        RemapperType::srgbToLinear(                                            \
            *reinterpret_cast<Tin const(*)[][Nin]>(inputData),                 \
            *reinterpret_cast<Tout(*)[][Nout]>(outputData),                    \
            size);                                                             \
      else if constexpr (!SrgbIn && SrgbOut)                                   \
        RemapperType::linearToSrgb(                                            \
            *reinterpret_cast<Tin const(*)[][Nin]>(inputData),                 \
            *reinterpret_cast<Tout(*)[][Nout]>(outputData),                    \
            size);                                                             \
    }                                                                          \
  } while (0)

anari::Array2D HdAnariTextureLoader::LoadHioTexture2D(anari::Device d,
    const std::string &file,
    MinMagFilter minMagFilter,
    ColorSpace colorspace,
    ANARIDataType requestedFormat)
{
  const auto image = HioImage::OpenForReading(file);
  if (!image) {
    TF_WARN("failed to open texture file '%s'\n", file.c_str());
    return {};
  }

  HioImage::StorageSpec desc;
  desc.format = image->GetFormat();
  desc.width = image->GetWidth();
  desc.height = image->GetHeight();
  desc.depth = 1;
  desc.flipped = true;

  auto inputFormat = HioFormatToAnari(desc.format);
  auto outputFormat =
      requestedFormat == ANARI_UNKNOWN ? inputFormat : requestedFormat;
  if (outputFormat == ANARI_UNKNOWN) {
    TF_WARN("failed to load texture '%s' due to unsupported format\n",
        file.c_str());
    return {};
  }

  if (anari::componentsOf(inputFormat) < anari::componentsOf(outputFormat)) {
    TF_WARN("failed to load texture '%s', cannot convert from %s to %s\n",
        file.c_str(),
        anari::toString(inputFormat),
        anari::toString(outputFormat));
    return {};
  }

  std::vector<std::uint8_t> textureData;
  auto byteSize = anari::sizeOf(inputFormat);
  textureData.resize(desc.width * desc.height * byteSize);
  desc.data = textureData.data();

  bool loaded = image->Read(desc);
  if (!loaded) {
    TF_WARN("failed to read texture '%s'\n", file.c_str());
    return {};
  }

  outputFormat = updateWithColorSpace(outputFormat, ColorSpace::Raw);

  TF_DEBUG_MSG(HD_ANARI_RD_MATERIAL,
      "Loading texture '%s' from format %s as %s\n",
      file.c_str(),
      anari::toString(inputFormat),
      anari::toString(outputFormat));

  auto array = anari::newArray2D(d, outputFormat, desc.width, desc.height);
  auto inputData = desc.data;
  auto outputData = anari::map<void>(d, array);

  HDANARI_FOR_EACH_DATATYPE(inputFormat,
      HDANARI_FOR_EACH_DATATYPE_REC,
      outputFormat,
      REMAP,
      inputData,
      outputData,
      desc.width * desc.height);

  anari::unmap(d, array);
  anari::commitParameters(d, array);

  return array;
}

PXR_NAMESPACE_CLOSE_SCOPE
