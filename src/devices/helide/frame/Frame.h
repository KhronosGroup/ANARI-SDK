// Copyright 2021-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "camera/Camera.h"
#include "renderer/Renderer.h"
#include "scene/World.h"
// helium
#include "helium/BaseFrame.h"
// std
#include <future>
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
  float2 screenFromPixel(const float2 &p) const;
  void writeSample(int x, int y, const PixelSample &s);

  //// Data ////

  bool m_valid{false};
  int m_perPixelBytes{1};

  struct FrameData
  {
    int frameID{0};
    uint2 size;
    float2 invSize;
  } m_frameData;

  anari::DataType m_colorType{ANARI_UNKNOWN};
  anari::DataType m_depthType{ANARI_UNKNOWN};
  anari::DataType m_primIdType{ANARI_UNKNOWN};
  anari::DataType m_objIdType{ANARI_UNKNOWN};
  anari::DataType m_instIdType{ANARI_UNKNOWN};

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

  mutable std::future<void> m_future;
  std::packaged_task<void()> m_task;

  anari::FrameCompletionCallback m_callback{nullptr};
  const void *m_callbackUserPtr{nullptr};
};

} // namespace helide

HELIDE_ANARI_TYPEFOR_SPECIALIZATION(helide::Frame *, ANARI_FRAME);
