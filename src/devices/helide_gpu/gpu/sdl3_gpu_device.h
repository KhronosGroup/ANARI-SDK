// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <SDL3/SDL_gpu.h>

namespace helide_gpu {

// Thin owner/initializer for the SDL_GPUDevice singleton.
// One instance lives inside HelideGPUDeviceGlobalState::gpu.
struct SDLGPUDevice
{
  SDL_GPUDevice *device{nullptr};
  SDL_GPUShaderFormat shaderFormat{SDL_GPU_SHADERFORMAT_SPIRV};

  bool ensureVideoSubsystemInitialized();
  void init();
  void release();
};

} // namespace helide_gpu
