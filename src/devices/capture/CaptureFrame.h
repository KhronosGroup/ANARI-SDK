// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

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

  uint32_t m_index{0}; // among the frames created
  uint32_t m_renders{0};
  std::string m_captureText;

  anari::FrameCompletionCallback m_callback{nullptr};
  const void *m_callbackUserPtr{nullptr};
};

} // namespace capture_device
