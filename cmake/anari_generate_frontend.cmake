## Copyright 2021 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

function(anari_generate_frontend)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)

  function(validate_required_arguments)
    foreach(arg ${ARGN})
      if (NOT DEFINED ${arg})
        message(FATAL_ERROR "anari_generate_frontend() requires argument ${arg}")
      endif()
    endforeach()
  endfunction()

  cmake_parse_arguments(
  # prefix
    "FRONTEND"
  # options
    ""
  # single-arg options
    "TARGET;NAME;PREFIX;NAMESPACE;DESTINATION;DEFINITIONS;CODE_HEADER_FILE;EXTRA_OPTIONS;"
  # multi-arg options
    ""
  # string to parse
    ${ARGN}
  )

  validate_required_arguments(
    FRONTEND_TARGET
    FRONTEND_NAME
    FRONTEND_PREFIX
    FRONTEND_NAMESPACE
    FRONTEND_DESTINATION
    FRONTEND_DEFINITIONS
  )

  if (DEFINED FRONTEND_CODE_HEADER_FILE)
    string(PREPEND FRONTEND_CODE_HEADER_FILE "--header;")
  endif()

  set(GENERATE_SCRIPT ${ANARI_CODE_GEN_ROOT}/generate_device_frontend.py)
  set(GENERATE_COMMAND
    ${Python3_EXECUTABLE}
    ${GENERATE_SCRIPT}
    --name ${FRONTEND_NAME}
    --json ${ANARI_CODE_GEN_ROOT}
    --namespace ${FRONTEND_NAMESPACE}
    --prefix ${FRONTEND_PREFIX}
    --device ${FRONTEND_DEFINITIONS}
    ${FRONTEND_CODE_HEADER_FILE}
    --output ${FRONTEND_DESTINATION}
    ${FRONTEND_EXTRA_OPTIONS}
  )

  if (NOT EXISTS ${FRONTEND_DESTINATION}/CMakeLists.txt)
    execute_process(COMMAND ${GENERATE_COMMAND})
  endif()

  add_custom_target(${FRONTEND_TARGET}
    COMMAND ${GENERATE_COMMAND}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  )

  add_custom_target(${FRONTEND_TARGET}_force
    COMMAND ${GENERATE_COMMAND} --force
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  )
endfunction()
