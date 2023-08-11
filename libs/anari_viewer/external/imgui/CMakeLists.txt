## Copyright 2023 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

project(anari_viewer_imgui_glfw VERSION ${PROJECT_VERSION} LANGUAGES CXX)

anari_sdk_fetch_project(
  NAME ${PROJECT_NAME}
  # v1.89 docking branch
  URL https://github.com/ocornut/imgui/archive/9964740a47fda96ee937cfea272ccac85dc6a500.zip
  MD5 8294d2fbceafcda4ebddf38f4ea2d4e8
)

include(CMakeFindDependencyMacro)

set(OpenGL_GL_PREFERENCE "LEGACY")
find_dependency(OpenGL 4)
find_dependency(glfw3)

project_add_library(INTERFACE)

project_sources(
INTERFACE
  $<BUILD_INTERFACE:${anari_viewer_imgui_glfw_LOCATION}/imgui.cpp>
  $<BUILD_INTERFACE:${anari_viewer_imgui_glfw_LOCATION}/imgui_draw.cpp>
  $<BUILD_INTERFACE:${anari_viewer_imgui_glfw_LOCATION}/imgui_demo.cpp>
  $<BUILD_INTERFACE:${anari_viewer_imgui_glfw_LOCATION}/imgui_tables.cpp>
  $<BUILD_INTERFACE:${anari_viewer_imgui_glfw_LOCATION}/imgui_widgets.cpp>

  $<BUILD_INTERFACE:${anari_viewer_imgui_glfw_LOCATION}/backends/imgui_impl_glfw.cpp>
  $<BUILD_INTERFACE:${anari_viewer_imgui_glfw_LOCATION}/backends/imgui_impl_opengl2.cpp>

  $<BUILD_INTERFACE:${anari_viewer_imgui_glfw_LOCATION}/misc/cpp/imgui_stdlib.cpp>
)

project_link_libraries(INTERFACE glfw OpenGL::GL)

project_include_directories(
INTERFACE
  $<BUILD_INTERFACE:${anari_viewer_imgui_glfw_LOCATION}>
  $<BUILD_INTERFACE:${anari_viewer_imgui_glfw_LOCATION}/backends>
  $<BUILD_INTERFACE:${anari_viewer_imgui_glfw_LOCATION}/misc/cpp>
)
