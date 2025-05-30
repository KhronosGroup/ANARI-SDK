## Copyright 2021-2025 The Khronos Group
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

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(CMAKE_BUILD_TYPE_INIT Release)

set(CMAKE_INSTALL_MESSAGE LAZY)

## Establish project

project(anari_examples LANGUAGES C CXX)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

include(GNUInstallDirs)

## Dependencies

find_package(anari REQUIRED)

if (NOT IN_SDK_SOURCE_TREE)
  add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../external/stb_image stb_image)
endif()

## Example apps

add_executable(anariTutorial anariTutorial.c)
target_link_libraries(anariTutorial PRIVATE anari::anari stb_image)

add_executable(anariTutorialCpp anariTutorial.cpp)
target_link_libraries(anariTutorialCpp PRIVATE anari::anari stb_image)

add_executable(anariTutorialDebug anariTutorialDebug.c)
target_link_libraries(anariTutorialDebug PRIVATE anari::anari stb_image)

add_executable(anariTutorialInitFromEnv anariTutorialInitFromEnv.c)
target_link_libraries(anariTutorialInitFromEnv PRIVATE anari::anari)

add_executable(anariInfo anariInfo.cpp)
target_link_libraries(anariInfo PRIVATE anari::anari_static)

option(USE_MPI "Enable MPI and Distributed Tutorial")
if (USE_MPI)
  find_package(MPI REQUIRED)
  add_executable(anariDistributedTutorial anariDistributedTutorial.cpp)
  target_link_libraries(anariDistributedTutorial PRIVATE anari::anari stb_image MPI::MPI_CXX)
endif()

if (IN_SDK_SOURCE_TREE)
  install(TARGETS anariInfo RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

  if (TARGET anari_library_helide)
    add_executable(anariTutorialDirectLink anariTutorialDirectLink.c)
    target_link_libraries(anariTutorialDirectLink
    PRIVATE
      anari_static
      # This app links to the include 'helide' device library directly to use
      # its direct device creation function. Vendors can choose to ship a header
      # with such a direct function in it.
      anari_library_helide
    )
  endif()

  if (BUILD_TESTING)
    add_test(NAME anariTutorial COMMAND anariTutorial)
    add_test(NAME anariTutorialCpp COMMAND anariTutorialCpp)
    add_test(NAME anariTutorialDebug COMMAND anariTutorialDebug)
  endif()
endif()
