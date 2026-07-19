// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <SDL3/SDL_gpu.h>
#include <cstdint>

namespace helide_gpu {

struct ShaderData
{
  const uint8_t *spv;
  uint32_t spv_len;
#ifdef HELIDE_GPU_HAS_MSL_SHADERS
  const char *msl;
  uint32_t msl_len;
#endif
};

inline SDL_GPUShaderFormat detectShaderFormat(SDL_GPUDevice *device)
{
  SDL_GPUShaderFormat supported = SDL_GetGPUShaderFormats(device);
  if (supported & SDL_GPU_SHADERFORMAT_MSL)
    return SDL_GPU_SHADERFORMAT_MSL;
  return SDL_GPU_SHADERFORMAT_SPIRV;
}

inline void fillShaderInfo(SDL_GPUShaderCreateInfo &info,
    const ShaderData &data,
    SDL_GPUShaderFormat format,
    SDL_GPUShaderStage stage)
{
  info.stage = stage;
  info.format = format;
#ifdef HELIDE_GPU_HAS_MSL_SHADERS
  if (format == SDL_GPU_SHADERFORMAT_MSL) {
    info.code = reinterpret_cast<const uint8_t *>(data.msl);
    info.code_size = data.msl_len;
    info.entrypoint = "main0";
  } else
#endif
  {
    info.code = data.spv;
    info.code_size = data.spv_len;
    info.entrypoint = "main";
  }
}

inline void fillComputePipelineInfo(SDL_GPUComputePipelineCreateInfo &info,
    const ShaderData &data,
    SDL_GPUShaderFormat format)
{
  info.format = format;
#ifdef HELIDE_GPU_HAS_MSL_SHADERS
  if (format == SDL_GPU_SHADERFORMAT_MSL) {
    info.code = reinterpret_cast<const uint8_t *>(data.msl);
    info.code_size = data.msl_len;
    info.entrypoint = "main0";
  } else
#endif
  {
    info.code = data.spv;
    info.code_size = data.spv_len;
    info.entrypoint = "main";
  }
}

} // namespace helide_gpu
