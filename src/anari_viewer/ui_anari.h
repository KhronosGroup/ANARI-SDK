// Copyright 2023-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// imgui
#define IMGUI_DISABLE_INCLUDE_IMCONFIG_H
#include <imgui.h>
// helium
#include "helium/utility/AnariAny.h"
#include "helium/utility/ParameterInfo.h"
// std
#include <string>
#include <vector>

struct SDL_Window;

namespace anari_viewer::ui {

using Any = helium::AnariAny;
using ParameterInfo = helium::ParameterInfo;
using ParameterInfoList = helium::ParameterInfoList;

void init();
void shutdown();

ParameterInfoList parseParameters(
    anari::Device d, ANARIDataType objectType, const char *subtype);

bool buildUI(SDL_Window *w, ParameterInfo &p);
void buildUI(SDL_Window *w, anari::Device d, anari::Object o, ParameterInfo &p);

} // namespace anari_viewer::ui
