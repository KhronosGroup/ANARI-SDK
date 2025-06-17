// Copyright 2021-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "BaseObject.h"

namespace helium {

struct BaseFrame : public BaseObject
{
  BaseFrame(BaseGlobalDeviceState *state);

  // Implement anariRenderFrame()
  virtual void renderFrame() = 0;

  // Implement anariMapFrame()
  virtual void *map(std::string_view channel,
      uint32_t *width,
      uint32_t *height,
      ANARIDataType *pixelType) = 0;

  // Implement anariUnmapFrame()
  virtual void unmap(std::string_view channel) = 0;

  // Implement anariFrameReady()
  virtual int frameReady(ANARIWaitMask m) = 0;

  // Implement anariDiscardFrame()
  virtual void discard() = 0;

  private:
  void on_NoPublicReferences() override;
};

} // namespace helium

HELIUM_ANARI_TYPEFOR_SPECIALIZATION(helium::BaseFrame *, ANARI_FRAME);
