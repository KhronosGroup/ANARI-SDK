## Copyright 2023-2024 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

project(anari_viewer_imgui_glfw LANGUAGES CXX)

anari_sdk_fetch_project(
  NAME ${PROJECT_NAME}
  # v1.94 docking branch
  URL https://github.com/ocornut/imgui/archive/adcc3217c38142e965eae679a299a34ce1c963d5.zip
  MD5 6c69f7df87bd0c16de7872fd01333ada
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
