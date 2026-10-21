// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "HalcyonDeviceGlobalState.h"
#include "HalcyonMath.h"
// helium
#include "helium/BaseObject.h"
#include "helium/utility/ChangeObserverPtr.h"
// std
#include <string_view>

namespace halcyon {

struct Object : public helium::BaseObject
{
  Object(ANARIDataType type,
      HalcyonDeviceGlobalState *s,
      size_t ispcObjectSize = 0);
  virtual ~Object() override;

  virtual bool getProperty(const std::string_view &name,
      ANARIDataType type,
      void *ptr,
      uint64_t size,
      uint32_t flags) override;

  virtual void commitParameters() override;
  virtual void finalize() override;

  bool isValid() const override;

  HalcyonDeviceGlobalState *deviceState() const;

  void *getIE() const;

 protected:
  size_t objectIndex() const;
  size_t objectSize() const;

 private:
  size_t m_ispcIndex{INVALID_INDEX};
  void *m_ispcSlot{nullptr};
};

// This gets instantiated for all object subtypes which are not known
struct UnknownObject : public Object
{
  UnknownObject(ANARIDataType type,
      HalcyonDeviceGlobalState *s,
      size_t ispcObjectSize = 0);
  ~UnknownObject() override = default;
  bool isValid() const override;
};

} // namespace halcyon

HALCYON_ANARI_TYPEFOR_SPECIALIZATION(halcyon::Object *, ANARI_OBJECT);
