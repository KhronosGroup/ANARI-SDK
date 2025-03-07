## Copyright 2021-2025 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

set(BLENDER_DIR "" CACHE PATH "Blender Directory")

if(BLENDER_DIR)
  file(GLOB BLENDER_VERSION_DIR ${BLENDER_DIR}/[2-4].[0-9])
  # Get Blender version from path
  string(REGEX REPLACE "/+$" "" BLENDER_PATH "${BLENDER_VERSION_DIR}")
  string(REGEX REPLACE ".*/([^/]+)$" "\\1" BLENDER_VERSION "${BLENDER_PATH}")

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
          ${CMAKE_BINARY_DIR}/pyanari.so
        DESTINATION ${BLENDER_PYTHON_LIBS}
      )
      if (${BLENDER_VERSION} VERSION_LESS 4.3)
        install(
          FILES
            CustomAnariRender.py
          DESTINATION ${BLENDER_VERSION_DIR}/scripts/addons/
        )
      else()
        install(
          FILES
            CustomAnariRender.py
          DESTINATION ${BLENDER_VERSION_DIR}/scripts/addons_core/
        )
      endif()
  endif()
endif()
