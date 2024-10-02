## Copyright 2021-2024 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

project(anari_test_scenes LANGUAGES CXX)

project_add_library()
project_sources(PRIVATE
  anari_test_scenes.cpp

  scenes/scene.cpp

  scenes/demo/cornell_box.cpp
  scenes/demo/gravity_spheres_volume.cpp

  scenes/file/obj.cpp
  scenes/file/mikktspace/mikktspace.c

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
project_link_libraries(PUBLIC helium PUBLIC $<BUILD_INTERFACE:stb_image>)

if (VIEWER_ENABLE_GLTF)
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

include(GenerateExportHeader)
generate_export_header(${PROJECT_NAME}
  EXPORT_MACRO_NAME "ANARI_TEST_SCENES_INTERFACE"
)

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
