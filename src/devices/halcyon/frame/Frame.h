// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "camera/Camera.h"
#include "renderer/Renderer.h"
#include "world/World.h"
// helium
#include "helium/BaseFrame.h"
#include "helium/TaskQueue.h"
// std
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace halcyon {

struct Frame : public helium::BaseFrame
{
  Frame(HalcyonDeviceGlobalState *s);
  ~Frame();

  bool isValid() const override;

  HalcyonDeviceGlobalState *deviceState() const;

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
  // False, with an ERROR, if the wait would deadlock
  bool wait();

  // What a renderer's render() reads of the frame: the ISPC struct its
  // kernels take, the frame's size, its world (valid while rendering) and
  // whether it has the albedo and normal channels
  void *getIE();
  uint2 size() const;
  const World &world() const;
  bool hasAlbedoChannel() const;
  bool hasNormalChannel() const;
  // Calls spanFcn(x, y) in parallel for every SIMD-wide span of the grid of
  // every 'stride'-th pixel along x and y (the frame's pixels with 'stride'
  // 1), x being the span's first grid column and y its grid row, in tasks of
  // the renderer's grain size in grid points. Every pass over the frame's
  // pixels goes through here, so they all share one decomposition.
  void runPass(
      const std::function<void(int x, int y)> &spanFcn, int stride = 1);

 private:
  void flushAndRender();
  // Replaces the renderer's state with 'state' (made by 'owner'), under
  // m_rendererStateMutex
  void setRendererState(std::unique_ptr<RendererFrameState> state,
      helium::IntrusivePtr<Renderer> owner);

  //// Data ////

  size_t m_ispcIndex{INVALID_INDEX};
  void *m_ispcSlot{nullptr}; // registry slots never move

  bool m_valid{false};
  int m_perPixelBytes{1};
  uint2 m_size{0u, 0u};

  anari::DataType m_colorType{ANARI_UNKNOWN};
  anari::DataType m_depthType{ANARI_UNKNOWN};
  anari::DataType m_primIdType{ANARI_UNKNOWN};
  anari::DataType m_objIdType{ANARI_UNKNOWN};
  anari::DataType m_instIdType{ANARI_UNKNOWN};
  // ANARI_UNKNOWN unless a pixel type the extensions allow
  anari::DataType m_albedoType{ANARI_UNKNOWN};
  anari::DataType m_normalType{ANARI_UNKNOWN};

  std::vector<uint8_t> m_pixelBuffer;
  std::vector<float> m_depthBuffer;
  std::vector<uint32_t> m_primIdBuffer;
  std::vector<uint32_t> m_objIdBuffer;
  std::vector<uint32_t> m_instIdBuffer;
  // in m_albedoType / m_normalType, empty unless requested
  std::vector<uint8_t> m_albedoBuffer;
  std::vector<uint8_t> m_normalBuffer;

  helium::IntrusivePtr<Renderer> m_renderer;
  helium::IntrusivePtr<Camera> m_camera;
  helium::IntrusivePtr<World> m_world;
  // The state m_rendererStateOwner made for this frame (newFrameState()),
  // made again when the frame renders with another renderer and dropped by
  // an invalid render; the reference keeps a renderer at another address
  // from passing for the owner. Only the worker changes them, under the
  // mutex, which getProperty() takes to ask the state for its properties.
  std::unique_ptr<RendererFrameState> m_rendererState;
  helium::IntrusivePtr<Renderer> m_rendererStateOwner;
  std::mutex m_rendererStateMutex;

  helium::TimeStamp m_frameLastRendered{0};

  std::atomic<float> m_duration{0.f};
  // This frame's queued jobs that may still wait on device work: a render
  // until its device work (flush and render) ends, a dropped render's
  // callback job until it starts (renders queued ahead of it may be waiting)
  std::atomic<int> m_jobsAwaitingDeviceWork{0};

  anari::FrameCompletionCallback m_callback{nullptr};
  const void *m_callbackUserPtr{nullptr};

  mutable helium::tasking::Future m_future;
};

} // namespace halcyon

HALCYON_ANARI_TYPEFOR_SPECIALIZATION(halcyon::Frame *, ANARI_FRAME);
