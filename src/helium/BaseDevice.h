// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "BaseGlobalDeviceState.h"
#include "LockableObject.h"
#include "utility/IntrusivePtr.h"
#include "utility/ParameterizedObject.h"
// anari
#include "anari/backend/DeviceImpl.h"
// std
#include <functional>

namespace helium {

/*
 * Concrete base class that implements the anari::DeviceImpl interface on top of
 * the Helium infrastructure. Routes all ANARI C API calls (setParameter,
 * commitParameters, mapArray, renderFrame, etc.) to the appropriate BaseObject,
 * BaseArray, or BaseFrame subclass instances, acquiring per-object locks around
 * each call. Device implementors subclass BaseDevice, override
 * deviceCommitParameters() to read device-level parameters, and override the
 * object factory methods (newCamera(), newGeometry(), ...) declared in
 * anari::DeviceImpl. All objects passed through the API must derive from
 * BaseObject, BaseArray, or BaseFrame. The device owns m_state and is expected
 * to populate it with a concrete BaseGlobalDeviceState subclass.
 *
 * Waiting for a frame can be the exception to the per-object locks:
 * frameReady(ANARI_WAIT) and frameBufferMap() first call
 * BaseFrame::waitWithoutObjectLock() without the frame's lock (holding an
 * internal reference to it instead), and only then lock the frame to call
 * BaseFrame::frameReady() or map(). A frame opts in by overriding
 * waitWithoutObjectLock(); by default it does nothing, and the wait happens
 * under the lock.
 */
struct BaseDevice : public anari::DeviceImpl,
                    ParameterizedObject,
                    LockableObject
{
  // Data Arrays //////////////////////////////////////////////////////////////

  void *mapArray(ANARIArray) override;
  void unmapArray(ANARIArray) override;

  // Object + Parameter Lifetime Management ///////////////////////////////////

  int getProperty(ANARIObject o,
      const char *name,
      ANARIDataType type,
      void *mem,
      uint64_t size,
      uint32_t mask) override;

  void setParameter(ANARIObject o,
      const char *name,
      ANARIDataType type,
      const void *mem) override;

  void unsetParameter(ANARIObject o, const char *name) override;
  void unsetAllParameters(ANARIObject o) override;

  void *mapParameterArray1D(ANARIObject o,
      const char *name,
      ANARIDataType dataType,
      uint64_t numElements1,
      uint64_t *elementStride) override;
  void *mapParameterArray2D(ANARIObject o,
      const char *name,
      ANARIDataType dataType,
      uint64_t numElements1,
      uint64_t numElements2,
      uint64_t *elementStride) override;
  void *mapParameterArray3D(ANARIObject o,
      const char *name,
      ANARIDataType dataType,
      uint64_t numElements1,
      uint64_t numElements2,
      uint64_t numElements3,
      uint64_t *elementStride) override;
  void unmapParameterArray(ANARIObject o, const char *name) override;

  void commitParameters(ANARIObject o) override;

  void release(ANARIObject o) override;
  void retain(ANARIObject o) override;

  // FrameBuffer Manipulation /////////////////////////////////////////////////

  const void *frameBufferMap(ANARIFrame f,
      const char *channel,
      uint32_t *width,
      uint32_t *height,
      ANARIDataType *pixelType) override;

  void frameBufferUnmap(ANARIFrame f, const char *channel) override;

  // Frame Rendering //////////////////////////////////////////////////////////

  void renderFrame(ANARIFrame f) override;
  int frameReady(ANARIFrame f, ANARIWaitMask m) override;
  void discardFrame(ANARIFrame f) override;

  /////////////////////////////////////////////////////////////////////////////
  // Helper/other functions and data members
  /////////////////////////////////////////////////////////////////////////////

  BaseDevice(ANARIStatusCallback defaultCallback, const void *userPtr);
  BaseDevice(ANARILibrary l);
  virtual ~BaseDevice() override;

 protected:
  template <typename... Args>
  void reportMessage(
      ANARIStatusSeverity, const char *fmt, Args &&...args) const;

  virtual void deviceCommitParameters();
  virtual int deviceGetProperty(const char *name,
      ANARIDataType type,
      void *mem,
      uint64_t size,
      uint32_t mask);

  // Device work is work that must not overlap the device's own commit-buffer
  // flushes or renders. The defaults run it on the calling thread. A device
  // that flushes or renders on a worker thread overrides both hooks to run
  // the work there, after the work already queued, and must run it directly
  // when called on that worker (e.g. from a completion or status callback).

  // Runs a device query: getProperty() runs an object's ANARI_WAIT query this
  // way (flush the commit buffer, then query the object). Pass 'work' whether
  // it runs on the thread that called this (directly on the worker counts):
  // run on another, the query does not take a frame's object lock, as a
  // thread holding it may be waiting for the frame's work queued behind the
  // query. A device whose deviceGetProperty() flushes for ANARI_WAIT should
  // run that flush through here too. An override that can't wait for the
  // work (it would deadlock) may refuse it, reporting why, and return false
  // without running it; getProperty() then returns 0.
  virtual bool runDeviceQuery(const std::function<void(bool)> &work);
  // Runs a device release: release() runs the app's last release of an array
  // the device still uses this way, as it privatizes the array. An override
  // must run 'work', which drops the reference, even if it can't wait for the
  // device's queued work.
  virtual void runDeviceRelease(const std::function<void()> &work);

  std::unique_ptr<BaseGlobalDeviceState> m_state;

 private:
  // Holds the object's lock; empty for a frame whose completion callback is
  // running on this thread (see BaseFrame::completingOnThisThread()).
  std::unique_lock<std::mutex> getObjectLock(ANARIObject object);

  void deviceGetProperty(const char *id, ANARIDataType type, const void *mem);
  void deviceSetParameter(const char *id, ANARIDataType type, const void *mem);
  void deviceUnsetParameter(const char *id);
  void deviceUnsetAllParameters();
  uint32_t m_refCount{1};
};

std::string string_printf(const char *fmt, ...);

// Inlined definitions ////////////////////////////////////////////////////////

template <typename... Args>
inline void BaseDevice::reportMessage(
    ANARIStatusSeverity severity, const char *fmt, Args &&...args) const
{
  auto msg = string_printf(fmt, std::forward<Args>(args)...);
  m_state->messageFunction(severity, msg, ANARI_DEVICE, this);
}

// Helper functions ///////////////////////////////////////////////////////////

template <typename OBJECT_T = BaseObject, typename HANDLE_T = ANARIObject>
inline OBJECT_T &referenceFromHandle(HANDLE_T handle)
{
  return *((OBJECT_T *)handle);
}

} // namespace helium
