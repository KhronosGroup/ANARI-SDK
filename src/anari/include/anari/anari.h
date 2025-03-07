// Copyright 2021-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// This file was generated by generate_headers.py
// Don't make changes to this directly


#pragma once

#include <stdint.h>
#include <sys/types.h>

#ifndef NULL
#if __cplusplus >= 201103L
#define NULL nullptr
#else
#define NULL 0
#endif
#endif

#define ANARI_INVALID_HANDLE NULL

#include "frontend/anari_sdk_version.h"
#include "frontend/anari_enums.h"

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
#ifdef __cplusplus
namespace anari {
namespace api {
struct Library {};
struct Object {};
struct Device : public Object {};
struct Camera : public Object {};
struct Array : public Object {};
struct Array1D : public Array {};
struct Array2D : public Array {};
struct Array3D : public Array {};
struct Frame : public Object {};
struct Future : public Object {};
struct Geometry : public Object {};
struct Group : public Object {};
struct Instance : public Object {};
struct Light : public Object {};
struct Material : public Object {};
struct Sampler : public Object {};
struct Surface : public Object {};
struct Renderer : public Object {};
struct SpatialField : public Object {};
struct Volume : public Object {};
struct World : public Object {};
} // namespace api
} // namespace anari
typedef anari::api::Library *ANARILibrary;
typedef anari::api::Object *ANARIObject;
typedef anari::api::Device *ANARIDevice;
typedef anari::api::Camera *ANARICamera;
typedef anari::api::Array *ANARIArray;
typedef anari::api::Array1D *ANARIArray1D;
typedef anari::api::Array2D *ANARIArray2D;
typedef anari::api::Array3D *ANARIArray3D;
typedef anari::api::Frame *ANARIFrame;
typedef anari::api::Future *ANARIFuture;
typedef anari::api::Geometry *ANARIGeometry;
typedef anari::api::Group *ANARIGroup;
typedef anari::api::Instance *ANARIInstance;
typedef anari::api::Light *ANARILight;
typedef anari::api::Material *ANARIMaterial;
typedef anari::api::Sampler *ANARISampler;
typedef anari::api::Surface *ANARISurface;
typedef anari::api::Renderer *ANARIRenderer;
typedef anari::api::SpatialField *ANARISpatialField;
typedef anari::api::Volume *ANARIVolume;
typedef anari::api::World *ANARIWorld;
#else
typedef void* ANARILibrary;
typedef void* ANARIObject;
typedef void* ANARIDevice;
typedef void* ANARICamera;
typedef void* ANARIArray;
typedef void* ANARIArray1D;
typedef void* ANARIArray2D;
typedef void* ANARIArray3D;
typedef void* ANARIFrame;
typedef void* ANARIFuture;
typedef void* ANARIGeometry;
typedef void* ANARIGroup;
typedef void* ANARIInstance;
typedef void* ANARILight;
typedef void* ANARIMaterial;
typedef void* ANARISampler;
typedef void* ANARISurface;
typedef void* ANARIRenderer;
typedef void* ANARISpatialField;
typedef void* ANARIVolume;
typedef void* ANARIWorld;
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  const char* name;
  ANARIDataType type;
} ANARIParameter;

typedef void (*ANARIMemoryDeleter)(const void* userPtr, const void* appMemory);
typedef void (*ANARIStatusCallback)(const void* userPtr, ANARIDevice device, ANARIObject source, ANARIDataType sourceType, ANARIStatusSeverity severity, ANARIStatusCode code, const char* message);
typedef void (*ANARIFrameCompletionCallback)(const void* userPtr, ANARIDevice device, ANARIFrame frame);

ANARI_INTERFACE ANARILibrary anariLoadLibrary(const char* name, ANARIStatusCallback statusCallback ANARI_DEFAULT_VAL(0), const void* statusCallbackUserData ANARI_DEFAULT_VAL(0));
ANARI_INTERFACE void anariUnloadLibrary(ANARILibrary module);
ANARI_INTERFACE void anariLoadModule(ANARILibrary library, const char* name);
ANARI_INTERFACE void anariUnloadModule(ANARILibrary library, const char* name);
ANARI_INTERFACE ANARIDevice anariNewDevice(ANARILibrary library, const char* type ANARI_DEFAULT_VAL("default"));
ANARI_INTERFACE ANARIArray1D anariNewArray1D(ANARIDevice device, const void* appMemory, ANARIMemoryDeleter deleter, const void* userData, ANARIDataType dataType, uint64_t numElements1);
ANARI_INTERFACE ANARIArray2D anariNewArray2D(ANARIDevice device, const void* appMemory, ANARIMemoryDeleter deleter, const void* userData, ANARIDataType dataType, uint64_t numElements1, uint64_t numElements2);
ANARI_INTERFACE ANARIArray3D anariNewArray3D(ANARIDevice device, const void* appMemory, ANARIMemoryDeleter deleter, const void* userData, ANARIDataType dataType, uint64_t numElements1, uint64_t numElements2, uint64_t numElements3);
ANARI_INTERFACE void* anariMapArray(ANARIDevice device, ANARIArray array);
ANARI_INTERFACE void anariUnmapArray(ANARIDevice device, ANARIArray array);
ANARI_INTERFACE ANARILight anariNewLight(ANARIDevice device, const char* type);
ANARI_INTERFACE ANARICamera anariNewCamera(ANARIDevice device, const char* type);
ANARI_INTERFACE ANARIGeometry anariNewGeometry(ANARIDevice device, const char* type);
ANARI_INTERFACE ANARISpatialField anariNewSpatialField(ANARIDevice device, const char* type);
ANARI_INTERFACE ANARIVolume anariNewVolume(ANARIDevice device, const char* type);
ANARI_INTERFACE ANARISurface anariNewSurface(ANARIDevice device);
ANARI_INTERFACE ANARIMaterial anariNewMaterial(ANARIDevice device, const char* type);
ANARI_INTERFACE ANARISampler anariNewSampler(ANARIDevice device, const char* type);
ANARI_INTERFACE ANARIGroup anariNewGroup(ANARIDevice device);
ANARI_INTERFACE ANARIInstance anariNewInstance(ANARIDevice device, const char* type);
ANARI_INTERFACE ANARIWorld anariNewWorld(ANARIDevice device);
ANARI_INTERFACE ANARIObject anariNewObject(ANARIDevice device, const char* objectType, const char* type);
ANARI_INTERFACE void anariSetParameter(ANARIDevice device, ANARIObject object, const char* name, ANARIDataType dataType, const void* mem);
ANARI_INTERFACE void anariUnsetParameter(ANARIDevice device, ANARIObject object, const char* name);
ANARI_INTERFACE void anariUnsetAllParameters(ANARIDevice device, ANARIObject object);
ANARI_INTERFACE void* anariMapParameterArray1D(ANARIDevice device, ANARIObject object, const char* name, ANARIDataType dataType, uint64_t numElements1, uint64_t* elementStride);
ANARI_INTERFACE void* anariMapParameterArray2D(ANARIDevice device, ANARIObject object, const char* name, ANARIDataType dataType, uint64_t numElements1, uint64_t numElements2, uint64_t* elementStride);
ANARI_INTERFACE void* anariMapParameterArray3D(ANARIDevice device, ANARIObject object, const char* name, ANARIDataType dataType, uint64_t numElements1, uint64_t numElements2, uint64_t numElements3, uint64_t* elementStride);
ANARI_INTERFACE void anariUnmapParameterArray(ANARIDevice device, ANARIObject object, const char* name);
ANARI_INTERFACE void anariCommitParameters(ANARIDevice device, ANARIObject object);
ANARI_INTERFACE void anariRelease(ANARIDevice device, ANARIObject object);
ANARI_INTERFACE void anariRetain(ANARIDevice device, ANARIObject object);
ANARI_INTERFACE const char ** anariGetDeviceSubtypes(ANARILibrary library);
ANARI_INTERFACE const char ** anariGetDeviceExtensions(ANARILibrary library, const char* deviceSubtype);
ANARI_INTERFACE const char ** anariGetObjectSubtypes(ANARIDevice device, ANARIDataType objectType);
ANARI_INTERFACE const void* anariGetObjectInfo(ANARIDevice device, ANARIDataType objectType, const char* objectSubtype, const char* infoName, ANARIDataType infoType);
ANARI_INTERFACE const void* anariGetParameterInfo(ANARIDevice device, ANARIDataType objectType, const char* objectSubtype, const char* parameterName, ANARIDataType parameterType, const char* infoName, ANARIDataType infoType);
ANARI_INTERFACE int anariGetProperty(ANARIDevice device, ANARIObject object, const char* name, ANARIDataType type, void* mem, uint64_t size, ANARIWaitMask mask);
ANARI_INTERFACE ANARIFrame anariNewFrame(ANARIDevice device);
ANARI_INTERFACE const void* anariMapFrame(ANARIDevice device, ANARIFrame frame, const char* channel, uint32_t* width, uint32_t* height, ANARIDataType* pixelType);
ANARI_INTERFACE void anariUnmapFrame(ANARIDevice device, ANARIFrame frame, const char* channel);
ANARI_INTERFACE ANARIRenderer anariNewRenderer(ANARIDevice device, const char* type);
ANARI_INTERFACE void anariRenderFrame(ANARIDevice device, ANARIFrame frame);
ANARI_INTERFACE int anariFrameReady(ANARIDevice device, ANARIFrame frame, ANARIWaitMask mask);
ANARI_INTERFACE void anariDiscardFrame(ANARIDevice device, ANARIFrame frame);

#ifdef __cplusplus
} // extern "C"
#endif
