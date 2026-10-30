## Copyright 2022-2026 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

# Builds Embree from source as a static library. Consumers link anari_embree,
# which also brings in Embree's internal tasking system (used by parallel_for()
# loops) and its headers.
#
# helide and halcyon each carry this same file (src/devices/*/external/embree),
# so that either builds alone; keep the copies identical. In the SDK's tree
# Embree's own targets can be added only once, so the first device to get here
# builds it and the other links the same anari_embree.

if (TARGET anari_embree)
  return()
endif()

project(anari_embree LANGUAGES CXX)

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
# Embree picks the best enabled ISA at runtime; ARM builds a single one
if (COMPILE_FOR_ARM)
  option(EMBREE_ISA_NEON           "" OFF)
  option(EMBREE_ISA_NEON2X         "" ON)
else()
  option(EMBREE_ISA_SSE2           "" ON)
  option(EMBREE_ISA_SSE42          "" ON)
  option(EMBREE_ISA_AVX            "" ON)
  option(EMBREE_ISA_AVX2           "" ON)
  # Embree itself leaves AVX512 off by default on Windows and macOS
  if (WIN32 OR APPLE)
    option(EMBREE_ISA_AVX512       "" OFF)
  else()
    option(EMBREE_ISA_AVX512       "" ON)
  endif()
endif()
option(EMBREE_ISPC_SUPPORT             "" OFF)
option(EMBREE_STATIC_LIB               "" ON)
option(EMBREE_STAT_COUNTERS            "" OFF)
option(EMBREE_SYCL_SUPPORT             "" OFF)
option(EMBREE_TUTORIALS                "" OFF)
option(EMBREE_TUTORIALS_GLFW           "" OFF)
# No EMBREE_API_NAMESPACE: halcyon's ISPC shading code calls the C API, which a
# namespace would turn into C++ symbols. The static library's symbols have
# hidden visibility instead, so they don't clash with another Embree in the
# same process (see --exclude-libs below).
set(EMBREE_TASKING_SYSTEM "INTERNAL" CACHE STRING "" FORCE)
set(EMBREE_MAX_INSTANCE_LEVEL_COUNT "1" CACHE STRING "" FORCE)

anari_sdk_fetch_project(
  NAME anari_embree
  URL "https://github.com/RenderKit/embree/archive/refs/tags/v4.4.1.zip"
  MD5 6e2eecafb312d8cf1f1ff555702637cf
  ADD_SUBDIR
)

project_add_library(INTERFACE)
project_link_libraries(INTERFACE embree tasking)
project_compile_definitions(INTERFACE TASKING_INTERNAL)
project_include_directories(INTERFACE ${anari_embree_LOCATION}/common)
if (WIN32)
  project_compile_options(
  INTERFACE
    /D__SSE__ /D__SSE2__ /D__SSE3__ /D__SSSE3__ /D__SSE4_1__ /D__SSE4_2__
  )
elseif(COMPILE_FOR_ARM)
  project_compile_options(INTERFACE
    $<$<COMPILE_LANGUAGE:CXX>:-flax-vector-conversions>
    $<$<COMPILE_LANGUAGE:CXX>:-fsigned-char>
  )
endif()

# Embree's tasking library exports its TaskScheduler; keep it out of the
# device's dynamic symbols, so another Embree loaded in the same process can't
# interpose it
if (UNIX AND NOT APPLE)
  target_link_options(${PROJECT_NAME}
    INTERFACE "LINKER:--exclude-libs,$<TARGET_LINKER_FILE_NAME:tasking>")
endif()

# Keep Embree's and FetchContent's options out of the way in CMake GUIs
get_cmake_property(cacheVariables CACHE_VARIABLES)
foreach (var ${cacheVariables})
  if (var MATCHES "^(EMBREE|FETCHCONTENT)_")
    mark_as_advanced(${var})
  endif()
endforeach()
