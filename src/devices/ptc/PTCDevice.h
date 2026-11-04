// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// ptc
#include "MpiExtension.h"
#include "Status.h"
// helium
#include "helium/BaseDevice.h"
// std
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

namespace anari_ptc {

struct FrameWrapper;

struct PTCDevice : public anari::DeviceImpl, helium::ParameterizedObject
{
  /////////////////////////////////////////////////////////////////////////////
  // Main interface to accepting API calls
  /////////////////////////////////////////////////////////////////////////////

  // Object creation //////////////////////////////////////////////////////////

  // Implement anariNewArray1D()
  ANARIArray1D newArray1D(const void *appMemory,
      ANARIMemoryDeleter deleter,
      const void *userdata,
      ANARIDataType,
      uint64_t numItems1) override;

  // Implement anariNewArray2D()
  ANARIArray2D newArray2D(const void *appMemory,
      ANARIMemoryDeleter deleter,
      const void *userdata,
      ANARIDataType,
      uint64_t numItems1,
      uint64_t numItems2) override;

  // Implement anariNewArray3D()
  ANARIArray3D newArray3D(const void *appMemory,
      ANARIMemoryDeleter deleter,
      const void *userdata,
      ANARIDataType,
      uint64_t numItems1,
      uint64_t numItems2,
      uint64_t numItems3) override;

  // Implement anariNewGeometry()
  ANARIGeometry newGeometry(const char *type) override;

  // Implement anariNewMaterial()
  ANARIMaterial newMaterial(const char *material_type) override;

  // Implement anariNewSampler()
  ANARISampler newSampler(const char *type) override;

  // Implement anariNewSurface()
  ANARISurface newSurface() override;

  // Implement anariNewSpatialField()
  ANARISpatialField newSpatialField(const char *type) override;

  // Implement anariNewVolume()
  ANARIVolume newVolume(const char *type) override;

  // Implement anariNewLight()
  ANARILight newLight(const char *type) override;

  // Implement anariGroup()
  ANARIGroup newGroup() override;

  // Implement anariNewInstance()
  ANARIInstance newInstance(const char *type) override;

  // Implement anariNewWorld()
  ANARIWorld newWorld() override;

  // Implement anariNewCamera()
  ANARICamera newCamera(const char *type) override;

  // Implement anariNewRenderer()
  ANARIRenderer newRenderer(const char *type) override;

  // Implement anariNewFrame()
  ANARIFrame newFrame() override;

  // Object + Parameter Lifetime Management ///////////////////////////////////

  // Implement anariSetParameter()
  void setParameter(ANARIObject object,
      const char *name,
      ANARIDataType type,
      const void *mem) override;

  // Implement anariUnsetParameter()
  void unsetParameter(ANARIObject object, const char *name) override;

  // Implement anariUnsetAllParameters()
  void unsetAllParameters(ANARIObject object) override;

  // Implement anariMapParameterArray1D()
  void *mapParameterArray1D(ANARIObject object,
      const char *name,
      ANARIDataType dataType,
      uint64_t numElements1,
      uint64_t *elementStride) override;

  // Implement anariMapParameterArray2D()
  void *mapParameterArray2D(ANARIObject object,
      const char *name,
      ANARIDataType dataType,
      uint64_t numElements1,
      uint64_t numElements2,
      uint64_t *elementStride) override;

  // Implement anariMapParameterArray3D()
  void *mapParameterArray3D(ANARIObject object,
      const char *name,
      ANARIDataType dataType,
      uint64_t numElements1,
      uint64_t numElements2,
      uint64_t numElements3,
      uint64_t *elementStride) override;

  // Implement anariUnmapParameterArray()
  void unmapParameterArray(ANARIObject object, const char *name) override;

  // Implement anariCommitParameters()
  void commitParameters(ANARIObject object) override;

  // Implement anariRelease()
  void release(ANARIObject _obj) override;

  // Implement anariRetain()
  void retain(ANARIObject _obj) override;

  // Implement anariMapArray()
  void *mapArray(ANARIArray) override;

  // Implement anariUnmapArray()
  void unmapArray(ANARIArray) override;

  // Implement anariGetProperty()
  int getProperty(ANARIObject object,
      const char *name,
      ANARIDataType type,
      void *mem,
      uint64_t size,
      ANARIWaitMask mask) override;

  // Object Query Interface ///////////////////////////////////////////////////

  // Implement anariGetObjectSubtypes()
  const char **getObjectSubtypes(ANARIDataType objectType) override;

  // Implement anariGetObjectInfo()
  const void *getObjectInfo(ANARIDataType objectType,
      const char *objectSubtype,
      const char *infoName,
      ANARIDataType infoType) override;

  // Implement anariGetParameterInfo()
  const void *getParameterInfo(ANARIDataType objectType,
      const char *objectSubtype,
      const char *parameterName,
      ANARIDataType parameterType,
      const char *infoName,
      ANARIDataType infoType) override;

  // FrameBuffer Manipulation /////////////////////////////////////////////////

  // Implement anariFrameBufferMap
  const void *frameBufferMap(ANARIFrame fb,
      const char *channel,
      uint32_t *width,
      uint32_t *height,
      ANARIDataType *pixelType) override;

  // Implement anariFrameBufferUnmap
  void frameBufferUnmap(ANARIFrame fb, const char *channel) override;

  // Frame Rendering //////////////////////////////////////////////////////////

  // Implement anariRenderFrame()
  void renderFrame(ANARIFrame) override;

  // Implement anariFrameReady()
  int frameReady(ANARIFrame, ANARIWaitMask) override;

  // Implement anariDiscardFrame()
  void discardFrame(ANARIFrame) override;

  /////////////////////////////////////////////////////////////////////////////
  // Helper/other functions and data members
  /////////////////////////////////////////////////////////////////////////////

  PTCDevice(ANARIStatusCallback defaultCallback, const void *userPtr);
  PTCDevice(ANARILibrary);
  ~PTCDevice() override;

  void initDevice();

 private:
  friend struct FrameWrapper; // reports status, announces its destruction

  // The wrapped device, after initDevice(); throws if there is none
  anari::DeviceImpl &wrapped();
  // 'f' as a frame of this device; throws if it isn't one
  FrameWrapper *checkedFrameWrapper(ANARIFrame f);

  /*! initializes MPI privately unless it is already; if ptc owns MPI, this
      device then keeps it initialized until it is destroyed */
  void acquireMpi();
  struct PrivateMpi;
  void deviceCommitParameters();
  int deviceGetProperty(
      const char *name, ANARIDataType type, void *mem, uint64_t size);

  bool isFrameHandle(ANARIObject o);
  void removeFrameWrapper(FrameWrapper *fw);
  static FrameWrapper *asFrameWrapper(ANARIObject o);

  // Runs the one of onDevice(), onFrame(FrameWrapper *) or
  // onWrapped(anari::DeviceImpl &) that 'object' belongs to; all three return
  // the same type. wrapped() is only evaluated for the last.
  template <typename OnDevice, typename OnFrame, typename OnWrapped>
  auto route(ANARIObject object,
      OnDevice &&onDevice,
      OnFrame &&onFrame,
      OnWrapped &&onWrapped) -> decltype(onDevice());
  // As route(), for entry points that never address the device itself
  template <typename OnFrame, typename OnWrapped>
  auto routeFrameOrWrapped(
      ANARIObject object, OnFrame &&onFrame, OnWrapped &&onWrapped)
      -> decltype(onWrapped(std::declval<anari::DeviceImpl &>()));
  // Runs 'entryPoint' guarded as 'fn' on the wrapped device; an error returns
  // a value-initialized R
  template <typename R, typename... P, typename... A>
  R forwardToWrapped(
      const char *entryPoint, R (anari::DeviceImpl::*fn)(P...), A &&...args);

  /*! reports through the status callback, with ANARI_STATUS_NO_ERROR
      below ERROR and ANARI_STATUS_UNKNOWN_ERROR from ERROR up */
  template <typename... Args>
  void reportMessage(
      ANARIStatusSeverity, const char *fmt, Args &&...args) const;
  /*! reports 'msg' through the status callback with this status code */
  void reportStatus(
      ANARIStatusSeverity, ANARIStatusCode, const char *msg) const;

  // No exception may escape through the C API: every entry point runs its
  // body through guarded(), which reports an exception as an ERROR through
  // the status callback and then returns `onError`.
  template <typename R, typename F>
  R guarded(const char *entryPoint, R onError, F &&f) const noexcept;
  template <typename F>
  void guarded(const char *entryPoint, F &&f) const noexcept;
  // reports the exception being handled; only call from a catch block
  void reportException(const char *entryPoint) const noexcept;

  uint32_t m_refCount{1};

  // The wrapped device: chosen, then created at most once (lazy), and a
  // failure to create it is sticky. All state checks live here.
  class WrappedDevice
  {
   public:
    // before initialization: remember the committed choice
    // ("": PTC_ANARI_LIBRARY or helide; subtype defaults to "default")
    void choose(std::string library, std::string subtype);
    bool initialized() const;
    bool chosenDiffersFrom(
        const std::string &library, const std::string &subtype) const;
    const std::string &library() const;
    const std::string &subtype() const;
    // marks initialization as done (it happens once, whatever the outcome)
    void markInitialized();
    void setReady(anari::DeviceImpl *d);
    void setFailed(std::string why);
    // calls f(device, handle) if the wrapped device is ready; returns
    // whether it was
    template <typename F>
    bool ifReady(F &&f) const;
    // the wrapped device; throws if initialization failed
    anari::DeviceImpl &require() const;
    // releases the wrapped device, if any
    void release();

   private:
    bool m_initialized{false};
    anari::DeviceImpl *m_device{nullptr};
    std::string m_failure; // why m_device is null once initialized
    std::string m_library;
    std::string m_subtype{"default"};
  } m_wrapped;
  // Handles of the live FrameWrappers, for O(1) isFrameHandle() on every
  // object-taking entry point. Thread-safety policy: deliberately unguarded.
  // ANARI does not require a device to be thread-safe (the application
  // externally synchronizes calls on one device, as for every Helium device),
  // and newFrame()/release() mutate this set only from such calls. Unlike
  // m_extensions, which is lazily built by property queries that apps may
  // issue concurrently, nothing here is touched outside the API entry points.
  std::unordered_set<const void *> m_frameWrappers;
  std::mutex m_extensionsMutex;
  std::unique_ptr<ExtensionList> m_extensions; // built on first query
  FrameParameterLists m_frameParameterLists;

  // Private MPI, if ptc initialized it: shared by every device initialized
  // while it is alive, finalized when the last of them releases it. Null
  // when the app owns MPI (ptc never finalizes that) or before initDevice().
  std::shared_ptr<PrivateMpi> m_privateMpi;
};

// Inlined definitions ////////////////////////////////////////////////////////

inline FrameWrapper *PTCDevice::asFrameWrapper(ANARIObject o)
{
  return reinterpret_cast<FrameWrapper *>(o);
}

inline bool PTCDevice::WrappedDevice::initialized() const
{
  return m_initialized;
}

inline const std::string &PTCDevice::WrappedDevice::library() const
{
  return m_library;
}

inline const std::string &PTCDevice::WrappedDevice::subtype() const
{
  return m_subtype;
}

inline void PTCDevice::WrappedDevice::markInitialized()
{
  m_initialized = true;
}

inline void PTCDevice::WrappedDevice::setReady(anari::DeviceImpl *d)
{
  m_device = d;
}

inline void PTCDevice::WrappedDevice::setFailed(std::string why)
{
  m_failure = std::move(why);
}

template <typename F>
inline bool PTCDevice::WrappedDevice::ifReady(F &&f) const
{
  if (!m_device)
    return false;
  f(*m_device, m_device->this_device());
  return true;
}

template <typename... Args>
inline void PTCDevice::reportMessage(
    ANARIStatusSeverity severity, const char *fmt, Args &&...args) const
{
  if (!defaultStatusCallback())
    return;
  auto msg = helium::string_printf(fmt, std::forward<Args>(args)...);
  reportStatus(severity, defaultStatusCode(severity), msg.c_str());
}

inline void PTCDevice::reportStatus(
    ANARIStatusSeverity severity, ANARIStatusCode code, const char *msg) const
{
  if (auto statusCB = defaultStatusCallback()) {
    statusCB(defaultStatusCallbackUserPtr(),
        this_device(),
        this_device(),
        ANARI_OBJECT,
        severity,
        code,
        msg);
  }
}

template <typename R, typename F>
inline R PTCDevice::guarded(
    const char *entryPoint, R onError, F &&f) const noexcept
{
  try {
    return f();
  } catch (...) {
    reportException(entryPoint);
  }
  return onError;
}

template <typename F>
inline void PTCDevice::guarded(const char *entryPoint, F &&f) const noexcept
{
  try {
    f();
  } catch (...) {
    reportException(entryPoint);
  }
}

template <typename OnDevice, typename OnFrame, typename OnWrapped>
inline auto PTCDevice::route(ANARIObject object,
    OnDevice &&onDevice,
    OnFrame &&onFrame,
    OnWrapped &&onWrapped) -> decltype(onDevice())
{
  if (handleIsDevice(object))
    return onDevice();
  else if (isFrameHandle(object))
    return onFrame(asFrameWrapper(object));
  else
    return onWrapped(wrapped());
}

template <typename OnFrame, typename OnWrapped>
inline auto PTCDevice::routeFrameOrWrapped(
    ANARIObject object, OnFrame &&onFrame, OnWrapped &&onWrapped)
    -> decltype(onWrapped(std::declval<anari::DeviceImpl &>()))
{
  if (isFrameHandle(object))
    return onFrame(asFrameWrapper(object));
  else
    return onWrapped(wrapped());
}

template <typename R, typename... P, typename... A>
inline R PTCDevice::forwardToWrapped(
    const char *entryPoint, R (anari::DeviceImpl::*fn)(P...), A &&...args)
{
  if constexpr (std::is_void_v<R>) {
    guarded(entryPoint, [&] { (wrapped().*fn)(std::forward<A>(args)...); });
  } else {
    return guarded(entryPoint, R{}, [&] {
      return (wrapped().*fn)(std::forward<A>(args)...);
    });
  }
}

} // namespace anari_ptc
