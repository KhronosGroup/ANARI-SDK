## Copyright 2021-2025 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

## Installation of scripts (independent of code-gen targets) ##

option(INSTALL_CODE_GEN_SCRIPTS "Install scripts in code_gen/" ON)
if (INSTALL_CODE_GEN_SCRIPTS)
  install(DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/anari
    COMPONENT code_gen
    FILES_MATCHING
      PATTERN *.py
      PATTERN *.py.in
      PATTERN *.h
      PATTERN *.cpp
      PATTERN *.json
      PATTERN templates/CMakeLists.txt
      PATTERN ./CMakeLists.txt EXCLUDE
  )
endif()

## Local targets for code-gen + API bindings ##

if(CMAKE_VERSION VERSION_LESS "3.12")
  return()
endif()

find_package(Python3 QUIET COMPONENTS Interpreter Development.Module)
if (NOT TARGET Python3::Interpreter)
  message(WARNING "Unable to find python interpreter, skipping code-gen + API bindings")
  return()
endif()

option(INSTALL_PYTHON_BINDINGS "Install python bindings" OFF) # If turned on, draws in all the custom targets below (generate_all)
cmake_dependent_option(LINK_BINDINGS_TO_RELEASE_PYTHON "Force the python bindings to implicitly link to a release python library on Windows, overriding possible debug python linking" OFF INSTALL_PYTHON_BINDINGS OFF)

option(INSTALL_LEGACY_PYTHON_BINDINGS "Install legacy python bindings" OFF) # If turned on, draws in all the custom targets below (generate_all)
cmake_dependent_option(LINK_BINDINGS_TO_RELEASE_PYTHON "Force the python bindings to implicitly link to a release python library on Windows, overriding possible debug python linking" OFF INSTALL_LEGACY_PYTHON_BINDINGS OFF)


set(API_JSON api/anari_core_1_0.json api/khr_frame_completion_callback.json)

add_custom_target(generate_type_utility
  COMMAND ${Python3_EXECUTABLE} generate_headers.py ${CMAKE_SOURCE_DIR}/src/anari/include/anari/frontend/type_utility.h ${API_JSON}
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  DEPENDS ${API_JSON} generate_headers.py
)

add_custom_target(generate_extension_utility
  COMMAND ${Python3_EXECUTABLE} generate_extension_utility.py
  -j ${CMAKE_CURRENT_SOURCE_DIR}/api/
  -d devices/experimental_device.json
  -o ${CMAKE_SOURCE_DIR}/src/anari/include/anari/frontend
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  DEPENDS devices/experimental_device.json generate_extension_utility.py
)

add_custom_target(generate_anari_h
  COMMAND ${Python3_EXECUTABLE} generate_headers.py ${CMAKE_SOURCE_DIR}/src/anari/include/anari/anari.h ${API_JSON}
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  DEPENDS ${API_JSON} generate_headers.py
)

add_custom_target(generate_anari_enums_h
  COMMAND ${Python3_EXECUTABLE} generate_headers.py ${CMAKE_SOURCE_DIR}/src/anari/include/anari/frontend/anari_enums.h ${API_JSON}
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  DEPENDS ${API_JSON} generate_headers.py
)
add_custom_target(generate_headers DEPENDS
    generate_type_utility
    generate_extension_utility
    generate_anari_h
    generate_anari_enums_h
)

add_dependencies(generate_all generate_headers)

if (NOT TARGET Python3::Module)
  message(WARNING "Unable to find python Module, skipping python bindings")
  return()
endif()

if (INSTALL_PYTHON_BINDINGS)
  set(BUILD_PYTHON_BINDINGS_BY_DEFAULT ALL)
endif()

if (LINK_BINDINGS_TO_RELEASE_PYTHON)
  set(LINK_BINDINGS_TO_RELEASE_PYTHON_FLAG "-R")
endif()

# Generate the C file using the Python script
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/pyanari_bindings.c
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/pyanari_build.py
        -d ${CMAKE_CURRENT_SOURCE_DIR}/devices/generic_device.json
        -j ${CMAKE_CURRENT_SOURCE_DIR}/api/
        ${LINK_BINDINGS_TO_RELEASE_PYTHON_FLAG}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    DEPENDS pyanari_build.py ${API_JSON} devices/generic_device.json
    COMMENT "Generating Python bindings C file"
)

# Create the Python extension module
add_library(pyanari MODULE EXCLUDE_FROM_ALL
    ${CMAKE_CURRENT_BINARY_DIR}/pyanari_bindings.c
)

# Set the output name to match Python's expectations
if (WIN32)
  set(PYTHON_OBJ_EXT .pyd)
else()
  set(PYTHON_OBJ_EXT .so)
endif()
set_target_properties(pyanari
    PROPERTIES
      PREFIX ""
      SUFFIX ${PYTHON_OBJ_EXT}
)

# Force cffi to keep the python api as usual
target_compile_definitions(pyanari PRIVATE
    _CFFI_NO_LIMITED_API
)

# Link against Python libraries and anari
target_link_libraries(pyanari PRIVATE Python3::Module anari)

# Include directories for Python and anari headers
target_include_directories(pyanari PRIVATE
    ${Python3_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/src/anari/include/
    ${CMAKE_BINARY_DIR}/src/anari/include/anari/
)

# Add the custom target for Python bindings
add_custom_target(python_bindings ${BUILD_PYTHON_BINDINGS_BY_DEFAULT}
    DEPENDS pyanari
)

if (INSTALL_LEGACY_PYTHON_BINDINGS)
  install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/anari.py
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/python_bindings)
  install(TARGETS pyanari
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/python_bindings
    RUNTIME DESTINATION ${CMAKE_INSTALL_LIBDIR}/python_bindings)
endif()

# New bindings

# Generate the C file using the Python script
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/anari_api_bindings.c ${CMAKE_CURRENT_BINARY_DIR}/utils.py
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/cffi_gen.py
        -d ${CMAKE_CURRENT_SOURCE_DIR}/devices/generic_device.json
        -j ${CMAKE_CURRENT_SOURCE_DIR}/api/
        -n anari_api
        -O ${CMAKE_CURRENT_BINARY_DIR}
        ${LINK_BINDINGS_TO_RELEASE_PYTHON_FLAG}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    DEPENDS cffi_gen.py ${API_JSON} devices/generic_device.json
    COMMENT "Generating Python bindings C file"
)

# Create the Python extension module
add_library(anari_api MODULE EXCLUDE_FROM_ALL
    ${CMAKE_CURRENT_BINARY_DIR}/anari_api_bindings.c
)

# Set the output name to match Python's expectations
if (WIN32)
  set(PYTHON_OBJ_EXT .pyd)
else()
  set(PYTHON_OBJ_EXT .so)
endif()
set_target_properties(anari_api
    PROPERTIES
      PREFIX ""
      SUFFIX ${PYTHON_OBJ_EXT}
)

# Force cffi to keep the python api as usual
target_compile_definitions(anari_api PRIVATE
    _CFFI_NO_LIMITED_API
)

# Link against Python libraries and anari
target_link_libraries(anari_api PRIVATE Python3::Module anari_static)

set(ANARI_MODULE_NAME anari_api)
configure_file(${ANARI_CODE_GEN_ROOT}/__init__.py.in ${CMAKE_CURRENT_BINARY_DIR}/__init__.py)

# Add the custom target for Python bindings
add_custom_target(neo_bindings ${BUILD_PYTHON_BINDINGS_BY_DEFAULT}
    DEPENDS anari_api
)

install(
  FILES ${CMAKE_CURRENT_BINARY_DIR}/__init__.py ${CMAKE_CURRENT_BINARY_DIR}/utils.py
  EXCLUDE_FROM_ALL
  COMPONENT python_module
  DESTINATION anari)
install(
  TARGETS anari_api
  EXCLUDE_FROM_ALL
  COMPONENT python_module
  LIBRARY DESTINATION anari
  RUNTIME DESTINATION anari)
