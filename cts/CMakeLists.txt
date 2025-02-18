## Copyright 2022-2024 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.1)

if (ANARI_LIBRARY_ONLY)
  return()
endif()

# Fix "DOWNLOAD_EXTRACT_TIMESTAMP" warning
if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.24.0")
  cmake_policy(SET CMP0135 NEW)
endif()

if (TARGET anari)
  list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/../cmake)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(REFERENCE_LIB helide)

project(anariCTSBackend LANGUAGES CXX)

set(Python3_ROOT_DIR "" CACHE PATH "Path to python3 directory")
set(Python3_FIND_REGISTRY "LAST")
set(Python3_FIND_STRATEGY "LOCATION")
find_package(anari REQUIRED)
find_package(Python3 3.9...<3.20 REQUIRED COMPONENTS Interpreter Development)
message("CTS Python version: ${Python3_VERSION}\nCTS Python Path: ${Python3_EXECUTABLE}")
include(pybind11.cmake)

pybind11_add_module(${PROJECT_NAME}
  src/anariInfo.cpp
  src/anariWrapper.cpp
  src/main.cpp
  src/PrimitiveGenerator.cpp
  src/SceneGenerator.cpp
  src/ColorPalette.cpp
  src/TextureGenerator.cpp
)

anari_generate_queries(
  DEVICE_TARGET ${PROJECT_NAME}
  CPP_NAMESPACE cts
  JSON_DEFINITIONS_FILE ${ANARI_CODE_GEN_ROOT}/devices/experimental_device.json
)

target_include_directories(${PROJECT_NAME}
PUBLIC
  ${CMAKE_CURRENT_LIST_DIR}/header
)

target_link_libraries(${PROJECT_NAME} PUBLIC helium anari_test_scenes)

file(GLOB python_files ${CMAKE_CURRENT_LIST_DIR}/*.py)

add_custom_target(generate_cts_baseline
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${CMAKE_CFG_INTDIR}/createReferenceData.py
	  --test_scenes ${CMAKE_CURRENT_LIST_DIR}/test_scenes -l ${REFERENCE_LIB}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
add_dependencies(generate_cts_baseline ${PROJECT_NAME})

add_custom_target(generate_cts)
add_dependencies(generate_cts generate_cts_baseline)
add_dependencies(generate_all generate_cts)

option(CTS_DEV "Copy compiled python module next to python script to make developing easier" OFF)
mark_as_advanced(CTS_DEV)
if (CTS_DEV)
  add_custom_command(TARGET ${PROJECT_NAME}
    COMMAND ${CMAKE_COMMAND}
        -DBUILD_CONFIG_DIR=${CMAKE_CFG_INTDIR}
        -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
        -DREFERENCE_LIB=${REFERENCE_LIB}
    -P "${CMAKE_CURRENT_LIST_DIR}/copy_post_build.cmake"
    VERBATIM
  )
endif()

if (CTS_ENABLE_GLTF)
  anari_sdk_fetch_project(
    NAME anari_cts_glTFSampleAssets
    URL https://github.com/KhronosGroup/glTF-Sample-Assets/archive/754a32a333b27698b6c36d8bec8ff7ed5f729538.zip
    MD5 1250786407d1d0e649faaade748c24ab
  )

  message("Copying glTF Sample Models")
  file(GLOB GLTF_SAMPLE_ASSETS_FILES ${anari_cts_glTFSampleAssets_LOCATION}/Models/*)
  list(FILTER GLTF_SAMPLE_ASSETS_FILES EXCLUDE REGEX ".*Unicode❤♻Test")
  file(COPY ${GLTF_SAMPLE_ASSETS_FILES} DESTINATION ${CMAKE_CURRENT_LIST_DIR}/gltf-Sample-Assets)
endif()

## Installation ##

if (INSTALL_CTS)
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
    ${CMAKE_CURRENT_LIST_DIR}/pyproject.toml
    ${CMAKE_CURRENT_LIST_DIR}/poetry.lock
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/anari/cts
  )

  install(DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/test_scenes
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/anari/cts
  )

  if (CTS_ENABLE_GLTF)
    install(DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/gltf-Sample-Assets
      DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/anari/cts
    )
  endif()
endif()
