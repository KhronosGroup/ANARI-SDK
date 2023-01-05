// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "HelideGlobalState.h"
#include "helide_math.h"
// helium
#include "helium/BaseObject.h"
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
      uint32_t flags);

  virtual void commit();

  virtual bool isValid() const;

  HelideGlobalState *deviceState() const;
};

struct UnknownObject : public Object
{
  UnknownObject(ANARIDataType type, HelideGlobalState *s);
  ~UnknownObject() override;
  bool isValid() const override;
};

} // namespace helide

#define HELIDE_ANARI_TYPEFOR_SPECIALIZATION(type, anari_type)                  \
  namespace anari {                                                            \
  ANARI_TYPEFOR_SPECIALIZATION(type, anari_type);                              \
  }

#define HELIDE_ANARI_TYPEFOR_DEFINITION(type)                                  \
  namespace anari {                                                            \
  ANARI_TYPEFOR_DEFINITION(type);                                              \
  }

HELIDE_ANARI_TYPEFOR_SPECIALIZATION(helide::Object *, ANARI_OBJECT);
