// Copyright 2022 The Khronos Group
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
  bool hasParam(const std::string &name);

  // Set the value of the parameter 'name', or add it if it doesn't exist yet
  void setParam(const std::string &name, ANARIDataType type, const void *v);

  // Get the value of the parameter associated with 'name', or return
  // 'valueIfNotFound' if the parameter isn't set. This is strongly typed by
  // using the anari::ANARITypeFor<T> to get the ANARIDataType of the provided
  // C++ type 'T'. Implementations should specialize `anari::ANARITypeFor<>` for
  // things like vector math types and matricies. This function is not able to
  // access ANARIObject or ANARIString parameters, see special methods for
  // getting parameters of those types.
  template <typename T>
  T getParam(const std::string &name, T valIfNotFound);

  // Get the value of the parameter associated with 'name' and write it to
  // location 'v', returning whether the was actually read. Just like the
  // templated version above, this requires that 'type' exactly match what the
  // application set. This function also cannot get objects or strings.
  bool getParam(const std::string &name, ANARIDataType type, void *v);

  // Get the pointer to an object parameter (returns null if not present). While
  // ParameterizedObject will track object lifetime appropriately, accessing
  // an object parameter does _not_ influence lifetime considerations -- devices
  // should consider using `helium::IntrusivePtr<>` to guarantee correct
  // lifetime handling.
  template <typename T>
  T *getParamObject(const std::string &name);

  // Get a string parameter value
  std::string getParamString(
      const std::string &name, const std::string &valIfNotFound);

  // Get/Set the container holding the value of a parameter (default constructed
  // AnariAny if not present). Getting this container will create a copy of the
  // parameter value, which for objects will incur the correct ref count changes
  // accordingly (handled by AnariAny).
  AnariAny getParamDirect(const std::string &name);
  void setParamDirect(const std::string &name, const AnariAny &v);

  // Remove the value of the parameter associated with 'name'.
  void removeParam(const std::string &name);

 private:
  // Data members //

  using Param = std::pair<std::string, AnariAny>;

  Param *findParam(const std::string &name, bool addIfNotExist = false);

  std::vector<Param> m_params;
};

// Inlined ParameterizedObject definitions ////////////////////////////////////

template <typename T>
inline T ParameterizedObject::getParam(const std::string &name, T valIfNotFound)
{
  constexpr ANARIDataType type = anari::ANARITypeFor<T>::value;
  static_assert(!anari::isObject(type),
      "use ParameterizedObect::getParamObject() for getting objects");
  static_assert(type != ANARI_STRING && !std::is_same_v<T, std::string>,
      "use ParameterizedObject::getParamString() for getting strings");
  auto *p = findParam(name);
  return p && p->second.type() == type ? p->second.get<T>() : valIfNotFound;
}

template <typename T>
inline T *ParameterizedObject::getParamObject(const std::string &name)
{
  auto *p = findParam(name);
  return p ? p->second.getObject<T>() : nullptr;
}

} // namespace helium
