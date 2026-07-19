// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Sampler.h"
#include "gpu/GPUTexture.h"
// SDL3
#include <SDL3/SDL_gpu.h>
// std
#include <string>

namespace helide_gpu {

struct ImageSamplerGPUState
{
  GPUTexture texture;
  SDL_GPUSampler *sampler{nullptr};
};

struct ImageSampler : public Sampler
{
  ImageSampler(HelideGPUDeviceGlobalState *s);
  ~ImageSampler() override;

  const ImageSamplerGPUState &gpuState() const;

  int samplerMode() const override;
  bool isValid() const override;

 protected:
  SDL_GPUFilter sdlFilter() const;
  SDL_GPUSamplerAddressMode sdlWrapMode(const std::string &mode) const;

  void gpu_freeResources();

  std::string m_filter{"nearest"};
  std::string m_wrapMode1{"clampToEdge"};
  std::string m_wrapMode2{"clampToEdge"};
  std::string m_wrapMode3{"clampToEdge"};

  ImageSamplerGPUState m_gpuState;
};

} // namespace helide_gpu
