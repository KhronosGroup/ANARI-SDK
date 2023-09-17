## Copyright 2023 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

if (NOT TARGET anari::anari)
  find_package(anari MODULE REQUIRED)
endif()

add_subdirectory(external)

project(anari_viewer)

project_add_library(INTERFACE)

project_sources(
INTERFACE
  ${CMAKE_CURRENT_LIST_DIR}/windows/LightsEditor.cpp
  ${CMAKE_CURRENT_LIST_DIR}/windows/SceneSelector.cpp
  ${CMAKE_CURRENT_LIST_DIR}/windows/Viewport.cpp
  ${CMAKE_CURRENT_LIST_DIR}/windows/Window.cpp
  ${CMAKE_CURRENT_LIST_DIR}/Application.cpp
  ${CMAKE_CURRENT_LIST_DIR}/HDRImage.cpp
  ${CMAKE_CURRENT_LIST_DIR}/Orbit.cpp
  ${CMAKE_CURRENT_LIST_DIR}/ui_anari.cpp
)
project_include_directories(INTERFACE ${CMAKE_CURRENT_LIST_DIR}/..)
project_link_libraries(INTERFACE
  anari::anari
  anari::anari_test_scenes
  anari_viewer_glad
  anari_viewer_imgui_glfw
  anari_viewer_nfd
  anari_viewer_stb_image
)

add_library(anari::anari_viewer ALIAS ${PROJECT_NAME})

## Installation ##

if (INSTALL_VIEWER_LIBRARY)
  set(SRC_INSTALL_ROOT ${CMAKE_INSTALL_DATAROOTDIR}/anari/anari_viewer)
  install(DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
    DESTINATION ${SRC_INSTALL_ROOT}/..
    FILES_MATCHING
      PATTERN *.h
      PATTERN *.c
      PATTERN *.cpp
      PATTERN CMakeLists.txt
  )
endif()
