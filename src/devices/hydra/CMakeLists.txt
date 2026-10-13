## Copyright 2026 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20)

project(anari_library_hydra LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_INSTALL_RPATH "$ORIGIN")

# Dependencies: ANARI-SDK (helium + code-gen) and OpenUSD (Hydra). Point
# CMAKE_PREFIX_PATH (or anari_DIR / pxr_DIR) at their install prefixes.
find_package(anari REQUIRED)
find_package(pxr REQUIRED)
find_package(OpenGL REQUIRED)

# anari_generate_queries (below) comes from the SDK: in its source tree from
# the in-tree code_gen, standalone only from an SDK installed with
# INSTALL_CODE_GEN_SCRIPTS=ON.
if(NOT COMMAND anari_generate_queries)
  message(FATAL_ERROR "The hydra device needs the ANARI-SDK code generator "
    "(anari_generate_queries): install the SDK with INSTALL_CODE_GEN_SCRIPTS=ON "
    "(found anari at ${anari_DIR})")
endif()

add_library(anari_library_hydra SHARED
  DelegateRegistry.cpp
  HydraDevice.cpp
  HydraGlobalState.cpp
  HydraLibrary.cpp
  Introspection.cpp
  Object.cpp

  hydra/Worker.cpp

  scene/Mirror.cpp
  scene/PrimBuilder.cpp
  scene/MaterialBuilder.cpp
  scene/InstancerBuilder.cpp
  scene/LightBuilder.cpp
  scene/AnariImage.cpp
  scene/AnariArResolver.cpp
  scene/AnariField.cpp
  scene/VolumeBuilder.cpp

  light/Light.cpp

  sampler/Sampler.cpp

  spatial_field/SpatialField.cpp

  volume/Volume.cpp

  geometry/Geometry.cpp
  geometry/Triangle.cpp
  geometry/Quad.cpp
  geometry/Sphere.cpp
  geometry/Curve.cpp
  geometry/Cone.cpp
  geometry/Cylinder.cpp
  geometry/Isosurface.cpp
  geometry/TubeMesh.cpp

  material/Material.cpp

  surface/Surface.cpp

  world/Group.cpp
  world/Instance.cpp
  world/World.cpp

  camera/Camera.cpp

  renderer/Renderer.cpp

  frame/Frame.cpp
)

# Object/parameter introspection tables: the ANARI-SDK query generator resolves
# the device root (introspection/hydra_device.json) against the SDK's own
# shipped KHR schema files (share/anari/code_gen/api, vanilla) plus the
# bridge-only parameters in introspection/*.json, and emits
# anari_library_hydra_queries.{h,cpp}; Introspection.cpp gates the result on the
# bound delegate's extension list at query time. hydra_device.json must list
# hydra_bridge_parameters after the KHR files: the SDK merge keeps the
# first-seen entry per parameter name (README.md "Object/parameter
# introspection").
file(GLOB _anari_hydra_introspection_jsons CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/introspection/*.json)
anari_generate_queries(
  DEVICE_TARGET anari_library_hydra
  CPP_NAMESPACE anari_hydra
  JSON_ROOT_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/introspection
  JSON_DEFINITIONS_FILE ${CMAKE_CURRENT_SOURCE_DIR}/introspection/hydra_device.json
  JSON_EXTENSION_FILES "${_anari_hydra_introspection_jsons}"
)

# NanoVDB (header-only) backs the `nanovdb` spatial field and
# KHR_SPATIAL_FIELD_NANOVDB. By default the headers are downloaded at configure
# time from a pinned OpenVDB release (the SDK's anari_sdk_fetch_project, as
# helide does for embree); set ANARI_HYDRA_NANOVDB_INCLUDE_DIR to use local
# headers instead (offline builds), or turn ANARI_HYDRA_ENABLE_NANOVDB off to
# drop the field type. The anari_hydra_nanovdb interface target carries the
# headers to the tests.
option(ANARI_HYDRA_ENABLE_NANOVDB
  "Support 'nanovdb' spatial fields (needs the header-only NanoVDB)" ON)
set(ANARI_HYDRA_NANOVDB_INCLUDE_DIR "" CACHE PATH
  "Directory containing nanovdb/NanoVDB.h (empty: download OpenVDB v13.0.0)")
if(NOT ANARI_HYDRA_ENABLE_NANOVDB)
  message(STATUS "hydra device: NanoVDB: disabled (ANARI_HYDRA_ENABLE_NANOVDB=OFF)")
else()
  if(ANARI_HYDRA_NANOVDB_INCLUDE_DIR)
    if(NOT EXISTS "${ANARI_HYDRA_NANOVDB_INCLUDE_DIR}/nanovdb/NanoVDB.h")
      message(FATAL_ERROR "hydra device: ANARI_HYDRA_NANOVDB_INCLUDE_DIR="
        "${ANARI_HYDRA_NANOVDB_INCLUDE_DIR} has no nanovdb/NanoVDB.h")
    endif()
    set(_anari_hydra_nanovdb_include ${ANARI_HYDRA_NANOVDB_INCLUDE_DIR})
    set(_anari_hydra_nanovdb_source "ANARI_HYDRA_NANOVDB_INCLUDE_DIR")
  else()
    anari_sdk_fetch_project(
      NAME anari_hydra_openvdb
      URL "https://github.com/AcademySoftwareFoundation/openvdb/archive/refs/tags/v13.0.0.zip"
      MD5 9c58b986991c3e05f2e59049e24b02bc
    )
    set(_anari_hydra_nanovdb_include ${anari_hydra_openvdb_LOCATION}/nanovdb)
    set(_anari_hydra_nanovdb_source "downloaded OpenVDB v13.0.0")
  endif()
  message(STATUS "hydra device: NanoVDB: enabled (${_anari_hydra_nanovdb_source}: "
    "${_anari_hydra_nanovdb_include})")
  add_library(anari_hydra_nanovdb INTERFACE)
  target_include_directories(anari_hydra_nanovdb SYSTEM INTERFACE
    ${_anari_hydra_nanovdb_include})
  target_compile_definitions(anari_hydra_nanovdb INTERFACE
    ANARI_HYDRA_HAS_NANOVDB)
  target_link_libraries(anari_library_hydra PRIVATE anari_hydra_nanovdb)
endif()

include(GenerateExportHeader)
generate_export_header(anari_library_hydra
  EXPORT_MACRO_NAME HYDRA_EXPORT
)

# OpenUSD still includes <ext/hash_set> internally; silence the noise.
target_compile_options(anari_library_hydra PRIVATE -Wno-deprecated)

# USD tags TF_REGISTRY_FUNCTIONs (e.g. the AnariHioImage factory) by
# MFB_ALT_PACKAGE_NAME; without it the registration is never associated with a
# processable library and the plugin type is left factory-less (proto 013).
target_compile_definitions(anari_library_hydra PRIVATE
  MFB_PACKAGE_NAME=anariHydra
  MFB_ALT_PACKAGE_NAME=anariHydra
  MFB_PACKAGE_MODULE=AnariHydra)

# The Hio image plugin's plugInfo.json ships next to the device library so
# EnsureAnariHioRegistered() finds it via the library's own path.
add_custom_command(TARGET anari_library_hydra POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_CURRENT_SOURCE_DIR}/plugInfo.json
    $<TARGET_FILE_DIR:anari_library_hydra>/plugInfo.json)

target_include_directories(anari_library_hydra
PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(anari_library_hydra
PRIVATE
  anari::helium
  # OpenUSD imaging + base (the Hydra library subset + hdx/Storm ride-along).
  hd hdx hdSt hgi hgiGL hf hio sdf sdr ar plug
  glf garch cameraUtil pxOsd
  tf gf vt trace work
  OpenGL::GL
)

set_target_properties(anari_library_hydra PROPERTIES
  CXX_VISIBILITY_PRESET hidden
  C_VISIBILITY_PRESET hidden
)

## Installation — the device target only ##

include(GNUInstallDirs)

install(TARGETS anari_library_hydra
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/anari_library_hydra_export.h
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/anari/ext/hydra
)

# The Hio plugin's plugInfo.json installs next to the device library.
install(FILES plugInfo.json DESTINATION ${CMAKE_INSTALL_LIBDIR})

## Tests ##

# Inside the ANARI-SDK the tests follow BUILD_TESTING; a standalone build of
# this directory uses ANARI_HYDRA_TESTS (default ON).
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  option(ANARI_HYDRA_TESTS "Build the hydra device's end-to-end tests" ON)
  set(_anari_hydra_tests ${ANARI_HYDRA_TESTS})
else()
  set(_anari_hydra_tests ${BUILD_TESTING})
endif()

if(_anari_hydra_tests)
  enable_testing()
  add_subdirectory(tests)
endif()
