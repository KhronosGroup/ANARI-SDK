// Copyright 2022-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "IntrusivePtr.h"
// anari
#include <anari/anari_cpp.hpp>
// std
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace helium {

namespace detail {

template <typename T>
constexpr bool validType()
{
  return std::is_same<T, bool>::value
      || anari::ANARITypeFor<T>::value != ANARI_UNKNOWN;
}

} // namespace detail

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

  // Raw data access, note that string values will be limited in storage size
  const void *data() const;
  void *data();

  template <typename T>
  T get() const;

  template <typename T>
  T *getObject() const;

  std::string getString() const;
  void reserveString(size_t size);
  void resizeString(size_t size);

  std::vector<std::string> getStringList() const;
  void reserveStringList(size_t size);
  void resizeStringList(size_t size);

  template <typename T>
  bool is() const;

  bool is(ANARIDataType t) const;

  ANARIDataType type() const;

  bool valid() const;
  operator bool() const;
  void reset();

  bool operator==(const AnariAny &rhs) const;
  bool operator!=(const AnariAny &rhs) const;

 private:
  template <typename T>
  T storageAs() const;

  void refIncObject() const;
  void refDecObject() const;

  constexpr static int MAX_LOCAL_STORAGE = 16 * sizeof(float);

  std::array<uint8_t, MAX_LOCAL_STORAGE> m_storage;
  std::string m_string;
  std::vector<std::string> m_stringList;
  mutable std::vector<const char *> m_stringListPtrs;
  ANARIDataType m_type{ANARI_UNKNOWN};
};

// Inlined definitions ////////////////////////////////////////////////////////

inline AnariAny::AnariAny()
{
  reset();
}

inline AnariAny::AnariAny(const AnariAny &copy)
{
  std::memcpy(m_storage.data(), copy.m_storage.data(), m_storage.size());
  m_string = copy.m_string;
  m_stringList = copy.m_stringList;
  m_type = copy.m_type;
  refIncObject();
}

inline AnariAny::AnariAny(AnariAny &&tmp)
{
  std::memcpy(m_storage.data(), tmp.m_storage.data(), m_storage.size());
  m_string = std::move(tmp.m_string);
  m_stringList = std::move(tmp.m_stringList);
  m_stringListPtrs = std::move(tmp.m_stringListPtrs);
  m_type = tmp.m_type;
  tmp.m_type = ANARI_UNKNOWN;
}

template <typename T>
inline AnariAny::AnariAny(T value) : AnariAny()
{
  constexpr auto type = anari::ANARITypeFor<T>::value;
  static_assert(
      detail::validType<T>(), "unknown type used initialize visrtx::AnariAny");

  static_assert(type != ANARI_STRING_LIST,
      "Don't know how to build a string list from the given type. Please specialize AnariAny constructor.");

  if constexpr (type == ANARI_STRING)
    m_string = value;
  else
    std::memcpy(m_storage.data(), &value, sizeof(value));

  m_type = type;
  refIncObject();
}

template <>
inline AnariAny::AnariAny(bool value) : AnariAny()
{
  uint8_t b = value;
  *this = AnariAny(ANARI_BOOL, &b);
}

template <>
inline AnariAny::AnariAny(std::vector<std::string> value) : AnariAny()
{
  m_stringList = value;
}

template <>
inline AnariAny::AnariAny(char **value) : AnariAny(ANARI_STRING_LIST, value)
{}

template <>
inline AnariAny::AnariAny(const char **value)
    : AnariAny(ANARI_STRING_LIST, value)
{}

template <>
inline AnariAny::AnariAny(char *const *value)
    : AnariAny(ANARI_STRING_LIST, value)
{}

template <>
inline AnariAny::AnariAny(const char *const *value)
    : AnariAny(ANARI_STRING_LIST, value)
{}

inline AnariAny::AnariAny(ANARIDataType type, const void *v) : AnariAny()
{
  m_type = type;
  if (v != nullptr) {
    if (type == ANARI_STRING)
      m_string = (const char *)v;
    else if (type == ANARI_STRING_LIST) {
      const char **stringPtr = (const char **)v;
      m_stringList.clear();
      m_stringListPtrs.clear();
      while (stringPtr && *stringPtr)
        m_stringList.push_back(*stringPtr++);
    } else if (type == ANARI_VOID_POINTER)
      std::memcpy(m_storage.data(), &v, anari::sizeOf(type));
    else
      std::memcpy(m_storage.data(), v, anari::sizeOf(type));
  }
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
  m_stringList = rhs.m_stringList;
  m_stringListPtrs.clear();
  m_type = rhs.m_type;
  refIncObject();
  return *this;
}

inline AnariAny &AnariAny::operator=(AnariAny &&rhs)
{
  reset();
  std::memcpy(m_storage.data(), rhs.m_storage.data(), m_storage.size());
  m_string = std::move(rhs.m_string);
  m_stringList = std::move(rhs.m_stringList);
  m_stringListPtrs = std::move(rhs.m_stringListPtrs);
  m_type = rhs.m_type;
  rhs.m_type = ANARI_UNKNOWN;
  return *this;
}

template <typename T>
inline AnariAny &AnariAny::operator=(T rhs)
{
  return *this = AnariAny(rhs);
}

template <typename T>
inline T AnariAny::get() const
{
  constexpr ANARIDataType type = anari::ANARITypeFor<T>::value;
  static_assert(
      !anari::isObject(type), "use AnariAny::getObject() for getting objects");
  static_assert(
      type != ANARI_STRING, "use AnariAny::getString() for getting strings");
  static_assert(type != ANARI_STRING_LIST,
      "use AnaryAny::getStringList() for getting string lists");

  if (!valid())
    throw std::runtime_error("get() called on empty visrtx::AnariAny");
  if (!is<T>()) {
    throw std::runtime_error(
        "get() called with incorrect type on visrtx::AnariAny");
  }

  return storageAs<T>();
}

template <>
inline bool AnariAny::get() const
{
  if (!valid())
    throw std::runtime_error("get() called on empty visrtx::AnariAny");
  if (!is(ANARI_BOOL)) {
    throw std::runtime_error(
        "get() called with incorrect type on visrtx::AnariAny");
  }

  return storageAs<uint32_t>();
}

inline const void *AnariAny::data() const
{
  switch (type()) {
  case ANARI_STRING:
    return m_string.data();
  case ANARI_STRING_LIST: {
    if (m_stringListPtrs.empty()) {
      m_stringListPtrs.reserve(m_stringList.size() + 1);
      for (const auto &s : m_stringList)
        m_stringListPtrs.push_back(s.c_str());
      m_stringListPtrs.push_back(nullptr);
    }
    return m_stringListPtrs.data();
  }
  default:
    return m_storage.data();
  }
}

inline void *AnariAny::data()
{
  return const_cast<void *>(const_cast<const AnariAny *>(this)->data());
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

template <>
inline bool AnariAny::is<bool>() const
{
  return is(ANARI_BOOL);
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

inline AnariAny::operator bool() const
{
  return valid();
}

inline void AnariAny::reset()
{
  refDecObject();
  std::fill(m_storage.begin(), m_storage.end(), 0);
  m_string.clear();
  m_stringList.clear();
  m_stringListPtrs.clear();
  m_type = ANARI_UNKNOWN;
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
  else if (type() == ANARI_STRING_LIST)
    return m_stringList == rhs.m_stringList;
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

inline void AnariAny::reserveString(size_t size)
{
  m_string.reserve(size);
}

inline void AnariAny::resizeString(size_t size)
{
  m_string.resize(size);
}

inline std::vector<std::string> AnariAny::getStringList() const
{
  return type() == ANARI_STRING_LIST ? m_stringList
                                     : std::vector<std::string>{};
}

inline void AnariAny::reserveStringList(size_t size)
{
  m_stringList.reserve(size);
}

inline void AnariAny::resizeStringList(size_t size)
{
  m_stringListPtrs.clear();
  m_stringList.resize(size);
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
