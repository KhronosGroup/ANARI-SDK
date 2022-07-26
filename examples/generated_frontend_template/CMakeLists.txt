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

project(template_device)

find_package(anari 0.1.0 REQUIRED)

# generator scripts will add dependencies to this target
add_custom_target(generate_all)

anari_generate_frontend(
  TARGET generate_template_frontend
  DEFINITIONS ${CMAKE_CURRENT_SOURCE_DIR}/device/template_device.json
  NAME tplt
  PREFIX Tplt
  NAMESPACE tplt
  DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/device
  CODE_HEADER_FILE ${CMAKE_CURRENT_LIST_DIR}/license.txt
)

add_subdirectory(device)
