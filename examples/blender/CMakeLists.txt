## Copyright 2021 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

set(BLENDER_DIR "" CACHE PATH "Blender Directory")

if(BLENDER_DIR)
  file(GLOB BLENDER_VERSION_DIR ${BLENDER_DIR}/[2-4].[0-9])
  if(BLENDER_VERSION_DIR)
      file(GLOB BLENDER_PYTHON_EXECUTABLE ${BLENDER_VERSION_DIR}/python/bin/python3.*)
      file(GLOB BLENDER_PYTHON_LIBS ${BLENDER_VERSION_DIR}/python/lib/python3.*)

      add_custom_target(blender_anari ALL
        COMMAND
          ${BLENDER_PYTHON_EXECUTABLE} -m ensurepip &&
          ${BLENDER_PYTHON_EXECUTABLE} -m pip install cffi
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        DEPENDS python_bindings
      )

      install(
        FILES
          ${CMAKE_BINARY_DIR}/code_gen/anari.py
          ${CMAKE_BINARY_DIR}/code_gen/pyanari.so
        DESTINATION ${BLENDER_PYTHON_LIBS}
      )
      install(
        FILES
          CustomAnariRender.py
        DESTINATION ${BLENDER_VERSION_DIR}/scripts/addons/
      )
  endif()
endif()
