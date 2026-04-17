// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// helium
#include "helium/BaseGlobalDeviceState.h"
#include "helium/TaskQueue.h"
// std
#include <memory>
#include <vector>

#include "gpu/sdl3_gpu_device.h"

namespace helide_gpu {

struct HelideGPUDeviceGlobalState : public helium::BaseGlobalDeviceState
{
  struct GPUState
  {
    helium::tasking::TaskQueue thread{128};
    SDLGPUDevice sdlDevice;
    SDL_GPUDevice *device{nullptr}; // convenience alias for sdlDevice.device
    std::vector<const char *> extensions;

    // 1x1x1 white dummy texture+sampler for when no real sampler is bound
    SDL_GPUTexture *dummyTexture{nullptr};
    SDL_GPUSampler *dummySampler{nullptr};

    // 16-byte dummy storage buffer for unused read-only SSBO slots. Safe to
    // alias across multiple readonly bindings in the same pass.
    SDL_GPUBuffer *dummyStorageBuffer{nullptr};

    // Minimal vertex buffer bound by the triangle pipeline to force a
    // [[stage_in]] in its MSL shader. Content is irrelevant; triangle.vert
    // pulls all real vertex data from SSBOs via gl_VertexIndex.
    SDL_GPUBuffer *dummyVertexBuffer{nullptr};

    // SSAO shared resources
    SDL_GPUTexture *ssaoNoiseTex{nullptr};
    SDL_GPUSampler *ssaoNoiseSampler{nullptr};
    SDL_GPUSampler *linearClampSampler{nullptr};
    SDL_GPUSampler *nearestClampSampler{nullptr};

    // Graphics pipelines (owned by device, not by Renderer)
    SDL_GPUGraphicsPipeline *trianglePipeline{nullptr};
    SDL_GPUGraphicsPipeline *triangleMeshPipeline{nullptr};
    SDL_GPUGraphicsPipeline *spherePipeline{nullptr};
    SDL_GPUGraphicsPipeline *volumePipeline{nullptr};
    SDL_GPUGraphicsPipeline *ssaoPipeline{nullptr};
    SDL_GPUGraphicsPipeline *blurPipeline{nullptr};
    SDL_GPUGraphicsPipeline *compositePipeline{nullptr};
  } gpu;

  ANARIDevice device{nullptr};

  HelideGPUDeviceGlobalState(ANARIDevice d);

  void gpu_initPipelines();
  void gpu_destroyPipelines();
};

// Helper functions/macros ////////////////////////////////////////////////////

#define HELIDE_GPU_ANARI_TYPEFOR_SPECIALIZATION(type, anari_type)              \
  namespace anari {                                                            \
  ANARI_TYPEFOR_SPECIALIZATION(type, anari_type);                              \
  }

#define HELIDE_GPU_ANARI_TYPEFOR_DEFINITION(type)                              \
  namespace anari {                                                            \
  ANARI_TYPEFOR_DEFINITION(type);                                              \
  }

} // namespace helide_gpu
