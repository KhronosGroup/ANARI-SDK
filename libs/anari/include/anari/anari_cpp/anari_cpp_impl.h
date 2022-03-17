// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Traits.h"
// std
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace anari {

// Helper functions //

namespace detail {

template <typename T>
inline ANARIDataType getType()
{
  constexpr ANARIDataType type = ANARITypeFor<T>::value;
  static_assert(ANARITypeFor<T>::value != ANARI_UNKNOWN,
      "Type used which doesn't have an inferrable ANARIDataType");
  return type;
}

} // namespace detail

///////////////////////////////////////////////////////////////////////////////
// Initialization + Introspection /////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

inline Library loadLibrary(
    const char *name, StatusCallback defaultStatus, void *defaultStatusPtr)
{
  return anariLoadLibrary(name, defaultStatus, defaultStatusPtr);
}

inline void unloadLibrary(Library l)
{
  anariUnloadLibrary(l);
}

inline void loadModule(Library l, const char *name)
{
  anariLoadModule(l, name);
}

inline void unloadModule(Library l, const char *name)
{
  anariUnloadModule(l, name);
}

///////////////////////////////////////////////////////////////////////////////
// Object Creation ////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

inline anari::Device newDevice(Library l, const char *name)
{
  return anariNewDevice(l, name);
}

inline Object newObject(Device d, const char *type, const char *subtype)
{
  return anariNewObject(d, type, subtype);
}

template <typename T>
inline T newObject(Device)
{
  static_assert(isObject(ANARITypeFor<T>::value),
      "anari::newObject<>() can only be instantiated with ANARI handle types");
  return nullptr;
}

template <typename T>
inline T newObject(Device, const char *subtype)
{
  static_assert(isObject(ANARITypeFor<T>::value),
      "anari::newObject<>() can only be instantiated with ANARI handle types");
  return nullptr;
}

// Non-subtyped Objects //

template <>
inline Surface newObject<Surface>(Device d)
{
  return anariNewSurface(d);
}

template <>
inline Group newObject<Group>(Device d)
{
  return anariNewGroup(d);
}

template <>
inline Instance newObject<Instance>(Device d)
{
  return anariNewInstance(d);
}

template <>
inline World newObject<World>(Device d)
{
  return anariNewWorld(d);
}

template <>
inline Frame newObject<Frame>(Device d)
{
  return anariNewFrame(d);
}

// Subtyped Objects //

template <>
inline Camera newObject<Camera>(Device d, const char *subtype)
{
  return anariNewCamera(d, subtype);
}

template <>
inline Light newObject<Light>(Device d, const char *subtype)
{
  return anariNewLight(d, subtype);
}

template <>
inline Geometry newObject<Geometry>(Device d, const char *subtype)
{
  return anariNewGeometry(d, subtype);
}

template <>
inline SpatialField newObject<SpatialField>(Device d, const char *subtype)
{
  return anariNewSpatialField(d, subtype);
}

template <>
inline Volume newObject<Volume>(Device d, const char *subtype)
{
  return anariNewVolume(d, subtype);
}

template <>
inline Material newObject<Material>(Device d, const char *subtype)
{
  return anariNewMaterial(d, subtype);
}

template <>
inline Sampler newObject<Sampler>(Device d, const char *subtype)
{
  return anariNewSampler(d, subtype);
}

template <>
inline Renderer newObject<Renderer>(Device d, const char *subtype)
{
  return anariNewRenderer(d, subtype);
}

///////////////////////////////////////////////////////////////////////////////
// Arrays /////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

// 1D //

template <typename T>
inline Array1D newArray1D(Device d,
    T *appMemory,
    MemoryDeleter deleter,
    void *userPtr,
    uint64_t numItems1,
    uint64_t byteStride1)
{
  return anariNewArray1D(d,
      appMemory,
      deleter,
      userPtr,
      detail::getType<T>(),
      numItems1,
      byteStride1);
}

template <typename T>
inline Array1D newArray1D(
    Device d, T *appMemory, uint64_t numItems1, uint64_t byteStride1)
{
  return anariNewArray1D(d,
      appMemory,
      nullptr,
      nullptr,
      detail::getType<T>(),
      numItems1,
      byteStride1);
}

inline Array1D newArray1D(Device d, ANARIDataType type, uint64_t numItems1)
{
  return anariNewArray1D(d, nullptr, nullptr, nullptr, type, numItems1);
}

// 2D //

template <typename T>
inline Array2D newArray2D(Device d,
    T *appMemory,
    MemoryDeleter deleter,
    void *userPtr,
    uint64_t numItems1,
    uint64_t numItems2,
    uint64_t byteStride1,
    uint64_t byteStride2)
{
  return anariNewArray2D(d,
      appMemory,
      deleter,
      userPtr,
      detail::getType<T>(),
      numItems1,
      numItems2,
      byteStride1,
      byteStride2);
}

template <typename T>
inline Array2D newArray2D(Device d,
    T *appMemory,
    uint64_t numItems1,
    uint64_t numItems2,
    uint64_t byteStride1,
    uint64_t byteStride2)
{
  return anariNewArray2D(d,
      appMemory,
      nullptr,
      nullptr,
      detail::getType<T>(),
      numItems1,
      numItems2,
      byteStride1,
      byteStride2);
}

inline Array2D newArray2D(
    Device d, ANARIDataType type, uint64_t numItems1, uint64_t numItems2)
{
  return anariNewArray2D(
      d, nullptr, nullptr, nullptr, type, numItems1, numItems2);
}

// 3D //

template <typename T>
inline Array3D newArray3D(Device d,
    T *appMemory,
    MemoryDeleter deleter,
    void *userPtr,
    uint64_t numItems1,
    uint64_t numItems2,
    uint64_t numItems3,
    uint64_t byteStride1,
    uint64_t byteStride2,
    uint64_t byteStride3)
{
  return anariNewArray3D(d,
      appMemory,
      deleter,
      userPtr,
      detail::getType<T>(),
      numItems1,
      numItems2,
      numItems3,
      byteStride1,
      byteStride2,
      byteStride3);
}

template <typename T>
inline Array3D newArray3D(Device d,
    T *appMemory,
    uint64_t numItems1,
    uint64_t numItems2,
    uint64_t numItems3,
    uint64_t byteStride1,
    uint64_t byteStride2,
    uint64_t byteStride3)
{
  return anariNewArray3D(d,
      appMemory,
      nullptr,
      nullptr,
      detail::getType<T>(),
      numItems1,
      numItems2,
      numItems3,
      byteStride1,
      byteStride2,
      byteStride3);
}

inline Array3D newArray3D(Device d,
    ANARIDataType type,
    uint64_t numItems1,
    uint64_t numItems2,
    uint64_t numItems3)
{
  return anariNewArray3D(
      d, nullptr, nullptr, nullptr, type, numItems1, numItems2, numItems3);
}

// Data Updates //

inline void *map(Device d, Array a)
{
  return anariMapArray(d, a);
}

inline void unmap(Device d, Array a)
{
  anariUnmapArray(d, a);
}

///////////////////////////////////////////////////////////////////////////////
// Object + Parameter Lifetime Management /////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

template <typename T>
inline void setParameter(ANARIDevice d, ANARIObject o, const char *name, T &&v)
{
  using TYPE = typename std::remove_reference<T>::type;
  constexpr bool validType = ANARITypeFor<TYPE>::value != ANARI_UNKNOWN;
  constexpr bool isString = std::is_convertible<TYPE, const char *>::value;
  constexpr bool isStringLiteral = isString && std::is_array<TYPE>::value;
  constexpr bool isVoidPtr = ANARITypeFor<TYPE>::value == ANARI_VOID_POINTER;
  static_assert(validType || isString,
      "Only types corresponding to ANARIDataType values can be set "
      "as parameters on ANARI objects.");
  if (isStringLiteral)
    anariSetParameter(d, o, name, ANARI_STRING, (const char *)&v);
  else if (isString)
    anariSetParameter(d, o, name, ANARI_STRING, *((const char **)&v));
  else if (isVoidPtr)
    anariSetParameter(d, o, name, ANARI_VOID_POINTER, *((const void **)&v));
  else
    anariSetParameter(d, o, name, ANARITypeFor<TYPE>::value, &v);
}

inline void setParameter(
    ANARIDevice d, ANARIObject o, const char *name, std::string v)
{
  setParameter(d, o, name, v.c_str());
}

inline void setParameter(
    Device d, Object o, const char *name, ANARIDataType type, const void *v)
{
  anariSetParameter(d, o, name, type, v);
}

template <typename T>
inline void setAndReleaseParameter(
    ANARIDevice d, ANARIObject o, const char *name, const T &v)
{
  static_assert(isObject(ANARITypeFor<T>::value),
      "anari::setAndReleaseParameter() can only set ANARI objects as parameters");
  setParameter(d, o, name, v);
  anariRelease(d, v);
}

inline void unsetParameter(Device d, Object o, const char *id)
{
  anariUnsetParameter(d, o, id);
}

inline void commit(Device d, Object o)
{
  anariCommit(d, o);
}

inline void release(Device d, Object o)
{
  anariRelease(d, o);
}

inline void retain(Device d, Object o)
{
  anariRetain(d, o);
}

///////////////////////////////////////////////////////////////////////////////
// Object Query Interface /////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

template <typename T>
inline bool getProperty(
    Device d, Object o, const char *propertyName, T &dst, WaitMask mask)
{
  return anariGetProperty(
      d, o, propertyName, detail::getType<T>(), &dst, sizeof(T), mask);
}

///////////////////////////////////////////////////////////////////////////////
// Frame Operations ///////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

inline const void *map(Device d, Frame f, const char *channel)
{
  return anariMapFrame(d, f, channel);
}

inline void unmap(Device d, Frame f, const char *channel)
{
  anariUnmapFrame(d, f, channel);
}

inline void render(Device d, Frame f)
{
  anariRenderFrame(d, f);
}

inline bool isReady(Device d, Frame f)
{
  return anariFrameReady(d, f, ANARI_NO_WAIT);
}

inline void wait(Device d, Frame f)
{
  anariFrameReady(d, f, ANARI_WAIT);
}

inline void discard(Device d, Frame f)
{
  anariDiscardFrame(d, f);
}

///////////////////////////////////////////////////////////////////////////////
// Extensions /////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

inline bool deviceImplements(Device d, const char *extension)
{
  return anariDeviceImplements(d, extension);
}

} // namespace anari
