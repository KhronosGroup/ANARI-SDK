// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Image1D.h"
#include "HelideGPUColorSpace.h"
// helium
#include "helium/array/Array1D.h"
// std
#include <cstring>
#include <vector>

namespace helide_gpu {

Image1D::Image1D(HelideGPUDeviceGlobalState *s) : ImageSampler(s), m_image(this) {}

void Image1D::commitParameters()
{
  m_image = getParamObject<Array1D>("image");
  m_filter = getParamString("filter", "nearest");
  m_wrapMode1 = getParamString("wrapMode", "clampToEdge");
  m_wrapMode2 = "clampToEdge"; // 1D uses only one wrap dimension
  readCommonParameters();
}

void Image1D::finalize()
{
  if (!m_image) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "missing required parameter 'image' on image1D sampler");
  }

  gpu_enqueue_method(this, &Image1D::gpu_createTexture);
}

void Image1D::gpu_createTexture()
{
  gpu_freeResources();

  if (!m_image || m_image->size() == 0)
    return;

  auto *dev = deviceState()->gpu.device;
  if (!dev)
    return;

  const size_t width = m_image->size();

  // Convert all texels to RGBA32F (correctly linearizing sRGB sources)
  const bool srgb = isElementTypeSRGB(m_image->elementType());
  const int nc = srgb ? srgbComponentCount(m_image->elementType()) : 0;
  std::vector<vec4> texels(width);
  for (size_t i = 0; i < width; ++i) {
    if (srgb) {
      const auto *bytes =
          static_cast<const uint8_t *>(m_image->begin()) + i * nc;
      texels[i] = srgbBytesToLinear(bytes, nc);
    } else {
      auto v = m_image->readAsAttributeValue(static_cast<int32_t>(i));
      std::memcpy(&texels[i], &v, sizeof(vec4));
    }
  }

  // Create and upload GPU texture (Nx1x1)
  m_gpuState.texture = GPUTexture(dev,
      SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT,
      SDL_GPU_TEXTUREUSAGE_SAMPLER,
      SDL_GPU_TEXTURETYPE_3D);
  if (!m_gpuState.texture.upload(
          texels.data(), texels.size(), static_cast<uint32_t>(width), 1, 1)) {
    reportMessage(ANARI_SEVERITY_ERROR,
        "[SDL3_gpu] Failed to create 1D texture: %s",
        SDL_GetError());
    return;
  }

  // Create sampler
  SDL_GPUSamplerCreateInfo sampInfo{};
  sampInfo.min_filter = sdlFilter();
  sampInfo.mag_filter = sdlFilter();
  sampInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
  sampInfo.address_mode_u = sdlWrapMode(m_wrapMode1);
  sampInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  sampInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  m_gpuState.sampler = SDL_CreateGPUSampler(dev, &sampInfo);
  if (!m_gpuState.sampler) {
    reportMessage(ANARI_SEVERITY_ERROR,
        "[SDL3_gpu] Failed to create sampler: %s",
        SDL_GetError());
  }
}

} // namespace helide_gpu
