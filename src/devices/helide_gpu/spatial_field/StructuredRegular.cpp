// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "StructuredRegular.h"
#include "gpu/ArrayUpload.h"
// helium
#include "helium/array/Array3D.h"
// glm
#include <glm/gtx/component_wise.hpp>
// std
#include <cstdint>

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
  auto *dev = deviceState()->gpu.device;
  if (!dev)
    return;

  const auto d = m_data ? m_data->size() : anari::math::uint3(0u);
  const uint32_t width = d.x;
  const uint32_t height = d.y;
  const uint32_t depth = d.z;

  if (width == 0 || height == 0 || depth == 0) {
    gpu_freeResources();
    return;
  }

  // SDL_gpu transfer sizes are 32-bit
  const size_t numVoxels = size_t(width) * height * depth;
  if (numVoxels * sizeof(float) > UINT32_MAX) {
    reportMessage(ANARI_SEVERITY_ERROR,
        "structuredRegular data of %ux%ux%u voxels is too large to upload",
        width,
        height,
        depth);
    gpu_freeResources();
    return;
  }

  // A data update with unchanged dimensions reuses the texture and sampler:
  // GPUTexture only reallocates when the dimensions change.
  if (!m_gpuState.texture.valid()) {
    m_gpuState.texture = GPUTexture(dev,
        SDL_GPU_TEXTUREFORMAT_R32_FLOAT,
        SDL_GPU_TEXTUREUSAGE_SAMPLER,
        SDL_GPU_TEXTURETYPE_3D);
  }

  // Convert the voxels straight into the GPU staging memory
  const auto *data = m_data.get();
  if (!m_gpuState.texture.uploadWith(
          static_cast<uint32_t>(numVoxels * sizeof(float)),
          width,
          height,
          depth,
          [&](void *dst) {
            writeScalarFieldVoxels(data, static_cast<float *>(dst));
          })) {
    reportMessage(ANARI_SEVERITY_ERROR,
        "[SDL3_gpu] Failed to create 3D texture for structuredRegular: %s",
        SDL_GetError());
    m_gpuState.texture = {};
    return;
  }

  if (m_gpuState.sampler)
    return;

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
