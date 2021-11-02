## Copyright 2021 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

option(ANARI_USE_GENERATED_HEADERS "Use generated headers" OFF)
mark_as_advanced(ANARI_USE_GENERATED_HEADERS)

if (NOT ANARI_USE_GENERATED_HEADERS)
  return()
endif()

cmake_minimum_required(VERSION 3.12)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(JSON anari_core_1_0.json frame_continuation.json)

add_custom_command(
  OUTPUT ${CMAKE_BINARY_DIR}/include/anari_util/type_utility.h
  COMMAND ${Python3_EXECUTABLE} generate_headers.py ${CMAKE_BINARY_DIR}/include/anari_util/type_utility.h ${JSON}
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  DEPENDS ${JSON} generate_headers.py
)

add_custom_command(
  OUTPUT ${CMAKE_BINARY_DIR}/include/anari/anari.h
  COMMAND ${Python3_EXECUTABLE} generate_headers.py ${CMAKE_BINARY_DIR}/include/anari/anari.h ${JSON}
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  DEPENDS ${JSON} generate_headers.py
)

add_custom_command(
  OUTPUT ${CMAKE_BINARY_DIR}/include/anari/anari_enums.h
  COMMAND ${Python3_EXECUTABLE} generate_headers.py ${CMAKE_BINARY_DIR}/include/anari/anari_enums.h ${JSON}
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  DEPENDS ${JSON} generate_headers.py
)

add_custom_target(GeneratedHeaders DEPENDS
  ${CMAKE_BINARY_DIR}/include/anari/anari.h
  ${CMAKE_BINARY_DIR}/include/anari/anari_enums.h
  ${CMAKE_BINARY_DIR}/include/anari_util/type_utility.h
)
