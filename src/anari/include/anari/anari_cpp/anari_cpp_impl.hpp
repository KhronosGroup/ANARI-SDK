// Copyright 2021-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Traits.h"
#include "anari/frontend/type_utility.h"
// std
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace anari {

// Helper functions //

namespace detail {

template <typename T>
constexpr DataType getType()
{
  return ANARITypeFor<T>::value;
}

} // namespace detail

///////////////////////////////////////////////////////////////////////////////
// Initialization + Introspection /////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

inline Library loadLibrary(const char *name,
    StatusCallback defaultStatus,
    const void *defaultStatusPtr)
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

inline Device newDevice(Library l, const char *name)
{
  return anariNewDevice(l, name);
}

inline Object newObject(Device d, const char *type, const char *subtype)
{
  return anariNewObject(d, type, subtype);
}

template <typename T>
struct NewObjectImpl
{
};

template <>
struct NewObjectImpl<Surface>
{
  static Surface newObject(Device d)
  {
    return anariNewSurface(d);
  }
};

template <>
struct NewObjectImpl<Group>
{
  static Group newObject(Device d)
  {
    return anariNewGroup(d);
  }
};

template <>
struct NewObjectImpl<World>
{
  static World newObject(Device d)
  {
    return anariNewWorld(d);
  }
};

template <>
struct NewObjectImpl<Frame>
{
  static Frame newObject(Device d)
  {
    return anariNewFrame(d);
  }
};

template <>
struct NewObjectImpl<Instance>
{
  static Instance newObject(Device d, const char *subtype)
  {
    return anariNewInstance(d, subtype);
  }
};

template <>
struct NewObjectImpl<Camera>
{
  static Camera newObject(Device d, const char *subtype)
  {
    return anariNewCamera(d, subtype);
  }
};

template <>
struct NewObjectImpl<Light>
{
  static Light newObject(Device d, const char *subtype)
  {
    return anariNewLight(d, subtype);
  }
};

template <>
struct NewObjectImpl<Geometry>
{
  static Geometry newObject(Device d, const char *subtype)
  {
    return anariNewGeometry(d, subtype);
  }
};

template <>
struct NewObjectImpl<SpatialField>
{
  static SpatialField newObject(Device d, const char *subtype)
  {
    return anariNewSpatialField(d, subtype);
  }
};

template <>
struct NewObjectImpl<Volume>
{
  static Volume newObject(Device d, const char *subtype)
  {
    return anariNewVolume(d, subtype);
  }
};

template <>
struct NewObjectImpl<Material>
{
  static Material newObject(Device d, const char *subtype)
  {
    return anariNewMaterial(d, subtype);
  }
};

template <>
struct NewObjectImpl<Sampler>
{
  static Sampler newObject(Device d, const char *subtype)
  {
    return anariNewSampler(d, subtype);
  }
};

template <>
struct NewObjectImpl<Renderer>
{
  static Renderer newObject(Device d, const char *subtype)
  {
    return anariNewRenderer(d, subtype);
  }
};

template <typename T>
inline T newObject(Device d)
{
  return NewObjectImpl<T>::newObject(d);
}

template <typename T>
inline T newObject(Device d, const char *subtype)
{
  return NewObjectImpl<T>::newObject(d, subtype);
}

///////////////////////////////////////////////////////////////////////////////
// Arrays /////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

// 1D //

inline Array1D newArray1D(Device d,
    const void *appMemory,
    MemoryDeleter deleter,
    const void *userPtr,
    DataType elementType,
    uint64_t numItems1)
{
  return anariNewArray1D(
      d, appMemory, deleter, userPtr, elementType, numItems1);
}

template <typename T>
inline Array1D newArray1D(Device d,
    const T *appMemory,
    MemoryDeleter deleter,
    const void *userPtr,
    uint64_t numItems1)
{
  return anariNewArray1D(
      d, appMemory, deleter, userPtr, detail::getType<T>(), numItems1);
}

template <typename T>
inline Array1D newArray1D(Device d, const T *appMemory, uint64_t numItems1)
{
  return anariNewArray1D(
      d, appMemory, nullptr, nullptr, detail::getType<T>(), numItems1);
}

inline Array1D newArray1D(Device d, DataType type, uint64_t numItems1)
{
  return anariNewArray1D(d, nullptr, nullptr, nullptr, type, numItems1);
}

// 2D //

inline Array2D newArray2D(Device d,
    const void *appMemory,
    MemoryDeleter deleter,
    const void *userPtr,
    DataType elementType,
    uint64_t numItems1,
    uint64_t numItems2)
{
  return anariNewArray2D(
      d, appMemory, deleter, userPtr, elementType, numItems1, numItems2);
}

template <typename T>
inline Array2D newArray2D(Device d,
    const T *appMemory,
    MemoryDeleter deleter,
    const void *userPtr,
    uint64_t numItems1,
    uint64_t numItems2)
{
  return anariNewArray2D(d,
      appMemory,
      deleter,
      userPtr,
      detail::getType<T>(),
      numItems1,
      numItems2);
}

template <typename T>
inline Array2D newArray2D(
    Device d, const T *appMemory, uint64_t numItems1, uint64_t numItems2)
{
  return anariNewArray2D(d,
      appMemory,
      nullptr,
      nullptr,
      detail::getType<T>(),
      numItems1,
      numItems2);
}

inline Array2D newArray2D(
    Device d, DataType type, uint64_t numItems1, uint64_t numItems2)
{
  return anariNewArray2D(
      d, nullptr, nullptr, nullptr, type, numItems1, numItems2);
}

// 3D //

inline Array3D newArray3D(Device d,
    const void *appMemory,
    MemoryDeleter deleter,
    const void *userPtr,
    DataType elementType,
    uint64_t numItems1,
    uint64_t numItems2,
    uint64_t numItems3)
{
  return anariNewArray3D(d,
      appMemory,
      deleter,
      userPtr,
      elementType,
      numItems1,
      numItems2,
      numItems3);
}

template <typename T>
inline Array3D newArray3D(Device d,
    const T *appMemory,
    MemoryDeleter deleter,
    const void *userPtr,
    uint64_t numItems1,
    uint64_t numItems2,
    uint64_t numItems3)
{
  return anariNewArray3D(d,
      appMemory,
      deleter,
      userPtr,
      detail::getType<T>(),
      numItems1,
      numItems2,
      numItems3);
}

template <typename T>
inline Array3D newArray3D(Device d,
    const T *appMemory,
    uint64_t numItems1,
    uint64_t numItems2,
    uint64_t numItems3)
{
  return anariNewArray3D(d,
      appMemory,
      nullptr,
      nullptr,
      detail::getType<T>(),
      numItems1,
      numItems2,
      numItems3);
}

inline Array3D newArray3D(Device d,
    DataType type,
    uint64_t numItems1,
    uint64_t numItems2,
    uint64_t numItems3)
{
  return anariNewArray3D(
      d, nullptr, nullptr, nullptr, type, numItems1, numItems2, numItems3);
}

// Data Updates //

template <typename T>
inline T *map(Device d, Array a)
{
  return static_cast<T *>(anariMapArray(d, a));
}

inline void unmap(Device d, Array a)
{
  anariUnmapArray(d, a);
}

// Directly Mapped Array Parameters //

inline void setParameterArray1DStrided(Device d,
    Object o,
    const char *name,
    DataType type,
    const void *v,
    uint64_t numElements1,
    uint64_t strideIn)
{
  const bool incomingDataIsDense = strideIn == sizeOf(type);
  uint64_t strideOut = 0;
  if (void *mem = anariMapParameterArray1D(
          d, o, name, type, numElements1, &strideOut)) {
    uint64_t elementSize = sizeOf(type);
    if (incomingDataIsDense && (strideOut == 0 || elementSize == strideOut)) {
      std::memcpy(mem, v, elementSize * numElements1);
    } else {
      char *cmem = (char *)mem;
      const char *cv = (const char *)v;
      for (uint64_t i = 0; i < numElements1; ++i) {
        std::memcpy(cmem + strideOut * i, cv + strideIn * i, elementSize);
      }
    }
    anariUnmapParameterArray(d, o, name);
  }
}

inline void setParameterArray1D(Device d,
    Object o,
    const char *name,
    DataType type,
    const void *v,
    uint64_t numElements)
{
  setParameterArray1DStrided(d, o, name, type, v, numElements, sizeOf(type));
}

template <typename T>
inline void setParameterArray1D(
    Device d, Object o, const char *name, const T *v, uint64_t numElements1)
{
  setParameterArray1D(d, o, name, ANARITypeFor<T>::value, v, numElements1);
}

inline void setParameterArray2D(Device d,
    Object o,
    const char *name,
    DataType type,
    const void *v,
    uint64_t numElements1,
    uint64_t numElements2)
{
  uint64_t elementStride = 0;
  if (void *mem = anariMapParameterArray2D(
          d, o, name, type, numElements1, numElements2, &elementStride)) {
    uint64_t elementSize = sizeOf(type);
    uint64_t totalElements = numElements1 * numElements2;
    if (elementStride == 0 || elementSize == elementStride) {
      std::memcpy(mem, v, elementSize * totalElements);
    } else {
      char *cmem = (char *)mem;
      const char *cv = (const char *)v;
      for (uint64_t i = 0; i < totalElements; ++i) {
        std::memcpy(
            cmem + elementStride * i, cv + elementSize * i, elementSize);
      }
    }
    anariUnmapParameterArray(d, o, name);
  }
}

template <typename T>
inline void setParameterArray2D(Device d,
    Object o,
    const char *name,
    const T *v,
    uint64_t numElements1,
    uint64_t numElements2)
{
  setParameterArray2D(
      d, o, name, ANARITypeFor<T>::value, v, numElements1, numElements2);
}

inline void setParameterArray3D(Device d,
    Object o,
    const char *name,
    DataType type,
    const void *v,
    uint64_t numElements1,
    uint64_t numElements2,
    uint64_t numElements3)
{
  uint64_t elementStride = 0;
  if (void *mem = anariMapParameterArray3D(d,
          o,
          name,
          type,
          numElements1,
          numElements2,
          numElements3,
          &elementStride)) {
    uint64_t elementSize = sizeOf(type);
    uint64_t totalElements = numElements1 * numElements2 * numElements3;
    if (elementStride == 0 || elementSize == elementStride) {
      std::memcpy(mem, v, elementSize * totalElements);
    } else {
      char *cmem = (char *)mem;
      const char *cv = (const char *)v;
      for (uint64_t i = 0; i < totalElements; ++i) {
        std::memcpy(
            cmem + elementStride * i, cv + elementSize * i, elementSize);
      }
    }
    anariUnmapParameterArray(d, o, name);
  }
}

template <typename T>
inline void setParameterArray3D(Device d,
    Object o,
    const char *name,
    const T *v,
    uint64_t numElements1,
    uint64_t numElements2,
    uint64_t numElements3)
{
  setParameterArray3D(d,
      o,
      name,
      ANARITypeFor<T>::value,
      v,
      numElements1,
      numElements2,
      numElements3);
}

///////////////////////////////////////////////////////////////////////////////
// Object + Parameter Lifetime Management /////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

template <typename T>
inline void setParameter(Device d, Object o, const char *name, T &&v)
{
  using TYPE = typename std::remove_reference<T>::type;
  constexpr bool validType = detail::getType<TYPE>() != ANARI_UNKNOWN;
  constexpr bool isString = std::is_convertible<TYPE, const char *>::value;
  constexpr bool isStringLiteral = isString && std::is_array<TYPE>::value;
  constexpr bool isVoidPtr = detail::getType<TYPE>() == ANARI_VOID_POINTER;
  static_assert(validType || isString,
      "Only types corresponding to DataType values can be set "
      "as parameters on  objects.");
  if (isStringLiteral)
    anariSetParameter(d, o, name, ANARI_STRING, (const char *)&v);
  else if (isString)
    anariSetParameter(d, o, name, ANARI_STRING, *((const char **)&v));
  else if (isVoidPtr)
    anariSetParameter(d, o, name, ANARI_VOID_POINTER, *((const void **)&v));
  else
    anariSetParameter(d, o, name, ANARITypeFor<TYPE>::value, &v);
}

inline void setParameter(Device d, Object o, const char *name, std::string v)
{
  setParameter(d, o, name, v.c_str());
}

inline void setParameter(Device d, Object o, const char *name, bool v)
{
  const uint32_t b = v;
  anariSetParameter(d, o, name, ANARI_BOOL, &b);
}

inline void setParameter(
    Device d, Object o, const char *name, DataType type, const void *v)
{
  anariSetParameter(d, o, name, type, v);
}

template <typename T>
inline void setParameterAs(
    Device d, Object o, const char *name, DataType type, T &&v)
{
  anariSetParameter(d, o, name, type, &v);
}

template <typename T>
inline void setAndReleaseParameter(
    Device d, Object o, const char *name, const T &v)
{
  static_assert(isObject(ANARITypeFor<T>::value),
      "anari::setAndReleaseParameter() can only set anari::Objects as parameters");
  setParameter(d, o, name, v);
  anariRelease(d, v);
}

inline void unsetParameter(Device d, Object o, const char *id)
{
  anariUnsetParameter(d, o, id);
}

inline void unsetAllParameters(Device d, Object o)
{
  anariUnsetAllParameters(d, o);
}

inline void commitParameters(Device d, Object o)
{
  anariCommitParameters(d, o);
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

template <>
inline bool getProperty(
    Device d, Object o, const char *propertyName, bool &dst, WaitMask mask)
{
  uint32_t tmp = 0;
  const auto retval =
      anariGetProperty(d, o, propertyName, ANARI_BOOL, &tmp, sizeof(tmp), mask);
  if (retval)
    dst = bool(tmp);
  return retval;
}

///////////////////////////////////////////////////////////////////////////////
// Frame Operations ///////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

template <typename T>
inline MappedFrameData<T> map(Device d, Frame f, const char *channel)
{
  MappedFrameData<T> retval;
  retval.data = static_cast<const T *>(anariMapFrame(
      d, f, channel, &retval.width, &retval.height, &retval.pixelType));
  return retval;
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
// C++ Extension Utilities ////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

namespace extension {

inline Extensions getDeviceExtensionStruct(Library l, const char *device)
{
  Extensions f;
  anariGetDeviceExtensionStruct(&f, l, device);
  return f;
}

inline Extensions getObjectExtensionStruct(
    Device d, DataType objectType, const char *objectSubtype)
{
  Extensions f;
  anariGetObjectExtensionStruct(&f, d, objectType, objectSubtype);
  return f;
}

inline Extensions getInstanceExtensionStruct(Device d, Object o)
{
  Extensions f;
  anariGetInstanceExtensionStruct(&f, d, o);
  return f;
}

} // namespace extension

} // namespace anari
