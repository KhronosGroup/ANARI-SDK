## Copyright 2022-2026 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

# The CTS is now a self-contained C++ tool (`cts`, target anariCts) built by
# src/anari_test_scenes, plus a thin Python reporting layer (ctsReport.py) that
# only reads the per-Case sidecar tree. The former pybind/Python backend, the
# JSON scene format, and the orchestration scripts have been removed.
#
# This directory provides:
#   - the optional glTF sample-asset payload the glTF asset-scan factory reads
#     (in-tree under gltf-Sample-Assets/; the factory registers no glTF Tests if
#     it is absent), and
#   - installation of the Python reporting layer.
#
# It is added to the build when BUILD_CTS or CTS_ENABLE_GLTF is set.

if (CTS_ENABLE_GLTF AND NOT EXISTS ${CMAKE_CURRENT_LIST_DIR}/gltf-Sample-Assets)
  message(WARNING
    "CTS_ENABLE_GLTF is ON but ${CMAKE_CURRENT_LIST_DIR}/gltf-Sample-Assets is "
    "missing; the glTF asset tests will register nothing. Fetch the assets from "
    "https://github.com/KhronosGroup/glTF-Sample-Assets into that directory.")
endif()

## Installation ##

if (INSTALL_CTS)
  install(FILES
    ${CMAKE_CURRENT_LIST_DIR}/ctsReport.py
    ${CMAKE_CURRENT_LIST_DIR}/pyproject.toml
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/anari/cts
  )
  if (CTS_ENABLE_GLTF AND EXISTS ${CMAKE_CURRENT_LIST_DIR}/gltf-Sample-Assets)
    install(DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/gltf-Sample-Assets
      DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/anari/cts
    )
  endif()
endif()
