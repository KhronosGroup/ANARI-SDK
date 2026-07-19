// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "sdl3_gpu_device.h"
#include "ShaderData.h"
#include <SDL3/SDL.h>

namespace helide_gpu {

bool SDLGPUDevice::ensureVideoSubsystemInitialized()
{
  if ((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) != 0)
    return true;

  return SDL_InitSubSystem(SDL_INIT_VIDEO);
}

void SDLGPUDevice::init()
{
  SDL_GPUShaderFormat formats = SDL_GPU_SHADERFORMAT_SPIRV;
#ifdef HELIDE_GPU_HAS_MSL_SHADERS
  formats |= SDL_GPU_SHADERFORMAT_MSL;
#endif

  device = SDL_CreateGPUDevice(formats, false, NULL);
  if (device)
    shaderFormat = detectShaderFormat(device);
}

void SDLGPUDevice::release()
{
  if (device) {
    SDL_WaitForGPUIdle(device);
    SDL_DestroyGPUDevice(device);
    device = nullptr;
  }
#if 0
  SDL_Quit();
#endif
}

} // namespace helide_gpu
