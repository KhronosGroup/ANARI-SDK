// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// A minimal helium::BaseDevice for tests that drive BaseDevice's API routing
// directly: it owns a plain BaseGlobalDeviceState and creates no objects (tests
// construct their own BaseObject subclasses, or StubObjects, against state()).

#pragma once

#include "anari/anari_cpp/Traits.h"
#include "helium/BaseDevice.h"
#include "helium/BaseObject.h"
#include "helium/array/Array1D.h"
// std
#include <memory>
#include <vector>

namespace helium_test {

struct TestDevice : public helium::BaseDevice
{
  TestDevice() : BaseDevice(nullptr, nullptr)
  {
    m_state = std::make_unique<helium::BaseGlobalDeviceState>(this_device());
  }

  helium::BaseGlobalDeviceState *state()
  {
    return m_state.get();
  }

  ANARIArray1D newArray1D(const void *,
      ANARIMemoryDeleter,
      const void *,
      ANARIDataType,
      uint64_t) override
  {
    return nullptr;
  }
  ANARIArray2D newArray2D(const void *,
      ANARIMemoryDeleter,
      const void *,
      ANARIDataType,
      uint64_t,
      uint64_t) override
  {
    return nullptr;
  }
  ANARIArray3D newArray3D(const void *,
      ANARIMemoryDeleter,
      const void *,
      ANARIDataType,
      uint64_t,
      uint64_t,
      uint64_t) override
  {
    return nullptr;
  }
  ANARIGeometry newGeometry(const char *) override
  {
    return nullptr;
  }
  ANARIMaterial newMaterial(const char *) override
  {
    return nullptr;
  }
  ANARISampler newSampler(const char *) override
  {
    return nullptr;
  }
  ANARISurface newSurface() override
  {
    return nullptr;
  }
  ANARISpatialField newSpatialField(const char *) override
  {
    return nullptr;
  }
  ANARIVolume newVolume(const char *) override
  {
    return nullptr;
  }
  ANARILight newLight(const char *) override
  {
    return nullptr;
  }
  ANARIGroup newGroup() override
  {
    return nullptr;
  }
  ANARIInstance newInstance(const char *) override
  {
    return nullptr;
  }
  ANARIWorld newWorld() override
  {
    return nullptr;
  }
  ANARICamera newCamera(const char *) override
  {
    return nullptr;
  }
  ANARIRenderer newRenderer(const char *) override
  {
    return nullptr;
  }
  ANARIFrame newFrame() override
  {
    return nullptr;
  }
  const char **getObjectSubtypes(ANARIDataType) override
  {
    return nullptr;
  }
  const void *getObjectInfo(
      ANARIDataType, const char *, const char *, ANARIDataType) override
  {
    return nullptr;
  }
  const void *getParameterInfo(ANARIDataType,
      const char *,
      const char *,
      ANARIDataType,
      const char *,
      ANARIDataType) override
  {
    return nullptr;
  }
};

// A BaseObject that does nothing, for tests that need objects to reference.
struct StubObject : public helium::BaseObject
{
  StubObject(ANARIDataType type, helium::BaseGlobalDeviceState *s)
      : BaseObject(type, s)
  {}

  bool isValid() const override
  {
    return true;
  }
  bool getProperty(const std::string_view &,
      ANARIDataType,
      void *,
      uint64_t,
      uint32_t) override
  {
    return false;
  }
  void commitParameters() override {}
  void finalize() override {}
};

// A committed Array1D of 'numItems' elements of 'elementType' sharing
// 'appMemory' (which must outlive it), holding one public reference.
inline helium::Array1D *newSharedArray(helium::BaseGlobalDeviceState *s,
    const void *appMemory,
    ANARIDataType elementType,
    size_t numItems)
{
  helium::Array1DMemoryDescriptor md;
  md.appMemory = appMemory;
  md.elementType = elementType;
  md.numItems = numItems;
  auto *array = new helium::Array1D(s, md);
  array->commitParameters();
  return array;
}

// As above, with the element type and count taken from 'appData'.
template <typename T>
helium::Array1D *newSharedArray(
    helium::BaseGlobalDeviceState *s, const std::vector<T> &appData)
{
  return newSharedArray(
      s, appData.data(), anari::ANARITypeFor<T>::value, appData.size());
}

// Commits a range-only change of 1D array 'a' (an Array1D or ObjectArray) to
// [begin, end).
inline void setRange(helium::Array *a, size_t begin, size_t end)
{
  a->setParam("begin", uint64_t(begin));
  a->setParam("end", uint64_t(end));
  a->commitParameters();
  a->finalize();
}

} // namespace helium_test
