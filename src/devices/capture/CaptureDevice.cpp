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

#define CAPTURE_NEW_OBJECT(Function, Handle, Type)                             \
  Handle CaptureDevice::Function(const char *subtype)                          \
  {                                                                            \
    return (Handle) new Object(Type, subtype, deviceState());                  \
  }

CAPTURE_NEW_OBJECT(newCamera, ANARICamera, ANARI_CAMERA)
CAPTURE_NEW_OBJECT(newGeometry, ANARIGeometry, ANARI_GEOMETRY)
CAPTURE_NEW_OBJECT(newInstance, ANARIInstance, ANARI_INSTANCE)
CAPTURE_NEW_OBJECT(newLight, ANARILight, ANARI_LIGHT)
CAPTURE_NEW_OBJECT(newMaterial, ANARIMaterial, ANARI_MATERIAL)
CAPTURE_NEW_OBJECT(newRenderer, ANARIRenderer, ANARI_RENDERER)
CAPTURE_NEW_OBJECT(newSampler, ANARISampler, ANARI_SAMPLER)
CAPTURE_NEW_OBJECT(newSpatialField, ANARISpatialField, ANARI_SPATIAL_FIELD)
CAPTURE_NEW_OBJECT(newVolume, ANARIVolume, ANARI_VOLUME)

#undef CAPTURE_NEW_OBJECT

ANARIFrame CaptureDevice::newFrame()
{
  return (ANARIFrame) new Frame(deviceState());
}

ANARIGroup CaptureDevice::newGroup()
{
  return (ANARIGroup) new Object(ANARI_GROUP, nullptr, deviceState());
}

ANARISurface CaptureDevice::newSurface()
{
  return (ANARISurface) new Object(ANARI_SURFACE, nullptr, deviceState());
}

ANARIWorld CaptureDevice::newWorld()
{
  return (ANARIWorld) new Object(ANARI_WORLD, nullptr, deviceState());
}

// Query functions ////////////////////////////////////////////////////////////

const char **CaptureDevice::getObjectSubtypes(ANARIDataType objectType)
{
  return query_object_types(objectType);
}

const void *CaptureDevice::getObjectInfo(ANARIDataType objectType,
    const char *objectSubtype,
    const char *infoName,
    ANARIDataType infoType)
{
  return query_object_info(objectType, objectSubtype, infoName, infoType);
}

const void *CaptureDevice::getParameterInfo(ANARIDataType objectType,
    const char *objectSubtype,
    const char *parameterName,
    ANARIDataType parameterType,
    const char *infoName,
    ANARIDataType infoType)
{
  return query_param_info(objectType,
      objectSubtype,
      parameterName,
      parameterType,
      infoName,
      infoType);
}

// Other CaptureDevice definitions ////////////////////////////////////////////

CaptureState::CaptureState(ANARIDevice d) : helium::BaseGlobalDeviceState(d) {}

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
