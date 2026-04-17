// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "ImageSampler.h"

namespace helide_gpu {

ImageSampler::ImageSampler(HelideGPUDeviceGlobalState *s) : Sampler(s) {}

ImageSampler::~ImageSampler()
{
  gpu_enqueue_method(this, &ImageSampler::gpu_freeResources).wait();
}

const ImageSamplerGPUState &ImageSampler::gpuState() const
{
  return m_gpuState;
}

int ImageSampler::samplerMode() const
{
  return 2;
}

bool ImageSampler::isValid() const
{
  return m_gpuState.texture.valid() && m_gpuState.sampler != nullptr;
}

SDL_GPUFilter ImageSampler::sdlFilter() const
{
  return (m_filter == "linear") ? SDL_GPU_FILTER_LINEAR
                                : SDL_GPU_FILTER_NEAREST;
}

SDL_GPUSamplerAddressMode ImageSampler::sdlWrapMode(
    const std::string &mode) const
{
  if (mode == "repeat")
    return SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
  if (mode == "mirrorRepeat")
    return SDL_GPU_SAMPLERADDRESSMODE_MIRRORED_REPEAT;
  return SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
}

void ImageSampler::gpu_freeResources()
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
