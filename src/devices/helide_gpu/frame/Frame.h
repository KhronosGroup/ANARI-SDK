// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "camera/Camera.h"
#include "gpu/GPUTexture.h"
#include "renderer/Renderer.h"
#include "scene/World.h"
#include "surface/Surface.h"
#include "volume/Volume.h"
// helium
#include "helium/BaseFrame.h"
#include "helium/utility/TimeStamp.h"
// std
#include <functional>
#include <future>
#include <vector>

namespace helide_gpu {

struct FrameGPUState
{
  GPUTexture colorTarget;
  void *mappedColorPtr{nullptr};

  GPUTexture depthTarget;
  void *mappedDepthPtr{nullptr};

  // Packed ID target: R=primitiveId, G=objectId, B=instanceId (RGBA32_UINT),
  // A=the surface's packed ambient light (shaders/ambient_packing.glsl)
  GPUTexture idsTarget;

  GPUTexture albedoTarget;
  GPUTexture normalTarget;

  // SSAO post-process targets: the SSAO pass writes aoTarget, the blur
  // passes ping-pong between the two
  GPUTexture aoTarget;
  GPUTexture aoBlurTarget;
  GPUTexture colorTargetB; // composite output (ping-pong)

  // Track which texture was last downloaded for color readback
  GPUTexture *lastColorDownload{nullptr};
};

struct Frame : public helium::BaseFrame
{
  Frame(HelideGPUDeviceGlobalState *s);
  ~Frame();

  bool isValid() const override;

  HelideGPUDeviceGlobalState *deviceState() const;

  bool getProperty(const std::string_view &name,
      ANARIDataType type,
      void *ptr,
      uint64_t size,
      uint32_t flags) override;

  void commitParameters() override;
  void finalize() override;

  void renderFrame() override;

  void *map(std::string_view channel,
      uint32_t *width,
      uint32_t *height,
      ANARIDataType *pixelType) override;
  void unmap(std::string_view channel) override;
  int frameReady(ANARIWaitMask m) override;
  void discard() override;

  bool ready() const;
  void wait() const;

 private:
  template <typename METHOD_T>
  helium::tasking::Future gpu_enqueue_method(METHOD_T m);

  void gpu_allocateObjects();
  void gpu_freeObjects();
  void gpu_renderFrame();
  // 'deferAmbient': leave the ambient light out of the color target, for the
  // composite pass to add after ambient occlusion
  void gpu_renderFrame_geometryPass(
      SDL_GPUCommandBuffer *cmd, bool deferAmbient);
  // Pushes the lighting uniforms (shaders/shading.glsl LightingUniforms)
  void gpu_pushLightingUniforms(
      SDL_GPUCommandBuffer *cmd, const mat4 &V, bool deferAmbient);
  // Runs the SSAO and blur passes with world-space occlusion radius 'radius'
  // (> 0); returns the texture holding the blurred occlusion factor
  SDL_GPUTexture *gpu_renderFrame_ssaoPass(
      SDL_GPUCommandBuffer *cmd, const Camera *cam, float radius);
  void gpu_renderFrame_compositePass(
      SDL_GPUCommandBuffer *cmd, SDL_GPUTexture *aoTex, bool needsSRGB);
  void gpu_renderFrame_downloadPass(
      SDL_GPUCommandBuffer *cmd, bool doComposite);
  void gpu_mapColorBuffer();
  void gpu_unmapColorBuffer();
  void gpu_mapDepthBuffer();
  void gpu_unmapDepthBuffer();
  void gpu_mapIdsBuffer();
  void gpu_unmapIdsBuffer();
  void gpu_mapAlbedoBuffer();
  void gpu_unmapAlbedoBuffer();
  void gpu_mapNormalBuffer();
  void gpu_unmapNormalBuffer();

  //// Data ////

  FrameGPUState m_gpuState;
  helium::tasking::Future m_future;

  uvec2 m_size{0u, 0u};
  anari::DataType m_colorType{ANARI_UNKNOWN};
  anari::DataType m_depthType{ANARI_UNKNOWN};
  anari::DataType m_primIdType{ANARI_UNKNOWN};
  anari::DataType m_objIdType{ANARI_UNKNOWN};
  anari::DataType m_instIdType{ANARI_UNKNOWN};
  anari::DataType m_albedoType{ANARI_UNKNOWN};
  anari::DataType m_normalType{ANARI_UNKNOWN};

  // Staging buffers for GPU → CPU conversion on readback
  std::vector<uint32_t> m_primIdStaging;
  std::vector<uint32_t> m_objIdStaging;
  std::vector<uint32_t> m_instIdStaging;
  std::vector<vec3> m_albedoStaging;
  std::vector<vec3> m_normalStaging;

  helium::IntrusivePtr<Renderer> m_renderer;
  helium::IntrusivePtr<Camera> m_camera;
  helium::IntrusivePtr<World> m_world;

  std::chrono::time_point<std::chrono::steady_clock> m_renderStartTime;
  std::chrono::time_point<std::chrono::steady_clock> m_renderEndTime;
  float m_duration{0.f};
  helium::TimeStamp m_frameLastRendered{0};
  // the world light count last warned about exceeding the shaders' limit
  size_t m_lastWarnedLightCount{0};
};

// Inlined definitions ////////////////////////////////////////////////////////

template <typename METHOD_T>
inline helium::tasking::Future Frame::gpu_enqueue_method(METHOD_T m)
{
  auto &state = *deviceState();
  if (state.gpu.thread.onWorkerThread()) {
    // Already on the GPU thread (e.g. finalize() during a commitBuffer flush
    // inside gpu_renderFrame): run now, in order, so GPU resources exist before
    // the render pass. TaskQueue::enqueue no longer runs worker-thread work
    // inline (that FIFO change is required by the forward device), so this
    // synchronous path must live here.
    std::packaged_task<void()> t(std::bind(m, this));
    auto f = t.get_future();
    t();
    return f;
  }
  return state.gpu.thread.enqueue(m, this);
}

} // namespace helide_gpu

HELIDE_GPU_ANARI_TYPEFOR_SPECIALIZATION(helide_gpu::Frame *, ANARI_FRAME);
