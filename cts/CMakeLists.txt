## Copyright 2022-2024 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.1)

if (TARGET anari)
  list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/../cmake)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(REFERENCE_LIB helide)

project(anariCTSBackend LANGUAGES CXX)

find_package(anari REQUIRED)
include(pybind11.cmake)

pybind11_add_module(${PROJECT_NAME}
  src/anariInfo.cpp
  src/anariWrapper.cpp
  src/ctsQueries.cpp
  src/main.cpp
  src/PrimitiveGenerator.cpp
  src/SceneGenerator.cpp
  src/ColorPalette.cpp
  src/TextureGenerator.cpp
)

target_include_directories(${PROJECT_NAME}
PUBLIC
  ${CMAKE_CURRENT_LIST_DIR}/header
)

target_link_libraries(${PROJECT_NAME} PUBLIC helium anari_test_scenes)

find_package(Python3 REQUIRED COMPONENTS Interpreter Development)

file(GLOB python_files ${CMAKE_CURRENT_LIST_DIR}/*.py)

add_subdirectory(src) # query generation target

add_custom_target(generate_cts_baseline
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${CMAKE_CFG_INTDIR}/createReferenceData.py
	  --test_scenes ${CMAKE_CURRENT_LIST_DIR}/test_scenes -l ${REFERENCE_LIB}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
add_dependencies(generate_cts_baseline ${PROJECT_NAME})

add_custom_target(generate_cts)
add_dependencies(generate_cts generate_cts_queries generate_cts_baseline)
add_dependencies(generate_all generate_cts)

option(CTS_DEV "Copy compiled python module next to python script to make developing easier" OFF)
mark_as_advanced(CTS_DEV)
if (CTS_DEV AND NOT BUILD_SHARED_LIBS)
  add_custom_command(TARGET ${PROJECT_NAME}
    COMMAND ${CMAKE_COMMAND}
        -DBUILD_CONFIG_DIR=${CMAKE_CFG_INTDIR}
        -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
        -DREFERENCE_LIB=${REFERENCE_LIB}
    -P "${CMAKE_CURRENT_LIST_DIR}/copy_post_build.cmake"
    VERBATIM
  )
endif()

## Installation ##

# .pyd files are considered library files
if (WIN32)
  install(TARGETS ${PROJECT_NAME}
    LIBRARY DESTINATION ${CMAKE_INSTALL_BINDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  )
else()
  install(TARGETS ${PROJECT_NAME}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  )
endif()

install(FILES ${python_files} ${CMAKE_CURRENT_LIST_DIR}/default_test_scene.json
  DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/anari/cts
)

install(DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/test_scenes
  DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/anari/cts
)
