// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// anari
#include "anari/anari.h"
#include "anari/anari_cpp/Traits.h"
#include "anari/backend/DeviceImpl.h"
#include "anari/backend/LibraryImpl.h"
#include "anari/ext/anari_ext_interface.h"
// std
#include <cstdlib>
#include <limits>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>

#define ANARI_CATCH_BEGIN try {
#define ANARI_CATCH_END(a)                                                     \
  }                                                                            \
  catch (const std::exception &e)                                              \
  {                                                                            \
    fprintf(stderr,                                                            \
        "TERMINATING DUE TO UNCAUGHT ANARI EXCEPTION (std::exception): %s\n",  \
        e.what());                                                             \
    std::terminate();                                                          \
    return a;                                                                  \
  }                                                                            \
  catch (...)                                                                  \
  {                                                                            \
    fprintf(stderr,                                                            \
        "TERMINATING DUE TO UNCAUGHT ANARI EXCEPTION (unknown type)\n");       \
    std::terminate();                                                          \
    return a;                                                                  \
  }
#define ANARI_NORETURN /**/
#define ANARI_CATCH_END_NORETURN() ANARI_CATCH_END(ANARI_NORETURN)

namespace {
template <typename T, typename... Args>
static std::unique_ptr<T> make_unique(Args &&...args)
{
  return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}
} // namespace

using anari::DeviceImpl;
using anari::LibraryImpl;

///////////////////////////////////////////////////////////////////////////////
// Helper functions ///////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

static LibraryImpl &libraryRef(ANARILibrary l)
{
  return *((LibraryImpl *)l);
}

static DeviceImpl &deviceRef(ANARIDevice d)
{
  return *((DeviceImpl *)d);
}

static void invokeStatusCallback(ANARIStatusCallback cb,
    const void *userPtr,
    const char *message,
    ANARIDevice device = nullptr,
    ANARIObject source = nullptr,
    ANARIDataType sourceType = ANARI_UNKNOWN,
    ANARIStatusSeverity severity = ANARI_SEVERITY_FATAL_ERROR,
    ANARIStatusCode code = ANARI_STATUS_UNKNOWN_ERROR)
{
  if (cb)
    cb(userPtr, device, source, sourceType, severity, code, message);
}

///////////////////////////////////////////////////////////////////////////////
// Initialization /////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

extern "C" ANARILibrary anariLoadLibrary(const char *libraryName,
    ANARIStatusCallback statusCB,
    const void *statusCBUserPtr) ANARI_CATCH_BEGIN
{
  if (std::string(libraryName) == "environment") {
    char *libraryFromEnv = getenv("ANARI_LIBRARY");
    if (!libraryFromEnv) {
      invokeStatusCallback(statusCB,
          statusCBUserPtr,
          "'environment' library selected but ANARI_LIBRARY is not set");
    }

    libraryName = libraryFromEnv;
  }

  ANARILibrary retval = nullptr;

  // Use a unique_ptr to cleanup if the Library constructor throws an exception
  try {
    auto l = make_unique<LibraryImpl>(libraryName, statusCB, statusCBUserPtr);
    retval = (ANARILibrary)l.get();
    l.release();
  } catch (const std::exception &e) {
    std::string msg = "failed to load ANARILibrary '";
    msg += libraryName;
    msg += "'\nreason: ";
    msg += e.what();
    invokeStatusCallback(statusCB,
        statusCBUserPtr,
        msg.c_str(),
        nullptr,
        nullptr,
        ANARI_LIBRARY,
        ANARI_SEVERITY_ERROR,
        ANARI_STATUS_INVALID_OPERATION);
  }

  return retval;
}
ANARI_CATCH_END(nullptr)

extern "C" void anariUnloadLibrary(ANARILibrary l) ANARI_CATCH_BEGIN
{
  delete l;
}
ANARI_CATCH_END_NORETURN()

extern "C" void anariLoadModule(
    ANARILibrary l, const char *name) ANARI_CATCH_BEGIN
{
  libraryRef(l).loadModule(name);
}
ANARI_CATCH_END_NORETURN()

extern "C" void anariUnloadModule(
    ANARILibrary l, const char *name) ANARI_CATCH_BEGIN
{
  libraryRef(l).unloadModule(name);
}
ANARI_CATCH_END_NORETURN()

extern "C" ANARIDevice anariNewDevice(
    ANARILibrary l, const char *deviceType) ANARI_CATCH_BEGIN
{
  return libraryRef(l).newDevice(deviceType);
}
ANARI_CATCH_END(nullptr)

///////////////////////////////////////////////////////////////////////////////
// Object Introspection ///////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

extern "C" const char **anariGetDeviceSubtypes(ANARILibrary l) ANARI_CATCH_BEGIN
{
  return libraryRef(l).getDeviceSubtypes();
}
ANARI_CATCH_END(nullptr)

extern "C" const char **anariGetObjectSubtypes(ANARILibrary l,
    const char *deviceSubtype,
    ANARIDataType objectType) ANARI_CATCH_BEGIN
{
  if (std::string(deviceSubtype) == "default")
    deviceSubtype = libraryRef(l).defaultDeviceName();
  return libraryRef(l).getObjectSubtypes(deviceSubtype, objectType);
}
ANARI_CATCH_END(nullptr)

extern "C" const void *anariGetObjectInfo(ANARILibrary l,
    const char *deviceSubtype,
    const char *objectSubtype,
    ANARIDataType objectType,
    const char *infoName,
    ANARIDataType infoType) ANARI_CATCH_BEGIN
{
  if (std::string(deviceSubtype) == "default")
    deviceSubtype = libraryRef(l).defaultDeviceName();

  return libraryRef(l).getObjectProperty(
      deviceSubtype, objectSubtype, objectType, infoName, infoType);
}
ANARI_CATCH_END(nullptr)

extern "C" const void *anariGetParameterInfo(ANARILibrary l,
    const char *deviceSubtype,
    const char *objectSubtype,
    ANARIDataType objectType,
    const char *parameterName,
    ANARIDataType parameterType,
    const char *infoName,
    ANARIDataType infoType) ANARI_CATCH_BEGIN
{
  if (std::string(deviceSubtype) == "default")
    deviceSubtype = libraryRef(l).defaultDeviceName();

  return libraryRef(l).getParameterProperty(deviceSubtype,
      objectSubtype,
      objectType,
      parameterName,
      parameterType,
      infoName,
      infoType);
}
ANARI_CATCH_END(nullptr)

///////////////////////////////////////////////////////////////////////////////
// ANARI Data Arrays //////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

extern "C" ANARIArray1D anariNewArray1D(ANARIDevice d,
    const void *appMemory,
    ANARIMemoryDeleter deleter,
    const void *userdata,
    ANARIDataType type,
    uint64_t numItems1) ANARI_CATCH_BEGIN
{
  return deviceRef(d).newArray1D(
      appMemory, deleter, userdata, type, numItems1);
}
ANARI_CATCH_END(nullptr)

extern "C" ANARIArray2D anariNewArray2D(ANARIDevice d,
    const void *appMemory,
    ANARIMemoryDeleter deleter,
    const void *userdata,
    ANARIDataType type,
    uint64_t numItems1,
    uint64_t numItems2) ANARI_CATCH_BEGIN
{
  return deviceRef(d).newArray2D(appMemory,
      deleter,
      userdata,
      type,
      numItems1,
      numItems2);
}
ANARI_CATCH_END(nullptr)

extern "C" ANARIArray3D anariNewArray3D(ANARIDevice d,
    const void *appMemory,
    ANARIMemoryDeleter deleter,
    const void *userdata,
    ANARIDataType type,
    uint64_t numItems1,
    uint64_t numItems2,
    uint64_t numItems3) ANARI_CATCH_BEGIN
{
  return deviceRef(d).newArray3D(appMemory,
      deleter,
      userdata,
      type,
      numItems1,
      numItems2,
      numItems3);
}
ANARI_CATCH_END(nullptr)

extern "C" void *anariMapArray(ANARIDevice d, ANARIArray a) ANARI_CATCH_BEGIN
{
  return deviceRef(d).mapArray(a);
}
ANARI_CATCH_END(nullptr)

extern "C" void anariUnmapArray(ANARIDevice d, ANARIArray a) ANARI_CATCH_BEGIN
{
  deviceRef(d).unmapArray(a);
}
ANARI_CATCH_END_NORETURN()

///////////////////////////////////////////////////////////////////////////////
// Renderable Objects /////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

extern "C" ANARILight anariNewLight(
    ANARIDevice d, const char *type) ANARI_CATCH_BEGIN
{
  return deviceRef(d).newLight(type);
}
ANARI_CATCH_END(nullptr)

extern "C" ANARICamera anariNewCamera(
    ANARIDevice d, const char *type) ANARI_CATCH_BEGIN
{
  return deviceRef(d).newCamera(type);
}
ANARI_CATCH_END(nullptr)

extern "C" ANARIGeometry anariNewGeometry(
    ANARIDevice d, const char *type) ANARI_CATCH_BEGIN
{
  return deviceRef(d).newGeometry(type);
}
ANARI_CATCH_END(nullptr)

extern "C" ANARISpatialField anariNewSpatialField(
    ANARIDevice d, const char *type) ANARI_CATCH_BEGIN
{
  return deviceRef(d).newSpatialField(type);
}
ANARI_CATCH_END(nullptr)

extern "C" ANARISurface anariNewSurface(ANARIDevice d) ANARI_CATCH_BEGIN
{
  return deviceRef(d).newSurface();
}
ANARI_CATCH_END(nullptr)

extern "C" ANARIVolume anariNewVolume(
    ANARIDevice d, const char *type) ANARI_CATCH_BEGIN
{
  return deviceRef(d).newVolume(type);
}
ANARI_CATCH_END(nullptr)

///////////////////////////////////////////////////////////////////////////////
// Surface Meta-Data //////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

extern "C" ANARIMaterial anariNewMaterial(
    ANARIDevice d, const char *material_type) ANARI_CATCH_BEGIN
{
  return deviceRef(d).newMaterial(material_type);
}
ANARI_CATCH_END(nullptr)

extern "C" ANARISampler anariNewSampler(
    ANARIDevice d, const char *type) ANARI_CATCH_BEGIN
{
  return deviceRef(d).newSampler(type);
}
ANARI_CATCH_END(nullptr)

///////////////////////////////////////////////////////////////////////////////
// Instancing /////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

extern "C" ANARIGroup anariNewGroup(ANARIDevice d) ANARI_CATCH_BEGIN
{
  return deviceRef(d).newGroup();
}
ANARI_CATCH_END(nullptr)

extern "C" ANARIInstance anariNewInstance(ANARIDevice d) ANARI_CATCH_BEGIN
{
  return deviceRef(d).newInstance();
}
ANARI_CATCH_END(nullptr)

///////////////////////////////////////////////////////////////////////////////
// Top-level Worlds ///////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

extern "C" ANARIWorld anariNewWorld(ANARIDevice d) ANARI_CATCH_BEGIN
{
  return deviceRef(d).newWorld();
}
ANARI_CATCH_END(nullptr)

///////////////////////////////////////////////////////////////////////////////
// Object + Parameter Lifetime Management /////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

extern "C" void anariSetParameter(ANARIDevice d,
    ANARIObject object,
    const char *id,
    ANARIDataType type,
    const void *mem) ANARI_CATCH_BEGIN
{
  deviceRef(d).setParameter(object, id, type, mem);
}
ANARI_CATCH_END_NORETURN()

extern "C" void anariUnsetParameter(
    ANARIDevice d, ANARIObject object, const char *id) ANARI_CATCH_BEGIN
{
  deviceRef(d).unsetParameter(object, id);
}
ANARI_CATCH_END_NORETURN()

extern "C" void* anariMapParameterArray1D(ANARIDevice d,
    ANARIObject object,
    const char* name,
    ANARIDataType dataType,
    uint64_t numElements1,
  uint64_t *elementStride) ANARI_CATCH_BEGIN
{
  return deviceRef(d).mapParameterArray1D(object,
    name,
    dataType,
    numElements1,
    elementStride);
}
ANARI_CATCH_END(nullptr)

extern "C" void* anariMapParameterArray2D(ANARIDevice d,
  ANARIObject object,
  const char* name,
  ANARIDataType dataType,
  uint64_t numElements1,
  uint64_t numElements2,
  uint64_t *elementStride) ANARI_CATCH_BEGIN
{
  return deviceRef(d).mapParameterArray2D(object,
      name,
      dataType,
      numElements1,
      numElements2,
      elementStride);
}
ANARI_CATCH_END(nullptr)

extern "C" void* anariMapParameterArray3D(ANARIDevice d,
  ANARIObject object,
  const char* name,
  ANARIDataType dataType,
  uint64_t numElements1,
  uint64_t numElements2,
  uint64_t numElements3,
  uint64_t *elementStride) ANARI_CATCH_BEGIN
{
  return deviceRef(d).mapParameterArray3D(object,
      name,
      dataType,
      numElements1,
      numElements2,
      numElements3,
      elementStride);
}
ANARI_CATCH_END(nullptr)

extern "C" void anariUnmapParameterArray(ANARIDevice d,
  ANARIObject object,
  const char* name) ANARI_CATCH_BEGIN
{
  deviceRef(d).unmapParameterArray(object, name);  
}
ANARI_CATCH_END_NORETURN()

extern "C" void anariCommitParameters(
    ANARIDevice d, ANARIObject object) ANARI_CATCH_BEGIN
{
  deviceRef(d).commitParameters(object);
}
ANARI_CATCH_END_NORETURN()

extern "C" void anariRelease(
    ANARIDevice d, ANARIObject object) ANARI_CATCH_BEGIN
{
  deviceRef(d).release(object);
}
ANARI_CATCH_END_NORETURN()

extern "C" void anariRetain(ANARIDevice d, ANARIObject object) ANARI_CATCH_BEGIN
{
  deviceRef(d).retain(object);
}
ANARI_CATCH_END_NORETURN()

///////////////////////////////////////////////////////////////////////////////
// Object Query Interface /////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

extern "C" int anariGetProperty(ANARIDevice d,
    ANARIObject obj,
    const char *name,
    ANARIDataType type,
    void *mem,
    uint64_t size,
    ANARIWaitMask mask) ANARI_CATCH_BEGIN
{
  return deviceRef(d).getProperty(obj, name, type, mem, size, mask);
}
ANARI_CATCH_END(0)

///////////////////////////////////////////////////////////////////////////////
// FrameBuffer Manipulation ///////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

extern "C" ANARIFrame anariNewFrame(ANARIDevice d) ANARI_CATCH_BEGIN
{
  return deviceRef(d).newFrame();
}
ANARI_CATCH_END(nullptr)

extern "C" const void *anariMapFrame(ANARIDevice d,
    ANARIFrame fb,
    const char *channel,
    uint32_t *width,
    uint32_t *height,
    ANARIDataType *pixelType) ANARI_CATCH_BEGIN
{
  return deviceRef(d).frameBufferMap(fb, channel, width, height, pixelType);
}
ANARI_CATCH_END(nullptr)

extern "C" void anariUnmapFrame(
    ANARIDevice d, ANARIFrame fb, const char *channel) ANARI_CATCH_BEGIN
{
  deviceRef(d).frameBufferUnmap(fb, channel);
}
ANARI_CATCH_END_NORETURN()

///////////////////////////////////////////////////////////////////////////////
// Frame Rendering ////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

extern "C" ANARIRenderer anariNewRenderer(
    ANARIDevice d, const char *type) ANARI_CATCH_BEGIN
{
  return deviceRef(d).newRenderer(type);
}
ANARI_CATCH_END(nullptr)

extern "C" void anariRenderFrame(ANARIDevice d, ANARIFrame f) ANARI_CATCH_BEGIN
{
  deviceRef(d).renderFrame(f);
}
ANARI_CATCH_END()

extern "C" int anariFrameReady(
    ANARIDevice d, ANARIFrame f, ANARIWaitMask mask) ANARI_CATCH_BEGIN
{
  return deviceRef(d).frameReady(f, mask);
}
ANARI_CATCH_END(1)

extern "C" void anariDiscardFrame(ANARIDevice d, ANARIFrame f) ANARI_CATCH_BEGIN
{
  return deviceRef(d).discardFrame(f);
}
ANARI_CATCH_END_NORETURN()

ANARIObject anariNewObject(
    ANARIDevice d, const char *objectType, const char *type) ANARI_CATCH_BEGIN
{
  return deviceRef(d).newObject(objectType, type);
}
ANARI_CATCH_END(nullptr)

extern "C" void (*anariDeviceGetProcAddress(ANARIDevice d, const char *name))(
    void)
{
  ANARI_CATCH_BEGIN
  {
    return deviceRef(d).getProcAddress(name);
  }
  ANARI_CATCH_END(nullptr)
}

namespace anari {

ANARI_TYPEFOR_DEFINITION(void *);
ANARI_TYPEFOR_DEFINITION_STRING(char *);
ANARI_TYPEFOR_DEFINITION_STRING(char[]);
ANARI_TYPEFOR_DEFINITION_STRING(const char *);
ANARI_TYPEFOR_DEFINITION_STRING(const char[]);

ANARI_TYPEFOR_DEFINITION(bool);

ANARI_TYPEFOR_DEFINITION(int8_t);
ANARI_TYPEFOR_DEFINITION(int8_t[2]);
ANARI_TYPEFOR_DEFINITION(int8_t[3]);
ANARI_TYPEFOR_DEFINITION(int8_t[4]);

ANARI_TYPEFOR_DEFINITION(uint8_t);
ANARI_TYPEFOR_DEFINITION(uint8_t[2]);
ANARI_TYPEFOR_DEFINITION(uint8_t[3]);
ANARI_TYPEFOR_DEFINITION(uint8_t[4]);

ANARI_TYPEFOR_DEFINITION(int16_t);
ANARI_TYPEFOR_DEFINITION(int16_t[2]);
ANARI_TYPEFOR_DEFINITION(int16_t[3]);
ANARI_TYPEFOR_DEFINITION(int16_t[4]);

ANARI_TYPEFOR_DEFINITION(uint16_t);
ANARI_TYPEFOR_DEFINITION(uint16_t[2]);
ANARI_TYPEFOR_DEFINITION(uint16_t[3]);
ANARI_TYPEFOR_DEFINITION(uint16_t[4]);

ANARI_TYPEFOR_DEFINITION(int32_t);
ANARI_TYPEFOR_DEFINITION(int32_t[2]);
ANARI_TYPEFOR_DEFINITION(int32_t[3]);
ANARI_TYPEFOR_DEFINITION(int32_t[4]);

ANARI_TYPEFOR_DEFINITION(uint32_t);
ANARI_TYPEFOR_DEFINITION(uint32_t[2]);
ANARI_TYPEFOR_DEFINITION(uint32_t[3]);
ANARI_TYPEFOR_DEFINITION(uint32_t[4]);

ANARI_TYPEFOR_DEFINITION(int64_t);
ANARI_TYPEFOR_DEFINITION(int64_t[2]);
ANARI_TYPEFOR_DEFINITION(int64_t[3]);
ANARI_TYPEFOR_DEFINITION(int64_t[4]);

ANARI_TYPEFOR_DEFINITION(uint64_t);
ANARI_TYPEFOR_DEFINITION(uint64_t[2]);
ANARI_TYPEFOR_DEFINITION(uint64_t[3]);
ANARI_TYPEFOR_DEFINITION(uint64_t[4]);

ANARI_TYPEFOR_DEFINITION(float);
ANARI_TYPEFOR_DEFINITION(float[2]);
ANARI_TYPEFOR_DEFINITION(float[3]);
ANARI_TYPEFOR_DEFINITION(float[4]);
ANARI_TYPEFOR_DEFINITION(float[12]);

ANARI_TYPEFOR_DEFINITION(double);
ANARI_TYPEFOR_DEFINITION(double[2]);
ANARI_TYPEFOR_DEFINITION(double[3]);
ANARI_TYPEFOR_DEFINITION(double[4]);

ANARI_TYPEFOR_DEFINITION(ANARILibrary);
ANARI_TYPEFOR_DEFINITION(ANARIObject);
ANARI_TYPEFOR_DEFINITION(ANARICamera);
ANARI_TYPEFOR_DEFINITION(ANARIArray);
ANARI_TYPEFOR_DEFINITION(ANARIArray1D);
ANARI_TYPEFOR_DEFINITION(ANARIArray2D);
ANARI_TYPEFOR_DEFINITION(ANARIArray3D);
ANARI_TYPEFOR_DEFINITION(ANARIFrame);
ANARI_TYPEFOR_DEFINITION(ANARIGeometry);
ANARI_TYPEFOR_DEFINITION(ANARIGroup);
ANARI_TYPEFOR_DEFINITION(ANARIInstance);
ANARI_TYPEFOR_DEFINITION(ANARILight);
ANARI_TYPEFOR_DEFINITION(ANARIMaterial);
ANARI_TYPEFOR_DEFINITION(ANARIRenderer);
ANARI_TYPEFOR_DEFINITION(ANARISampler);
ANARI_TYPEFOR_DEFINITION(ANARISurface);
ANARI_TYPEFOR_DEFINITION(ANARISpatialField);
ANARI_TYPEFOR_DEFINITION(ANARIVolume);
ANARI_TYPEFOR_DEFINITION(ANARIWorld);

ANARI_TYPEFOR_DEFINITION(ANARIStatusCallback);
ANARI_TYPEFOR_DEFINITION(ANARIMemoryDeleter);
ANARI_TYPEFOR_DEFINITION(ANARIFrameCompletionCallback);

ANARI_TYPEFOR_DEFINITION(ANARIDataType);

} // namespace anari
