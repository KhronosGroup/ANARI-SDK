// Copyright 2023 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// match3D
#include <match3D/match3D.h>
// helium
#include "helium/utility/AnariAny.h"
// std
#include <string>
#include <vector>

namespace ui {

using Any = helium::AnariAny;

struct Parameter
{
  std::string name;
  Any value;
  std::string description;
};

using ParameterList = std::vector<Parameter>;

ParameterList parseParameters(
    anari::Library l, ANARIDataType objectType, const char *subtype);

void buildUI(anari::Device d, anari::Object o, Parameter &p);

} // namespace ui
