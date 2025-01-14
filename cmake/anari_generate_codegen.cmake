## Copyright 2023-2024 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

function(anari_generate_queries)
  if(CMAKE_VERSION VERSION_LESS "3.12")
    return()
  endif()

  find_package(Python3 REQUIRED COMPONENTS Interpreter)

  cmake_parse_arguments(
  # prefix
    "GENERATE"
  # options
    ""
  # single-arg options
    "CPP_NAMESPACE;JSON_DEFINITIONS_FILE;JSON_ROOT_LOCATION;JSON_EXTENSION_FILES;DEVICE_TARGET"
  # multi-arg options
    ""
  # string to parse
    ${ARGN}
  )

  file(GLOB_RECURSE CORE_JSONS ${ANARI_CODE_GEN_ROOT}/api/*.json)

  if (NOT DEFINED GENERATE_DEVICE_TARGET)
    message(FATAL_ERROR "DEVICE_TARGET option required for anari_generate_queries()")
  endif()

  set(GENERATE_PREFIX "${GENERATE_DEVICE_TARGET}")

  if (DEFINED GENERATE_JSON_ROOT_LOCATION)
    set(EXTRA_JSON_OPTION --json ${GENERATE_JSON_ROOT_LOCATION})
  endif()

  set(OUTPUT_LOC ${CMAKE_CURRENT_BINARY_DIR})
  set(OUTPUT_H ${OUTPUT_LOC}/${GENERATE_PREFIX}_queries.h)
  set(OUTPUT_CPP ${OUTPUT_LOC}/${GENERATE_PREFIX}_queries.cpp)

  add_custom_command(
    OUTPUT ${OUTPUT_H} ${OUTPUT_CPP}
    COMMAND ${Python3_EXECUTABLE} ${ANARI_CODE_GEN_ROOT}/generate_queries.py
      --json ${ANARI_CODE_GEN_ROOT}
      ${EXTRA_JSON_OPTION}
      --prefix ${GENERATE_PREFIX}
      --device ${GENERATE_JSON_DEFINITIONS_FILE}
      --namespace ${GENERATE_CPP_NAMESPACE}
      --output ${OUTPUT_LOC}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    DEPENDS ${CORE_JSONS} ${GENERATE_JSON_DEFINITIONS_FILE} ${GENERATE_JSON_EXTENSION_FILES}
  )

  set_source_files_properties(${OUTPUT_H} ${OUTPUT_CPP} PROPERTIES GENERATED ON)
  target_sources(${GENERATE_DEVICE_TARGET} PRIVATE ${OUTPUT_CPP})
  target_include_directories(${GENERATE_DEVICE_TARGET} PRIVATE ${OUTPUT_LOC})
endfunction()
