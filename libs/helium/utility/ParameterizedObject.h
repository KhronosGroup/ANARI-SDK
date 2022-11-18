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

  bool hasParam(const std::string &name);

  void setParam(const std::string &name, ANARIDataType type, const void *v);

  template <typename T>
  T getParam(const std::string &name, T valIfNotFound);

  template <typename T>
  T *getParamObject(const std::string &name);

  std::string getParamString(
      const std::string &name, const std::string &valIfNotFound);

  AnariAny getParamDirect(const std::string &name);
  void setParamDirect(const std::string &name, const AnariAny &v);

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
