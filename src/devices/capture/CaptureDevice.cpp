// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "CaptureDevice.h"

#include "CaptureFrame.h"
#include "CaptureObject.h"
// helium
#include "helium/array/Array1D.h"
#include "helium/array/Array2D.h"
#include "helium/array/Array3D.h"
#include "helium/array/ObjectArray.h"
// std
#include <algorithm>

#include "anari_library_capture_queries.h"

namespace capture_device {

// API Objects ////////////////////////////////////////////////////////////////

ANARIArray1D CaptureDevice::newArray1D(const void *appMemory,
    ANARIMemoryDeleter deleter,
    const void *userData,
    ANARIDataType type,
    uint64_t numItems)
{
  count(ApiCall::NewArray1D);
  helium::Array1DMemoryDescriptor md;
  md.appMemory = appMemory;
  md.deleter = deleter;
  md.deleterPtr = userData;
  md.elementType = type;
  md.numItems = numItems;

  if (anari::isObject(type))
    return (ANARIArray1D) new helium::ObjectArray(deviceState(), md);
  else
    return (ANARIArray1D) new helium::Array1D(deviceState(), md);
}

ANARIArray2D CaptureDevice::newArray2D(const void *appMemory,
    ANARIMemoryDeleter deleter,
    const void *userData,
    ANARIDataType type,
    uint64_t numItems1,
    uint64_t numItems2)
{
  count(ApiCall::NewArray2D);
  helium::Array2DMemoryDescriptor md;
  md.appMemory = appMemory;
  md.deleter = deleter;
  md.deleterPtr = userData;
  md.elementType = type;
  md.numItems1 = numItems1;
  md.numItems2 = numItems2;
  return (ANARIArray2D) new helium::Array2D(deviceState(), md);
}

ANARIArray3D CaptureDevice::newArray3D(const void *appMemory,
    ANARIMemoryDeleter deleter,
    const void *userData,
    ANARIDataType type,
    uint64_t numItems1,
    uint64_t numItems2,
    uint64_t numItems3)
{
  count(ApiCall::NewArray3D);
  helium::Array3DMemoryDescriptor md;
  md.appMemory = appMemory;
  md.deleter = deleter;
  md.deleterPtr = userData;
  md.elementType = type;
  md.numItems1 = numItems1;
  md.numItems2 = numItems2;
  md.numItems3 = numItems3;
  return (ANARIArray3D) new helium::Array3D(deviceState(), md);
}

#define CAPTURE_NEW_OBJECT(Function, Call, Handle, Type)                       \
  Handle CaptureDevice::Function(const char *subtype)                          \
  {                                                                            \
    count(ApiCall::Call);                                                      \
    return (Handle) new Object(Type, subtype, deviceState());                  \
  }

CAPTURE_NEW_OBJECT(newCamera, NewCamera, ANARICamera, ANARI_CAMERA)
CAPTURE_NEW_OBJECT(newGeometry, NewGeometry, ANARIGeometry, ANARI_GEOMETRY)
CAPTURE_NEW_OBJECT(newInstance, NewInstance, ANARIInstance, ANARI_INSTANCE)
CAPTURE_NEW_OBJECT(newLight, NewLight, ANARILight, ANARI_LIGHT)
CAPTURE_NEW_OBJECT(newMaterial, NewMaterial, ANARIMaterial, ANARI_MATERIAL)
CAPTURE_NEW_OBJECT(newRenderer, NewRenderer, ANARIRenderer, ANARI_RENDERER)
CAPTURE_NEW_OBJECT(newSampler, NewSampler, ANARISampler, ANARI_SAMPLER)
CAPTURE_NEW_OBJECT(
    newSpatialField, NewSpatialField, ANARISpatialField, ANARI_SPATIAL_FIELD)
CAPTURE_NEW_OBJECT(newVolume, NewVolume, ANARIVolume, ANARI_VOLUME)

#undef CAPTURE_NEW_OBJECT

ANARIFrame CaptureDevice::newFrame()
{
  count(ApiCall::NewFrame);
  return (ANARIFrame) new Frame(deviceState());
}

ANARIGroup CaptureDevice::newGroup()
{
  count(ApiCall::NewGroup);
  return (ANARIGroup) new Object(ANARI_GROUP, nullptr, deviceState());
}

ANARISurface CaptureDevice::newSurface()
{
  count(ApiCall::NewSurface);
  return (ANARISurface) new Object(ANARI_SURFACE, nullptr, deviceState());
}

ANARIWorld CaptureDevice::newWorld()
{
  count(ApiCall::NewWorld);
  return (ANARIWorld) new Object(ANARI_WORLD, nullptr, deviceState());
}

ANARIObject CaptureDevice::newObject(const char *objectType, const char *type)
{
  count(ApiCall::NewObject);
  return anari::DeviceImpl::newObject(objectType, type);
}

// The rest of the API, counted and passed on to helium ///////////////////////

namespace {

// Set while helium calls the device on the application's behalf, on this
// thread, so those calls are not counted as the application's
thread_local bool t_internalCall = false;

struct InternalCalls
{
  InternalCalls() : m_was(t_internalCall)
  {
    t_internalCall = true;
  }
  ~InternalCalls()
  {
    t_internalCall = m_was;
  }

 private:
  bool m_was;
};

} // namespace

void CaptureDevice::count(ApiCall call)
{
  if (!t_internalCall)
    deviceState()->apiCalls[size_t(call)].fetch_add(
        1, std::memory_order_relaxed);
}

void *CaptureDevice::mapArray(ANARIArray a)
{
  count(ApiCall::MapArray);
  return helium::BaseDevice::mapArray(a);
}

void CaptureDevice::unmapArray(ANARIArray a)
{
  count(ApiCall::UnmapArray);
  helium::BaseDevice::unmapArray(a);
}

int CaptureDevice::getProperty(ANARIObject o,
    const char *name,
    ANARIDataType type,
    void *mem,
    uint64_t size,
    uint32_t mask)
{
  count(ApiCall::GetProperty);
  return helium::BaseDevice::getProperty(o, name, type, mem, size, mask);
}

void CaptureDevice::setParameter(
    ANARIObject o, const char *name, ANARIDataType type, const void *mem)
{
  count(ApiCall::SetParameter);
  helium::BaseDevice::setParameter(o, name, type, mem);
}

void CaptureDevice::unsetParameter(ANARIObject o, const char *name)
{
  count(ApiCall::UnsetParameter);
  helium::BaseDevice::unsetParameter(o, name);
}

void CaptureDevice::unsetAllParameters(ANARIObject o)
{
  count(ApiCall::UnsetAllParameters);
  helium::BaseDevice::unsetAllParameters(o);
}

void *CaptureDevice::mapParameterArray1D(ANARIObject o,
    const char *name,
    ANARIDataType dataType,
    uint64_t numElements1,
    uint64_t *elementStride)
{
  count(ApiCall::MapParameterArray1D);
  InternalCalls internal;
  return helium::BaseDevice::mapParameterArray1D(
      o, name, dataType, numElements1, elementStride);
}

void *CaptureDevice::mapParameterArray2D(ANARIObject o,
    const char *name,
    ANARIDataType dataType,
    uint64_t numElements1,
    uint64_t numElements2,
    uint64_t *elementStride)
{
  count(ApiCall::MapParameterArray2D);
  InternalCalls internal;
  return helium::BaseDevice::mapParameterArray2D(
      o, name, dataType, numElements1, numElements2, elementStride);
}

void *CaptureDevice::mapParameterArray3D(ANARIObject o,
    const char *name,
    ANARIDataType dataType,
    uint64_t numElements1,
    uint64_t numElements2,
    uint64_t numElements3,
    uint64_t *elementStride)
{
  count(ApiCall::MapParameterArray3D);
  InternalCalls internal;
  return helium::BaseDevice::mapParameterArray3D(o,
      name,
      dataType,
      numElements1,
      numElements2,
      numElements3,
      elementStride);
}

void CaptureDevice::unmapParameterArray(ANARIObject o, const char *name)
{
  count(ApiCall::UnmapParameterArray);
  InternalCalls internal;
  helium::BaseDevice::unmapParameterArray(o, name);
}

void CaptureDevice::commitParameters(ANARIObject o)
{
  count(ApiCall::CommitParameters);
  helium::BaseDevice::commitParameters(o);
}

void CaptureDevice::release(ANARIObject o)
{
  count(ApiCall::Release);
  helium::BaseDevice::release(o);
}

void CaptureDevice::retain(ANARIObject o)
{
  count(ApiCall::Retain);
  helium::BaseDevice::retain(o);
}

const void *CaptureDevice::frameBufferMap(ANARIFrame f,
    const char *channel,
    uint32_t *width,
    uint32_t *height,
    ANARIDataType *pixelType)
{
  count(ApiCall::MapFrame);
  return helium::BaseDevice::frameBufferMap(
      f, channel, width, height, pixelType);
}

void CaptureDevice::frameBufferUnmap(ANARIFrame f, const char *channel)
{
  count(ApiCall::UnmapFrame);
  helium::BaseDevice::frameBufferUnmap(f, channel);
}

void CaptureDevice::renderFrame(ANARIFrame f)
{
  // Counted first: a capture counts the render that takes it
  count(ApiCall::RenderFrame);
  helium::BaseDevice::renderFrame(f);
}

int CaptureDevice::frameReady(ANARIFrame f, ANARIWaitMask m)
{
  count(ApiCall::FrameReady);
  return helium::BaseDevice::frameReady(f, m);
}

void CaptureDevice::discardFrame(ANARIFrame f)
{
  count(ApiCall::DiscardFrame);
  helium::BaseDevice::discardFrame(f);
}

void (*CaptureDevice::getProcAddress(const char *name))(void)
{
  count(ApiCall::DeviceGetProcAddress);
  return anari::DeviceImpl::getProcAddress(name);
}

// Query functions ////////////////////////////////////////////////////////////

const char **CaptureDevice::getObjectSubtypes(ANARIDataType objectType)
{
  count(ApiCall::GetObjectSubtypes);
  return query_object_types(objectType);
}

const void *CaptureDevice::getObjectInfo(ANARIDataType objectType,
    const char *objectSubtype,
    const char *infoName,
    ANARIDataType infoType)
{
  count(ApiCall::GetObjectInfo);
  return query_object_info(objectType, objectSubtype, infoName, infoType);
}

const void *CaptureDevice::getParameterInfo(ANARIDataType objectType,
    const char *objectSubtype,
    const char *parameterName,
    ANARIDataType parameterType,
    const char *infoName,
    ANARIDataType infoType)
{
  count(ApiCall::GetParameterInfo);
  return query_param_info(objectType,
      objectSubtype,
      parameterName,
      parameterType,
      infoName,
      infoType);
}

// Other CaptureDevice definitions ////////////////////////////////////////////

CaptureState::CaptureState(ANARIDevice d) : helium::BaseGlobalDeviceState(d)
{
  for (auto &calls : apiCalls)
    calls = 0;
}

std::vector<std::pair<std::string, uint64_t>> CaptureState::callCounts() const
{
  static const char *names[] = {
#define CAPTURE_API_CALL_NAME(Name) "anari" #Name,
      CAPTURE_API_CALLS(CAPTURE_API_CALL_NAME)
#undef CAPTURE_API_CALL_NAME
  };
  std::vector<std::pair<std::string, uint64_t>> counts;
  for (size_t i = 0; i < apiCalls.size(); ++i) {
    const uint64_t n = apiCalls[i].load(std::memory_order_relaxed);
    if (n)
      counts.emplace_back(names[i], n);
  }
  std::sort(counts.begin(), counts.end());
  return counts;
}

CaptureDevice::CaptureDevice(ANARILibrary l) : helium::BaseDevice(l)
{
  auto state = std::make_unique<CaptureState>(this_device());
  state->anariDevice = this_device();
  m_state = std::move(state);
  deviceCommitParameters();
}

CaptureDevice::~CaptureDevice()
{
  deviceState()->commitBuffer.clear();
}

void CaptureDevice::deviceCommitParameters()
{
  auto &state = *deviceState();

  state.inlineArrayLimit = getParam<uint32_t>("inlineArrayLimit", 64);
  state.captureDir = getParamString("captureDir", "");

  if (hasParam("extensions", ANARI_STRING_LIST)) {
    state.extensions = getParamDirect("extensions").getStringList();
  } else {
    state.extensions.clear();
    for (auto **e = query_extensions(); e && *e; ++e)
      state.extensions.push_back(*e);
  }
  std::sort(state.extensions.begin(), state.extensions.end());
  state.extensions.erase(
      std::unique(state.extensions.begin(), state.extensions.end()),
      state.extensions.end());
  state.extensionList.clear();
  for (const auto &e : state.extensions)
    state.extensionList.push_back(e.c_str());
  state.extensionList.push_back(nullptr);

  helium::BaseDevice::deviceCommitParameters();
}

int CaptureDevice::deviceGetProperty(
    const char *name, ANARIDataType type, void *mem, uint64_t, uint32_t)
{
  std::string_view prop = name;
  if (prop == "extension" && type == ANARI_STRING_LIST) {
    helium::writeToVoidP(mem, deviceState()->extensionList.data());
    return 1;
  } else if (prop == "version" && type == ANARI_INT32) {
    int version = ANARI_SDK_VERSION_MAJOR * 1000 + ANARI_SDK_VERSION_MINOR * 100
        + ANARI_SDK_VERSION_PATCH;
    helium::writeToVoidP(mem, version);
    return 1;
  } else if (prop == "version.major" && type == ANARI_INT32) {
    helium::writeToVoidP(mem, int(ANARI_SDK_VERSION_MAJOR));
    return 1;
  } else if (prop == "version.minor" && type == ANARI_INT32) {
    helium::writeToVoidP(mem, int(ANARI_SDK_VERSION_MINOR));
    return 1;
  } else if (prop == "version.patch" && type == ANARI_INT32) {
    helium::writeToVoidP(mem, int(ANARI_SDK_VERSION_PATCH));
    return 1;
  }
  return 0;
}

CaptureState *CaptureDevice::deviceState() const
{
  return asCaptureState(helium::BaseDevice::m_state.get());
}

} // namespace capture_device
