// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// anari
#include "anari/anari.h"
#include "anari/anari_cpp/Traits.h"
#include "anari/detail/Device.h"
#include "anari/detail/Library.h"
#include "anari/ext/anari_ext_interface.h"
// std
#include <cstdlib>
#include <limits>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>

#if ANARI_INTERCEPT_NULL_OBJECTS_AND_STRINGS
#define THROW_IF_NULL(obj, name)                                               \
  if (obj == nullptr)                                                          \
  throw std::runtime_error(std::string("null ") + name                         \
      + std::string(" provided to ") + __FUNCTION__)
#else
#define THROW_IF_NULL(obj, name)
#endif

// convenience macros for repeated use of the above
#define THROW_IF_NULL_OBJECT(obj) THROW_IF_NULL(obj, "handle")
#define THROW_IF_NULL_STRING(str) THROW_IF_NULL(str, "string")

#define ANARI_CATCH_BEGIN try {
#define ANARI_CATCH_END(a)                                                     \
  }                                                                            \
  catch (const std::bad_alloc &)                                               \
  {                                                                            \
    printf("ANARI BAD ALLOC\n");                                               \
    return a;                                                                  \
  }                                                                            \
  catch (const std::exception &e)                                              \
  {                                                                            \
    printf("ANARI ERROR: %s\n", e.what());                                     \
    return a;                                                                  \
  }                                                                            \
  catch (...)                                                                  \
  {                                                                            \
    printf("ANARI UNKOWN EXCEPTION\n");                                        \
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

using anari::Device;
using anari::Library;

///////////////////////////////////////////////////////////////////////////////
// Helper functions ///////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

static inline Library &libraryRef(ANARILibrary l)
{
  if (l == nullptr) {
    throw std::runtime_error("null library provided");
  }
  return *((Library *)l);
}

static inline Device &deviceRef(ANARIDevice d)
{
  if (d == nullptr) {
    throw std::runtime_error("null device provided");
  }
  return *((Device *)d);
}

///////////////////////////////////////////////////////////////////////////////
// Initialization /////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

extern "C" ANARILibrary anariLoadLibrary(const char *libraryName,
    ANARIStatusCallback statusCB,
    void *statusCBUserPtr) ANARI_CATCH_BEGIN
{
  THROW_IF_NULL_STRING(libraryName);

  if (std::string(libraryName) == "environment") {
    char *libraryFromEnv = getenv("ANARI_LIBRARY");
    if (!libraryFromEnv) {
      throw std::runtime_error(
          "'environment' library selected but ANARI_LIBRARY is not set");
    }

    libraryName = libraryFromEnv;
  }

  // Use a unique_ptr to cleanup if the Library constructor throws an exception
  auto l = make_unique<Library>(libraryName, statusCB, statusCBUserPtr);
  auto *ptr = l.get();
  l.release();
  return (ANARILibrary)ptr;
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
  THROW_IF_NULL_STRING(deviceType)

  auto &lib = libraryRef(l);

  if (std::string(deviceType) == "default")
    deviceType = lib.defaultDeviceName();

  return (ANARIDevice)Device::createDevice(
      deviceType, lib.defaultStatusCB(), lib.defaultStatusCBUserPtr());
}
ANARI_CATCH_END(nullptr)

extern "C" int anariDeviceImplements(
    ANARIDevice d, const char *ext) ANARI_CATCH_BEGIN
{
  return deviceRef(d).deviceImplements(ext);
}
ANARI_CATCH_END(0)

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
  THROW_IF_NULL_STRING(deviceSubtype);

  if (std::string(deviceSubtype) == "default")
    deviceSubtype = libraryRef(l).defaultDeviceName();

  return libraryRef(l).getObjectSubtypes(deviceSubtype, objectType);
}
ANARI_CATCH_END(nullptr)

extern "C" const ANARIParameter *anariGetObjectParameters(ANARILibrary l,
    const char *deviceSubtype,
    const char *objectSubtype,
    ANARIDataType objectType) ANARI_CATCH_BEGIN
{
  THROW_IF_NULL_STRING(deviceSubtype);
  THROW_IF_NULL_STRING(objectSubtype);

  if (std::string(deviceSubtype) == "default")
    deviceSubtype = libraryRef(l).defaultDeviceName();

  return libraryRef(l).getObjectParameters(
      deviceSubtype, objectSubtype, objectType);
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
  THROW_IF_NULL_STRING(deviceSubtype);
  THROW_IF_NULL_STRING(objectSubtype);
  THROW_IF_NULL_STRING(parameterName);
  THROW_IF_NULL_STRING(parameterType);

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
    void *appMemory,
    ANARIMemoryDeleter deleter,
    void *userdata,
    ANARIDataType type,
    uint64_t numItems1,
    uint64_t byteStride1) ANARI_CATCH_BEGIN
{
  return deviceRef(d).newArray1D(
      appMemory, deleter, userdata, type, numItems1, byteStride1);
}
ANARI_CATCH_END(nullptr)

extern "C" ANARIArray2D anariNewArray2D(ANARIDevice d,
    void *appMemory,
    ANARIMemoryDeleter deleter,
    void *userdata,
    ANARIDataType type,
    uint64_t numItems1,
    uint64_t numItems2,
    uint64_t byteStride1,
    uint64_t byteStride2) ANARI_CATCH_BEGIN
{
  return deviceRef(d).newArray2D(appMemory,
      deleter,
      userdata,
      type,
      numItems1,
      numItems2,
      byteStride1,
      byteStride2);
}
ANARI_CATCH_END(nullptr)

extern "C" ANARIArray3D anariNewArray3D(ANARIDevice d,
    void *appMemory,
    ANARIMemoryDeleter deleter,
    void *userdata,
    ANARIDataType type,
    uint64_t numItems1,
    uint64_t numItems2,
    uint64_t numItems3,
    uint64_t byteStride1,
    uint64_t byteStride2,
    uint64_t byteStride3) ANARI_CATCH_BEGIN
{
  return deviceRef(d).newArray3D(appMemory,
      deleter,
      userdata,
      type,
      numItems1,
      numItems2,
      numItems3,
      byteStride1,
      byteStride2,
      byteStride3);
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
  THROW_IF_NULL_STRING(type);
  return deviceRef(d).newLight(type);
}
ANARI_CATCH_END(nullptr)

extern "C" ANARICamera anariNewCamera(
    ANARIDevice d, const char *type) ANARI_CATCH_BEGIN
{
  THROW_IF_NULL_STRING(type);
  return deviceRef(d).newCamera(type);
}
ANARI_CATCH_END(nullptr)

extern "C" ANARIGeometry anariNewGeometry(
    ANARIDevice d, const char *type) ANARI_CATCH_BEGIN
{
  THROW_IF_NULL_STRING(type);
  return deviceRef(d).newGeometry(type);
}
ANARI_CATCH_END(nullptr)

extern "C" ANARISpatialField anariNewSpatialField(
    ANARIDevice d, const char *type) ANARI_CATCH_BEGIN
{
  THROW_IF_NULL_STRING(type);
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
  THROW_IF_NULL_STRING(material_type);
  return deviceRef(d).newMaterial(material_type);
}
ANARI_CATCH_END(nullptr)

extern "C" ANARISampler anariNewSampler(
    ANARIDevice d, const char *type) ANARI_CATCH_BEGIN
{
  THROW_IF_NULL_STRING(type);
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
  THROW_IF_NULL_OBJECT(object);
  THROW_IF_NULL_STRING(id);
  if (d == object)
    deviceRef(d).deviceSetParameter(id, type, mem);
  else
    deviceRef(d).setParameter(object, id, type, mem);
}
ANARI_CATCH_END_NORETURN()

extern "C" void anariUnsetParameter(
    ANARIDevice d, ANARIObject object, const char *id) ANARI_CATCH_BEGIN
{
  THROW_IF_NULL_OBJECT(object);
  THROW_IF_NULL_STRING(id);
  if (d == object)
    deviceRef(d).deviceUnsetParameter(id);
  else
    deviceRef(d).unsetParameter(object, id);
}
ANARI_CATCH_END_NORETURN()

extern "C" void anariCommit(ANARIDevice d, ANARIObject object) ANARI_CATCH_BEGIN
{
  THROW_IF_NULL_OBJECT(object);
  if (d == object)
    deviceRef(d).deviceCommit();
  else
    deviceRef(d).commit(object);
}
ANARI_CATCH_END_NORETURN()

extern "C" void anariRelease(
    ANARIDevice d, ANARIObject object) ANARI_CATCH_BEGIN
{
  if (d == object)
    deviceRef(d).deviceRelease();
  else
    deviceRef(d).release(object);
}
ANARI_CATCH_END_NORETURN()

extern "C" void anariRetain(ANARIDevice d, ANARIObject object) ANARI_CATCH_BEGIN
{
  THROW_IF_NULL_OBJECT(object);
  if (d == object)
    deviceRef(d).deviceRetain();
  else
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
  THROW_IF_NULL_OBJECT(obj);
  THROW_IF_NULL_STRING(name);
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

extern "C" const void *anariMapFrame(
    ANARIDevice d, ANARIFrame fb, const char *channel) ANARI_CATCH_BEGIN
{
  THROW_IF_NULL_OBJECT(fb);
  return deviceRef(d).frameBufferMap(fb, channel);
}
ANARI_CATCH_END(nullptr)

extern "C" void anariUnmapFrame(
    ANARIDevice d, ANARIFrame fb, const char *channel) ANARI_CATCH_BEGIN
{
  THROW_IF_NULL_OBJECT(fb);
  deviceRef(d).frameBufferUnmap(fb, channel);
}
ANARI_CATCH_END_NORETURN()

///////////////////////////////////////////////////////////////////////////////
// Frame Rendering ////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

extern "C" ANARIRenderer anariNewRenderer(
    ANARIDevice d, const char *type) ANARI_CATCH_BEGIN
{
  THROW_IF_NULL_STRING(type);
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

ANARI_INTERFACE ANARIObject anariNewObject(
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
