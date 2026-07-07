## Copyright 2021-2026 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

add_subdirectory(external)

project(anari_test_scenes LANGUAGES C CXX)

project_add_library()
project_sources(PRIVATE
  anari_test_scenes.cpp

  generators/ColorPalette.cpp
  generators/PrimitiveGenerator.cpp
  generators/TextureGenerator.cpp

  scenes/scene.cpp

  scenes/demo/cornell_box.cpp
  scenes/demo/gravity_spheres_volume.cpp

  scenes/file/obj.cpp

  scenes/performance/materials.cpp
  scenes/performance/particles.cpp
  scenes/performance/primitives.cpp
  scenes/performance/spinning_cubes.cpp
  scenes/performance/surfaces.cpp

  scenes/test/attributes.cpp
  scenes/test/instanced_cubes.cpp
  scenes/test/pbr_spheres.cpp
  scenes/test/random_cylinders.cpp
  scenes/test/random_spheres.cpp
  scenes/test/textured_cube.cpp
)

project_include_directories(
PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/anari/anari_test_scenes>
)
project_link_libraries(PUBLIC helium anari_static PUBLIC $<BUILD_INTERFACE:stb_image>)
project_link_libraries(PUBLIC helium anari_static PUBLIC $<BUILD_INTERFACE:mikktspace>)

# glTF support is needed by the viewer and, when built, the CTS core. Enabling
# CTS glTF tests without a CTS consumer must not add the loader to this general-
# purpose scene library.
if (VIEWER_ENABLE_GLTF OR ((BUILD_CTS OR BUILD_TESTING) AND CTS_ENABLE_GLTF))
  project_sources(PRIVATE scenes/file/glTF.cpp)
  project_compile_definitions(PRIVATE ENABLE_GLTF=1)
endif()

if (USE_DRACO)
  find_package(draco REQUIRED)
  project_link_libraries(PUBLIC draco::draco)
  project_compile_definitions(PUBLIC -DUSE_DRACO)
endif()

if (USE_WEBP)
  find_package(WebP REQUIRED)
  project_link_libraries(PUBLIC WebP::webp)
  project_compile_definitions(PUBLIC -DUSE_WEBP)
endif()

if (USE_KTX)
  find_package(Ktx REQUIRED)
  project_link_libraries(PUBLIC KTX::ktx)
  project_compile_definitions(PUBLIC -DUSE_KTX)
endif()

if (USE_KOKKOS)
  project_link_libraries(PRIVATE Kokkos::kokkos)
  project_compile_definitions(PRIVATE -DUSE_KOKKOS)
endif ()

# For initializing an aggregate with designated initializers
project_compile_features(PRIVATE cxx_std_20)

include(GenerateExportHeader)
generate_export_header(${PROJECT_NAME}
  EXPORT_MACRO_NAME "ANARI_TEST_SCENES_INTERFACE"
)

## Internal CTS core ##

# Keep the CTS implementation out of the installed scene library. The core is
# explicitly static so an installed anariCts remains self-contained and does
# not introduce another runtime library or exported SDK target.
if (BUILD_CTS OR BUILD_TESTING)
  add_library(anari_cts_core STATIC
    cts/ArtifactPublication.cpp
    cts/BuildContext.cpp
    cts/BuiltinTests.cpp
    cts/Case.cpp
    cts/Catalog.cpp
    cts/Expansion.cpp
    cts/Filter.cpp
    cts/FrameFormats.cpp
    cts/FrameReadback.cpp
    cts/GeometryBuilder.cpp
    cts/GeometryLayout.cpp
    cts/HtmlReport.cpp
    cts/Image.cpp
    cts/InstanceBuilder.cpp
    cts/LightBuilder.cpp
    cts/Metrics.cpp
    cts/ParameterBinding.cpp
    cts/Report.cpp
    cts/Runner.cpp
    cts/Sidecar.cpp
    cts/SamplerBuilder.cpp
    cts/SurfaceBuilder.cpp
    cts/TestBuilder.cpp
    cts/Value.cpp
    cts/ViewBuilder.cpp
    cts/VolumeBuilder.cpp
    cts/WorldBuilder.cpp
    cts/Workdir.cpp

    cts/tests/camera.cpp
    cts/tests/frame.cpp
    cts/tests/geometry.cpp
    cts/tests/gltf.cpp
    cts/tests/instance.cpp
    cts/tests/light.cpp
    cts/tests/material.cpp
    cts/tests/renderer.cpp
    cts/tests/sampler.cpp
    cts/tests/volume.cpp
  )
  target_include_directories(anari_cts_core
  PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}
  )
  target_link_libraries(anari_cts_core
  PUBLIC
    anari_test_scenes
    anari_device_introspection
  )
  target_compile_features(anari_cts_core PUBLIC cxx_std_20)

  if (CTS_ENABLE_GLTF)
    target_compile_definitions(anari_cts_core
    PRIVATE
      ENABLE_GLTF=1
      CTS_GLTF_ASSET_DIR="${CMAKE_CURRENT_LIST_DIR}/../../cts/gltf-Sample-Assets"
    )
  endif()
endif()

## Installation ##

install(TARGETS ${PROJECT_NAME}
  EXPORT anari_Exports
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(
FILES
  ${PROJECT_BINARY_DIR}/${PROJECT_NAME}_export.h
  ${CMAKE_CURRENT_LIST_DIR}/anari_test_scenes.h
  ${CMAKE_CURRENT_LIST_DIR}/scenes/scene.h
DESTINATION
  ${CMAKE_INSTALL_INCLUDEDIR}/anari/anari_test_scenes
)
