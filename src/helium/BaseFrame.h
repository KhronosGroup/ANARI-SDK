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
 * the render and waits for it, preventing the frame from being used after
 * deletion (a device may override this; see on_NoPublicReferences()).
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

  // Opt-in: waits for the frame's render, if one is in flight, to end
  // (completion callback included). BaseDevice calls this *without* the
  // frame's object lock before frameReady(ANARI_WAIT) and map(), which it then
  // calls under the lock, so that other threads -- in particular another
  // frame's completion callback, which may run ahead of this frame's render
  // -- can call into the frame meanwhile. The default does nothing, so a
  // device that doesn't override it waits in frameReady()/map() under the
  // lock, as before (and such a callback can deadlock on that lock).
  //
  // An override may run concurrently with any of the frame's other calls
  // (renderFrame(), commits, map(), and waits on other threads), so it must
  // synchronize the render state it reads itself. It must not report: if the
  // calling thread can't wait (it would deadlock), return without waiting,
  // and let frameReady() or map(), under the lock, report why and not wait
  // either. It may rethrow the render's exception, as frameReady() would. A
  // render started after it returns is waited for under the lock.
  virtual void waitWithoutObjectLock();

  // Implement anariDiscardFrame()
  virtual void discard() = 0;

  // True while this frame's completion callback runs on the calling thread.
  // BaseDevice skips the frame's object lock for such calls: a thread may hold
  // that lock while it waits for the callback to return (e.g. a device's
  // renderFrame() waiting for the frame's previous render).
  bool completingOnThisThread() const;

 protected:
  // Runs when the app releases the frame's last public reference. By default,
  // if a render is in flight, discards it and waits for it to end, so the
  // frame isn't destroyed under it. A device whose renders hold a reference to
  // their frame until they end (so the frame outlives the release) can
  // override this not to wait: the wait is needless there, and can't be
  // satisfied on a thread that can't wait for the render (e.g. one holding a
  // mapped array the render waits for, or a completion callback on the
  // device's worker).
  void on_NoPublicReferences() override;

  // Call the app's frame completion callback. Devices should invoke callbacks
  // only through this, so the callback may call back into the device on this
  // frame (map, frameReady, getProperty, ...).
  void invokeCompletionCallback(
      ANARIFrameCompletionCallback cb, const void *userPtr, ANARIDevice device);
};

} // namespace helium

HELIUM_ANARI_TYPEFOR_SPECIALIZATION(helium::BaseFrame *, ANARI_FRAME);
