## Copyright 2026 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.17)
project(hecore_python_package LANGUAGES CXX)

execute_process(
  COMMAND ${PY_BUILD_CMAKE_BUILD_PYTHON_INTERPRETER} -m anari --cmake-dir
  OUTPUT_VARIABLE anari_DIR
  OUTPUT_STRIP_TRAILING_WHITESPACE
  RESULT_VARIABLE result
)
if (NOT result EQUAL 0)
  message(FATAL_ERROR "the 'anari' Python package was not found")
endif()
find_package(anari REQUIRED)

add_subdirectory(hecore)

set(DEVICE_LIBRARY_NAME hecore)
configure_file(${ANARI_CODE_GEN_ROOT}/device_trampoline.py.in
  ${CMAKE_CURRENT_BINARY_DIR}/__init__.py)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/__init__.py
  COMPONENT hecore_module DESTINATION hecore)
install(TARGETS anari_library_hecore
  COMPONENT hecore_module LIBRARY DESTINATION hecore RUNTIME DESTINATION hecore)
