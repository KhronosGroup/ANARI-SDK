## Copyright 2022-2026 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

# The CTS is a self-contained C++ tool (`anariCts`) plus a thin Python reporting
# layer (ctsReport.py) that only reads the per-Case sidecar tree. The former
# pybind/Python backend, the JSON scene format, and the orchestration scripts
# have been removed.
#
# The anariCts binary's entry point lives here (src/main.cpp). The CTS harness,
# runner, and Test definitions it drives compile into anari_test_scenes (see
# src/anari_test_scenes/cts/), so anariCts just links that library.
#
# This directory also provides:
#   - the optional glTF sample-asset payload the glTF asset-scan factory reads
#     (in-tree under gltf-Sample-Assets/; the factory registers no glTF Tests if
#     it is absent), and
#   - installation of the Python reporting layer.
#
# It is added to the build when BUILD_CTS, CTS_ENABLE_GLTF, or BUILD_TESTING is set.

## CTS conformance tool ##

# The anariCts CLI binary. main.cpp's `cts/*.h` includes resolve through
# anari_test_scenes's PUBLIC include dirs (the harness lives in that library).
# Built when the CTS is requested (BUILD_CTS) or for the test suite (BUILD_TESTING).
if (BUILD_CTS OR BUILD_TESTING)
  add_executable(anariCts src/main.cpp)
  target_link_libraries(anariCts
  PRIVATE
    anari_test_scenes
    anari_device_introspection
  )
  target_compile_features(anariCts PRIVATE cxx_std_20)

  if (BUILD_CTS)
    install(TARGETS anariCts RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  endif()
endif()

if (BUILD_TESTING)
  # Device-free smoke tests of the tool surface. (Device-backed run/generate
  # are covered by the runner integration test in tests/unit.)
  add_test(NAME cts_tool::list COMMAND anariCts list)
  add_test(NAME cts_tool::query-metadata COMMAND anariCts query-metadata)

  find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
  add_test(
    NAME cts_report::contract
    COMMAND ${Python3_EXECUTABLE} -m unittest cts.test_cts_report
  )
  set_tests_properties(cts_report::contract PROPERTIES
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
  )
endif()

## glTF sample assets ##

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
