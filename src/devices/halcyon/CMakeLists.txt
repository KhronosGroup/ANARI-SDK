## Copyright 2025-2026 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.19)

message(STATUS "CMake version: ${CMAKE_VERSION}")

## Language setup ##

set(CMAKE_DISABLE_SOURCE_CHANGES ON)
set(CMAKE_DISABLE_IN_SOURCE_BUILD ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(CMAKE_BUILD_TYPE_INIT Release)

set(CMAKE_INSTALL_RPATH "$ORIGIN")
set(CMAKE_INSTALL_MESSAGE LAZY)

## Establish project ##

project(anari_library_halcyon LANGUAGES CXX ISPC)

# At -O0 ISPC gives each inlined local its own stack slot, which makes the
# pathtracer's shading frames (PathPbm_shade()'s, 9.6 MB) overflow an 8 MB
# stack: Debug builds compile ISPC at -O1 instead, still with -g
string(REPLACE "-O0" "-O1" CMAKE_ISPC_FLAGS_DEBUG "${CMAKE_ISPC_FLAGS_DEBUG}")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

include(GNUInstallDirs)

## Dependencies ##

find_package(anari REQUIRED)
find_package(Threads REQUIRED)

# halcyon takes the ANARI SDK's version, as helide does: the root's when built
# in the SDK's tree (src/devices/halcyon), the found SDK's when built alone
if (DEFINED ANARI_SDK_VERSION)
  set(HALCYON_VERSION ${ANARI_SDK_VERSION})
elseif (DEFINED anari_VERSION)
  set(HALCYON_VERSION ${anari_VERSION})
else()
  message(FATAL_ERROR "halcyon could not determine the ANARI SDK's version")
endif()

# Embree is fetched and built as a static library, like helide's
add_subdirectory(external/embree EXCLUDE_FROM_ALL)

# Typhoon's material library (external/typhoon) and OpenQMC's tables
# (external/openqmc), for the 'quality' renderer
add_subdirectory(external/typhoon)
add_subdirectory(external/openqmc)

# MaterialX's Core and Format libraries and its embedded data library
# (external/materialx), for the 'materialx' material subtype
add_subdirectory(external/materialx)

# stb_image and tinyexr, the image file readers of the pathtracer's texture
# system (external/images)
add_subdirectory(external/images)

## Build device target ##

project_add_library(SHARED
  HalcyonDevice.cpp
  HalcyonDeviceGlobalState.cpp
  HalcyonLibrary.cpp
  Object.cpp
  camera/Camera.cpp
  camera/Camera.ispc
  frame/Frame.cpp
  frame/Frame.ispc
  geometry/Geometry.cpp
  geometry/Geometry.ispc
  geometry/GeometryDerivatives.ispc
  geometry/MeshTangents.cpp
  light/Directional.cpp
  light/Hdri.cpp
  light/HdriDistribution.cpp
  light/Light.cpp
  light/Light.ispc
  light/Point.cpp
  light/QuadLight.cpp
  light/Ring.cpp
  light/Spot.cpp
  material/Material.cpp
  material/Material.ispc
  renderer/Renderer.cpp
  renderer/Renderer.ispc
  renderer/pathtracer/PathTracer.cpp
  renderer/pathtracer/PathTracer.ispc
  renderer/preview/Preview.cpp
  renderer/preview/Preview.ispc
  sampler/Sampler.cpp
  sampler/Sampler.ispc
  spatial_field/SpatialField.cpp
  spatial_field/SpatialField.ispc
  surface/Surface.cpp
  surface/Surface.ispc
  volume/Volume.cpp
  volume/Volume.ispc
  world/Group.cpp
  world/Group.ispc
  world/Instance.cpp
  world/Instance.ispc
  world/World.cpp
  world/World.ispc
)

project_link_libraries(PRIVATE
  anari::helium
  anari::anari
  anari_embree
  halcyon_path_materials # the pathtracer's materials, below
  halcyon_materialx_documents # 'materialx' materials, below
)

# The derivative hooks round like Typhoon's C++, which x86-64 compilers build
# without fused multiply-adds (GeometryDerivatives.ispc)
set_source_files_properties(geometry/GeometryDerivatives.ispc
  PROPERTIES COMPILE_OPTIONS "--opt=disable-fma")

# The device's version properties report the SDK's version (a patch of 0
# where it has none)
string(REGEX MATCH "^([0-9]+)\\.([0-9]+)(\\.([0-9]+))?" _ "${HALCYON_VERSION}")
math(EXPR HALCYON_VERSION_MAJOR_INT "${CMAKE_MATCH_1} + 0")
math(EXPR HALCYON_VERSION_MINOR_INT "${CMAKE_MATCH_2} + 0")
math(EXPR HALCYON_VERSION_PATCH_INT "0${CMAKE_MATCH_4} + 0")
target_compile_definitions(${PROJECT_NAME} PRIVATE
  HALCYON_VERSION_MAJOR=${HALCYON_VERSION_MAJOR_INT}
  HALCYON_VERSION_MINOR=${HALCYON_VERSION_MINOR_INT}
  HALCYON_VERSION_PATCH=${HALCYON_VERSION_PATCH_INT}
  HALCYON_VERSION_STRING="${HALCYON_VERSION}"
)

include(GenerateExportHeader)
generate_export_header(${PROJECT_NAME} EXPORT_MACRO_NAME "HALCYON_EXPORT")

# include/ holds the anariNewHalcyonDevice() header; it's usable from the
# build tree (the device ctest links it) but not installed
project_include_directories(
PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
PRIVATE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}>
)

## Pathtracer materials ##

# The pathtracer's material interface (renderer/pathtracer/PathMaterial.*,
# docs/adr/0004-pathtracer-material-interface.md): the ISPC records and the C++
# side of the calls into Typhoon's materials, the graphs of halcyon's
# materials and their texture system over halcyon's samplers
# (docs/adr/0005-pathtracer-texture-system.md); also the C++ data of the
# pathtracer's sample generator (renderer/pathtracer/Sampling.isph), its
# homogeneous media (renderer/pathtracer/Medium.ispc), its subsurface
# random walk (renderer/pathtracer/Subsurface.ispc) and its hero-wavelength
# conversions (renderer/pathtracer/HeroWavelength.ispc)
add_library(halcyon_path_materials STATIC
  renderer/pathtracer/AdaptiveSampling.ispc
  renderer/pathtracer/ClosureAlbedo.cpp
  renderer/pathtracer/ClosureClassification.cpp
  renderer/pathtracer/ClosureSampling.cpp
  renderer/pathtracer/ColorManagement.cpp
  renderer/pathtracer/ContextGroups.cpp
  renderer/pathtracer/HalcyonTextureSystem.cpp
  renderer/pathtracer/HalcyonTextureSystem.ispc
  renderer/pathtracer/HeroWavelength.ispc
  renderer/pathtracer/ImageFile.cpp
  renderer/pathtracer/MaterialConverter.cpp
  renderer/pathtracer/Medium.ispc
  renderer/pathtracer/PathMaterial.cpp
  renderer/pathtracer/PathMaterial.ispc
  renderer/pathtracer/PathPbm.ispc
  renderer/pathtracer/PathPbmTables.cpp
  renderer/pathtracer/SobolBnTables.cpp
  renderer/pathtracer/SpectralTables.cpp
  renderer/pathtracer/Subsurface.ispc
)
target_include_directories(halcyon_path_materials PUBLIC
  ${CMAKE_CURRENT_LIST_DIR}
  ${CMAKE_CURRENT_BINARY_DIR} # PathMaterial_ispc.h
)
# Embree and ANARI for shading/halcyon_objects.isph's includes
target_link_libraries(halcyon_path_materials PUBLIC
  halcyon_typhoon_materials
  anari_embree
  anari::anari
)
# Native physicallyBased rounds like the C++ it ports, which x86-64 compilers
# build without fused multiply-adds and which calls the system's cosf(),
# sinf() and powf() (PathPbm.ispc)
set_source_files_properties(renderer/pathtracer/PathPbm.ispc
  PROPERTIES COMPILE_OPTIONS "--opt=disable-fma;--math-lib=system")
# Adaptive sampling decides when a pixel converges as Typhoon's C++ does,
# which x86-64 compilers build without fused multiply-adds
# (AdaptiveSampling.ispc)
set_source_files_properties(renderer/pathtracer/AdaptiveSampling.ispc
  PROPERTIES COMPILE_OPTIONS "--opt=disable-fma")
# The subsurface random walk branches on its numbers at every step, so it
# rounds as Typhoon's C++ does, which x86-64 compilers build without fused
# multiply-adds and which calls the system's expf(), logf(), powf(), atanf(),
# sinf() and cosf() (Subsurface.ispc)
set_source_files_properties(renderer/pathtracer/Subsurface.ispc
  PROPERTIES COMPILE_OPTIONS "--opt=disable-fma;--math-lib=system")
# Homogeneous media's free flights and phase samples decide where paths go,
# so they round as Typhoon's C++ does too: no fused multiply-adds, the
# system's expf(), logf(), sinf() and cosf() (Medium.ispc)
set_source_files_properties(renderer/pathtracer/Medium.ispc
  PROPERTIES COMPILE_OPTIONS "--opt=disable-fma;--math-lib=system")
# Hero-wavelength conversions round as Typhoon's C++ does, which x86-64
# compilers build without fused multiply-adds (HeroWavelength.ispc)
set_source_files_properties(renderer/pathtracer/HeroWavelength.ispc
  PROPERTIES COMPILE_OPTIONS "--opt=disable-fma")
# OpenQMC's blue-noise tables for the sample generator (Sampling.isph)
target_link_libraries(halcyon_path_materials PRIVATE halcyon_openqmc_data)
# stb_image and tinyexr for image files (ImageFile.cpp)
target_link_libraries(halcyon_path_materials PRIVATE halcyon_image_loaders)

# Keep the material libraries' symbols out of the device's dynamic symbols,
# as Embree's tasking library's (external/embree)
if (UNIX AND NOT APPLE)
  target_link_options(${PROJECT_NAME} PRIVATE
    "LINKER:--exclude-libs,$<TARGET_LINKER_FILE_NAME:halcyon_path_materials>"
    "LINKER:--exclude-libs,$<TARGET_LINKER_FILE_NAME:halcyon_typhoon_materials>"
    "LINKER:--exclude-libs,$<TARGET_LINKER_FILE_NAME:halcyon_materialx_documents>"
    "LINKER:--exclude-libs,$<TARGET_LINKER_FILE_NAME:halcyon_materialx_definitions>"
  )
endif()

## MaterialX documents ##

# MaterialX documents as MaterialXCpp graphs, for the 'materialx' material
# subtype (renderer/pathtracer/MaterialXDocument.*,
# docs/adr/0006-materialx-documents.md): the only code that includes
# MaterialX's headers, so the device's own objects instantiate none of its
# symbols, and halcyon_materialx keeps its libraries' symbols out of the
# device's dynamic symbols
add_library(halcyon_materialx_documents STATIC
  renderer/pathtracer/MaterialXDocument.cpp
  renderer/pathtracer/MaterialXLibrary.cpp
)
target_link_libraries(halcyon_materialx_documents
  PUBLIC halcyon_path_materials
  PRIVATE halcyon_materialx halcyon_materialx_definitions
)

## Code generation ##

# halcyon's own extensions (HALCYON_*), one JSON definition each in
# extensions/, which HalcyonDefinitions.json lists among its dependencies
file(GLOB HALCYON_EXTENSION_JSONS CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/extensions/*.json)
anari_generate_queries(
  DEVICE_TARGET ${PROJECT_NAME}
  CPP_NAMESPACE halcyon
  JSON_DEFINITIONS_FILE ${CMAKE_CURRENT_SOURCE_DIR}/HalcyonDefinitions.json
  JSON_ROOT_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/extensions
  JSON_EXTENSION_FILES "${HALCYON_EXTENSION_JSONS}"
)
# JSON_EXTENSION_FILES is a single-value option, so the generator depends on
# the first file only: make every extension a dependency
add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}_queries.h
  APPEND DEPENDS ${HALCYON_EXTENSION_JSONS}
)

## Tests ##

option(HALCYON_BUILD_TESTS "Build halcyon unit tests" ON)

if (HALCYON_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

## Installation ##

install(TARGETS ${PROJECT_NAME}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
