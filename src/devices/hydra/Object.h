// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "HydraGlobalState.h"
// helium
#include "helium/BaseObject.h"
// std
#include <string_view>

namespace anari_hydra {

struct Object : public helium::BaseObject
{
  Object(ANARIDataType type, HydraGlobalState *s);
  ~Object() override = default;

  virtual bool getProperty(const std::string_view &name,
      ANARIDataType type,
      void *ptr,
      uint64_t size,
      uint32_t flags) override;

  virtual void commitParameters() override;
  virtual void finalize() override;

  bool isValid() const override;

  HydraGlobalState *deviceState() const;
};

// Used for unknown/unsupported subtypes so the device can keep running and
// honestly report the object as invalid (helium contract).
struct UnknownObject : public Object
{
  UnknownObject(ANARIDataType type, HydraGlobalState *s);
  ~UnknownObject() override = default;
  bool isValid() const override;
};

} // namespace anari_hydra

namespace anari {
ANARI_TYPEFOR_SPECIALIZATION(anari_hydra::Object *, ANARI_OBJECT);
}
