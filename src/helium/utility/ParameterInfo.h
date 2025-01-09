// Copyright 2022-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "AnariAny.h"
// std
#include <string>
#include <vector>

namespace helium {

struct ParameterInfo
{
  std::string name;
  AnariAny value;
  AnariAny min;
  AnariAny max;
  std::string description;

  // valid values if this parameter is ANARI_STRING_LIST
  std::vector<std::string> stringValues;
  // which string is selected in 'stringValues', if applicable
  int currentSelection{0};
};

using ParameterInfoList = std::vector<ParameterInfo>;

template <typename T>
ParameterInfo makeParameterInfo(
    const char *name, const char *description, T value);

template <typename T>
ParameterInfo makeParameterInfo(
    const char *name, const char *description, T value, T min, T max);

template <typename T>
ParameterInfo makeParameterInfo(const char *name,
    const char *description,
    const char *value,
    std::vector<std::string> stringValues);

// Inlined definitions ////////////////////////////////////////////////////////

template <typename T>
inline ParameterInfo makeParameterInfo(
    const char *name, const char *description, T value)
{
  ParameterInfo retval;
  retval.name = name;
  retval.description = description;
  retval.value = value;
  return retval;
}

template <typename T>
inline ParameterInfo makeParameterInfo(
    const char *name, const char *description, T value, T min, T max)
{
  ParameterInfo retval;
  retval.name = name;
  retval.description = description;
  retval.value = value;
  retval.min = min;
  retval.max = max;
  return retval;
}

template <typename T>
inline ParameterInfo makeParameterInfo(const char *name,
    const char *description,
    const char *value,
    std::vector<std::string> stringValues)
{
  ParameterInfo retval;
  retval.name = name;
  retval.description = description;
  retval.value = value;
  retval.stringValues = stringValues;
  return retval;
}

} // namespace helium