## Copyright 2022-2025 The Khronos Group
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

## Core device target ##

project_add_library(SHARED)

anari_generate_queries(
  DEVICE_TARGET ${PROJECT_NAME}
  CPP_NAMESPACE helide
  JSON_DEFINITIONS_FILE ${CMAKE_CURRENT_SOURCE_DIR}/HelideDefinitions.json
)

project_sources(PRIVATE
  HelideDevice.cpp
  HelideGlobalState.cpp
  HelideLibrary.cpp
  Object.cpp
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
  scene/surface/material/PBM.cpp
  scene/surface/material/sampler/Image1D.cpp
  scene/surface/material/sampler/Image2D.cpp
  scene/surface/material/sampler/Image3D.cpp
  scene/surface/material/sampler/PrimitiveSampler.cpp
  scene/surface/material/sampler/Sampler.cpp
  scene/surface/material/sampler/TransformSampler.cpp
  scene/volume/TransferFunction1D.cpp
  scene/volume/Volume.cpp
  scene/volume/spatial_field/SpatialField.cpp
  scene/volume/spatial_field/StructuredRegularField.cpp
)

include(GenerateExportHeader)
generate_export_header(${PROJECT_NAME} EXPORT_MACRO_NAME "HELIDE_EXPORT")

project_include_directories(
PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
)

project_link_libraries(PUBLIC anari::helium PRIVATE local_embree)

if(WIN32)
  project_compile_definitions(PRIVATE _USE_MATH_DEFINES)
endif()

set_target_properties(anari_library_helide
PROPERTIES
  CXX_VISIBILITY_PRESET hidden
  C_VISIBILITY_PRESET hidden
)

## Installation ##

install(TARGETS ${PROJECT_NAME}
  EXPORT anari_Exports
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

######################################################################
# build python module
######################################################################

set(DEVICE_LIBRARY_NAME helide)
configure_file(${ANARI_CODE_GEN_ROOT}/device_trampoline.py.in ${CMAKE_CURRENT_BINARY_DIR}/__init__.py)

install(
  FILES ${CMAKE_CURRENT_BINARY_DIR}/__init__.py
  EXCLUDE_FROM_ALL
  COMPONENT helide_module
  DESTINATION helide/)
install(TARGETS anari_library_helide
  EXCLUDE_FROM_ALL
  COMPONENT helide_module
  LIBRARY DESTINATION helide
  RUNTIME DESTINATION helide)