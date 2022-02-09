## Copyright 2021 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

set(CMAKE_CXX_STANDARD 17)

project(anari_library_example LANGUAGES CXX)

if(APPLE)
  if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    set(OpenMP_C_FLAGS "-Xpreprocessor -fopenmp")
    set(OpenMP_C_LIB_NAMES "omp")
  endif()
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(OpenMP_CXX_FLAGS "-Xpreprocessor -fopenmp")
    set(OpenMP_CXX_LIB_NAMES "omp")
  endif()
endif()

if (NOT WIN32)
  find_package(OpenMP)
endif()

if(EXISTS ${CMAKE_SOURCE_DIR}/.git AND IS_DIRECTORY ${CMAKE_SOURCE_DIR}/.git)
  find_package(Git REQUIRED)
  execute_process(
    COMMAND ${GIT_EXECUTABLE} rev-list --count HEAD
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE "REFERENCE_DEVICE_VERSION"
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
else()
  set(REFERENCE_DEVICE_VERSION -1)
endif()

if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.12")
  find_package(Python3 REQUIRED COMPONENTS Interpreter)

  add_custom_target(generate_example_device
    COMMAND ${Python3_EXECUTABLE} ${ANARI_JSON_PATH}/generate_queries.py
      --json ${ANARI_JSON_PATH}
      --prefix ExampleDevice
      --device ${CMAKE_CURRENT_SOURCE_DIR}/example_device.json
      --namespace example_device
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    DEPENDS example_device.json
  )

  add_dependencies(generate_all generate_example_device)

  add_custom_target(generate_example_debug_objects
    COMMAND ${Python3_EXECUTABLE} ${ANARI_JSON_PATH}/generate_debug_objects.py
      --json ${ANARI_JSON_PATH}
      --device ${CMAKE_CURRENT_SOURCE_DIR}/example_device.json
      --prefix ExampleDevice
      --namespace example_device
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    DEPENDS example_device.json
  )

  add_dependencies(generate_all generate_example_debug_objects)

endif()

add_library(${PROJECT_NAME} SHARED
  glm_extras.cpp

  Array.cpp
  Object.cpp
  ExampleDevice.cpp

  ExampleDeviceQueries.cpp
  ExampleDeviceDebugFactory.cpp

  camera/Camera.cpp
  camera/Orthographic.cpp
  camera/Perspective.cpp

  frame/Frame.cpp

  material/Material.cpp
  material/Matte.cpp

  renderer/AmbientOcclusion.cpp
  renderer/Debug.cpp
  renderer/Raycast.cpp
  renderer/RayDir.cpp
  renderer/Renderer.cpp

  scene/Group.cpp
  scene/Instance.cpp
  scene/World.cpp

  scene/geometry/Geometry.cpp
  scene/geometry/Spheres.cpp
  scene/geometry/Surface.cpp
  scene/geometry/Triangles.cpp

  scene/volume/StructuredRegularField.cpp
  scene/volume/SpatialField.cpp
  scene/volume/Volume.cpp
)

target_include_directories(${PROJECT_NAME}
INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>
)

target_link_libraries(${PROJECT_NAME}
PUBLIC
  anari
  anari_utilities
  anari_library_debug
PRIVATE
  glm::glm
)

if (NOT WIN32 AND TARGET OpenMP::OpenMP_CXX)
  target_link_libraries(${PROJECT_NAME} PRIVATE OpenMP::OpenMP_CXX)
endif()

target_compile_definitions(${PROJECT_NAME}
PRIVATE
  -DDEVICE_VERSION=${REFERENCE_DEVICE_VERSION}
  "example_device_EXPORTS"
)

install(TARGETS ${PROJECT_NAME}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
