// Copyright 2021-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "AnariAny.h"
// anari
#include "anari/anari_cpp/Traits.h"
// stl
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

namespace helium {

struct ParameterizedObject
{
  ParameterizedObject() = default;
  virtual ~ParameterizedObject() = default;

  // Return true if there was a parameter set with the corresponding 'name'
  bool hasParam(const std::string &name) const;

  // Return true if there was a parameter set with the corresponding 'name' and
  // if it matches the corresponding type
  bool hasParam(const std::string &name, ANARIDataType type) const;

  // Set the value of the parameter 'name', or add it if it doesn't exist yet
  //
  // Returns 'true' if the value for that parameter actually changed
  bool setParam(const std::string &name, ANARIDataType type, const void *v);

  // Set the value of the parameter 'name', or add it if it doesn't exist yet
  //
  // Returns 'true' if the value for that parameter actually changed
  template <typename T>
  bool setParam(const std::string &name, const T &v);

  // Get the value of the parameter associated with 'name', or return
  // 'valueIfNotFound' if the parameter isn't set. This is strongly typed by
  // using the anari::ANARITypeFor<T> to get the ANARIDataType of the provided
  // C++ type 'T'. Implementations should specialize `anari::ANARITypeFor<>` for
  // things like vector math types and matricies. This function is not able to
  // access ANARIObject or ANARIString parameters, see special methods for
  // getting parameters of those types.
  template <typename T>
  T getParam(const std::string &name, T valIfNotFound) const;

  // Get the value of the parameter associated with 'name' and write it to
  // location 'v', returning whether the was actually read. Just like the
  // templated version above, this requires that 'type' exactly match what the
  // application set. This function also cannot get objects or strings.
  bool getParam(const std::string &name, ANARIDataType type, void *v) const;

  // Get the pointer to an object parameter (returns null if not present). While
  // ParameterizedObject will track object lifetime appropriately, accessing
  // an object parameter does _not_ influence lifetime considerations -- devices
  // should consider using `helium::IntrusivePtr<>` to guarantee correct
  // lifetime handling.
  template <typename T>
  T *getParamObject(const std::string &name) const;

  // Get a string parameter value
  std::string getParamString(
      const std::string &name, const std::string &valIfNotFound) const;

  // Get/Set the container holding the value of a parameter (default constructed
  // AnariAny if not present). Getting this container will create a copy of the
  // parameter value, which for objects will incur the correct ref count changes
  // accordingly (handled by AnariAny).
  AnariAny getParamDirect(const std::string &name) const;
  void setParamDirect(const std::string &name, const AnariAny &v);

  // Remove the value of the parameter associated with 'name'.
  //
  // Returns 'true' if anything actually happened
  bool removeParam(const std::string &name);

  // Remove all set parameters
  //
  // Returns 'true' if anything actually happened
  bool removeAllParams();

 protected:
  using Param = std::pair<std::string, AnariAny>;
  using ParameterList = std::vector<Param>;

  ParameterList::iterator params_begin();
  ParameterList::iterator params_end();

 private:
  // Data members //

  const Param *findParam(const std::string &name) const;
  Param *findParam(const std::string &name);

  ParameterList m_params;
};

// Inlined ParameterizedObject definitions ////////////////////////////////////

template <typename T>
inline bool ParameterizedObject::setParam(const std::string &name, const T &v)
{
  constexpr ANARIDataType type = anari::ANARITypeFor<T>::value;
  return setParam(name, type, &v);
}

template <>
inline bool ParameterizedObject::setParam(
    const std::string &name, const std::string &v)
{
  return setParam(name, ANARI_STRING, v.c_str());
}

template <>
inline bool ParameterizedObject::setParam(
    const std::string &name, const bool &v)
{
  uint8_t b = v;
  return setParam(name, ANARI_BOOL, &b);
}

template <typename T>
inline T ParameterizedObject::getParam(
    const std::string &name, T valIfNotFound) const
{
  constexpr ANARIDataType type = anari::ANARITypeFor<T>::value;
  static_assert(!anari::isObject(type),
      "use ParameterizedObect::getParamObject() for getting objects");
  static_assert(type != ANARI_STRING && !std::is_same_v<T, std::string>,
      "use ParameterizedObject::getParamString() for getting strings");
  auto *p = findParam(name);
  return p && p->second.is(type) ? p->second.get<T>() : valIfNotFound;
}

template <>
inline bool ParameterizedObject::getParam(
    const std::string &name, bool valIfNotFound) const
{
  auto *p = findParam(name);
  return p && p->second.is(ANARI_BOOL) ? p->second.get<bool>() : valIfNotFound;
}

template <typename T>
inline T *ParameterizedObject::getParamObject(const std::string &name) const
{
  auto *p = findParam(name);
  return p ? p->second.getObject<T>() : nullptr;
}

} // namespace helium
