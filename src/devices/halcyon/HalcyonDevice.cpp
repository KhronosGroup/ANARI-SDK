// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "HalcyonDevice.h"

#include "array/Array1D.h"
#include "array/Array2D.h"
#include "array/Array3D.h"
#include "array/ObjectArray.h"
#include "frame/Frame.h"
#include "scene/volume/spatial_field/SpatialField.h"

#include "anari_library_halcyon_queries.h"

// std
#include <algorithm>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace halcyon {

namespace {

// The ANARI SDK's version, passed in by CMakeLists.txt
constexpr int VERSION_MAJOR = HALCYON_VERSION_MAJOR;
constexpr int VERSION_MINOR = HALCYON_VERSION_MINOR;
constexpr int VERSION_PATCH = HALCYON_VERSION_PATCH;

const std::string &versionName()
{
  static const std::string name = "halcyon " HALCYON_VERSION_STRING;
  return name;
}

} // namespace

// Data Arrays ////////////////////////////////////////////////////////////////

void *HalcyonDevice::mapArray(ANARIArray a)
{
  auto &semaphore = deviceState()->renderingSemaphore;
  while (true) {
    {
      std::lock_guard<std::recursive_mutex> lock(m_arrayMapMutex);
      // Mapping an array that's already mapped (helium warns) doesn't hold
      // the semaphore again: one unmap unmaps it
      auto &array = helium::referenceFromHandle<helium::Array>(a);
      if (array.isMapped() || semaphore.tryArrayMapAcquire(a))
        return helium::BaseDevice::mapArray(a);
    }
    semaphore.waitForDeviceWork();
  }
}

void HalcyonDevice::unmapArray(ANARIArray a)
{
  std::lock_guard<std::recursive_mutex> lock(m_arrayMapMutex);
  // Unmapping an array that isn't mapped (helium warns) releases nothing
  const bool wasMapped =
      helium::referenceFromHandle<helium::Array>(a).isMapped();
  helium::BaseDevice::unmapArray(a);
  if (wasMapped)
    deviceState()->renderingSemaphore.arrayMapRelease(a);
}

// Object + Parameter Lifetime Management /////////////////////////////////////

void HalcyonDevice::release(ANARIObject o)
{
  if (!o || handleIsDevice(o)
      || !anari::isArray(helium::referenceFromHandle(o).type())) {
    helium::BaseDevice::release(o);
    return;
  }

  auto &semaphore = deviceState()->renderingSemaphore;
  auto &array = helium::referenceFromHandle<helium::Array>(o);

  // The last public release of a shared array that objects still use
  // privatizes it (helium copies it, see array/Array1D.h), and then the app
  // may free its memory, which a render in progress may still be reading. So
  // the release holds device work off, as a mapping does. helium doesn't make
  // a release wait (BaseDevice::release() only drops the reference). Like
  // mapArray(), this waits for device work without holding m_arrayMapMutex: a
  // status callback during that work may map, unmap or release arrays. Inside
  // device work (such a callback) there is nothing to wait for but the work
  // itself, so the release goes ahead with a WARNING: the work in progress
  // may still read the app's memory.
  // A status callback from the copy that waits on device work gets an ERROR
  // (see RenderingSemaphore::whyThisThreadCantWait()), not a hang
  const char holdKey = 0; // identifies this release's hold on device work
  bool holding = false;
  while (true) {
    {
      // Held through the release, so two threads releasing the last
      // references can't both skip the unmap (or the hold)
      std::lock_guard<std::recursive_mutex> lock(m_arrayMapMutex);
      const bool last = array.useCount(helium::RefType::PUBLIC) == 1;
      const bool privatizes = last
          && array.ownership() == helium::ArrayDataOwnership::SHARED
          && !array.wasPrivatized()
          && array.useCount(helium::RefType::INTERNAL) > 0;
      const bool privatizesInDeviceWork =
          privatizes && RenderingSemaphore::DeviceWork::active();
      const bool mayRelease = !privatizes || privatizesInDeviceWork
          || (holding = semaphore.tryBeginReleaseHold(&holdKey));
      if (mayRelease) {
        if (privatizesInDeviceWork) {
          array.reportMessage(ANARI_SEVERITY_WARNING,
              "shared array released from a status callback during a render "
              "(or other device work), which can't wait for that work to end: "
              "it may still read the app's memory until it ends");
        }
        // The app can't unmap an array after its last public release, and a
        // mapping left behind would hold off device work for good: unmap it
        // (keeping what the app wrote), and report the app's mistake
        if (last && array.isMapped()) {
          array.reportMessage(ANARI_SEVERITY_ERROR,
              "array released while mapped; unmapping it");
          unmapArray((ANARIArray)o);
        }
        helium::BaseDevice::release(o);
        break;
      }
    }
    semaphore.waitForDeviceWork();
  }

  if (holding)
    semaphore.endReleaseHold(&holdKey);
}

// API Objects ////////////////////////////////////////////////////////////////

ANARIArray1D HalcyonDevice::newArray1D(const void *appMemory,
    ANARIMemoryDeleter deleter,
    const void *userData,
    ANARIDataType type,
    uint64_t numItems)
{
  initDevice();

  Array1DMemoryDescriptor md;
  md.appMemory = appMemory;
  md.deleter = deleter;
  md.deleterPtr = userData;
  md.elementType = type;
  md.numItems = numItems;

  if (anari::isObject(type))
    return (ANARIArray1D) new ObjectArray(deviceState(), md);
  else
    return (ANARIArray1D) new Array1D(deviceState(), md);
}

ANARIArray2D HalcyonDevice::newArray2D(const void *appMemory,
    ANARIMemoryDeleter deleter,
    const void *userData,
    ANARIDataType type,
    uint64_t numItems1,
    uint64_t numItems2)
{
  initDevice();

  Array2DMemoryDescriptor md;
  md.appMemory = appMemory;
  md.deleter = deleter;
  md.deleterPtr = userData;
  md.elementType = type;
  md.numItems1 = numItems1;
  md.numItems2 = numItems2;

  return (ANARIArray2D) new Array2D(deviceState(), md);
}

ANARIArray3D HalcyonDevice::newArray3D(const void *appMemory,
    ANARIMemoryDeleter deleter,
    const void *userData,
    ANARIDataType type,
    uint64_t numItems1,
    uint64_t numItems2,
    uint64_t numItems3)
{
  initDevice();

  Array3DMemoryDescriptor md;
  md.appMemory = appMemory;
  md.deleter = deleter;
  md.deleterPtr = userData;
  md.elementType = type;
  md.numItems1 = numItems1;
  md.numItems2 = numItems2;
  md.numItems3 = numItems3;

  return (ANARIArray3D) new Array3D(deviceState(), md);
}

ANARICamera HalcyonDevice::newCamera(const char *subtype)
{
  initDevice();
  return (ANARICamera)Camera::createInstance(subtype, deviceState());
}

ANARIFrame HalcyonDevice::newFrame()
{
  initDevice();
  return (ANARIFrame) new Frame(deviceState());
}

ANARIGeometry HalcyonDevice::newGeometry(const char *subtype)
{
  initDevice();
  return (ANARIGeometry)Geometry::createInstance(subtype, deviceState());
}

ANARIGroup HalcyonDevice::newGroup()
{
  initDevice();
  return (ANARIGroup) new Group(deviceState());
}

ANARIInstance HalcyonDevice::newInstance(const char * /*subtype*/)
{
  initDevice();
  return (ANARIInstance) new Instance(deviceState());
}

ANARILight HalcyonDevice::newLight(const char *subtype)
{
  initDevice();
  return (ANARILight)Light::createInstance(subtype, deviceState());
}

ANARIMaterial HalcyonDevice::newMaterial(const char *subtype)
{
  initDevice();
  return (ANARIMaterial)Material::createInstance(subtype, deviceState());
}

ANARIRenderer HalcyonDevice::newRenderer(const char *subtype)
{
  initDevice();
  return (ANARIRenderer)Renderer::createInstance(subtype, deviceState());
}

ANARISampler HalcyonDevice::newSampler(const char *subtype)
{
  initDevice();
  return (ANARISampler)Sampler::createInstance(subtype, deviceState());
}

ANARISpatialField HalcyonDevice::newSpatialField(const char *subtype)
{
  initDevice();
  return (ANARISpatialField)SpatialField::createInstance(
      subtype, deviceState());
}

ANARISurface HalcyonDevice::newSurface()
{
  initDevice();
  return (ANARISurface) new Surface(deviceState());
}

ANARIVolume HalcyonDevice::newVolume(const char *subtype)
{
  initDevice();
  return (ANARIVolume)Volume::createInstance(subtype, deviceState());
}

ANARIWorld HalcyonDevice::newWorld()
{
  initDevice();
  return (ANARIWorld) new World(deviceState());
}

// Object properties //////////////////////////////////////////////////////////

// Replaces helium::BaseDevice::getProperty() for objects. That flushes the
// commit buffer on the calling thread for ANARI_WAIT, so commits (and here
// World's scene update for "bounds") could run while the task queue renders.
// Here ANARI_WAIT queries run on the task queue instead, after work already
// issued, as device work that waits for mapped arrays.
int HalcyonDevice::getProperty(ANARIObject o,
    const char *name,
    ANARIDataType type,
    void *mem,
    uint64_t size,
    uint32_t mask)
{
  if (handleIsDevice(o))
    return helium::BaseDevice::getProperty(o, name, type, mem, size, mask);

  auto &state = *deviceState();

  auto query = [&]() -> int {
    auto &obj = helium::referenceFromHandle(o);
    // Frame properties are only written by the task queue, and an app thread
    // waiting on a frame (frameReady, map) holds its lock: taking it here
    // would deadlock a completion callback querying that frame.
    std::unique_lock<std::mutex> lock;
    if (obj.type() != ANARI_FRAME)
      lock = obj.scopeLockObject();
    return obj.getProperty(name, type, mem, size, mask);
  };

  if (mask != ANARI_WAIT)
    return query();

  // The query's device work can't start while this thread holds it off
  if (!RenderingSemaphore::DeviceWork::active()) {
    if (const char *why = state.renderingSemaphore.whyThisThreadCantWait()) {
      helium::referenceFromHandle(o).reportMessage(ANARI_SEVERITY_ERROR,
          "anariGetProperty() with ANARI_WAIT would deadlock: %s; not waiting",
          why);
      return 0;
    }
  }

  auto flushAndQuery = [&]() -> int {
    // Nested in device work (a status callback during a render or flush),
    // flushing again would reenter the commit buffer mid-flush
    if (RenderingSemaphore::DeviceWork::active())
      return query();
    RenderingSemaphore::DeviceWork deviceWork(state.renderingSemaphore);
    state.flushCommitBuffer();
    return query();
  };

  // On the worker (from a callback) no other device work is queued ahead
  if (state.taskQueue.onWorkerThread())
    return flushAndQuery();

  int result = 0;
  state.taskQueue.enqueue([&]() { result = flushAndQuery(); }).get();
  return result;
}

// Query functions ////////////////////////////////////////////////////////////

// The SDK's JSON declares the renderer parameters of its core and extensions
// on a renderer named 'default', so the generated queries list one after
// halcyon's own. 'default' is only the spec's alias of the first renderer
// listed, 'preview': it isn't listed, and introspection answers it as
// 'preview'.
static const char *rendererSubtype(ANARIDataType objectType, const char *subtype)
{
  return objectType == ANARI_RENDERER && subtype
          && std::string_view(subtype) == "default"
      ? "preview"
      : subtype;
}

const char **HalcyonDevice::getObjectSubtypes(ANARIDataType objectType)
{
  if (objectType != ANARI_RENDERER)
    return halcyon::query_object_types(objectType);

  static const std::vector<const char *> renderers = [] {
    std::vector<const char *> list;
    for (const char **s = halcyon::query_object_types(ANARI_RENDERER); *s; s++)
      if (std::string_view(*s) != "default")
        list.push_back(*s);
    list.push_back(nullptr);
    return list;
  }();
  return const_cast<const char **>(renderers.data());
}

const void *HalcyonDevice::getObjectInfo(ANARIDataType objectType,
    const char *objectSubtype,
    const char *infoName,
    ANARIDataType infoType)
{
  return halcyon::query_object_info(objectType,
      rendererSubtype(objectType, objectSubtype),
      infoName,
      infoType);
}

const void *HalcyonDevice::getParameterInfo(ANARIDataType objectType,
    const char *objectSubtype,
    const char *parameterName,
    ANARIDataType parameterType,
    const char *infoName,
    ANARIDataType infoType)
{
  return halcyon::query_param_info(objectType,
      rendererSubtype(objectType, objectSubtype),
      parameterName,
      parameterType,
      infoName,
      infoType);
}

// Other HalcyonDevice definitions /////////////////////////////////////////////

HalcyonDevice::HalcyonDevice(ANARIStatusCallback cb, const void *ptr)
    : helium::BaseDevice(cb, ptr)
{
  m_state = std::make_unique<HalcyonDeviceGlobalState>(this_device());
  deviceCommitParameters();
}

HalcyonDevice::HalcyonDevice(ANARILibrary l) : helium::BaseDevice(l)
{
  m_state = std::make_unique<HalcyonDeviceGlobalState>(this_device());
  deviceCommitParameters();
}

HalcyonDevice::~HalcyonDevice()
{
  auto &state = *deviceState();
  state.taskQueue.flush();
  state.commitBuffer.clear();
  reportMessage(ANARI_SEVERITY_DEBUG, "destroying Halcyon device (%p)", this);
  rtcReleaseDevice(state.embreeDevice);
}

void HalcyonDevice::initDevice()
{
  if (m_initialized)
    return;

  reportMessage(ANARI_SEVERITY_DEBUG, "initializing Halcyon device (%p)", this);

  auto &state = *deviceState();

  int numThreads = 0;
  const char *numThreadsFromEnv = getenv("HALCYON_NUM_THREADS");
  if (numThreadsFromEnv)
    numThreads = std::atoi(numThreadsFromEnv);

  std::string config = "threads=" + std::to_string(numThreads);

  state.anariDevice = (anari::Device)this;
  state.embreeDevice = rtcNewDevice(config.c_str());
  if (!state.embreeDevice) {
    reportMessage(ANARI_SEVERITY_FATAL_ERROR,
        "Embree error %d - cannot create device\n",
        rtcGetDeviceError(nullptr));
  }

  rtcSetDeviceErrorFunction(
      state.embreeDevice,
      [](void *userPtr, RTCError error, const char *str) {
        auto *d = (HalcyonDevice *)userPtr;
        d->reportMessage(
            ANARI_SEVERITY_ERROR, "Embree error %d - '%s'", error, str);
      },
      this);

  m_initialized = true;
}

void HalcyonDevice::deviceCommitParameters()
{
  // runs on the application thread while renders may be running on the
  // worker, so the values take effect at the next render
  HalcyonDeviceGlobalState::DeviceParameters p;
  p.allowInvalidSurfaceMaterials =
      getParam<bool>("allowInvalidMaterials", true);
  p.invalidMaterialColor =
      getParam<float4>("invalidMaterialColor", float4(1.f, 0.f, 1.f, 1.f));
  deviceState()->commitDeviceParameters(p);

  helium::BaseDevice::deviceCommitParameters();
}

int HalcyonDevice::deviceGetProperty(const char *name,
    ANARIDataType type,
    void *mem,
    uint64_t size,
    uint32_t flags)
{
  std::string_view prop = name;
  if (prop == "extension" && type == ANARI_STRING_LIST) {
    helium::writeToVoidP(mem, query_extensions());
    return 1;
  } else if (prop == "version" && type == ANARI_INT32) {
    helium::writeToVoidP(
        mem, VERSION_MAJOR * 10000 + VERSION_MINOR * 100 + VERSION_PATCH);
    return 1;
  } else if (prop == "version.major" && type == ANARI_INT32) {
    helium::writeToVoidP(mem, VERSION_MAJOR);
    return 1;
  } else if (prop == "version.minor" && type == ANARI_INT32) {
    helium::writeToVoidP(mem, VERSION_MINOR);
    return 1;
  } else if (prop == "version.patch" && type == ANARI_INT32) {
    helium::writeToVoidP(mem, VERSION_PATCH);
    return 1;
  } else if (prop == "version.name.size" && type == ANARI_UINT64) {
    helium::writeToVoidP(mem, uint64_t(versionName().size() + 1));
    return 1;
  } else if (prop == "version.name" && type == ANARI_STRING) {
    // size is the buffer size; the name is truncated to fit, terminated
    if (size == 0)
      return 0;
    const auto &name = versionName();
    std::memset(mem, 0, size);
    std::memcpy(mem, name.data(), std::min(uint64_t(name.size()), size - 1));
    return 1;
  } else if (prop == "anariVersion.major" && type == ANARI_INT32) {
    helium::writeToVoidP(mem, 1);
    return 1;
  } else if (prop == "anariVersion.minor" && type == ANARI_INT32) {
    helium::writeToVoidP(mem, 1);
    return 1;
  } else if (prop == "geometryMaxIndex" && type == ANARI_UINT64) {
    helium::writeToVoidP(mem, uint64_t(std::numeric_limits<uint32_t>::max()));
    return 1;
  } else if (prop == "halcyon" && type == ANARI_BOOL) {
    helium::writeToVoidP(mem, true);
    return 1;
  }
  return 0;
}

HalcyonDeviceGlobalState *HalcyonDevice::deviceState() const
{
  return (HalcyonDeviceGlobalState *)helium::BaseDevice::m_state.get();
}

} // namespace halcyon
