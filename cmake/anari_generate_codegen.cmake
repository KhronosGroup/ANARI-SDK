## Copyright 2023-2024 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

function(anari_generate_queries)
  if(CMAKE_VERSION VERSION_LESS "3.12")
    return()
  endif()

  cmake_parse_arguments(
  # prefix
    "GENERATE"
  # options
    ""
  # single-arg options
    "PREFIX;NAME;CPP_NAMESPACE;JSON_DEFINITIONS_FILE;JSON_ROOT_LOCATION;OUTPUT_LOCATION"
  # multi-arg options
    ""
  # string to parse
    ${ARGN}
  )

  find_package(Python3 OPTIONAL_COMPONENTS Interpreter)
  if (NOT TARGET Python3::Interpreter)
    message(WARNING "Unable to find python interpreter, skipping code-gen targets")
    return()
  endif()

  if (DEFINED GENERATE_JSON_ROOT_LOCATION)
    set(EXTRA_JSON_OPTION --json ${GENERATE_JSON_ROOT_LOCATION})
  endif()

  if (DEFINED GENERATE_OUTPUT_LOCATION)
    set(OUTPUT_LOCATION ${GENERATE_OUTPUT_LOCATION})
  else()
    set(OUTPUT_LOCATION ${CMAKE_CURRENT_SOURCE_DIR})
  endif()

  add_custom_target(generate_${GENERATE_NAME}
    COMMAND ${Python3_EXECUTABLE} ${ANARI_CODE_GEN_ROOT}/generate_queries.py
      --json ${ANARI_CODE_GEN_ROOT}
      ${EXTRA_JSON_OPTION}
      --prefix ${GENERATE_PREFIX}
      --device ${GENERATE_JSON_DEFINITIONS_FILE}
      --namespace ${GENERATE_CPP_NAMESPACE}
      --output ${OUTPUT_LOCATION}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    DEPENDS ${GENERATE_JSON_DEFINITIONS_FILE}
  )

  set_source_files_properties(
    ${OUTPUT_LOCATION}/${GENERATE_PREFIX}Queries.h ${OUTPUT_LOCATION}/${GENERATE_PREFIX}Queries.cpp
    PROPERTIES GENERATED ON
  )

  if (TARGET generate_all)
    add_dependencies(generate_all generate_${GENERATE_NAME})
  endif()
endfunction()
