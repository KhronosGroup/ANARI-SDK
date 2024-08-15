// Copyright 2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "textureLoader.h"

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

HioFormat HdAnariTextureLoader::AjudstColorspace(
    HioFormat format, ColorSpace colorspace)
{
  if (colorspace == ColorSpace::SRgb) {
    return format;
  }

  switch (format) {
  case HioFormatUNorm8srgb:
    return HioFormatUNorm8;
  case HioFormatUNorm8Vec2srgb:
    return HioFormatUNorm8Vec2;
  case HioFormatUNorm8Vec3srgb:
    return HioFormatUNorm8Vec3;
  case HioFormatUNorm8Vec4srgb:
    return HioFormatUNorm8Vec4;
  default:;
  }

  return format;
}

anari::Sampler HdAnariTextureLoader::LoadHioTexture2D(anari::Device d,
    const std::string &file,
    MinMagFilter minMagFilter,
    ColorSpace colorspace)
{
  const auto image = HioImage::OpenForReading(file);
  if (!image) {
    TF_WARN("failed to open texture file '%s'\n", file.c_str());
    return {};
  }

  HioImage::StorageSpec desc;
  desc.format = AjudstColorspace(image->GetFormat(), colorspace);
  desc.width = image->GetWidth();
  desc.height = image->GetHeight();
  desc.depth = 1;
  desc.flipped = true;

  auto format = HioFormatToAnari(desc.format);
  if (format == ANARI_UNKNOWN) {
    TF_WARN("failed to load texture '%s' due to unsupported format\n",
        file.c_str());
    return {};
  }

  auto array = anari::newArray2D(d, format, desc.width, desc.height);
  desc.data = anari::map<void>(d, array);

  bool loaded = image->Read(desc);
  if (!loaded) {
    TF_WARN("failed to load texture '%s'\n", file.c_str());
    anari::unmap(d, array);
    anari::release(d, array);
    return {};
  }

  anari::unmap(d, array);

  auto texture = anari::newObject<anari::Sampler>(d, "image2D");
  anari::setParameter(d, texture, "image", array);
  anari::setParameter(d,
      texture,
      "filter",
      minMagFilter == MinMagFilter::Nearest ? "nearest" : "linear");

  anari::release(d, array);

  return texture;
}

PXR_NAMESPACE_CLOSE_SCOPE
