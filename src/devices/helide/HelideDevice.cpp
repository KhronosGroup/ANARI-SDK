// Copyright 2021-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "HelideDevice.h"

#include "array/Array1D.h"
#include "array/Array2D.h"
#include "array/Array3D.h"
#include "array/ObjectArray.h"
#include "frame/Frame.h"
#include "spatial_field/SpatialField.h"
// std
#include <limits>

#include "anari_library_helide_queries.h"

namespace helide {

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
    return (ANARIArray1D) new ObjectArray(deviceState(), md);
  else
    return (ANARIArray1D) new Array1D(deviceState(), md);
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

  return (ANARIArray2D) new Array2D(deviceState(), md);
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

  return (ANARIArray3D) new Array3D(deviceState(), md);
}

ANARICamera HelideDevice::newCamera(const char *subtype)
{
  initDevice();
  return (ANARICamera)Camera::createInstance(subtype, deviceState());
}

ANARIFrame HelideDevice::newFrame()
{
  initDevice();
  return (ANARIFrame) new Frame(deviceState());
}

ANARIGeometry HelideDevice::newGeometry(const char *subtype)
{
  initDevice();
  return (ANARIGeometry)Geometry::createInstance(subtype, deviceState());
}

ANARIGroup HelideDevice::newGroup()
{
  initDevice();
  return (ANARIGroup) new Group(deviceState());
}

ANARIInstance HelideDevice::newInstance(const char * /*subtype*/)
{
  initDevice();
  return (ANARIInstance) new Instance(deviceState());
}

ANARILight HelideDevice::newLight(const char *subtype)
{
  initDevice();
  return (ANARILight)Light::createInstance(subtype, deviceState());
}

ANARIMaterial HelideDevice::newMaterial(const char *subtype)
{
  initDevice();
  return (ANARIMaterial)Material::createInstance(subtype, deviceState());
}

ANARIRenderer HelideDevice::newRenderer(const char *subtype)
{
  initDevice();
  return (ANARIRenderer)Renderer::createInstance(subtype, deviceState());
}

ANARISampler HelideDevice::newSampler(const char *subtype)
{
  initDevice();
  return (ANARISampler)Sampler::createInstance(subtype, deviceState());
}

ANARISpatialField HelideDevice::newSpatialField(const char *subtype)
{
  initDevice();
  return (ANARISpatialField)SpatialField::createInstance(
      subtype, deviceState());
}

ANARISurface HelideDevice::newSurface()
{
  initDevice();
  return (ANARISurface) new Surface(deviceState());
}

ANARIVolume HelideDevice::newVolume(const char *subtype)
{
  initDevice();
  return (ANARIVolume)Volume::createInstance(subtype, deviceState());
}

ANARIWorld HelideDevice::newWorld()
{
  initDevice();
  return (ANARIWorld) new World(deviceState());
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

  state.commitBuffer.clear();

  reportMessage(ANARI_SEVERITY_DEBUG, "destroying helide device (%p)", this);

  rtcReleaseDevice(state.embreeDevice);
}

void HelideDevice::initDevice()
{
  if (m_initialized)
    return;

  reportMessage(ANARI_SEVERITY_DEBUG, "initializing helide device (%p)", this);

  auto &state = *deviceState();

  int numThreads = 0;
  const char *numThreadsFromEnv = getenv("HELIDE_NUM_THREADS");
  if (numThreadsFromEnv)
    numThreads = std::atoi(numThreadsFromEnv);

  std::string config = "threads=" + std::to_string(numThreads);

  state.anariDevice = (anari::Device)this;
  state.embreeDevice = rtcNewDevice(config.c_str());

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

#define HELIDE_STRINGIFY(s) HELIDE_STRINGIFY2(s)
#define HELIDE_STRINGIFY2(s) #s
#define HELIDE_VERSION_STRING                                                  \
  "helide " HELIDE_STRINGIFY(ANARI_SDK_VERSION_MAJOR) "." HELIDE_STRINGIFY(    \
      ANARI_SDK_VERSION_MINOR) "." HELIDE_STRINGIFY(ANARI_SDK_VERSION_PATCH)

int HelideDevice::deviceGetProperty(const char *name,
    ANARIDataType type,
    void *mem,
    uint64_t size,
    uint32_t mask)
{
  static const std::string helide_version = HELIDE_VERSION_STRING;
  std::string_view prop = name;
  if (prop == "extension" && type == ANARI_STRING_LIST) {
    helium::writeToVoidP(mem, query_extensions());
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
  } else if (prop == "version.name.size" && type == ANARI_UINT64) {
    helium::writeToVoidP(mem, uint64_t(helide_version.size() + 1));
    return 1;
  } else if (prop == "version.name" && type == ANARI_STRING) {
    std::memset(mem, 0, size);
    std::memcpy(mem,
        helide_version.data(),
        std::min(uint64_t(helide_version.size()), size - 1));
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
