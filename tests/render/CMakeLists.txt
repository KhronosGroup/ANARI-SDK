## Copyright 2021-2024 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

project(anariRenderTests LANGUAGES CXX)

add_executable(${PROJECT_NAME} main.cpp)

target_link_libraries(${PROJECT_NAME} PRIVATE anari_test_scenes stb_image)

install(TARGETS ${PROJECT_NAME}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

foreach(SCENE cornell_box;gravity_spheres_volume)
  add_test(NAME render_test::${SCENE} COMMAND ${PROJECT_NAME} --scene demo ${SCENE})
endforeach()

foreach(SCENE instanced_cubes;textured_cube;random_spheres)
  add_test(NAME render_test::${SCENE} COMMAND ${PROJECT_NAME} --scene test ${SCENE})
endforeach()
