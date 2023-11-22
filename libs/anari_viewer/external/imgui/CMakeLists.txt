## Copyright 2023 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

project(anari_viewer_imgui_glfw LANGUAGES CXX)

anari_sdk_fetch_project(
  NAME ${PROJECT_NAME}
  # v1.90 docking branch
  URL https://github.com/ocornut/imgui/archive/ce0d0ac8298ce164b5d862577e8b087d92f6e90e.zip
  MD5 40f838f4235dfc17a9ab5b3b96b33c6c
)

include(CMakeFindDependencyMacro)

set(OpenGL_GL_PREFERENCE "LEGACY")
find_dependency(OpenGL 4 REQUIRED)
find_dependency(glfw3 REQUIRED)

project_add_library(INTERFACE)

project_sources(
INTERFACE
  ${anari_viewer_imgui_glfw_LOCATION}/imgui.cpp
  ${anari_viewer_imgui_glfw_LOCATION}/imgui_draw.cpp
  ${anari_viewer_imgui_glfw_LOCATION}/imgui_demo.cpp
  ${anari_viewer_imgui_glfw_LOCATION}/imgui_tables.cpp
  ${anari_viewer_imgui_glfw_LOCATION}/imgui_widgets.cpp
  ${anari_viewer_imgui_glfw_LOCATION}/backends/imgui_impl_glfw.cpp
  ${anari_viewer_imgui_glfw_LOCATION}/backends/imgui_impl_opengl2.cpp
  ${anari_viewer_imgui_glfw_LOCATION}/misc/cpp/imgui_stdlib.cpp
)

project_link_libraries(INTERFACE glfw OpenGL::GL)

project_include_directories(
INTERFACE
  ${anari_viewer_imgui_glfw_LOCATION}
  ${anari_viewer_imgui_glfw_LOCATION}/backends
  ${anari_viewer_imgui_glfw_LOCATION}/misc/cpp
)
