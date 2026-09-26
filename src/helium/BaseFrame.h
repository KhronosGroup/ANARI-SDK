// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "BaseObject.h"

namespace helium {

/*
 * Abstract base class for the ANARIFrame object. Device implementations
 * subclass this and implement renderFrame(), map(), unmap(), frameReady(), and
 * discard(). When the application releases its last public reference while a
 * render is still in flight, on_NoPublicReferences() calls discard() to abort
 * the render and prevent the frame from being used after deletion.
 */
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

  // True while this frame's completion callback runs on the calling thread.
  // BaseDevice skips the frame's object lock for such calls: an app thread
  // blocked in frameReady(ANARI_WAIT) or map on this frame holds that lock
  // while it waits for the callback to return.
  bool completingOnThisThread() const;

 protected:
  // Call the app's frame completion callback. Devices should invoke callbacks
  // only through this, so the callback may call back into the device on this
  // frame (map, frameReady, getProperty, ...).
  void invokeCompletionCallback(
      ANARIFrameCompletionCallback cb, const void *userPtr, ANARIDevice device);

 private:
  void on_NoPublicReferences() override;
};

} // namespace helium

HELIUM_ANARI_TYPEFOR_SPECIALIZATION(helium::BaseFrame *, ANARI_FRAME);
