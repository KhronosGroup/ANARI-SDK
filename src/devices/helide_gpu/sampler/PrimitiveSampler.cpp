// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "PrimitiveSampler.h"
#include "HelideGPUColorSpace.h"
// helium
#include "helium/array/Array1D.h"
// std
#include <cstring>
#include <vector>

namespace helide_gpu {

PrimitiveSampler::PrimitiveSampler(HelideGPUDeviceGlobalState *s)
    : Sampler(s), m_array(this)
{}

PrimitiveSampler::~PrimitiveSampler()
{
  gpu_enqueue_method(this, &PrimitiveSampler::gpu_freeResources).wait();
}

void PrimitiveSampler::commitParameters()
{
  m_array = getParamObject<Array1D>("array");
  m_offset = getParam<uint64_t>("offset", uint64_t(0));
  readCommonParameters();
}

void PrimitiveSampler::finalize()
{
  if (!m_array) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "missing required parameter 'array' on primitive sampler");
  }

  gpu_enqueue_method(this, &PrimitiveSampler::gpu_createTexture);
}

int PrimitiveSampler::samplerMode() const
{
  return 4;
}

bool PrimitiveSampler::isValid() const
{
  return m_gpuState.texture.valid() && m_gpuState.sampler != nullptr;
}

const ImageSamplerGPUState &PrimitiveSampler::gpuState() const
{
  return m_gpuState;
}

uint64_t PrimitiveSampler::primitiveOffset() const
{
  return m_offset;
}

uint32_t PrimitiveSampler::arraySize() const
{
  return m_arraySize;
}

void PrimitiveSampler::gpu_createTexture()
{
  gpu_freeResources();

  if (!m_array || m_array->size() == 0)
    return;

  auto *dev = deviceState()->gpu.device;
  if (!dev)
    return;

  const size_t width = m_array->size();
  m_arraySize = static_cast<uint32_t>(width);

  // Convert all elements to RGBA32F (correctly linearizing sRGB sources)
  const bool srgb = isElementTypeSRGB(m_array->elementType());
  const int nc = srgb ? srgbComponentCount(m_array->elementType()) : 0;
  std::vector<vec4> texels(width);
  for (size_t i = 0; i < width; ++i) {
    if (srgb) {
      const auto *bytes =
          static_cast<const uint8_t *>(m_array->begin()) + i * nc;
      texels[i] = srgbBytesToLinear(bytes, nc);
    } else {
      auto v = m_array->readAsAttributeValue(static_cast<int32_t>(i));
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
        "[SDL3_gpu] Failed to create primitive sampler texture: %s",
        SDL_GetError());
    return;
  }

  // Create sampler with nearest filtering (per-primitive, no interpolation)
  SDL_GPUSamplerCreateInfo sampInfo{};
  sampInfo.min_filter = SDL_GPU_FILTER_NEAREST;
  sampInfo.mag_filter = SDL_GPU_FILTER_NEAREST;
  sampInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
  sampInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  sampInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  sampInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  m_gpuState.sampler = SDL_CreateGPUSampler(dev, &sampInfo);
  if (!m_gpuState.sampler) {
    reportMessage(ANARI_SEVERITY_ERROR,
        "[SDL3_gpu] Failed to create primitive sampler: %s",
        SDL_GetError());
  }
}

void PrimitiveSampler::gpu_freeResources()
{
  auto *dev = deviceState()->gpu.device;
  if (!dev)
    return;

  if (m_gpuState.sampler) {
    SDL_ReleaseGPUSampler(dev, m_gpuState.sampler);
    m_gpuState.sampler = nullptr;
  }
  m_gpuState.texture = {};
}

} // namespace helide_gpu
