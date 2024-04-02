// Copyright 2022-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "HelideGlobalState.h"
#include "HelideMath.h"
// helium
#include "helium/BaseObject.h"
#include "helium/utility/CommitObserverPtr.h"
// std
#include <string_view>

namespace helide {

struct Object : public helium::BaseObject
{
  Object(ANARIDataType type, HelideGlobalState *s);
  virtual ~Object() = default;

  virtual bool getProperty(const std::string_view &name,
      ANARIDataType type,
      void *ptr,
      uint32_t flags) override;

  virtual void commit() override;

  bool isValid() const override;

  HelideGlobalState *deviceState() const;
};

struct UnknownObject : public Object
{
  UnknownObject(ANARIDataType type, HelideGlobalState *s);
  ~UnknownObject() override = default;
  bool isValid() const override;
};

} // namespace helide

HELIDE_ANARI_TYPEFOR_SPECIALIZATION(helide::Object *, ANARI_OBJECT);
