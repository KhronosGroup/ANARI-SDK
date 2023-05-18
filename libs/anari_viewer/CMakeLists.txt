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
find_package(match3D REQUIRED stb_image nativefiledialog)

## Create viewer library target ##

project(anari_viewer)

project_add_library(INTERFACE)
project_sources(
INTERFACE
  ${CMAKE_CURRENT_LIST_DIR}/windows/LightsEditor.cpp
  ${CMAKE_CURRENT_LIST_DIR}/windows/SceneSelector.cpp
  ${CMAKE_CURRENT_LIST_DIR}/windows/Viewport.cpp
  ${CMAKE_CURRENT_LIST_DIR}/Orbit.cpp
  ${CMAKE_CURRENT_LIST_DIR}/ui_anari.cpp
)
project_include_directories(INTERFACE ${CMAKE_CURRENT_LIST_DIR})
project_link_libraries(INTERFACE
  anari
  anari_test_scenes
  match3D::match3D
)
