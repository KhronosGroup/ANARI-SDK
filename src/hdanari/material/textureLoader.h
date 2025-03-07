// Copyright 2024-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <pxr/imaging/hio/image.h>
#include <pxr/usd/sdf/assetPath.h>
#include <anari/anari_cpp.hpp>

#include <array>

PXR_NAMESPACE_OPEN_SCOPE

class HdAnariTextureLoader
{
 public:
  enum class ColorSpace
  {
    Raw,
    SRgb,
  };

  enum class MinMagFilter
  {
    Nearest,
    Linear,
  };

  struct TextureDesc
  {
    std::string assetPath;
    ColorSpace colorspace = ColorSpace::Raw;
    MinMagFilter minMagFilter = MinMagFilter::Linear;
    // clang-format off
    std::array<float, 16> transform = {
      1.0f, 0.0f, 0.0f, 0.0f,
      0.0f, 1.0f, 0.0f, 0.0f,
      0.0f, 0.0f, 1.0f, 0.0f,
      0.0f, 0.0f, 0.0f, 1.0f,
      };
    std::array<float, 4> offset = { 0.0f, 0.0f, 0.0f, 0.0f };
    // clang-format on
  };

  static anari::DataType HioFormatToAnari(HioFormat f);
  static HioFormat AjudstColorspace(HioFormat format, ColorSpace colorspace);
  static anari::Sampler LoadHioTexture2D(anari::Device d,
      const std::string &file,
      MinMagFilter minMagFilter,
      ColorSpace colorspace);
};

PXR_NAMESPACE_CLOSE_SCOPE
