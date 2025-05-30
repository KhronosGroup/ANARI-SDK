## Copyright 2022-2025 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

project(local_embree LANGUAGES CXX)

if (APPLE AND ${CMAKE_SYSTEM_PROCESSOR} STREQUAL "arm64")
  set(COMPILE_FOR_ARM ON)
elseif (UNIX AND ${CMAKE_SYSTEM_PROCESSOR} STREQUAL "aarch64")
  set(COMPILE_FOR_ARM ON)
else()
  set(COMPILE_FOR_ARM OFF)
endif()

option(EMBREE_GEOMETRY_CURVE          "" ON)
option(EMBREE_GEOMETRY_GRID           "" OFF)
option(EMBREE_GEOMETRY_INSTANCE       "" ON)
option(EMBREE_GEOMETRY_INSTANCE_ARRAY "" ON)
option(EMBREE_GEOMETRY_POINT          "" ON)
option(EMBREE_GEOMETRY_QUAD           "" ON)
option(EMBREE_GEOMETRY_SUBDIVISION    "" OFF)
option(EMBREE_GEOMETRY_TRIANGLE       "" ON)
option(EMBREE_GEOMETRY_USER           "" OFF)
if (COMPILE_FOR_ARM)
  option(EMBREE_ISA_NEON           "" OFF)
  option(EMBREE_ISA_NEON2X         "" ON)
else()
  option(EMBREE_ISA_AVX            "" OFF)
  option(EMBREE_ISA_AVX2           "" OFF)
  option(EMBREE_ISA_AVX512         "" OFF)
  option(EMBREE_ISA_SSE42          "" ON)
  option(EMBREE_ISA_SSE2           "" OFF)
endif()
option(EMBREE_ISPC_SUPPORT             "" OFF)
option(EMBREE_STATIC_LIB               "" ON)
option(EMBREE_STAT_COUNTERS            "" OFF)
option(EMBREE_SYCL_SUPPORT             "" OFF)
option(EMBREE_TUTORIALS                "" OFF)
option(EMBREE_TUTORIALS_GLFW           "" OFF)
set(EMBREE_API_NAMESPACE "embree_for_helide" CACHE STRING "" FORCE)
set(EMBREE_TASKING_SYSTEM "INTERNAL" CACHE STRING "" FORCE)
set(EMBREE_MAX_INSTANCE_LEVEL_COUNT "1" CACHE STRING "" FORCE)

anari_sdk_fetch_project(
  NAME anari_helide_embree
  URL "https://github.com/RenderKit/embree/archive/refs/tags/v4.3.3.zip"
  MD5 a03f49d17084612dc0f28bdc36d92e89
  ADD_SUBDIR
)

project_add_library(INTERFACE)
project_link_libraries(INTERFACE embree tasking)
project_compile_definitions(INTERFACE TASKING_INTERNAL)
project_include_directories(INTERFACE ${anari_helide_embree_LOCATION}/common)
if (WIN32)
  project_compile_options(
  INTERFACE
    /D__SSE__ /D__SSE2__ /D__SSE3__ /D__SSSE3__ /D__SSE4_1__ /D__SSE4_2__
  )
elseif(COMPILE_FOR_ARM)
  project_compile_options(INTERFACE -flax-vector-conversions -fsigned-char)
endif()
