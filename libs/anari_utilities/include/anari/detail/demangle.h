// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// std
#include <string>
#include <typeinfo>

// anari
#include "anari/anari.h"

namespace anari {

ANARI_INTERFACE std::string demangle(const char *name);

template <class T>
inline std::string nameOf()
{
  return demangle(typeid(T).name());
}

} // namespace anari