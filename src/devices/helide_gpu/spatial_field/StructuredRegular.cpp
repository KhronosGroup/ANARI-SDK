// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "StructuredRegular.h"
// helium
#include "helium/array/Array3D.h"
// glm
#include <glm/gtx/component_wise.hpp>
// std
#include <cstring>
#include <vector>

namespace helide_gpu {

StructuredRegular::StructuredRegular(HelideGPUDeviceGlobalState *s)
    : SpatialField(s), m_data(this)
{}

StructuredRegular::~StructuredRegular()
{
  gpu_enqueue_method(this, &StructuredRegular::gpu_freeResources).wait();
}

void StructuredRegular::commitParameters()
{
  m_data = getParamObject<Array3D>("data");
  m_origin = getParam<vec3>("origin", vec3(0.f));
  m_spacing = getParam<vec3>("spacing", vec3(1.f));
}

void StructuredRegular::finalize()
{
  if (!m_data) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "missing required parameter 'data' on structuredRegular spatial field");
  }

  gpu_enqueue_method(this, &StructuredRegular::gpu_createTexture);
}

bool StructuredRegular::isValid() const
{
  return m_data && m_gpuState.texture.valid();
}

box3 StructuredRegular::bounds() const
{
  if (!m_data)
    return {};
  auto d = m_data->size();
  vec3 upper = m_origin
      + vec3(float(d.x - 1), float(d.y - 1), float(d.z - 1)) * m_spacing;
  box3 b;
  b.lower = m_origin;
  b.upper = upper;
  return b;
}

float StructuredRegular::stepSize() const
{
  return glm::compMin(m_spacing) * 0.5f;
}

vec3 StructuredRegular::origin() const
{
  return m_origin;
}

vec3 StructuredRegular::spacing() const
{
  return m_spacing;
}

uvec3 StructuredRegular::dims() const
{
  if (!m_data)
    return uvec3(0u);
  auto d = m_data->size();
  return uvec3(d.x, d.y, d.z);
}

SDL_GPUTexture *StructuredRegular::gpuTexture() const
{
  return m_gpuState.texture.sdlTexture();
}

SDL_GPUSampler *StructuredRegular::gpuSampler() const
{
  return m_gpuState.sampler;
}

void StructuredRegular::gpu_createTexture()
{
  gpu_freeResources();

  if (!m_data)
    return;

  auto *dev = deviceState()->gpu.device;
  if (!dev)
    return;

  auto d = m_data->size();
  const uint32_t width = d.x;
  const uint32_t height = d.y;
  const uint32_t depth = d.z;

  if (width == 0 || height == 0 || depth == 0)
    return;

  // Convert all voxels to R32_FLOAT
  const size_t numVoxels = size_t(width) * height * depth;
  std::vector<float> voxels(numVoxels);

  for (uint32_t z = 0; z < depth; ++z) {
    for (uint32_t y = 0; y < height; ++y) {
      for (uint32_t x = 0; x < width; ++x) {
        const size_t idx = size_t(z) * width * height + size_t(y) * width + x;
        auto v = m_data->readAsAttributeValue({static_cast<int32_t>(x),
            static_cast<int32_t>(y),
            static_cast<int32_t>(z)});
        float val;
        std::memcpy(&val, &v, sizeof(float));
        voxels[idx] = val;
      }
    }
  }

  // Create and upload 3D texture (R32_FLOAT)
  m_gpuState.texture = GPUTexture(dev,
      SDL_GPU_TEXTUREFORMAT_R32_FLOAT,
      SDL_GPU_TEXTUREUSAGE_SAMPLER,
      SDL_GPU_TEXTURETYPE_3D);
  if (!m_gpuState.texture.upload(
          voxels.data(), voxels.size(), width, height, depth)) {
    reportMessage(ANARI_SEVERITY_ERROR,
        "[SDL3_gpu] Failed to create 3D texture for structuredRegular: %s",
        SDL_GetError());
    return;
  }

  // Create sampler (linear filtering, clamp to edge)
  SDL_GPUSamplerCreateInfo sampInfo{};
  sampInfo.min_filter = SDL_GPU_FILTER_LINEAR;
  sampInfo.mag_filter = SDL_GPU_FILTER_LINEAR;
  sampInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
  sampInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  sampInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  sampInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  m_gpuState.sampler = SDL_CreateGPUSampler(dev, &sampInfo);
  if (!m_gpuState.sampler) {
    reportMessage(ANARI_SEVERITY_ERROR,
        "[SDL3_gpu] Failed to create sampler for structuredRegular: %s",
        SDL_GetError());
  }
}

void StructuredRegular::gpu_freeResources()
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
