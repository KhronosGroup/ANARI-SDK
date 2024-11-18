## Copyright 2024-2025 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20)

set(CMAKE_INSTALL_MESSAGE LAZY)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/cmake)

find_package(USD REQUIRED NAMES pxr)
find_package(anari REQUIRED)
if(LINUX)
  # MaterialX and USD might depend on X11 and OpenGL, but they
  # fail to declare that and directly use the related
  # targets. Try and find those packages as optional
  # so they don't actually break the build if missing.
  find_package(X11)
  find_package(OpenGL)
endif()

if(GCC OR CLANG)
  project_add_compile_options(-Wl,--no-undefined)
endif()

add_subdirectory(rd)
