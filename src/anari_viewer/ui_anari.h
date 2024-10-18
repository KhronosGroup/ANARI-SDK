// Copyright 2023-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// imgui
#define IMGUI_DISABLE_INCLUDE_IMCONFIG_H
#include <imgui.h>
// helium
#include "helium/utility/AnariAny.h"
// anari
#include "anari_test_scenes.h"
// std
#include <string>
#include <vector>

namespace anari_viewer::ui {

void init();
void shutdown();

using Any = helium::AnariAny;
using Parameter = anari::scenes::ParameterInfo;
using ParameterList = std::vector<Parameter>;

ParameterList parseParameters(
    anari::Device d, ANARIDataType objectType, const char *subtype);

bool buildUI(Parameter &p);
void buildUI(anari::scenes::SceneHandle s, Parameter &p);
void buildUI(anari::Device d, anari::Object o, Parameter &p);

} // namespace anari_viewer::ui
