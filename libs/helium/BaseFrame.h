// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "BaseObject.h"

namespace helium {

struct BaseFrame : public BaseObject
{
  BaseFrame(BaseGlobalDeviceState *state);

  virtual void renderFrame() = 0;
  virtual void *map(std::string_view channel,
      uint32_t *width,
      uint32_t *height,
      ANARIDataType *pixelType) = 0;
  virtual void unmap(std::string_view channel) = 0;
  virtual int frameReady(ANARIWaitMask m) = 0;
  virtual void discard() = 0;
};

} // namespace helium
