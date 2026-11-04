## Copyright 2024-2026 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

## ptc: data-parallel (MPI) pass-through compositing device ##
#
# Builds as part of the SDK (BUILD_PTC_DEVICE), or on its own against an
# installed SDK found with find_package(anari).

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

project(anari_library_ptc LANGUAGES CXX)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

include(GNUInstallDirs)

# ptc reports its own version as the device's 'version' property; the wrapped
# device's version is still available as 'anariVersion.*'.
set(PTC_VERSION_MAJOR 1)
set(PTC_VERSION_MINOR 1)
set(PTC_VERSION_PATCH 0)
set(PTC_VERSION ${PTC_VERSION_MAJOR}.${PTC_VERSION_MINOR}.${PTC_VERSION_PATCH})

find_package(anari REQUIRED)
find_package(MPI REQUIRED)
# 2021.6: non-preview task_scheduler_handle / tbb::finalize
find_package(TBB 2021.6 REQUIRED)

# A MODULE: ptc is only ever loaded at runtime, through anariLoadLibrary.
project_add_library(MODULE
  CPUCompositor.cpp
  FrameWrapper.cpp
  IdCompositor.cpp
  MpiExtension.cpp
  PTCDevice.cpp
  PTCLibrary.cpp
  WrappedLibrary.cpp
)

project_link_libraries(PRIVATE
  anari::helium
  anari::anari
  MPI::MPI_CXX
  TBB::tbb
)

project_compile_definitions(PRIVATE
  PTC_VERSION_MAJOR=${PTC_VERSION_MAJOR}
  PTC_VERSION_MINOR=${PTC_VERSION_MINOR}
  PTC_VERSION_PATCH=${PTC_VERSION_PATCH}
  PTC_VERSION_STRING="${PTC_VERSION}"
)

include(GenerateExportHeader)
generate_export_header(${PROJECT_NAME} EXPORT_MACRO_NAME "PTC_EXPORT")
project_include_directories(PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

set_target_properties(${PROJECT_NAME}
PROPERTIES
  CXX_VISIBILITY_PRESET hidden
  C_VISIBILITY_PRESET hidden
)

## Installation ##

# Not in anari_Exports: as a MODULE, there is nothing to link against.

install(TARGETS ${PROJECT_NAME}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

## Tests ##

# In the SDK, the tests follow the SDK's BUILD_TESTING; built on its own, ptc
# offers the same option.
if (STANDALONE_SOURCE_TREE)
  option(BUILD_TESTING "Build the ptc MPI tests" ON)
  if (BUILD_TESTING)
    enable_testing()
  endif()
endif()

if (BUILD_TESTING)
  add_subdirectory(tests)
endif()
