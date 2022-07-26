## Copyright 2021 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.11)

message(STATUS "CMake version: ${CMAKE_VERSION}")

if (${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_CURRENT_SOURCE_DIR})
  set(IN_SDK_SOURCE_TREE FALSE)
else()
  set(IN_SDK_SOURCE_TREE TRUE)
endif()

## Language setup

set(CMAKE_DISABLE_SOURCE_CHANGES ON)
set(CMAKE_DISABLE_IN_SOURCE_BUILD ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(CMAKE_BUILD_TYPE_INIT Release)

set(CMAKE_INSTALL_RPATH "$ORIGIN")
set(CMAKE_INSTALL_MESSAGE LAZY)

## Establish project

project(anari_library_example LANGUAGES CXX)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

include(GNUInstallDirs)

## Dependencies

set(glm_DIR ${CMAKE_CURRENT_LIST_DIR}/../../external/glm/lib/cmake/glm)
find_package(glm REQUIRED)
find_package(anari REQUIRED)

mark_as_advanced(glm_DIR)

if(APPLE)
  if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    set(OpenMP_C_FLAGS "-Xpreprocessor -fopenmp")
    set(OpenMP_C_LIB_NAMES "omp")
  endif()
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(OpenMP_CXX_FLAGS "-Xpreprocessor -fopenmp")
    set(OpenMP_CXX_LIB_NAMES "omp")
  endif()
endif()

if (NOT WIN32)
  find_package(OpenMP)
endif()

if(EXISTS ${CMAKE_SOURCE_DIR}/.git AND IS_DIRECTORY ${CMAKE_SOURCE_DIR}/.git)
  find_package(Git REQUIRED)
  execute_process(
    COMMAND ${GIT_EXECUTABLE} rev-list --count HEAD
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE "REFERENCE_DEVICE_VERSION"
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
else()
  set(REFERENCE_DEVICE_VERSION -1)
endif()

## Code generation

if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.12")
  find_package(Python3 REQUIRED COMPONENTS Interpreter)

  add_custom_target(generate_example_device
    COMMAND ${Python3_EXECUTABLE} ${ANARI_CODE_GEN_ROOT}/generate_queries.py
      --json ${ANARI_CODE_GEN_ROOT}
      --prefix ExampleDevice
      --device ${CMAKE_CURRENT_SOURCE_DIR}/example_device.json
      --namespace anari::example_device
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    DEPENDS example_device.json
  )

  add_custom_target(generate_example_debug_objects
    COMMAND ${Python3_EXECUTABLE} ${ANARI_CODE_GEN_ROOT}/generate_debug_objects.py
      --json ${ANARI_CODE_GEN_ROOT}
      --device ${CMAKE_CURRENT_SOURCE_DIR}/example_device.json
      --prefix ExampleDevice
      --namespace anari::example_device
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    DEPENDS example_device.json
  )

  if (IN_SDK_SOURCE_TREE)
    add_dependencies(generate_all generate_example_device)
    add_dependencies(generate_all generate_example_debug_objects)
  endif()
endif()

## Core device target

add_library(${PROJECT_NAME} SHARED
  glm_extras.cpp

  Array.cpp
  Object.cpp
  ExampleDevice.cpp

  ExampleDeviceQueries.cpp
  ExampleDeviceDebugFactory.cpp

  camera/Camera.cpp
  camera/Orthographic.cpp
  camera/Perspective.cpp

  frame/Frame.cpp

  material/Material.cpp
  material/Matte.cpp

  renderer/AmbientOcclusion.cpp
  renderer/Debug.cpp
  renderer/Raycast.cpp
  renderer/RayDir.cpp
  renderer/Renderer.cpp

  scene/Group.cpp
  scene/Instance.cpp
  scene/World.cpp

  scene/geometry/Geometry.cpp
  scene/geometry/Spheres.cpp
  scene/geometry/Surface.cpp
  scene/geometry/Triangles.cpp

  scene/volume/StructuredRegularField.cpp
  scene/volume/SpatialField.cpp
  scene/volume/Volume.cpp
)

target_include_directories(${PROJECT_NAME}
PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}>
)

target_link_libraries(${PROJECT_NAME}
PUBLIC
  anari::anari
  anari::anari_utilities
  anari::anari_library_debug
PRIVATE
  glm::glm
)

if (NOT WIN32 AND TARGET OpenMP::OpenMP_CXX)
  target_link_libraries(${PROJECT_NAME} PRIVATE OpenMP::OpenMP_CXX)
endif()

target_compile_definitions(${PROJECT_NAME}
PRIVATE
  -DDEVICE_VERSION=${REFERENCE_DEVICE_VERSION}
  "anari_library_example_EXPORTS"
)

install(TARGETS ${PROJECT_NAME}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/include/anari
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
