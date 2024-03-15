// Copyright 2021-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// anari
#include "anari/anari.h"
#include "anari/frontend/anari_extension_utility.h"
#include "anari/frontend/type_utility.h"
// std
#include <string>

namespace anari {

///////////////////////////////////////////////////////////////////////////////
// Types //////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

// clang-format off

// Handle Types //

using Library      = ANARILibrary;
using Object       = ANARIObject;
using Device       = ANARIDevice;
using Camera       = ANARICamera;
using Array        = ANARIArray;
using Array1D      = ANARIArray1D;
using Array2D      = ANARIArray2D;
using Array3D      = ANARIArray3D;
using Frame        = ANARIFrame;
using Geometry     = ANARIGeometry;
using Group        = ANARIGroup;
using Instance     = ANARIInstance;
using Light        = ANARILight;
using Material     = ANARIMaterial;
using Renderer     = ANARIRenderer;
using Surface      = ANARISurface;
using Sampler      = ANARISampler;
using SpatialField = ANARISpatialField;
using Volume       = ANARIVolume;
using World        = ANARIWorld;

// Auxiliary Types //

using StatusCallback          = ANARIStatusCallback;
using Parameter               = ANARIParameter;
using FrameCompletionCallback = ANARIFrameCompletionCallback;
using MemoryDeleter           = ANARIMemoryDeleter;

// Enum Types //

using StatusSeverity = ANARIStatusSeverity;
using StatusCode     = ANARIStatusCode;
using WaitMask       = ANARIWaitMask;
using DataType       = ANARIDataType;

// clang-format on

///////////////////////////////////////////////////////////////////////////////
// C++ API ////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

// Initialization + Introspection /////////////////////////////////////////////

Library loadLibrary(const char *name,
    StatusCallback defaultStatus = nullptr,
    const void *defaultStatusPtr = nullptr);
void unloadLibrary(Library);

void loadModule(Library, const char *name);
void unloadModule(Library, const char *name);

// Object Creation ////////////////////////////////////////////////////////////

anari::Device newDevice(Library, const char *name = "default");

Object newObject(Device d, const char *type, const char *subtype);

template <typename T>
T newObject(Device);
template <typename T>
T newObject(Device, const char *subtype);

// Arrays /////////////////////////////////////////////////////////////////////

// 1D //

template <typename T>
Array1D newArray1D(Device,
    const T *appMemory,
    MemoryDeleter,
    const void *userPtr,
    uint64_t numItems1);

template <typename T>
Array1D newArray1D(Device, const T *appMemory, uint64_t numItems1 = 1);

Array1D newArray1D(Device d, ANARIDataType type, uint64_t numItems1);

// 2D //

template <typename T>
Array2D newArray2D(Device,
    const T *appMemory,
    MemoryDeleter,
    const void *userPtr,
    uint64_t numItems1,
    uint64_t numItems2);

template <typename T>
Array2D newArray2D(
    Device, const T *appMemory, uint64_t numItems1, uint64_t numItems2);

Array2D newArray2D(
    Device d, ANARIDataType type, uint64_t numItems1, uint64_t numItems2);

// 3D //

template <typename T>
Array3D newArray3D(Device,
    const T *appMemory,
    MemoryDeleter,
    const void *userPtr,
    uint64_t numItems1,
    uint64_t numItems2,
    uint64_t numItems3);

template <typename T>
Array3D newArray3D(Device,
    const T *appMemory,
    uint64_t numItems1,
    uint64_t numItems2,
    uint64_t numItems3);

Array3D newArray3D(Device d,
    ANARIDataType type,
    uint64_t numItems1,
    uint64_t numItems2,
    uint64_t numItems3);

// Data Updates //

template <typename T>
T *map(Device, Array);
void unmap(Device, Array);

// Directly Mapped Array Parameters //

void setParameterArray1D(Device d,
    Object object,
    const char *name,
    ANARIDataType type,
    const void *v,
    uint64_t numElements1);

template <typename T>
void setParameterArray1D(ANARIDevice device,
    ANARIObject object,
    const char *name,
    const T *data,
    uint64_t numElements1);

void setParameterArray2D(Device d,
    Object o,
    const char *name,
    ANARIDataType type,
    const void *v,
    uint64_t numElements1,
    uint64_t numElements2);

template <typename T>
void setParameterArray2D(ANARIDevice device,
    ANARIObject object,
    const char *name,
    const T *data,
    uint64_t numElements1,
    uint64_t numElements2);

void setParameterArray3D(Device d,
    Object o,
    const char *name,
    ANARIDataType type,
    const void *v,
    uint64_t numElements1,
    uint64_t numElements2,
    uint64_t numElements3);

template <typename T>
void setParameterArray3D(ANARIDevice device,
    ANARIObject object,
    const char *name,
    const T *data,
    uint64_t numElements1,
    uint64_t numElements2,
    uint64_t numElements3);

// Object + Parameter Lifetime Management /////////////////////////////////////

template <typename T>
void setParameter(Device d, Object o, const char *name, T &&v);

void setParameter(Device d, Object o, const char *name, std::string v);

void setParameter(
    Device d, Object o, const char *name, ANARIDataType type, const void *v);

template <typename T>
void setAndReleaseParameter(Device d, Object o, const char *name, const T &v);

void unsetParameter(Device, Object, const char *id);
void unsetAllParameters(Device, Object);
void commitParameters(Device, Object);

void release(Device, Object);
void retain(Device, Object);

// Object Query Interface /////////////////////////////////////////////////////

template <typename T>
bool getProperty(
    Device, Object, const char *propertyName, T &dst, WaitMask = ANARI_WAIT);

// Frame Operations ///////////////////////////////////////////////////////////

template <typename T>
struct MappedFrameData
{
  uint32_t width{0};
  uint32_t height{0};
  DataType pixelType{ANARI_UNKNOWN};
  const T *data{nullptr};
};

template <typename T>
MappedFrameData<T> map(Device, Frame, const char *channel);
void unmap(Device, Frame, const char *channel);

void render(Device, Frame);
bool isReady(Device, Frame);
void wait(Device, Frame);
void discard(Device, Frame);

///////////////////////////////////////////////////////////////////////////////
// C++ Extension Utilities //////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

using Extensions = ANARIExtensions;

namespace extension {

Extensions getDeviceExtensionStruct(Library library, const char *deviceSubtype);

Extensions getObjectExtensionStruct(
    Device device, DataType objectType, const char *objectSubtype);

Extensions getInstanceExtensionStruct(Device, Object);

} // namespace extension

} // namespace anari

#include "anari_cpp/anari_cpp_impl.h"
