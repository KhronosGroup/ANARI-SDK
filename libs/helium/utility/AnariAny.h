// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "IntrusivePtr.h"
// anari
#include <anari/type_utility.h>
#include <anari/anari_cpp.hpp>
// std
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>

namespace helium {

struct AnariAny
{
  AnariAny();
  AnariAny(const AnariAny &copy);
  AnariAny(AnariAny &&tmp);

  template <typename T>
  AnariAny(T value);

  AnariAny(ANARIDataType type, const void *v);

  ~AnariAny();

  AnariAny &operator=(const AnariAny &rhs);
  AnariAny &operator=(AnariAny &&rhs);

  template <typename T>
  AnariAny &operator=(T rhs);

  bool operator==(const AnariAny &rhs) const;
  bool operator!=(const AnariAny &rhs) const;

  // Raw data access, note that string values will be limited in storage size
  const void *data() const;
  void *data();

  template <typename T>
  T get() const;

  template <typename T>
  T *getObject() const;

  std::string getString() const;

  template <typename T>
  bool is() const;

  bool is(ANARIDataType t) const;

  ANARIDataType type() const;

  bool valid() const;
  void reset();

 private:
  template <typename T>
  T storageAs() const;

  void refIncObject() const;
  void refDecObject() const;

  constexpr static int MAX_LOCAL_STORAGE = 16 * sizeof(float);

  std::array<uint8_t, MAX_LOCAL_STORAGE> m_storage;
  std::string m_string;
  ANARIDataType m_type{ANARI_UNKNOWN};
};

// Inlined definitions ////////////////////////////////////////////////////////

inline AnariAny::AnariAny()
{
  reset();
}

inline AnariAny::AnariAny(const AnariAny &copy)
{
  reset();
  std::memcpy(m_storage.data(), copy.m_storage.data(), m_storage.size());
  m_string = copy.m_string;
  m_type = copy.m_type;
  refIncObject();
}

inline AnariAny::AnariAny(AnariAny &&tmp)
{
  reset();
  std::memcpy(m_storage.data(), tmp.m_storage.data(), m_storage.size());
  m_string = std::move(tmp.m_string);
  m_type = tmp.m_type;
  tmp.m_type = ANARI_UNKNOWN;
}

template <typename T>
inline AnariAny::AnariAny(T value)
{
  constexpr auto type = anari::ANARITypeFor<T>::value;
  static_assert(
      type != ANARI_UNKNOWN, "unknown type used initialize visrtx::AnariAny");

  if constexpr (type == ANARI_STRING)
    m_string = value;
  else
    std::memcpy(m_storage.data(), &value, sizeof(value));

  m_type = type;
  refIncObject();
}

inline AnariAny::AnariAny(ANARIDataType type, const void *v)
{
  m_type = type;
  if (type == ANARI_STRING)
    m_string = (const char *)v;
  else
    std::memcpy(m_storage.data(), v, anari::sizeOf(type));
  refIncObject();
}

inline AnariAny::~AnariAny()
{
  reset();
}

inline AnariAny &AnariAny::operator=(const AnariAny &rhs)
{
  reset();
  std::memcpy(m_storage.data(), rhs.m_storage.data(), m_storage.size());
  m_string = rhs.m_string;
  m_type = rhs.m_type;
  refIncObject();
  return *this;
}

inline AnariAny &AnariAny::operator=(AnariAny &&rhs)
{
  reset();
  std::memcpy(m_storage.data(), rhs.m_storage.data(), m_storage.size());
  m_string = std::move(rhs.m_string);
  m_type = rhs.m_type;
  rhs.m_type = ANARI_UNKNOWN;
  return *this;
}

template <typename T>
inline AnariAny &AnariAny::operator=(T rhs)
{
  return *this = AnariAny(rhs);
}

inline bool AnariAny::operator==(const AnariAny &rhs) const
{
  if (!valid() || !rhs.valid())
    return false;
  if (type() != rhs.type())
    return false;
  if (type() == ANARI_BOOL)
    return get<bool>() == rhs.get<bool>();
  else if (type() == ANARI_STRING)
    return m_string == rhs.m_string;
  else {
    return std::equal(m_storage.data(),
        m_storage.data() + ::anari::sizeOf(type()),
        rhs.m_storage.data());
  }
}

inline bool AnariAny::operator!=(const AnariAny &rhs) const
{
  return !(*this == rhs);
}

template <typename T>
inline T AnariAny::get() const
{
  constexpr ANARIDataType type = anari::ANARITypeFor<T>::value;
  static_assert(
      !anari::isObject(type), "use AnariAny::getObject() for getting objects");
  static_assert(
      type != ANARI_STRING, "use AnariAny::getString() for getting strings");

  if (!valid())
    throw std::runtime_error("get() called on empty visrtx::AnariAny");
  if (!is<T>()) {
    throw std::runtime_error(
        "get() called with invalid type on visrtx::AnariAny");
  }

  return storageAs<T>();
}

inline const void *AnariAny::data() const
{
  return type() == ANARI_STRING ? (const void *)m_string.data()
                                : (const void *)m_storage.data();
}

inline void *AnariAny::data()
{
  return type() == ANARI_STRING ? (void *)m_string.data()
                                : (void *)m_storage.data();
}

template <typename T>
inline T *AnariAny::getObject() const
{
  constexpr ANARIDataType type = anari::ANARITypeFor<T *>::value;
  static_assert(
      anari::isObject(type), "use AnariAny::get() for getting non-objects");
  return anari::isObject(this->type()) ? storageAs<T *>() : nullptr;
}

template <typename T>
inline bool AnariAny::is() const
{
  return is(anari::ANARITypeFor<T>::value);
}

inline bool AnariAny::is(ANARIDataType t) const
{
  return type() == t;
}

inline ANARIDataType AnariAny::type() const
{
  return m_type;
}

inline bool AnariAny::valid() const
{
  return type() != ANARI_UNKNOWN;
}

inline void AnariAny::reset()
{
  refDecObject();
  std::fill(m_storage.begin(), m_storage.end(), 0);
  m_string.clear();
  m_type = ANARI_UNKNOWN;
}

template <typename T>
inline T AnariAny::storageAs() const
{
  static_assert(sizeof(T) <= MAX_LOCAL_STORAGE, "AnariAny: not enough storage");
  T retval;
  std::memcpy(&retval, m_storage.data(), sizeof(retval));
  return retval;
}

inline std::string AnariAny::getString() const
{
  return type() == ANARI_STRING ? m_string : "";
}

inline void AnariAny::refIncObject() const
{
  if (anari::isObject(type())) {
    auto *o = storageAs<RefCounted *>();
    if (o)
      o->refInc(RefType::INTERNAL);
  }
}

inline void AnariAny::refDecObject() const
{
  if (anari::isObject(type())) {
    auto *o = storageAs<RefCounted *>();
    if (o)
      o->refDec(RefType::INTERNAL);
  }
}

} // namespace helium
