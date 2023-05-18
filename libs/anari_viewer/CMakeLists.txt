## Copyright 2023 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

## Get match3D base viewer library ##

include(FetchContent)

set(match3D_LOC ${CMAKE_BINARY_DIR}/match3D_src)

FetchContent_Populate(match3D_src
  URL https://github.com/jeffamstutz/match3D/archive/refs/heads/main.zip
  SOURCE_DIR ${match3D_LOC}
)
set(match3D_DIR ${match3D_LOC}/cmake)
mark_as_advanced(
  FETCHCONTENT_BASE_DIR
  FETCHCONTENT_FULLY_DISCONNECTED
  FETCHCONTENT_QUIET
  FETCHCONTENT_UPDATES_DISCONNECTED
)

set(match3D_CACHE_IN_SOURCE_TREE ON)
find_package(match3D REQUIRED stb_image nativefiledialog imnodes)

## Create viewer library target ##

project(anari_viewer)

project_add_library(INTERFACE)

set(SRC_INSTALL_ROOT ${CMAKE_INSTALL_DATAROOTDIR}/anari/anari_viewer)
project_sources(
INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/windows/LightsEditor.cpp>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/windows/SceneSelector.cpp>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/windows/Viewport.cpp>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/Orbit.cpp>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/ui_anari.cpp>
  $<INSTALL_INTERFACE:${SRC_INSTALL_ROOT}/windows/LightsEditor.cpp>
  $<INSTALL_INTERFACE:${SRC_INSTALL_ROOT}/windows/SceneSelector.cpp>
  $<INSTALL_INTERFACE:${SRC_INSTALL_ROOT}/windows/Viewport.cpp>
  $<INSTALL_INTERFACE:${SRC_INSTALL_ROOT}/Orbit.cpp>
  $<INSTALL_INTERFACE:${SRC_INSTALL_ROOT}/ui_anari.cpp>
)
project_include_directories(
INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/..>
  $<INSTALL_INTERFACE:${SRC_INSTALL_ROOT}/..>
)
project_link_libraries(INTERFACE
  $<BUILD_INTERFACE:anari>
  $<BUILD_INTERFACE:anari_test_scenes>
  $<BUILD_INTERFACE:match3D::match3D>
  $<INSTALL_INTERFACE:anari::anari>
  $<INSTALL_INTERFACE:anari::anari_test_scenes>
)

## Installation ##

install(TARGETS ${PROJECT_NAME} EXPORT anari_Exports)

install(DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
  DESTINATION ${SRC_INSTALL_ROOT}/..
  FILES_MATCHING
    PATTERN *.h
    PATTERN *.cpp
)

install(DIRECTORY ${match3D_LOC} DESTINATION ${SRC_INSTALL_ROOT})
