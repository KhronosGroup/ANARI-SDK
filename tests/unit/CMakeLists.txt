## Copyright 2021-2026 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

enable_testing()

project(anariUnitTests LANGUAGES CXX)

add_executable(${PROJECT_NAME}
  catch_main.cpp

  test_helium_AnariAny.cpp
  test_helium_array1d_begin.cpp
  test_helium_array_privatize.cpp
  test_helium_array_sampling.cpp
  test_helium_commit_buffer_observers.cpp
  test_helium_commit_snapshot.cpp
  test_helium_concurrent_updates.cpp
  test_helium_frame_callback.cpp
  test_helium_object_array.cpp
  test_helium_ParameterizedObject.cpp
  test_helium_RefCounted.cpp
  test_helium_srgb.cpp
  test_helium_TaskQueue.cpp
  test_helium_wait_query.cpp
  test_helium_wrap_index.cpp

  test_hdanari_uvTextureTransform.cpp
  ../../src/hdanari/rd/material/uvTextureTransform.cpp
)

# helium for the direct base-layer tests; anari (the loader) for the device-
# backed tests (commit-snapshot, helide completion callbacks), which load helide
# and skip themselves if absent.
target_link_libraries(${PROJECT_NAME} PRIVATE helium anari)
find_package(Threads REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE Threads::Threads)
# hdanari's USD-free helpers are compiled in directly, so they're tested
# without USD.
target_include_directories(${PROJECT_NAME}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../src)

add_test(NAME unit_test::helium::AnariAny            COMMAND ${PROJECT_NAME} "[helium_AnariAny]"           )
add_test(NAME unit_test::helium::Array1DBegin        COMMAND ${PROJECT_NAME} "[helium_array1d_begin]"      )
add_test(NAME unit_test::helium::ArrayPrivatize      COMMAND ${PROJECT_NAME} "[helium_array_privatize]"    )
add_test(NAME unit_test::helium::ArraySampling       COMMAND ${PROJECT_NAME} "[helium_array_sampling]"     )
add_test(NAME unit_test::helium::ParameterizedObject COMMAND ${PROJECT_NAME} "[helium_ParameterizedObject]")
add_test(NAME unit_test::helium::RefCounted          COMMAND ${PROJECT_NAME} "[helium_RefCounted]"         )
add_test(NAME unit_test::helium::SRGB                COMMAND ${PROJECT_NAME} "[helium_srgb]"               )
add_test(NAME unit_test::helium::TaskQueue           COMMAND ${PROJECT_NAME} "[helium_TaskQueue]"          )
add_test(NAME unit_test::helium::WrapIndex           COMMAND ${PROJECT_NAME} "[helium_wrap_index]"         )
add_test(NAME unit_test::helium::CommitSnapshot      COMMAND ${PROJECT_NAME} "[helium_commit_snapshot]"    )
add_test(NAME unit_test::helium::ConcurrentUpdates   COMMAND ${PROJECT_NAME} "[helium_concurrent_updates]" )
add_test(NAME unit_test::helium::FrameCallback       COMMAND ${PROJECT_NAME} "[helium_frame_callback]"     )
add_test(NAME unit_test::helium::ObjectArray         COMMAND ${PROJECT_NAME} "[helium_object_array]"       )
add_test(NAME unit_test::helium::WaitQuery           COMMAND ${PROJECT_NAME} "[helium_wait_query]"         )

# A completion-callback or device-work regression hangs rather than fails
# (as do privatizing an array as device work, a flush nested in a flush, and
# re-rendering after a range-only array commit).
set_tests_properties(unit_test::helium::FrameCallback
  unit_test::helium::ObjectArray
  unit_test::helium::ConcurrentUpdates
  unit_test::helium::WaitQuery
  PROPERTIES TIMEOUT 60)

add_test(NAME unit_test::hdanari::UVTextureTransform COMMAND ${PROJECT_NAME} "[hdanari_uvTextureTransform]")

## CTS conformance-harness catalog tests ##

add_executable(anariCatalogTests
  catch_main.cpp

  test_cts_catalog.cpp
  test_cts_deviceinfo.cpp
  test_cts_metrics.cpp
  test_cts_rendererparams.cpp
  test_cts_report.cpp
  test_cts_results.cpp
  test_cts_runner.cpp
  test_cts_worldbuilder.cpp
)

target_link_libraries(anariCatalogTests
PRIVATE
  anari_cts_core
)

# Pure harness/metrics/results tests run anywhere; the device-backed tests
# (world-build smoke + runner integration) need helide and skip themselves if no
# device can be loaded.
add_test(NAME unit_test::cts::catalog COMMAND anariCatalogTests "[cts]~[helide]")
add_test(NAME unit_test::cts::device COMMAND anariCatalogTests "[helide]")
