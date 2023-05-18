## Copyright 2021 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

project(anari_test_scenes LANGUAGES CXX)

project_add_library(STATIC
  anari_test_scenes.cpp

  scenes/scene.cpp

  scenes/demo/cornell_box.cpp
  scenes/demo/gravity_spheres_volume.cpp

  scenes/file/obj.cpp

  scenes/test/attributes.cpp
  scenes/test/instanced_cubes.cpp
  scenes/test/pbr_spheres.cpp
  scenes/test/random_cylinders.cpp
  scenes/test/random_spheres.cpp
  scenes/test/textured_cube.cpp
)

project_include_directories(PUBLIC ${CMAKE_CURRENT_LIST_DIR})
project_link_libraries(PUBLIC anari helium glm::glm stb_image)
