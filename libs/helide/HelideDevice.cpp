// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "HelideDevice.h"

#include "array/Array1D.h"
#include "array/Array2D.h"
#include "array/Array3D.h"
#include "array/ObjectArray.h"
#include "frame/Frame.h"
#include "scene/volume/spatial_field/SpatialField.h"

namespace helide {

///////////////////////////////////////////////////////////////////////////////
// Generated function declarations ////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

const char **query_object_types(ANARIDataType type);

const void *query_object_info(ANARIDataType type,
    const char *subtype,
    const char *infoName,
    ANARIDataType infoType);

const void *query_param_info(ANARIDataType type,
    const char *subtype,
    const char *paramName,
    ANARIDataType paramType,
    const char *infoName,
    ANARIDataType infoType);

const char **query_extensions();

///////////////////////////////////////////////////////////////////////////////
// Helper functions ///////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

template <typename HANDLE_T, typename OBJECT_T>
inline HANDLE_T getHandleForAPI(OBJECT_T *object)
{
  return (HANDLE_T)object;
}

template <typename OBJECT_T, typename HANDLE_T, typename... Args>
inline HANDLE_T createObjectForAPI(HelideGlobalState *s, Args &&...args)
{
  return getHandleForAPI<HANDLE_T>(
      new OBJECT_T(s, std::forward<Args>(args)...));
}

///////////////////////////////////////////////////////////////////////////////
// HelideDevice definitions ///////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

// Data Arrays ////////////////////////////////////////////////////////////////

void *HelideDevice::mapArray(ANARIArray a)
{
  deviceState()->renderingSemaphore.arrayMapAcquire();
  return helium::BaseDevice::mapArray(a);
}

void HelideDevice::unmapArray(ANARIArray a)
{
  helium::BaseDevice::unmapArray(a);
  deviceState()->renderingSemaphore.arrayMapRelease();
}

// API Objects ////////////////////////////////////////////////////////////////

ANARIArray1D HelideDevice::newArray1D(const void *appMemory,
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
    return createObjectForAPI<ObjectArray, ANARIArray1D>(deviceState(), md);
  else
    return createObjectForAPI<Array1D, ANARIArray1D>(deviceState(), md);
}

ANARIArray2D HelideDevice::newArray2D(const void *appMemory,
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

  return createObjectForAPI<Array2D, ANARIArray2D>(deviceState(), md);
}

ANARIArray3D HelideDevice::newArray3D(const void *appMemory,
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

  return createObjectForAPI<Array3D, ANARIArray3D>(deviceState(), md);
}

ANARICamera HelideDevice::newCamera(const char *subtype)
{
  initDevice();
  return getHandleForAPI<ANARICamera>(
      Camera::createInstance(subtype, deviceState()));
}

ANARIFrame HelideDevice::newFrame()
{
  initDevice();
  return createObjectForAPI<Frame, ANARIFrame>(deviceState());
}

ANARIGeometry HelideDevice::newGeometry(const char *subtype)
{
  initDevice();
  return getHandleForAPI<ANARIGeometry>(
      Geometry::createInstance(subtype, deviceState()));
}

ANARIGroup HelideDevice::newGroup()
{
  initDevice();
  return createObjectForAPI<Group, ANARIGroup>(deviceState());
}

ANARIInstance HelideDevice::newInstance(const char * /*subtype*/)
{
  initDevice();
  return createObjectForAPI<Instance, ANARIInstance>(deviceState());
}

ANARILight HelideDevice::newLight(const char *subtype)
{
  initDevice();
  return getHandleForAPI<ANARILight>(
      Light::createInstance(subtype, deviceState()));
}

ANARIMaterial HelideDevice::newMaterial(const char *subtype)
{
  initDevice();
  return getHandleForAPI<ANARIMaterial>(
      Material::createInstance(subtype, deviceState()));
}

ANARIRenderer HelideDevice::newRenderer(const char *subtype)
{
  initDevice();
  return getHandleForAPI<ANARIRenderer>(
      Renderer::createInstance(subtype, deviceState()));
}

ANARISampler HelideDevice::newSampler(const char *subtype)
{
  initDevice();
  return getHandleForAPI<ANARISampler>(
      Sampler::createInstance(subtype, deviceState()));
}

ANARISpatialField HelideDevice::newSpatialField(const char *subtype)
{
  initDevice();
  return getHandleForAPI<ANARISpatialField>(
      SpatialField::createInstance(subtype, deviceState()));
}

ANARISurface HelideDevice::newSurface()
{
  initDevice();
  return createObjectForAPI<Surface, ANARISurface>(deviceState());
}

ANARIVolume HelideDevice::newVolume(const char *subtype)
{
  initDevice();
  return getHandleForAPI<ANARIVolume>(
      Volume::createInstance(subtype, deviceState()));
}

ANARIWorld HelideDevice::newWorld()
{
  initDevice();
  return createObjectForAPI<World, ANARIWorld>(deviceState());
}

// Query functions ////////////////////////////////////////////////////////////

const char **HelideDevice::getObjectSubtypes(ANARIDataType objectType)
{
  return helide::query_object_types(objectType);
}

const void *HelideDevice::getObjectInfo(ANARIDataType objectType,
    const char *objectSubtype,
    const char *infoName,
    ANARIDataType infoType)
{
  return helide::query_object_info(
      objectType, objectSubtype, infoName, infoType);
}

const void *HelideDevice::getParameterInfo(ANARIDataType objectType,
    const char *objectSubtype,
    const char *parameterName,
    ANARIDataType parameterType,
    const char *infoName,
    ANARIDataType infoType)
{
  return helide::query_param_info(objectType,
      objectSubtype,
      parameterName,
      parameterType,
      infoName,
      infoType);
}

// Object + Parameter Lifetime Management /////////////////////////////////////

int HelideDevice::getProperty(ANARIObject object,
    const char *name,
    ANARIDataType type,
    void *mem,
    uint64_t size,
    uint32_t mask)
{
  if (mask == ANARI_WAIT) {
    auto lock = scopeLockObject();
    deviceState()->waitOnCurrentFrame();
  }

  return helium::BaseDevice::getProperty(object, name, type, mem, size, mask);
}

// Other HelideDevice definitions /////////////////////////////////////////////

HelideDevice::HelideDevice(ANARIStatusCallback cb, const void *ptr)
    : helium::BaseDevice(cb, ptr)
{
  m_state = std::make_unique<HelideGlobalState>(this_device());
  deviceCommitParameters();
}

HelideDevice::HelideDevice(ANARILibrary l) : helium::BaseDevice(l)
{
  m_state = std::make_unique<HelideGlobalState>(this_device());
  deviceCommitParameters();
}

HelideDevice::~HelideDevice()
{
  auto &state = *deviceState();

  state.commitBufferClear();

  reportMessage(ANARI_SEVERITY_DEBUG, "destroying helide device (%p)", this);

  rtcReleaseDevice(state.embreeDevice);
}

void HelideDevice::initDevice()
{
  if (m_initialized)
    return;

  reportMessage(ANARI_SEVERITY_DEBUG, "initializing helide device (%p)", this);

  auto &state = *deviceState();

  state.embreeDevice = rtcNewDevice(nullptr);

  if (!state.embreeDevice) {
    reportMessage(ANARI_SEVERITY_ERROR,
        "Embree error %d - cannot create device\n",
        rtcGetDeviceError(nullptr));
  }

  rtcSetDeviceErrorFunction(
      state.embreeDevice,
      [](void *userPtr, RTCError error, const char *str) {
        auto *d = (HelideDevice *)userPtr;
        d->reportMessage(
            ANARI_SEVERITY_ERROR, "Embree error %d - '%s'", error, str);
      },
      this);

  m_initialized = true;
}

void HelideDevice::deviceCommitParameters()
{
  auto &state = *deviceState();

  bool allowInvalidSurfaceMaterials = state.allowInvalidSurfaceMaterials;

  state.allowInvalidSurfaceMaterials =
      getParam<bool>("allowInvalidMaterials", true);
  state.invalidMaterialColor =
      getParam<float4>("invalidMaterialColor", float4(1.f, 0.f, 1.f, 1.f));

  if (allowInvalidSurfaceMaterials != state.allowInvalidSurfaceMaterials)
    state.objectUpdates.lastBLSReconstructSceneRequest = helium::newTimeStamp();

  helium::BaseDevice::deviceCommitParameters();
}

int HelideDevice::deviceGetProperty(
    const char *name, ANARIDataType type, void *mem, uint64_t size)
{
  std::string_view prop = name;
  if (prop == "extension" && type == ANARI_STRING_LIST) {
    helium::writeToVoidP(mem, query_extensions());
    return 1;
  } else if (prop == "helide" && type == ANARI_BOOL) {
    helium::writeToVoidP(mem, true);
    return 1;
  }
  return 0;
}

HelideGlobalState *HelideDevice::deviceState() const
{
  return (HelideGlobalState *)helium::BaseDevice::m_state.get();
}

} // namespace helide
