## Copyright 2024 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.11)

if (${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_CURRENT_LIST_DIR})
  set(STANDALONE_SOURCE_TREE TRUE)
else()
  set(STANDALONE_SOURCE_TREE FALSE)
endif()

if (STANDALONE_SOURCE_TREE)
  message(STATUS "CMake version: ${CMAKE_VERSION}")
endif()

## Language setup ##

set(CMAKE_DISABLE_SOURCE_CHANGES ON)
set(CMAKE_DISABLE_IN_SOURCE_BUILD ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(CMAKE_BUILD_TYPE_INIT Release)

set(CMAKE_INSTALL_RPATH "$ORIGIN")
set(CMAKE_INSTALL_MESSAGE LAZY)

## Establish project ##

project(anari_library_hecore LANGUAGES CXX)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

include(GNUInstallDirs)

## Dependencies ##

find_package(anari REQUIRED)

## Code generation ##

anari_generate_queries(
  NAME hecore_queries
  PREFIX HeCoreDevice
  CPP_NAMESPACE hecore
  JSON_DEFINITIONS_FILE ${CMAKE_CURRENT_SOURCE_DIR}/HeCoreDefinitions.json
)
if (TARGET generate_all AND TARGET generate_hecore_queries)
  add_dependencies(generate_all generate_hecore_queries)
endif()

## Build device target ##

add_library(${PROJECT_NAME} SHARED
  HeCoreDevice.cpp
  HeCoreDeviceQueries.cpp
  HeCoreDeviceGlobalState.cpp
  HeCoreLibrary.cpp
  Object.cpp
  camera/Camera.cpp
  frame/Frame.cpp
  renderer/Renderer.cpp
  scene/Group.cpp
  scene/Instance.cpp
  scene/World.cpp
  scene/light/Light.cpp
  scene/surface/Surface.cpp
  scene/surface/geometry/Geometry.cpp
  scene/surface/material/Material.cpp
  scene/surface/material/sampler/Sampler.cpp
  scene/volume/Volume.cpp
  scene/volume/spatial_field/SpatialField.cpp
)

target_link_libraries(${PROJECT_NAME} PRIVATE anari::helium)

include(GenerateExportHeader)
generate_export_header(${PROJECT_NAME} EXPORT_MACRO_NAME "HECORE_EXPORT")

target_include_directories(${PROJECT_NAME}
PRIVATE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
)

## Installation ##

install(TARGETS ${PROJECT_NAME}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
