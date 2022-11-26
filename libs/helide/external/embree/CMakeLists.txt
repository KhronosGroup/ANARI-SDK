## Copyright 2022 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

project(local_embree LANGUAGES CXX)

include(cmake/CPM.cmake)

option(EMBREE_GEOMETRY_CURVE       "" ON)
option(EMBREE_GEOMETRY_GRID        "" OFF)
option(EMBREE_GEOMETRY_INSTANCE    "" ON)
option(EMBREE_GEOMETRY_POINT       "" ON)
option(EMBREE_GEOMETRY_QUAD        "" ON)
option(EMBREE_GEOMETRY_SUBDIVISION "" OFF)
option(EMBREE_GEOMETRY_TRIANGLE    "" ON)
option(EMBREE_GEOMETRY_USER        "" OFF)
if (APPLE AND ${CMAKE_SYSTEM_PROCESSOR} STREQUAL "arm64")
  option(EMBREE_ISA_NEON           "" OFF)
  option(EMBREE_ISA_NEON2X         "" ON)
else()
  option(EMBREE_ISA_AVX            "" OFF)
  option(EMBREE_ISA_AVX2           "" OFF)
  option(EMBREE_ISA_AVX512         "" OFF)
  option(EMBREE_ISA_SSE42          "" ON)
  option(EMBREE_ISA_SSE2           "" OFF)
endif()
option(EMBREE_ISPC_SUPPORT         "" OFF)
option(EMBREE_STATIC_LIB           "" ON)
option(EMBREE_TUTORIALS            "" OFF)
option(EMBREE_TUTORIALS_GLFW       "" OFF)
set(EMBREE_TASKING_SYSTEM "INTERNAL" CACHE STRING "" FORCE)
CPMAddPackage(
  NAME embree
  URL "https://github.com/embree/embree/archive/refs/tags/v3.13.5.zip"
)

add_library(${PROJECT_NAME} INTERFACE)
target_link_libraries(${PROJECT_NAME} INTERFACE embree tasking)
target_compile_definitions(${PROJECT_NAME} INTERFACE TASKING_INTERNAL)
target_include_directories(${PROJECT_NAME}
INTERFACE
  ${embree_SOURCE_DIR}/common
)
if (WIN32)
  target_compile_options(${PROJECT_NAME}
  INTERFACE
    /D__SSE__ /D__SSE2__ /D__SSE3__ /D__SSSE3__ /D__SSE4_1__ /D__SSE4_2__
  )
endif()
