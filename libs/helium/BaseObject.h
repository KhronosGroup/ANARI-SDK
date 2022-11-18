// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "utility/TimeStamp.h"
// anari_cpp
#include <anari/anari_cpp.hpp>
// std
#include <string_view>

#include "BaseGlobalDeviceState.h"
#include "utility/IntrusivePtr.h"
#include "utility/ParameterizedObject.h"

namespace helium {

struct BaseObject : public RefCounted, ParameterizedObject
{
  BaseObject(ANARIDataType type, BaseGlobalDeviceState *state);
  virtual ~BaseObject() = default;

  virtual bool getProperty(const std::string_view &name,
      ANARIDataType type,
      void *ptr,
      uint32_t flags) = 0;
  virtual void commit() = 0;
  virtual bool isValid() const;

  ANARIDataType type() const;

  TimeStamp lastUpdated() const;
  void markUpdated();

  TimeStamp lastCommitted() const;
  virtual void markCommitted();

  template <typename... Args>
  void reportMessage(
      ANARIStatusSeverity, const char *fmt, Args &&...args) const;

 protected:
  BaseGlobalDeviceState *m_state{nullptr};

 private:
  TimeStamp m_lastUpdated{0};
  TimeStamp m_lastCommitted{0};
  ANARIDataType m_type{ANARI_OBJECT};
};

int commitPriority(ANARIDataType type);

std::string string_printf(const char *fmt, ...);

// Inlined defintions /////////////////////////////////////////////////////////

template <typename... Args>
inline void BaseObject::reportMessage(
    ANARIStatusSeverity severity, const char *fmt, Args &&...args) const
{
  auto msg = string_printf(fmt, std::forward<Args>(args)...);
  m_state->messageFunction(severity, msg, this);
}

// Helper functions ///////////////////////////////////////////////////////////

template <typename T>
inline void writeToVoidP(void *_p, T v)
{
  T *p = (T *)_p;
  *p = v;
}

} // namespace helium

#define HELIUM_ANARI_TYPEFOR_SPECIALIZATION(type, anari_type)                  \
  namespace anari {                                                            \
  ANARI_TYPEFOR_SPECIALIZATION(type, anari_type);                              \
  }

#define HELIUM_ANARI_TYPEFOR_DEFINITION(type)                                  \
  namespace anari {                                                            \
  ANARI_TYPEFOR_DEFINITION(type);                                              \
  }

HELIUM_ANARI_TYPEFOR_SPECIALIZATION(helium::BaseObject *, ANARI_OBJECT);
