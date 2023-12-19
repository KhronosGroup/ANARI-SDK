// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "LockableObject.h"
#include "utility/TimeStamp.h"
// anari_cpp
#include <anari/anari_cpp.hpp>
// std
#include <string_view>

#include "BaseGlobalDeviceState.h"
#include "utility/IntrusivePtr.h"
#include "utility/ParameterizedObject.h"

namespace helium {

struct BaseObject : public RefCounted, ParameterizedObject, LockableObject
{
  // Construct
  BaseObject(ANARIDataType type, BaseGlobalDeviceState *state);
  virtual ~BaseObject();

  // Implement anariGetProperty()
  virtual bool getProperty(const std::string_view &name,
      ANARIDataType type,
      void *ptr,
      uint32_t flags) = 0;

  // Implement anariCommitParameters(), but this will only occur when the
  // commit buffer is flushed. This will get skipped if the object has not
  // received any parameter changes since the last commit. Simply call
  // markUpdated() before adding to the commit buffer if you want the commit to
  // occur apart from the public anariCommitParameters() API call.
  virtual void commit() = 0;

  // Base hook for devices to check if the object is valid to use, whatever that
  // means. Devices must be able to handle object subtypes that it does not
  // implement, or handle cases when objects are ill-formed. This gives a
  // generic place to ask the object if it's 'OK' to use.
  virtual bool isValid() const = 0;

  // Object
  ANARIDataType type() const;

  // Event tracking of when parameters have changed (via set or unset)
  TimeStamp lastUpdated() const;
  void markUpdated();

  // Event tracking of when the objects parameters have been committed
  TimeStamp lastCommitted() const;
  virtual void markCommitted();

  // Report a message through the status callback set on the device
  template <typename... Args>
  void reportMessage(
      ANARIStatusSeverity, const char *fmt, Args &&...args) const;

  void addCommitObserver(BaseObject *obj);
  void removeCommitObserver(BaseObject *obj);
  void notifyCommitObservers() const;

  BaseGlobalDeviceState *deviceState() const;

 protected:
  // Handle what happens when the observing object 'obj' is being notified of
  // that this object has changed.
  virtual void notifyObserver(BaseObject *obj) const;

  BaseGlobalDeviceState *m_state{nullptr};

 private:
  void incrementObjectCount();
  void decrementObjectCount();

  std::vector<BaseObject *> m_observers;
  TimeStamp m_lastUpdated{0};
  TimeStamp m_lastCommitted{0};
  ANARIDataType m_type{ANARI_OBJECT};
};

// Return a value to correctly order object by type in the commit buffer
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
