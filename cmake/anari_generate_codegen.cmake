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
    "PREFIX;NAME;CPP_NAMESPACE;JSON_DEFINITIONS_FILE;JSON_ROOT_LOCATION"
  # multi-arg options
    ""
  # string to parse
    ${ARGN}
  )

  find_package(Python3 REQUIRED COMPONENTS Interpreter)

  if (DEFINED GENERATE_JSON_ROOT_LOCATION)
    set(EXTRA_JSON_OPTION --json ${GENERATE_JSON_ROOT_LOCATION})
  endif()

  add_custom_target(generate_${GENERATE_NAME}_device
    COMMAND ${Python3_EXECUTABLE} ${ANARI_CODE_GEN_ROOT}/generate_queries.py
      --json ${ANARI_CODE_GEN_ROOT}
      ${EXTRA_JSON_OPTION}
      --prefix ${GENERATE_PREFIX}
      --device ${GENERATE_JSON_DEFINITIONS_FILE}
      --namespace ${GENERATE_CPP_NAMESPACE}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    DEPENDS ${GENERATE_JSON_DEFINITIONS_FILE}
  )

  if (TARGET generate_all)
    add_dependencies(generate_all generate_${GENERATE_NAME}_device)
  endif()
endfunction()
