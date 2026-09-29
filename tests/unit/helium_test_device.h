// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// A minimal helium::BaseDevice for tests that drive BaseDevice's API routing
// directly: it owns a plain BaseGlobalDeviceState and creates no objects (tests
// construct their own BaseObject subclasses against state()).

#pragma once

#include "helium/BaseDevice.h"
// std
#include <memory>

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

} // namespace helium_test
