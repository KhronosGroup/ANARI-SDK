## Copyright 2026 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

# Builds Typhoon's material library (MaterialXCpp graphs and BSDL closures,
# vendored unmodified in src/, see PROVENANCE.md) as the static library
# halcyon_typhoon_materials. It needs C++17 and Imath, not OpenUSD.

project(halcyon_typhoon_materials LANGUAGES CXX)

set(TYPHOON_SOURCE_ROOT ${CMAKE_CURRENT_LIST_DIR}/src)
set(TYPHOON_MATERIALS_DIR ${TYPHOON_SOURCE_ROOT}/renderer/materials)

## Imath: the system's, else fetched and built statically ##

find_package(Imath 3 CONFIG QUIET)
if (NOT Imath_FOUND)
  message(STATUS "Imath not found, fetching it")
  set(IMATH_INSTALL OFF CACHE BOOL "" FORCE)
  set(IMATH_INSTALL_PKG_CONFIG OFF CACHE BOOL "" FORCE)
  set(IMATH_INSTALL_SYM_LINK OFF CACHE BOOL "" FORCE)
  set(PYTHON OFF CACHE BOOL "" FORCE)
  set(BUILD_SHARED_LIBS OFF)
  set(BUILD_TESTING OFF)
  # Imath caches CMAKE_DEBUG_POSTFIX "_d", which would rename every library
  # configured after it, the device's too (libanari_library_halcyon_d.so,
  # which ANARI can't load as 'halcyon'); drop it unless the user set one
  if (NOT DEFINED CACHE{CMAKE_DEBUG_POSTFIX})
    set(HALCYON_DROP_DEBUG_POSTFIX ON)
  endif()
  anari_sdk_fetch_project(
    NAME halcyon_imath
    URL "https://github.com/AcademySoftwareFoundation/Imath/archive/refs/tags/v3.1.12.zip"
    MD5 d4059140972da68a2b5a1287ebe5a653
    ADD_SUBDIR
  )
  # Imath's build tree exposes its headers bare (<ImathVec.h>); Typhoon
  # includes them as installed (<Imath/ImathVec.h>)
  target_include_directories(Imath INTERFACE
    $<BUILD_INTERFACE:${halcyon_imath_LOCATION}/src>)
  if (HALCYON_DROP_DEBUG_POSTFIX)
    unset(CMAKE_DEBUG_POSTFIX CACHE)
  endif()
endif()

find_package(Threads REQUIRED)

## BSDL: its own bsdl.cmake, not its CMakeLists.txt (which installs BSDL) ##

# Generates BSDL's lookup tables at build time, and the ACEScg Jakob-Hanika
# spectral table that MaterialXCpp's spectral path uses, as hdEmbree does
include(${TYPHOON_MATERIALS_DIR}/BSDL/bsdl.cmake)
add_bsdl_library(halcyon_bsdl
  SUBDIR src/renderer/materials/BSDL
  SPECTRAL_COLOR_SPACES ACEScg
)

## MaterialXCpp ##

# Globbed so that a pin move picks up added and removed files; tests/ and the
# lut/ table generator have their own main()s
file(GLOB_RECURSE TYPHOON_MATERIAL_SOURCES CONFIGURE_DEPENDS
  ${TYPHOON_MATERIALS_DIR}/MaterialXCpp/*.cpp
)
list(FILTER TYPHOON_MATERIAL_SOURCES EXCLUDE REGEX "/MaterialXCpp/(tests|lut)/")

add_library(${PROJECT_NAME} STATIC
  ${TYPHOON_MATERIAL_SOURCES}
  ${TYPHOON_SOURCE_ROOT}/renderer/integrator/medium.cpp
  ${halcyon_bsdl_LUTS_CPP}
)

# Upstream includes are rooted at hdEmbree's source directory (<renderer/...>)
target_include_directories(${PROJECT_NAME} PUBLIC ${TYPHOON_SOURCE_ROOT})
target_link_libraries(${PROJECT_NAME} PUBLIC halcyon_bsdl Imath::Imath)
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_17)

# Keep Imath's options out of the way in CMake GUIs
get_cmake_property(cacheVariables CACHE_VARIABLES)
foreach (var ${cacheVariables})
  if (var MATCHES "^IMATH_")
    mark_as_advanced(${var})
  endif()
endforeach()
