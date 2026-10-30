// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <anari/anari_cpp/ext/linalg.h>
#include "CaptureObject.h"
// helium
#include "helium/BaseFrame.h"
// std
#include <map>
#include <string>
#include <vector>

namespace capture_device {

// A frame renders nothing: each render writes a capture of what the frame can
// reach.
struct Frame : public helium::BaseFrame, public CommittedParams
{
  Frame(CaptureState *state);

  bool isValid() const override;
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

 private:
  CaptureState *state() const;
  void writeFiles();

  struct Pattern
  {
    bool solid{false};
    uint32_t cellSize{8};
    anari::math::float3 color0{1.f, 1.f, 1.f};
    anari::math::float3 color1{0.f, 0.f, 0.f};
    float alpha{1.f};
    float depthNear{0.f};
    float depthFar{1.f};
  };

  // The bytes of a channel, as a test pattern, or zeros for a channel that
  // has none
  std::vector<uint8_t> channelPixels(
      const std::string &channel, ANARIDataType type) const;

  uint32_t m_index{0}; // among the frames created
  uint32_t m_renders{0};
  anari::math::uint2 m_size{0u, 0u};
  Pattern m_pattern;
  std::map<std::string, ANARIDataType> m_channels;
  std::vector<uint8_t> m_mapped;

  std::string m_captureText;
  std::map<std::string, std::vector<uint8_t>> m_blobs;

  anari::FrameCompletionCallback m_callback{nullptr};
  const void *m_callbackUserPtr{nullptr};
};

} // namespace capture_device
