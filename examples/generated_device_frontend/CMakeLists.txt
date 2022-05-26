## Copyright 2021 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.12)

set(CMAKE_DISABLE_SOURCE_CHANGES ON)
set(CMAKE_DISABLE_IN_SOURCE_BUILD ON)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_BUILD_TYPE_INIT "RelWithDebInfo")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

project(generate_device_frontend_example)

# Normally would do a find_package(anari) here when outside the ANARI-SDK tree

anari_generate_frontend(
  TARGET generate_tree_device
  PREFIX Tree
  NAMESPACE tree
  DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/device
  CODE_HEADER_FILE ${CMAKE_CURRENT_LIST_DIR}/license.txt
)

add_subdirectory(device)
