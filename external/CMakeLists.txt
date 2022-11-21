## Copyright 2021 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

include(FetchContent)

function(fetch_project)
  cmake_parse_arguments(FETCH_SOURCE "" "NAME;URL;DESTINATION" "" ${ARGN})

  FetchContent_Populate(${FETCH_SOURCE_NAME}
    URL ${FETCH_SOURCE_URL}
    SOURCE_DIR ${CMAKE_BINARY_DIR}/${FETCH_SOURCE_DESTINATION}
  )

  set(
    "${FETCH_SOURCE_NAME}_LOCATION"
    ${CMAKE_BINARY_DIR}/${FETCH_SOURCE_DESTINATION}
    PARENT_SCOPE
  )
endfunction()

add_subdirectory(stb_image)

if (NOT BUILD_VIEWER)
  return()
endif()

add_subdirectory(glad)

## imgui ##

fetch_project(
  NAME imgui_src
  URL https://github.com/ocornut/imgui/archive/refs/tags/v1.82.zip
  DESTINATION imgui
)

set(OpenGL_GL_PREFERENCE "LEGACY")
find_package(OpenGL 4 REQUIRED)
find_package(glfw3 REQUIRED)

set(IMGUI_SOURCES
  ${imgui_src_LOCATION}/imgui.cpp
  ${imgui_src_LOCATION}/imgui_draw.cpp
  ${imgui_src_LOCATION}/imgui_demo.cpp
  ${imgui_src_LOCATION}/imgui_tables.cpp
  ${imgui_src_LOCATION}/imgui_widgets.cpp

  ${imgui_src_LOCATION}/backends/imgui_impl_glfw.cpp
  ${imgui_src_LOCATION}/backends/imgui_impl_opengl3.cpp

  ${imgui_src_LOCATION}/misc/cpp/imgui_stdlib.cpp
)

add_library(imgui_glfw OBJECT
  ${IMGUI_SOURCES}
)

target_link_libraries(imgui_glfw PUBLIC glfw OpenGL::GL glad_gl)
target_compile_definitions(imgui_glfw PUBLIC IMGUI_IMPL_OPENGL_LOADER_GLAD2)

target_include_directories(imgui_glfw
PUBLIC
  ${imgui_src_LOCATION}
  ${imgui_src_LOCATION}/backends
  ${imgui_src_LOCATION}/misc/cpp
)

add_library(imgui_glfw_gles2 OBJECT
  ${IMGUI_SOURCES}
)

target_link_libraries(imgui_glfw_gles2 PUBLIC glfw OpenGL::GL glad_gles2)
target_compile_definitions(imgui_glfw_gles2 PUBLIC IMGUI_IMPL_OPENGL_LOADER_GLAD2 IMGUI_IMPL_OPENGL_ES3)

target_include_directories(imgui_glfw_gles2
PUBLIC
  ${imgui_src_LOCATION}
  ${imgui_src_LOCATION}/backends
  ${imgui_src_LOCATION}/misc/cpp
)
