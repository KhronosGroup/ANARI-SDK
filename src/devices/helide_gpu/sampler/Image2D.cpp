// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Image2D.h"
#include "HelideGPUColorSpace.h"
// helium
#include "helium/array/Array2D.h"
// std
#include <cstring>
#include <vector>

namespace helide_gpu {

Image2D::Image2D(HelideGPUDeviceGlobalState *s) : ImageSampler(s), m_image(this) {}

void Image2D::commitParameters()
{
  m_image = getParamObject<Array2D>("image");
  m_filter = getParamString("filter", "nearest");
  m_wrapMode1 = getParamString("wrapMode1", "clampToEdge");
  m_wrapMode2 = getParamString("wrapMode2", "clampToEdge");
  readCommonParameters();
}

void Image2D::finalize()
{
  if (!m_image) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "missing required parameter 'image' on image2D sampler");
  }

  gpu_enqueue_method(this, &Image2D::gpu_createTexture);
}

void Image2D::gpu_createTexture()
{
  gpu_freeResources();

  if (!m_image)
    return;

  auto *dev = deviceState()->gpu.device;
  if (!dev)
    return;

  const auto dims = m_image->size();
  const uint32_t width = dims.x;
  const uint32_t height = dims.y;

  if (width == 0 || height == 0)
    return;

  // Convert all texels to RGBA32F (row-major, correctly linearizing sRGB)
  const bool srgb = isElementTypeSRGB(m_image->elementType());
  const int nc = srgb ? srgbComponentCount(m_image->elementType()) : 0;
  std::vector<vec4> texels(width * height);
  for (uint32_t y = 0; y < height; ++y) {
    for (uint32_t x = 0; x < width; ++x) {
      if (srgb) {
        const size_t idx = y * width + x;
        const auto *bytes =
            static_cast<const uint8_t *>(m_image->data()) + idx * nc;
        texels[idx] = srgbBytesToLinear(bytes, nc);
      } else {
        auto v = m_image->readAsAttributeValue(
            {static_cast<int32_t>(x), static_cast<int32_t>(y)});
        std::memcpy(&texels[y * width + x], &v, sizeof(vec4));
      }
    }
  }

  // Create and upload GPU texture (WxHx1)
  m_gpuState.texture = GPUTexture(dev,
      SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT,
      SDL_GPU_TEXTUREUSAGE_SAMPLER,
      SDL_GPU_TEXTURETYPE_3D);
  if (!m_gpuState.texture.upload(
          texels.data(), texels.size(), width, height, 1)) {
    reportMessage(ANARI_SEVERITY_ERROR,
        "[SDL3_gpu] Failed to create 2D texture: %s",
        SDL_GetError());
    return;
  }

  // Create sampler
  SDL_GPUSamplerCreateInfo sampInfo{};
  sampInfo.min_filter = sdlFilter();
  sampInfo.mag_filter = sdlFilter();
  sampInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
  sampInfo.address_mode_u = sdlWrapMode(m_wrapMode1);
  sampInfo.address_mode_v = sdlWrapMode(m_wrapMode2);
  sampInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  m_gpuState.sampler = SDL_CreateGPUSampler(dev, &sampInfo);
  if (!m_gpuState.sampler) {
    reportMessage(ANARI_SEVERITY_ERROR,
        "[SDL3_gpu] Failed to create sampler: %s",
        SDL_GetError());
  }
}

} // namespace helide_gpu
