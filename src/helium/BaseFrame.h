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

  // Waits for the frame's render, if one is in flight, to end (completion
  // callback included). BaseDevice calls this *without* the frame's object
  // lock before frameReady(ANARI_WAIT) and map(), which it then calls under
  // the lock, so that other threads -- in particular another frame's
  // completion callback, which may run ahead of this frame's render -- can
  // call into the frame meanwhile. It may therefore run concurrently with any
  // of the frame's other calls (renderFrame(), map(), parameter changes, ...,
  // and waits on other threads), and must synchronize the state it reads
  // itself. A render started after it returns is waited for under the lock.
  //
  // If the calling thread can't wait (it would deadlock), return without
  // waiting or reporting: frameReady() or map(), under the lock, should then
  // report why and not wait either. The default calls frameReady(ANARI_WAIT);
  // a device whose frameReady() isn't safe without the lock must override it.
  virtual void waitWithoutObjectLock();

  // Implement anariDiscardFrame()
  virtual void discard() = 0;

  // True while this frame's completion callback runs on the calling thread.
  // BaseDevice skips the frame's object lock for such calls: a thread may hold
  // that lock while it waits for the callback to return (e.g. a device's
  // renderFrame() waiting for the frame's previous render).
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
