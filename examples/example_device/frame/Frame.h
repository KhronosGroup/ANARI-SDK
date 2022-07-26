// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../camera/Camera.h"
#include "../renderer/Renderer.h"
#include "../scene/World.h"
#include "Future.h"
// std
#include <memory>
#include <vector>

namespace anari {
namespace example_device {

struct Frame : public Object
{
  Frame();
  ~Frame();

  ivec2 size() const;
  ANARIDataType format() const;

  bool getProperty(const std::string &name,
      ANARIDataType type,
      void *ptr,
      ANARIWaitMask mask) override;

  void commit() override;

  void *map(const char *channel,
      uint32_t *width,
      uint32_t *height,
      ANARIDataType *pixelType);

  void renderFrame(int numThreads);
  void invokeContinuation(ANARIDevice device) const;

  void renewFuture();
  bool futureIsValid() const;
  FuturePtr future();

  void setDuration(float);
  float duration() const;

 private:
  void *mapColorBuffer();
  float *mapDepthBuffer();

  int frameID() const;
  void newFrame();

  vec2 screenFromPixel(const vec2 &p) const;

  // Data //
  ANARIDataType m_format{ANARI_UNKNOWN};

  uvec2 m_size;
  vec2 m_invSize;

  int m_frameID{-1};
  float m_invFrameID{1.f};
  float m_lastFrameDuration{0.f};

  std::vector<vec4> m_accum;
  std::vector<unsigned char> m_mappedPixelBuffer;

  std::vector<float> m_depthBuffer;

  IntrusivePtr<Renderer> m_renderer;
  IntrusivePtr<Camera> m_camera;
  IntrusivePtr<World> m_world;

  FuturePtr m_future;
  ANARIFrameCompletionCallback m_continuation{nullptr};
  void *m_continuationPtr{nullptr};

  bool m_frameChanged{true};
  TimeStamp m_cameraLastChanged{0};
  TimeStamp m_rendererLastChanged{0};
};

} // namespace example_device

ANARI_TYPEFOR_SPECIALIZATION(example_device::Frame *, ANARI_FRAME);

} // namespace anari
