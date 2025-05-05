## Copyright 2023-2025 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

project(anari_viewer_imgui_sdl LANGUAGES CXX)

anari_sdk_fetch_project(
  NAME ${PROJECT_NAME}
  URL https://github.com/ocornut/imgui/archive/refs/tags/v1.91.7-docking.zip
  MD5 2cfbf7b7790076d6debe5060ec1fb47f
)

include(CMakeFindDependencyMacro)

find_dependency(SDL3 REQUIRED)

project_add_library(INTERFACE)

project_sources(
INTERFACE
  ${anari_viewer_imgui_sdl_LOCATION}/imgui.cpp
  ${anari_viewer_imgui_sdl_LOCATION}/imgui_draw.cpp
  ${anari_viewer_imgui_sdl_LOCATION}/imgui_demo.cpp
  ${anari_viewer_imgui_sdl_LOCATION}/imgui_tables.cpp
  ${anari_viewer_imgui_sdl_LOCATION}/imgui_widgets.cpp
  ${anari_viewer_imgui_sdl_LOCATION}/backends/imgui_impl_sdl3.cpp
  ${anari_viewer_imgui_sdl_LOCATION}/backends/imgui_impl_sdlrenderer3.cpp
  ${anari_viewer_imgui_sdl_LOCATION}/misc/cpp/imgui_stdlib.cpp
)

project_link_libraries(INTERFACE SDL3::SDL3)

project_include_directories(
INTERFACE
  ${anari_viewer_imgui_sdl_LOCATION}
  ${anari_viewer_imgui_sdl_LOCATION}/backends
  ${anari_viewer_imgui_sdl_LOCATION}/misc/cpp
)
