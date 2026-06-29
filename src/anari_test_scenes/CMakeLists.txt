## Copyright 2021-2026 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

add_subdirectory(external)

project(anari_test_scenes LANGUAGES C CXX)

project_add_library()
project_sources(PRIVATE
  anari_test_scenes.cpp

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
  cts/Image.cpp
  cts/InstanceBuilder.cpp
  cts/LightBuilder.cpp
  cts/Metrics.cpp
  cts/ParameterBinding.cpp
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

  cts/tests/geometry.cpp
  cts/tests/material.cpp
  cts/tests/sampler.cpp
  cts/tests/light.cpp
  cts/tests/camera.cpp
  cts/tests/frame.cpp
  cts/tests/renderer.cpp
  cts/tests/instance.cpp
  cts/tests/volume.cpp
  cts/tests/gltf.cpp

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

# glTF support is needed by both the viewer (VIEWER_ENABLE_GLTF, gated on the
# viewer/GLFW) and the CTS glTF asset-scan factory (CTS_ENABLE_GLTF, root
# option). The latter only needs the loader, not the viewer.
if (VIEWER_ENABLE_GLTF OR CTS_ENABLE_GLTF)
  project_sources(PRIVATE scenes/file/glTF.cpp)
  project_compile_definitions(PRIVATE ENABLE_GLTF=1)
  # The glTF asset-scan factory (cts/tests/gltf.cpp) defaults to this source-tree
  # asset root; override at runtime with ANARI_CTS_GLTF_ASSETS.
  project_compile_definitions(PRIVATE
    CTS_GLTF_ASSET_DIR="${CMAKE_CURRENT_LIST_DIR}/../../cts/gltf-Sample-Assets")
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

# This library carries the CTS harness, runner, and Test definitions (cts/*),
# which the anariCts CLI binary links. That binary is built in cts/ (its entry
# point, cts/src/main.cpp, links this library).

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
