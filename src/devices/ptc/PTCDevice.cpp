// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// ptc
#include "PTCDevice.h"
#include "FrameWrapper.h"
#include "WrappedLibrary.h"
// helium
#include <helium/BaseGlobalDeviceState.h>
#include <helium/BaseObject.h>
// mpi
#include <mpi.h>
// std
#include <algorithm>
#include <cstring>
#include <memory>
#include <stdexcept>

namespace anari_ptc {

constexpr const char *WRAPPED_LIBRARY_PARAM = "wrappedLibrary";
constexpr const char *WRAPPED_SUBTYPE_PARAM = "wrappedDeviceSubtype";

// Private MPI /////////////////////////////////////////////////////////////////

// Owns an MPI_Init made by ptc; MPI_Finalize when the last holder lets go.
struct PTCDevice::PrivateMpi
{
  PrivateMpi();
  ~PrivateMpi();
  PrivateMpi(const PrivateMpi &) = delete;
  PrivateMpi &operator=(const PrivateMpi &) = delete;

  static std::mutex s_mutex; // guards s_current, s_finalized and release
  static std::weak_ptr<PrivateMpi> s_current;
  static bool s_finalized; // a PrivateMpi finalized MPI
};

PTCDevice::PrivateMpi::PrivateMpi()
{
  MPI_Init(nullptr, nullptr);
}

PTCDevice::PrivateMpi::~PrivateMpi()
{
  MPI_Finalize();
  s_finalized = true;
}

std::mutex PTCDevice::PrivateMpi::s_mutex;
std::weak_ptr<PTCDevice::PrivateMpi> PTCDevice::PrivateMpi::s_current;
bool PTCDevice::PrivateMpi::s_finalized = false;

// API Objects ////////////////////////////////////////////////////////////////

ANARIArray1D PTCDevice::newArray1D(const void *appMemory,
    ANARIMemoryDeleter deleter,
    const void *userData,
    ANARIDataType type,
    uint64_t numItems)
{
  return forwardToWrapped("anariNewArray1D",
      &anari::DeviceImpl::newArray1D,
      appMemory,
      deleter,
      userData,
      type,
      numItems);
}

ANARIArray2D PTCDevice::newArray2D(const void *appMemory,
    ANARIMemoryDeleter deleter,
    const void *userData,
    ANARIDataType type,
    uint64_t numItems1,
    uint64_t numItems2)
{
  return forwardToWrapped("anariNewArray2D",
      &anari::DeviceImpl::newArray2D,
      appMemory,
      deleter,
      userData,
      type,
      numItems1,
      numItems2);
}

ANARIArray3D PTCDevice::newArray3D(const void *appMemory,
    ANARIMemoryDeleter deleter,
    const void *userData,
    ANARIDataType type,
    uint64_t numItems1,
    uint64_t numItems2,
    uint64_t numItems3)
{
  return forwardToWrapped("anariNewArray3D",
      &anari::DeviceImpl::newArray3D,
      appMemory,
      deleter,
      userData,
      type,
      numItems1,
      numItems2,
      numItems3);
}

ANARICamera PTCDevice::newCamera(const char *subtype)
{
  return forwardToWrapped(
      "anariNewCamera", &anari::DeviceImpl::newCamera, subtype);
}

ANARIFrame PTCDevice::newFrame()
{
  return guarded("anariNewFrame", ANARIFrame(nullptr), [&] {
    auto &w = wrapped();
    auto *fw = new FrameWrapper(this, w.this_device(), w.newFrame());
    m_frameWrappers.insert(static_cast<const void *>(fw));
    return (ANARIFrame)fw;
  });
}

ANARIGeometry PTCDevice::newGeometry(const char *subtype)
{
  return forwardToWrapped(
      "anariNewGeometry", &anari::DeviceImpl::newGeometry, subtype);
}

ANARIGroup PTCDevice::newGroup()
{
  return forwardToWrapped("anariNewGroup", &anari::DeviceImpl::newGroup);
}

ANARIInstance PTCDevice::newInstance(const char *subtype)
{
  return forwardToWrapped(
      "anariNewInstance", &anari::DeviceImpl::newInstance, subtype);
}

ANARILight PTCDevice::newLight(const char *subtype)
{
  return forwardToWrapped(
      "anariNewLight", &anari::DeviceImpl::newLight, subtype);
}

ANARIMaterial PTCDevice::newMaterial(const char *subtype)
{
  return forwardToWrapped(
      "anariNewMaterial", &anari::DeviceImpl::newMaterial, subtype);
}

ANARIRenderer PTCDevice::newRenderer(const char *subtype)
{
  return forwardToWrapped(
      "anariNewRenderer", &anari::DeviceImpl::newRenderer, subtype);
}

ANARISampler PTCDevice::newSampler(const char *subtype)
{
  return forwardToWrapped(
      "anariNewSampler", &anari::DeviceImpl::newSampler, subtype);
}

ANARISpatialField PTCDevice::newSpatialField(const char *subtype)
{
  return forwardToWrapped(
      "anariNewSpatialField", &anari::DeviceImpl::newSpatialField, subtype);
}

ANARISurface PTCDevice::newSurface()
{
  return forwardToWrapped("anariNewSurface", &anari::DeviceImpl::newSurface);
}

ANARIVolume PTCDevice::newVolume(const char *subtype)
{
  return forwardToWrapped(
      "anariNewVolume", &anari::DeviceImpl::newVolume, subtype);
}

ANARIWorld PTCDevice::newWorld()
{
  return forwardToWrapped("anariNewWorld", &anari::DeviceImpl::newWorld);
}

// Object + Parameter Lifetime Management /////////////////////////////////////

void PTCDevice::setParameter(
    ANARIObject object, const char *name, ANARIDataType type, const void *mem)
{
  guarded("anariSetParameter", [&] {
    route(
        object,
        [&] { this->setParam(name, type, mem); },
        [&](FrameWrapper *fw) { fw->setParameter(name, type, mem); },
        [&](anari::DeviceImpl &w) { w.setParameter(object, name, type, mem); });
  });
}

void PTCDevice::unsetParameter(ANARIObject object, const char *name)
{
  guarded("anariUnsetParameter", [&] {
    route(
        object,
        [&] { this->removeParam(name); },
        [&](FrameWrapper *fw) { fw->unsetParameter(name); },
        [&](anari::DeviceImpl &w) { w.unsetParameter(object, name); });
  });
}

void PTCDevice::unsetAllParameters(ANARIObject object)
{
  guarded("anariUnsetAllParameters", [&] {
    route(
        object,
        [&] { this->removeAllParams(); },
        [&](FrameWrapper *fw) { fw->unsetAllParameters(); },
        [&](anari::DeviceImpl &w) { w.unsetAllParameters(object); });
  });
}

void *PTCDevice::mapParameterArray1D(ANARIObject object,
    const char *name,
    ANARIDataType dataType,
    uint64_t numElements1,
    uint64_t *elementStride)
{
  return guarded("anariMapParameterArray1D", (void *)nullptr, [&] {
    return routeFrameOrWrapped(
        object,
        [&](FrameWrapper *fw) {
          return fw->mapParameterArray1D(
              name, dataType, numElements1, elementStride);
        },
        [&](anari::DeviceImpl &w) {
          return w.mapParameterArray1D(
              object, name, dataType, numElements1, elementStride);
        });
  });
}

void *PTCDevice::mapParameterArray2D(ANARIObject object,
    const char *name,
    ANARIDataType dataType,
    uint64_t numElements1,
    uint64_t numElements2,
    uint64_t *elementStride)
{
  return guarded("anariMapParameterArray2D", (void *)nullptr, [&] {
    return routeFrameOrWrapped(
        object,
        [&](FrameWrapper *fw) {
          return fw->mapParameterArray2D(
              name, dataType, numElements1, numElements2, elementStride);
        },
        [&](anari::DeviceImpl &w) {
          return w.mapParameterArray2D(object,
              name,
              dataType,
              numElements1,
              numElements2,
              elementStride);
        });
  });
}

void *PTCDevice::mapParameterArray3D(ANARIObject object,
    const char *name,
    ANARIDataType dataType,
    uint64_t numElements1,
    uint64_t numElements2,
    uint64_t numElements3,
    uint64_t *elementStride)
{
  return guarded("anariMapParameterArray3D", (void *)nullptr, [&] {
    return routeFrameOrWrapped(
        object,
        [&](FrameWrapper *fw) {
          return fw->mapParameterArray3D(name,
              dataType,
              numElements1,
              numElements2,
              numElements3,
              elementStride);
        },
        [&](anari::DeviceImpl &w) {
          return w.mapParameterArray3D(object,
              name,
              dataType,
              numElements1,
              numElements2,
              numElements3,
              elementStride);
        });
  });
}

void PTCDevice::unmapParameterArray(ANARIObject object, const char *name)
{
  guarded("anariUnmapParameterArray", [&] {
    routeFrameOrWrapped(
        object,
        [&](FrameWrapper *fw) { fw->unmapParameterArray(name); },
        [&](anari::DeviceImpl &w) { w.unmapParameterArray(object, name); });
  });
}

void PTCDevice::commitParameters(ANARIObject object)
{
  guarded("anariCommitParameters", [&] {
    route(
        object,
        [&] { deviceCommitParameters(); },
        [&](FrameWrapper *fw) { fw->commitParameters(); },
        [&](anari::DeviceImpl &w) { w.commitParameters(object); });
  });
}

void PTCDevice::release(ANARIObject o)
{
  guarded("anariRelease", [&] {
    if (o == nullptr)
      return;

    route(
        o,
        [&] {
          // nothing may touch `this` after this, in here or in guarded()
          if (--m_refCount == 0)
            delete this;
        },
        [&](FrameWrapper *fw) { fw->release(); },
        [&](anari::DeviceImpl &w) { w.release(o); });
  });
}

void PTCDevice::retain(ANARIObject o)
{
  guarded("anariRetain", [&] {
    route(
        o,
        [&] { m_refCount++; },
        [&](FrameWrapper *fw) { fw->retain(); },
        [&](anari::DeviceImpl &w) { w.retain(o); });
  });
}

void *PTCDevice::mapArray(ANARIArray a)
{
  return forwardToWrapped("anariMapArray", &anari::DeviceImpl::mapArray, a);
}

void PTCDevice::unmapArray(ANARIArray a)
{
  forwardToWrapped("anariUnmapArray", &anari::DeviceImpl::unmapArray, a);
}

int PTCDevice::getProperty(ANARIObject object,
    const char *name,
    ANARIDataType type,
    void *mem,
    uint64_t size,
    ANARIWaitMask mask)
{
  return guarded("anariGetProperty", 0, [&] {
    return route(
        object,
        [&] { return deviceGetProperty(name, type, mem, size); },
        [&](FrameWrapper *fw) {
          return fw->getProperty(name, type, mem, size, mask);
        },
        [&](anari::DeviceImpl &w) {
          return w.getProperty(object, name, type, mem, size, mask);
        });
  });
}

// Query functions ////////////////////////////////////////////////////////////

const char **PTCDevice::getObjectSubtypes(ANARIDataType objectType)
{
  return forwardToWrapped("anariGetObjectSubtypes",
      &anari::DeviceImpl::getObjectSubtypes,
      objectType);
}

const void *PTCDevice::getObjectInfo(ANARIDataType objectType,
    const char *objectSubtype,
    const char *infoName,
    ANARIDataType infoType)
{
  const void *const onError = nullptr;
  return guarded("anariGetObjectInfo", onError, [&]() -> const void * {
    auto *info =
        wrapped().getObjectInfo(objectType, objectSubtype, infoName, infoType);
    if (objectType == ANARI_FRAME && infoName
        && std::strcmp(infoName, "parameter") == 0
        && infoType == ANARI_PARAMETER_LIST) {
      return m_frameParameterLists.withMpiCommunicator(
          objectSubtype, (const ANARIParameter *)info);
    }
    return info;
  });
}

const void *PTCDevice::getParameterInfo(ANARIDataType objectType,
    const char *objectSubtype,
    const char *parameterName,
    ANARIDataType parameterType,
    const char *infoName,
    ANARIDataType infoType)
{
  return guarded("anariGetParameterInfo", (const void *)nullptr, [&] {
    if (isMpiCommunicatorParameter(objectType, parameterName, parameterType))
      return mpiCommunicatorInfo(infoName, infoType);

    return wrapped().getParameterInfo(objectType,
        objectSubtype,
        parameterName,
        parameterType,
        infoName,
        infoType);
  });
}

// FrameBuffer Manipulation ///////////////////////////////////////////////////

const void *PTCDevice::frameBufferMap(ANARIFrame f,
    const char *channel,
    uint32_t *width,
    uint32_t *height,
    ANARIDataType *pixelType)
{
  return guarded("anariMapFrame", (const void *)nullptr, [&] {
    return checkedFrameWrapper(f)->frameBufferMap(
        channel, width, height, pixelType);
  });
}

void PTCDevice::frameBufferUnmap(ANARIFrame f, const char *channel)
{
  guarded("anariUnmapFrame",
      [&] { checkedFrameWrapper(f)->frameBufferUnmap(channel); });
}

// Frame Rendering ////////////////////////////////////////////////////////////

void PTCDevice::renderFrame(ANARIFrame f)
{
  guarded("anariRenderFrame", [&] { checkedFrameWrapper(f)->renderFrame(); });
}

int PTCDevice::frameReady(ANARIFrame f, ANARIWaitMask m)
{
  return guarded("anariFrameReady", 0, [&] {
    return checkedFrameWrapper(f)->frameReady(m);
  });
}

void PTCDevice::discardFrame(ANARIFrame f)
{
  guarded("anariDiscardFrame", [&] { checkedFrameWrapper(f)->discardFrame(); });
}

// Other PTCDevice definitions ////////////////////////////////////////////////

void PTCDevice::reportException(const char *entryPoint) const noexcept
{
  try {
    try {
      throw;
    } catch (const StatusError &e) {
      reportStatus(ANARI_SEVERITY_ERROR,
          e.code,
          (std::string(entryPoint) + ": " + e.what()).c_str());
    } catch (const std::exception &e) {
      reportMessage(ANARI_SEVERITY_ERROR, "%s: %s", entryPoint, e.what());
    } catch (...) {
      reportMessage(ANARI_SEVERITY_ERROR, "%s: unknown exception", entryPoint);
    }
  } catch (...) {
    // reporting failed as well (out of memory, or a throwing status
    // callback); there is nothing left to tell the app with
  }
}

PTCDevice::PTCDevice(ANARIStatusCallback defaultCallback, const void *userPtr)
{
  anari::DeviceImpl::m_defaultStatusCB = defaultCallback;
  anari::DeviceImpl::m_defaultStatusCBUserPtr = userPtr;
}

PTCDevice::PTCDevice(ANARILibrary l) : DeviceImpl(l) {}

PTCDevice::~PTCDevice()
{
  reportMessage(ANARI_SEVERITY_DEBUG, "destroying PTC device (%p)", this);

  m_wrapped.release();

  // Device release is collective, so every rank gets here for the same
  // device and finalizes together.
  // (Status messages go out after unlocking: the app's callback may create
  // or release devices.)
  if (m_privateMpi) {
    bool last = false;
    {
      // under the lock, so a concurrent acquireMpi() never sees MPI
      // mid-finalize
      std::lock_guard<std::mutex> lock(PrivateMpi::s_mutex);
      last = m_privateMpi.use_count() == 1;
      m_privateMpi.reset();
    }
    if (last)
      reportMessage(ANARI_SEVERITY_DEBUG, "finalized MPI");
  }
}

// Loads the wrapped library and creates the wrapped device, once. A failure
// is reported once as a FATAL_ERROR; from then on wrapped() throws, so every
// call that needs the wrapped device reports an ERROR and returns nothing.
void PTCDevice::initDevice()
{
  if (m_wrapped.initialized())
    return;

  reportMessage(ANARI_SEVERITY_DEBUG, "initializing PTC device (%p)", this);

  // first: if this throws, nothing else was set up yet
  acquireMpi();

  // whatever happens next, it happens once
  m_wrapped.markInitialized();

  auto w = loadWrappedLibrary(m_wrapped.library(),
      defaultStatusCallback(),
      defaultStatusCallbackUserPtr());
  if (!w.fallbackWarning.empty())
    reportMessage(ANARI_SEVERITY_WARNING, "%s", w.fallbackWarning.c_str());
  if (!w.library) {
    m_wrapped.setFailed(w.failure);
    reportMessage(ANARI_SEVERITY_FATAL_ERROR, "%s", w.failure.c_str());
    return;
  }

  auto *device = (anari::DeviceImpl *)anariNewDevice(
      w.library, m_wrapped.subtype().c_str());
  if (!device) {
    auto failure = "failed to create a '" + m_wrapped.subtype()
        + "' device from the wrapped ANARI library '" + w.name + "'";
    m_wrapped.setFailed(failure);
    reportMessage(ANARI_SEVERITY_FATAL_ERROR, "%s", failure.c_str());
    anariUnloadLibrary(w.library);
    return;
  }
  m_wrapped.setReady(device);

  reportMessage(ANARI_SEVERITY_DEBUG,
      "wrapping a '%s' device from ANARI library '%s'",
      m_wrapped.subtype().c_str(),
      w.name.c_str());
}

anari::DeviceImpl &PTCDevice::wrapped()
{
  initDevice();
  return m_wrapped.require();
}

void PTCDevice::WrappedDevice::choose(std::string library, std::string subtype)
{
  m_library = std::move(library);
  m_subtype = std::move(subtype);
}

bool PTCDevice::WrappedDevice::chosenDiffersFrom(
    const std::string &library, const std::string &subtype) const
{
  return library != m_library || subtype != m_subtype;
}

anari::DeviceImpl &PTCDevice::WrappedDevice::require() const
{
  if (!m_device)
    throw std::runtime_error("no wrapped device: " + m_failure);
  return *m_device;
}

void PTCDevice::WrappedDevice::release()
{
  if (m_device)
    m_device->release(m_device->this_device());
  m_device = nullptr;
}

FrameWrapper *PTCDevice::checkedFrameWrapper(ANARIFrame f)
{
  if (!isFrameHandle(f))
    throw std::runtime_error("not a frame of this device");
  return asFrameWrapper(f);
}

void PTCDevice::acquireMpi()
{
  if (m_privateMpi)
    return; // an earlier initDevice() got this far
  bool initializedHere = false;
  {
    std::lock_guard<std::mutex> lock(PrivateMpi::s_mutex);
    m_privateMpi = PrivateMpi::s_current.lock();
    if (!m_privateMpi) {
      int finalized = 0;
      MPI_Finalized(&finalized);
      if (finalized) {
        // MPI can't be initialized twice in one process
        throw std::runtime_error(PrivateMpi::s_finalized
                ? "MPI was finalized when the last ptc device that used it "
                  "was released; keep a ptc device alive, or initialize MPI "
                  "in the app, to create more"
                : "MPI was finalized by the app");
      }
      int initialized = 0;
      MPI_Initialized(&initialized);
      if (!initialized) {
        m_privateMpi = std::make_shared<PrivateMpi>();
        PrivateMpi::s_current = m_privateMpi;
        initializedHere = true;
      }
    }
  }
  reportMessage(ANARI_SEVERITY_DEBUG,
      initializedHere ? "initialized MPI" : "MPI already initialized");
}

// 'wrappedLibrary' and 'wrappedDeviceSubtype' choose the wrapped device; they
// take effect only if committed before anything initialized the device. The
// other device parameters are forwarded to the wrapped device.
void PTCDevice::deviceCommitParameters()
{
  const auto library = getParamString(WRAPPED_LIBRARY_PARAM, "");
  const auto subtype = getParamString(WRAPPED_SUBTYPE_PARAM, "default");
  if (!m_wrapped.initialized()) {
    m_wrapped.choose(library, subtype);
  } else if (m_wrapped.chosenDiffersFrom(library, subtype)) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "'%s' and '%s' are ignored: the wrapped device was already chosen "
        "(commit them before creating objects or querying the device)",
        WRAPPED_LIBRARY_PARAM,
        WRAPPED_SUBTYPE_PARAM);
  }

  initDevice();
  // if not ready: already reported as a FATAL_ERROR; nothing to forward to
  m_wrapped.ifReady([&](anari::DeviceImpl &w, ANARIDevice handle) {
    w.unsetAllParameters(handle);
    for (auto param = params_begin(); param != params_end(); ++param) {
      if (param->first == WRAPPED_LIBRARY_PARAM
          || param->first == WRAPPED_SUBTYPE_PARAM)
        continue;
      w.setParameter(handle,
          param->first.c_str(),
          param->second.type(),
          param->second.data());
    }
    w.commitParameters(handle);
  });
}

// Under KHR_DATA_PARALLEL_MPI anariGetProperty is collective, but device
// properties are the same on every rank, so no communication is needed.
int PTCDevice::deviceGetProperty(
    const char *name, ANARIDataType type, void *mem, uint64_t size)
{
  initDevice();

  if (name && std::strcmp(name, "extension") == 0
      && type == ANARI_STRING_LIST) {
    std::lock_guard<std::mutex> lock(m_extensionsMutex);
    if (!m_extensions) {
      const char *const *wrappedList = nullptr;
      m_wrapped.ifReady([&](anari::DeviceImpl &w, ANARIDevice handle) {
        w.getProperty(handle,
            "extension",
            ANARI_STRING_LIST,
            &wrappedList,
            sizeof(wrappedList),
            ANARI_WAIT);
      });
      m_extensions = std::make_unique<ExtensionList>(wrappedList);
    }
    helium::writeToVoidP(mem, m_extensions->data());
    return 1;
  }

  // ptc's own version (the wrapped device's anariVersion.* still applies)
  static const std::string versionName = PTC_VERSION_STRING;
  const std::string prop = name ? name : "";
  struct VersionInt
  {
    const char *name;
    int32_t value;
  };
  static const VersionInt versionInts[] = {
      {"version",
          int32_t(PTC_VERSION_MAJOR * 1000 + PTC_VERSION_MINOR * 100
              + PTC_VERSION_PATCH)},
      {"version.major", int32_t(PTC_VERSION_MAJOR)},
      {"version.minor", int32_t(PTC_VERSION_MINOR)},
      {"version.patch", int32_t(PTC_VERSION_PATCH)},
  };
  if (type == ANARI_INT32) {
    for (const auto &v : versionInts) {
      if (prop == v.name) {
        helium::writeToVoidP(mem, v.value);
        return 1;
      }
    }
  }
  if (prop == "version.name.size" && type == ANARI_UINT64) {
    helium::writeToVoidP(mem, uint64_t(versionName.size() + 1));
    return 1;
  } else if (prop == "version.name" && type == ANARI_STRING) {
    if (!mem || size == 0)
      return 0;
    std::memset(mem, 0, size);
    std::memcpy(mem,
        versionName.data(),
        std::min(uint64_t(versionName.size()), size - 1));
    return 1;
  }

  // Everything else (anariVersion.*, geometryMaxIndex, ...) is whatever the
  // wrapped device reports.
  int result = 0;
  m_wrapped.ifReady([&](anari::DeviceImpl &w, ANARIDevice handle) {
    result = w.getProperty(handle, name, type, mem, size, ANARI_WAIT);
  });
  return result;
}

bool PTCDevice::isFrameHandle(ANARIObject o)
{
  return m_frameWrappers.count(static_cast<const void *>(o)) != 0;
}

void PTCDevice::removeFrameWrapper(FrameWrapper *fw)
{
  m_frameWrappers.erase(static_cast<const void *>(fw));
}

} // namespace anari_ptc
