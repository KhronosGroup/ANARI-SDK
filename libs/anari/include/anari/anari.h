// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <sys/types.h>

#ifndef NULL
#if __cplusplus >= 201103L
#define NULL nullptr
#else
#define NULL 0
#endif
#endif

#define ANARI_INVALID_HANDLE NULL

#include "anari_enums.h"

#ifdef _WIN32
#ifdef ANARI_STATIC_DEFINE
#define ANARI_INTERFACE
#else
#ifdef anari_EXPORTS
#define ANARI_INTERFACE __declspec(dllexport)
#else
#define ANARI_INTERFACE __declspec(dllimport)
#endif
#endif
#elif defined __GNUC__
#define ANARI_INTERFACE __attribute__((__visibility__("default")))
#else
#define ANARI_INTERFACE
#endif

#ifdef __GNUC__
#define ANARI_DEPRECATED __attribute__((deprecated))
#elif defined(_MSC_VER)
#define ANARI_DEPRECATED __declspec(deprecated)
#else
#define ANARI_DEPRECATED
#endif

#ifdef __cplusplus
// C++ DOES support default initializers
#define ANARI_DEFAULT_VAL(a) = a
#else
/* C99 does NOT support default initializers, so we use this macro
   to define them away */
#define ANARI_DEFAULT_VAL(a)
#endif

// clang-format off

// Give ANARI handle types a concrete defintion to enable C++ type checking
#ifdef __cplusplus
namespace anari {
namespace api {

struct Object {};
struct Library      : public Object {};
struct Device       : public Object {};
struct Camera       : public Object {};
struct Data         : public Object {};
struct Array        : public Object {};
struct Array1D      : public Array  {};
struct Array2D      : public Array  {};
struct Array3D      : public Array  {};
struct Frame        : public Object {};
struct Geometry     : public Object {};
struct Group        : public Object {};
struct Instance     : public Object {};
struct Light        : public Object {};
struct Material     : public Object {};
struct Renderer     : public Object {};
struct Surface      : public Object {};
struct Sampler      : public Object {};
struct SpatialField : public Object {};
struct Volume       : public Object {};
struct World        : public Object {};

} // namespace api
} // namespace anari

// clang-format on

typedef anari::api::Object *ANARIObject;
typedef anari::api::Library *ANARILibrary;
typedef anari::api::Device *ANARIDevice;
typedef anari::api::Camera *ANARICamera;
typedef anari::api::Data *ANARIData;
typedef anari::api::Array *ANARIArray;
typedef anari::api::Array1D *ANARIArray1D;
typedef anari::api::Array2D *ANARIArray2D;
typedef anari::api::Array3D *ANARIArray3D;
typedef anari::api::Frame *ANARIFrame;
typedef anari::api::Geometry *ANARIGeometry;
typedef anari::api::Group *ANARIGroup;
typedef anari::api::Instance *ANARIInstance;
typedef anari::api::Light *ANARILight;
typedef anari::api::Material *ANARIMaterial;
typedef anari::api::Renderer *ANARIRenderer;
typedef anari::api::Sampler *ANARISampler;
typedef anari::api::Surface *ANARISurface;
typedef anari::api::SpatialField *ANARISpatialField;
typedef anari::api::Volume *ANARIVolume;
typedef anari::api::World *ANARIWorld;

#else
typedef void _ANARIObject;
/* Abstract object types. in C99, those are all the same because C99
   doesn't know inheritance, and we want to make sure that a
   ANARIGeometry can still be passed to a function that expects a
   ANARIObject, etc. */
typedef _ANARIObject *ANARIObject, *ANARILibrary, *ANARIDevice, *ANARICamera,
    *ANARIData, *ANARIArray, *ANARIArray1D, *ANARIArray2D, *ANARIArray3D,
    *ANARIFrame, *ANARIGeometry, *ANARIGroup, *ANARIGroup, *ANARIInstance,
    *ANARILight, *ANARIMaterial, *ANARIObject, *ANARIRenderer, *ANARISampler,
    *ANARISurface, *ANARISpatialField, *ANARIVolume, *ANARIWorld;
#endif

#ifdef __cplusplus
extern "C" {
#endif

// ANARI Initialization ///////////////////////////////////////////////////////

// Status message callback function type
typedef void (*ANARIStatusCallback)(void *userPtr,
    ANARIDevice,
    ANARIObject source,
    ANARIDataType sourceType,
    ANARIStatusSeverity,
    ANARIStatusCode,
    const char *message);

/* Load library 'name' from shared lib libanari_library_<name>.so
   errors are reported by invoking the passed ANARIStatusCallback if it is
   not NULL */
ANARI_INTERFACE ANARILibrary anariLoadLibrary(const char *name,
    ANARIStatusCallback defaultStatusCB ANARI_DEFAULT_VAL(NULL),
    void *defaultStatusCBUserPtr ANARI_DEFAULT_VAL(NULL));
ANARI_INTERFACE void anariUnloadLibrary(ANARILibrary);

// Load a vendor-specific module for runtime loaded extensions
ANARI_INTERFACE void anariLoadModule(ANARILibrary, const char *name);
ANARI_INTERFACE void anariUnloadModule(ANARILibrary, const char *name);

// Create an ANARI engine backend using explicit device string.
ANARI_INTERFACE ANARIDevice anariNewDevice(
    ANARILibrary, const char *deviceType ANARI_DEFAULT_VAL("default"));

// ANARI Data Arrays //////////////////////////////////////////////////////////

typedef void (*ANARIMemoryDeleter)(void *userPtr, void *appMemory);

ANARI_INTERFACE ANARIArray1D anariNewArray1D(ANARIDevice,
    void *appMemory,
    ANARIMemoryDeleter,
    void *userPtr,
    ANARIDataType,
    uint64_t numItems1,
    uint64_t byteStride1 ANARI_DEFAULT_VAL(0));

ANARI_INTERFACE ANARIArray2D anariNewArray2D(ANARIDevice,
    void *appMemory,
    ANARIMemoryDeleter,
    void *userPtr,
    ANARIDataType,
    uint64_t numItems1,
    uint64_t numItems2,
    uint64_t byteStride1 ANARI_DEFAULT_VAL(0),
    uint64_t byteStride2 ANARI_DEFAULT_VAL(0));

ANARI_INTERFACE ANARIArray3D anariNewArray3D(ANARIDevice,
    void *appMemory,
    ANARIMemoryDeleter,
    void *userPtr,
    ANARIDataType,
    uint64_t numItems1,
    uint64_t numItems2,
    uint64_t numItems3,
    uint64_t byteStride1 ANARI_DEFAULT_VAL(0),
    uint64_t byteStride2 ANARI_DEFAULT_VAL(0),
    uint64_t byteStride3 ANARI_DEFAULT_VAL(0));

ANARI_INTERFACE void *anariMapArray(ANARIDevice, ANARIArray);
ANARI_INTERFACE void anariUnmapArray(ANARIDevice, ANARIArray);

ANARI_INTERFACE void anariArrayRangeUpdated1D(
    ANARIDevice, ANARIArray1D, uint64_t startIndex1, uint64_t elementCount1);

ANARI_INTERFACE void anariArrayRangeUpdated2D(ANARIDevice,
    ANARIArray2D,
    uint64_t startIndex1,
    uint64_t startIndex2,
    uint64_t elementCount1,
    uint64_t elementCount2);

ANARI_INTERFACE void anariArrayRangeUpdated3D(ANARIDevice,
    ANARIArray3D,
    uint64_t startIndex1,
    uint64_t startIndex2,
    uint64_t startIndex3,
    uint64_t elementCount1,
    uint64_t elementCount2,
    uint64_t elementCount3);

// Renderable Objects /////////////////////////////////////////////////////////

ANARI_INTERFACE ANARILight anariNewLight(ANARIDevice, const char *type);

ANARI_INTERFACE ANARICamera anariNewCamera(ANARIDevice, const char *type);

ANARI_INTERFACE ANARIGeometry anariNewGeometry(ANARIDevice, const char *type);
ANARI_INTERFACE ANARISpatialField anariNewSpatialField(ANARIDevice, const char *type);

ANARI_INTERFACE ANARISurface anariNewSurface(ANARIDevice);
ANARI_INTERFACE ANARIVolume anariNewVolume(ANARIDevice, const char *type);

// Surface Meta-Data //////////////////////////////////////////////////////////

ANARI_INTERFACE ANARIMaterial anariNewMaterial(
    ANARIDevice, const char *materialType);

ANARI_INTERFACE ANARISampler anariNewSampler(ANARIDevice, const char *type);

// Instancing /////////////////////////////////////////////////////////////////

ANARI_INTERFACE ANARIGroup anariNewGroup(ANARIDevice);

ANARI_INTERFACE ANARIInstance anariNewInstance(ANARIDevice);

// Top-level Worlds ///////////////////////////////////////////////////////////

ANARI_INTERFACE ANARIWorld anariNewWorld(ANARIDevice);

// Extension Objects //////////////////////////////////////////////////////////

ANARI_INTERFACE ANARIObject anariNewObject(ANARIDevice, const char *objectType, const char *type);

// Object + Parameter Lifetime Management /////////////////////////////////////

// Set a parameter, where 'mem' points the address of the type specified
ANARI_INTERFACE void anariSetParameter(
    ANARIDevice, ANARIObject, const char *id, ANARIDataType, const void *mem);

// Return a parameter back to a state as-if it hasn't been set (after commit)
ANARI_INTERFACE void anariUnsetParameter(ANARIDevice, ANARIObject, const char *id);

// Make parameters which have been set visible to the object
ANARI_INTERFACE void anariCommit(ANARIDevice, ANARIObject);

// Reduce the application-side object ref count by 1
ANARI_INTERFACE void anariRelease(ANARIDevice, ANARIObject);
// Increace the application-side object ref count by 1
ANARI_INTERFACE void anariRetain(ANARIDevice, ANARIObject);

// Object Introspection ///////////////////////////////////////////////////////

ANARI_INTERFACE const char **anariGetDeviceSubtypes(ANARILibrary);

ANARI_INTERFACE const char **anariGetObjectSubtypes(
    ANARILibrary, const char *deviceSubtype, ANARIDataType objectType);

typedef struct
{
  const char *name;
  ANARIDataType type;
} ANARIParameter;

ANARI_INTERFACE const ANARIParameter *anariGetObjectParameters(ANARILibrary,
    const char *deviceSubtype,
    const char *objectSubtype,
    ANARIDataType objectType);

ANARI_INTERFACE const void *anariGetParameterInfo(ANARILibrary,
    const char *deviceSubtype,
    const char *objectSubtype,
    ANARIDataType objectType,
    const char *parameterName,
    ANARIDataType parameterType,
    const char *infoName,
    ANARIDataType infoType);

// Object Query Interface /////////////////////////////////////////////////////

ANARI_INTERFACE int anariGetProperty(ANARIDevice,
    ANARIObject,
    const char *propertyName,
    ANARIDataType propertyType,
    void *memory,
    uint64_t size,
    ANARIWaitMask);

// Frame Manipulation /////////////////////////////////////////////////////////

ANARI_INTERFACE ANARIFrame anariNewFrame(ANARIDevice);

// Pointer access (read-only) to the memory of the given frame buffer channel
ANARI_INTERFACE const void *anariMapFrame(
    ANARIDevice, ANARIFrame, const char *channel);

// Unmap a previously mapped frame buffer pointer
ANARI_INTERFACE void anariUnmapFrame(
    ANARIDevice, ANARIFrame, const char *channel);

// Frame Rendering ////////////////////////////////////////////////////////////

ANARI_INTERFACE ANARIRenderer anariNewRenderer(ANARIDevice, const char *type);

// Render a frame (non-blocking)
ANARI_INTERFACE void anariRenderFrame(ANARIDevice, ANARIFrame);

// Ask if the frame is ready (ANARI_NO_WAIT) or wait until ready (ANARI_WAIT)
ANARI_INTERFACE int anariFrameReady(ANARIDevice, ANARIFrame, ANARIWaitMask);

// Signal the running frame should be canceled if possible
ANARI_INTERFACE void anariDiscardFrame(ANARIDevice, ANARIFrame);

// Extensions /////////////////////////////////////////////////////////////////

// Query whether a device supports an extension
ANARI_INTERFACE int anariDeviceImplements(ANARIDevice, const char *extension);

// Frame Continuation core extension //
// ANARI_KHR_FRAME_COMPLETION_CALLBACK
//
typedef void (*ANARIFrameCompletionCallback)(void *userPtr, ANARIDevice, ANARIFrame);

#ifdef __cplusplus
} // extern "C"
#endif
