// Copyright 2023 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// match3D
#include <match3D/match3D.h>
// helium
#include "helium/utility/AnariAny.h"
// anari
#include "anari_test_scenes.h"
// std
#include <string>
#include <vector>

namespace ui {

void init();
void shutdown();

using Any = helium::AnariAny;
using Parameter = anari::scenes::ParameterInfo;
using ParameterList = std::vector<Parameter>;

ParameterList parseParameters(
    anari::Library l, ANARIDataType objectType, const char *subtype);

bool buildUI(Parameter &p);
void buildUI(anari::scenes::SceneHandle s, Parameter &p);
void buildUI(anari::Device d, anari::Object o, Parameter &p);

} // namespace ui
