// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "camera/Camera.h"
#include "renderer/Renderer.h"
#include "world/World.h"
// helium
#include "helium/BaseFrame.h"
#include "helium/TaskQueue.h"
#include <vector>

namespace helide {

struct Frame : public helium::BaseFrame
{
  Frame(HelideGlobalState *s);
  ~Frame();

  bool isValid() const override;

  HelideGlobalState *deviceState() const;

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
  void wait();

 private:
  void waitOnOutstandingWorkIfNeeded();
  float2 screenFromPixel(const float2 &p) const;
  void writeSample(int x, int y, const PixelSample &s);

  //// Data ////

  int m_perPixelBytes{1};

  struct FrameData
  {
    int frameID{0};
    uint2 size;
    float2 invSize;
  } m_frameData;

  struct FrameTypes
  {
    anari::DataType color{ANARI_UNKNOWN};
    anari::DataType depth{ANARI_UNKNOWN};
    anari::DataType primId{ANARI_UNKNOWN};
    anari::DataType objId{ANARI_UNKNOWN};
    anari::DataType instId{ANARI_UNKNOWN};
  };

  FrameTypes m_incomingTypes;
  FrameTypes m_currentTypes;
  uint2 m_incomingFrameSize{0, 0};

  std::vector<uint8_t> m_pixelBuffer;
  std::vector<float> m_depthBuffer;
  std::vector<uint32_t> m_primIdBuffer;
  std::vector<uint32_t> m_objIdBuffer;
  std::vector<uint32_t> m_instIdBuffer;

  helium::IntrusivePtr<Renderer> m_renderer;
  helium::IntrusivePtr<Camera> m_camera;
  helium::IntrusivePtr<World> m_world;

  float m_duration{0.f};

  bool m_frameChanged{false};
  helium::TimeStamp m_cameraLastChanged{0};
  helium::TimeStamp m_rendererLastChanged{0};
  helium::TimeStamp m_worldLastChanged{0};
  helium::TimeStamp m_lastCommitOccured{0};
  helium::TimeStamp m_frameLastRendered{0};

  mutable helium::tasking::Future m_future;

  anari::FrameCompletionCallback m_callback{nullptr};
  const void *m_callbackUserPtr{nullptr};
};

} // namespace helide

HELIDE_ANARI_TYPEFOR_SPECIALIZATION(helide::Frame *, ANARI_FRAME);
