## Copyright 2022 The Khronos Group
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

project(anari_library_helide LANGUAGES CXX)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

include(GNUInstallDirs)

## Dependencies ##

find_package(anari REQUIRED)

add_subdirectory(external/embree EXCLUDE_FROM_ALL)

## Code generation ##

if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.12")
  find_package(Python3 REQUIRED COMPONENTS Interpreter)

  add_custom_target(generate_helide_device
    COMMAND ${Python3_EXECUTABLE} ${ANARI_CODE_GEN_ROOT}/generate_queries.py
      --json ${ANARI_CODE_GEN_ROOT}
      --prefix HelideDevice
      --device ${CMAKE_CURRENT_SOURCE_DIR}/helide_device.json
      --namespace helide
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    DEPENDS helide_device.json
  )

  add_custom_target(generate_helide_debug_objects
    COMMAND ${Python3_EXECUTABLE} ${ANARI_CODE_GEN_ROOT}/generate_debug_objects.py
      --json ${ANARI_CODE_GEN_ROOT}
      --device ${CMAKE_CURRENT_SOURCE_DIR}/helide_device.json
      --prefix HelideDevice
      --namespace helide
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    DEPENDS helide_device.json
  )

  if (TARGET generate_all)
    add_dependencies(generate_all generate_helide_device)
    add_dependencies(generate_all generate_helide_debug_objects)
  endif()
endif()

## Core device target ##

add_library(${PROJECT_NAME} SHARED)

target_sources(${PROJECT_NAME} PRIVATE
  HelideDevice.cpp
  HelideDeviceQueries.cpp
  HelideDeviceDebugFactory.cpp
  HelideGlobalState.cpp
  Object.cpp
  array/Array.cpp
  array/Array1D.cpp
  array/Array2D.cpp
  array/Array3D.cpp
  array/ArraySampler1D.cpp
  array/ObjectArray.cpp
  camera/Camera.cpp
  camera/Orthographic.cpp
  camera/Perspective.cpp
  frame/Frame.cpp
  renderer/Renderer.cpp
  scene/Group.cpp
  scene/Instance.cpp
  scene/World.cpp
  scene/light/Light.cpp
  scene/surface/Surface.cpp
  scene/surface/geometry/Cone.cpp
  scene/surface/geometry/Curve.cpp
  scene/surface/geometry/Cylinder.cpp
  scene/surface/geometry/Geometry.cpp
  scene/surface/geometry/Quad.cpp
  scene/surface/geometry/Sphere.cpp
  scene/surface/geometry/Triangle.cpp
  scene/surface/material/Material.cpp
  scene/surface/material/Matte.cpp
  scene/surface/material/TransparentMatte.cpp
  scene/surface/material/sampler/Sampler.cpp
  scene/volume/Volume.cpp
  scene/volume/SciVisVolume.cpp
  scene/volume/spatial_field/SpatialField.cpp
  scene/volume/spatial_field/StructuredRegularField.cpp
)

include(GenerateExportHeader)
generate_export_header(${PROJECT_NAME}
  EXPORT_MACRO_NAME "HELIDE_DEVICE_INTERFACE"
)

target_include_directories(${PROJECT_NAME}
PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
)

target_link_libraries(${PROJECT_NAME}
PUBLIC
  anari::anari
  anari::anari_utilities
  anari::anari_library_debug
  helium
PRIVATE
  local_embree
)

if(WIN32)
  target_compile_definitions(${PROJECT_NAME} PRIVATE _USE_MATH_DEFINES)
endif()

## Installation ##

install(TARGETS ${PROJECT_NAME}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(
FILES
  ${PROJECT_BINARY_DIR}/${PROJECT_NAME}_export.h
  ${CMAKE_CURRENT_LIST_DIR}/include/anari/ext/helide/anariNewHelideDevice.h
DESTINATION
  ${CMAKE_INSTALL_INCLUDEDIR}/anari/ext/helide
)
