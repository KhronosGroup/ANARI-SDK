## Copyright 2023-2024 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

project(cat_implot LANGUAGES CXX)

anari_sdk_fetch_project(
  NAME ${PROJECT_NAME}
  URL https://github.com/epezent/implot/archive/refs/tags/v0.16.zip
  MD5 0529747ea75870e9c2f7648581a55583
)

include(CMakeFindDependencyMacro)

project_add_library(INTERFACE)

project_sources(
INTERFACE
  ${cat_implot_LOCATION}/implot.cpp
  ${cat_implot_LOCATION}/implot_demo.cpp
  ${cat_implot_LOCATION}/implot_items.cpp
)

project_link_libraries(INTERFACE anari_viewer_imgui_sdl)

project_include_directories(
INTERFACE
  ${cat_implot_LOCATION}
)
