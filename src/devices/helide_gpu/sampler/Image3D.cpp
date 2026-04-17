// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Image3D.h"
#include "HelideGPUColorSpace.h"
// helium
#include "helium/array/Array3D.h"
// std
#include <cstring>
#include <vector>

namespace helide_gpu {

Image3D::Image3D(HelideGPUDeviceGlobalState *s) : ImageSampler(s), m_image(this) {}

void Image3D::commitParameters()
{
  m_image = getParamObject<Array3D>("image");
  m_filter = getParamString("filter", "nearest");
  m_wrapMode1 = getParamString("wrapMode1", "clampToEdge");
  m_wrapMode2 = getParamString("wrapMode2", "clampToEdge");
  m_wrapMode3 = getParamString("wrapMode3", "clampToEdge");
  readCommonParameters();
}

void Image3D::finalize()
{
  if (!m_image) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "missing required parameter 'image' on image3D sampler");
  }

  gpu_enqueue_method(this, &Image3D::gpu_createTexture);
}

void Image3D::gpu_createTexture()
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
  const uint32_t depth = dims.z;

  if (width == 0 || height == 0 || depth == 0)
    return;

  // Convert all texels to RGBA32F (row-major, correctly linearizing sRGB)
  const bool srgb = isElementTypeSRGB(m_image->elementType());
  const int nc = srgb ? srgbComponentCount(m_image->elementType()) : 0;
  std::vector<vec4> texels(width * height * depth);
  for (uint32_t z = 0; z < depth; ++z) {
    for (uint32_t y = 0; y < height; ++y) {
      for (uint32_t x = 0; x < width; ++x) {
        const size_t idx = z * width * height + y * width + x;
        if (srgb) {
          const auto *bytes =
              static_cast<const uint8_t *>(m_image->data()) + idx * nc;
          texels[idx] = srgbBytesToLinear(bytes, nc);
        } else {
          auto v = m_image->readAsAttributeValue({static_cast<int32_t>(x),
              static_cast<int32_t>(y),
              static_cast<int32_t>(z)});
          std::memcpy(&texels[idx], &v, sizeof(vec4));
        }
      }
    }
  }

  // Create and upload GPU texture (WxHxD)
  m_gpuState.texture = GPUTexture(dev,
      SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT,
      SDL_GPU_TEXTUREUSAGE_SAMPLER,
      SDL_GPU_TEXTURETYPE_3D);
  if (!m_gpuState.texture.upload(
          texels.data(), texels.size(), width, height, depth)) {
    reportMessage(ANARI_SEVERITY_ERROR,
        "[SDL3_gpu] Failed to create 3D texture: %s",
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
  sampInfo.address_mode_w = sdlWrapMode(m_wrapMode3);
  m_gpuState.sampler = SDL_CreateGPUSampler(dev, &sampInfo);
  if (!m_gpuState.sampler) {
    reportMessage(ANARI_SEVERITY_ERROR,
        "[SDL3_gpu] Failed to create sampler: %s",
        SDL_GetError());
  }
}

} // namespace helide_gpu
