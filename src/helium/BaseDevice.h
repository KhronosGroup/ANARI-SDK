// Copyright 2021-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "BaseGlobalDeviceState.h"
#include "LockableObject.h"
#include "utility/IntrusivePtr.h"
#include "utility/ParameterizedObject.h"
// anari
#include "anari/backend/DeviceImpl.h"

namespace helium {

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
  virtual int deviceGetProperty(
      const char *name, ANARIDataType type, void *mem, uint64_t size);

  std::unique_ptr<BaseGlobalDeviceState> m_state;

 private:
  std::scoped_lock<std::mutex> getObjectLock(ANARIObject object);

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
