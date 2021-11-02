// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// std
#include <string>
#include <typeinfo>

namespace anari {

std::string demangle(const char *name);

template <class T>
inline std::string nameOf()
{
  return demangle(typeid(T).name());
}

} // namespace anari