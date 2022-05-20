// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Any.h"
#include "anari/anari_cpp/Traits.h"
// stl
#include <cstring>
#include <memory>
#include <vector>

namespace anari {

constexpr int PARAM_LOCAL_STORAGE_SIZE = sizeof(float) * 3 * 4;

struct ANARI_INTERFACE ParameterizedObject
{
  ParameterizedObject() = default;
  virtual ~ParameterizedObject() = default;

  struct ANARI_INTERFACE Param
  {
    Param(const std::string &name);
    virtual ~Param() = default;

    template <typename T>
    void set(const T &v);

    void setDirect(Any v);

    std::string name;
    ANARIDataType type{ANARI_UNKNOWN};
    bool query{false};

    Any data;
    uint8_t localStorage[PARAM_LOCAL_STORAGE_SIZE];
    bool locallyStored{false};
  };

  bool hasParam(const std::string &name);

  template <typename T>
  void setParam(const std::string &name, const T &t);

  Any getParamDirect(const std::string &name);
  void setParamDirect(const std::string &name, Any v);

  template <typename T>
  T getParam(const std::string &name, T valIfNotFound);

  void removeParam(const std::string &name);

  void resetAllParamQueryStatus();

 protected:
  Param *findParam(const std::string &name, bool addIfNotExist = false);

  std::vector<std::shared_ptr<Param>>::iterator params_begin();
  std::vector<std::shared_ptr<Param>>::iterator params_end();

 private:
  // Data members //

  // NOTE - Use std::shared_ptr because copy/move of a
  //        ParameterizedObject would end up copying parameters, where
  //        destruction of each copy should only result in freeing the
  //        parameters *once*
  std::vector<std::shared_ptr<Param>> paramList;
};

// Inlined ParameterizedObject definitions ////////////////////////////////////

template <typename T>
inline void ParameterizedObject::Param::set(const T &v)
{
  data.reset();
  type = ANARI_UNKNOWN;

  if (std::is_trivially_copyable<T>::value
      && sizeof(T) <= PARAM_LOCAL_STORAGE_SIZE) {
    std::memcpy(localStorage, &v, sizeof(T));
    locallyStored = true;
    type = ANARITypeFor<T>::value;
  } else {
    data = v;
    locallyStored = false;
  }
}

inline bool ParameterizedObject::hasParam(const std::string &name)
{
  return findParam(name, false) != nullptr;
}

template <typename T>
inline void ParameterizedObject::setParam(const std::string &name, const T &t)
{
  findParam(name, true)->set(t);
}

template <typename T>
inline T ParameterizedObject::getParam(const std::string &name, T valIfNotFound)
{
  Param *param = findParam(name);

  if (!param)
    return valIfNotFound;

  const bool correctType =
      param->type == ANARITypeFor<T>::value || param->data.is<T>();

  if (!correctType)
    return valIfNotFound;

  param->query = true;

  if (param->locallyStored) {
    T retval;
    std::memcpy(&retval, param->localStorage, sizeof(T));
    return retval;
  } else if (param->data.is<T>())
    return param->data.get<T>();
  else
    return valIfNotFound;
}

inline void ParameterizedObject::resetAllParamQueryStatus()
{
  for (auto p = params_begin(); p != params_end(); ++p)
    (*p)->query = false;
}

inline std::vector<std::shared_ptr<ParameterizedObject::Param>>::iterator
ParameterizedObject::params_begin()
{
  return paramList.begin();
}

inline std::vector<std::shared_ptr<ParameterizedObject::Param>>::iterator
ParameterizedObject::params_end()
{
  return paramList.end();
}

} // namespace anari
