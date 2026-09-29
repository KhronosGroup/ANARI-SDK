## Copyright 2021-2026 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

enable_testing()

project(anariUnitTests LANGUAGES CXX)

add_executable(${PROJECT_NAME}
  catch_main.cpp

  test_helide_geometry_bounds.cpp
  test_helide_image3d_sampler.cpp
  test_helide_instance.cpp
  test_helide_primitive_sampler.cpp
  test_helide_tf1d_opacity.cpp
  test_helide_transform_sampler.cpp
  test_helide_volume_instances.cpp
  test_helide_volume_interval.cpp
  test_helide_wait_query.cpp
  test_helide_world_bounds.cpp
  test_helium_AnariAny.cpp
  test_helium_array_sampling.cpp
  test_helium_commit_snapshot.cpp
  test_helium_concurrent_updates.cpp
  test_helium_frame_callback.cpp
  test_helium_ParameterizedObject.cpp
  test_helium_RefCounted.cpp
  test_helium_srgb.cpp
  test_helium_TaskQueue.cpp
  test_helium_wait_query.cpp
  test_helium_wrap_index.cpp
)

# helium for the direct base-layer tests; anari (the loader) for the device-
# backed tests (commit-snapshot, helide sphere/curve bounds checks, instance
# normals and id arrays, image3D, primitive and transform samplers,
# transferFunction1D opacity, volume interval selection, volumes in instances,
# WAIT queries while rendering, world bounds), which load helide and skip
# themselves if absent.
target_link_libraries(${PROJECT_NAME} PRIVATE helium anari)
find_package(Threads REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE Threads::Threads)

add_test(NAME unit_test::helium::AnariAny            COMMAND ${PROJECT_NAME} "[helium_AnariAny]"           )
add_test(NAME unit_test::helium::ArraySampling       COMMAND ${PROJECT_NAME} "[helium_array_sampling]"     )
add_test(NAME unit_test::helium::ParameterizedObject COMMAND ${PROJECT_NAME} "[helium_ParameterizedObject]")
add_test(NAME unit_test::helium::RefCounted          COMMAND ${PROJECT_NAME} "[helium_RefCounted]"         )
add_test(NAME unit_test::helium::SRGB                COMMAND ${PROJECT_NAME} "[helium_srgb]"               )
add_test(NAME unit_test::helium::TaskQueue           COMMAND ${PROJECT_NAME} "[helium_TaskQueue]"          )
add_test(NAME unit_test::helium::WrapIndex           COMMAND ${PROJECT_NAME} "[helium_wrap_index]"         )
add_test(NAME unit_test::helium::CommitSnapshot      COMMAND ${PROJECT_NAME} "[helium_commit_snapshot]"    )
add_test(NAME unit_test::helium::ConcurrentUpdates   COMMAND ${PROJECT_NAME} "[helium_concurrent_updates]" )
add_test(NAME unit_test::helium::FrameCallback       COMMAND ${PROJECT_NAME} "[helium_frame_callback]"     )
add_test(NAME unit_test::helium::WaitQuery           COMMAND ${PROJECT_NAME} "[helium_wait_query]"         )
add_test(NAME unit_test::helide::GeometryBounds      COMMAND ${PROJECT_NAME} "[helide_geometry_bounds]"    )
add_test(NAME unit_test::helide::Image3DSampler      COMMAND ${PROJECT_NAME} "[helide_image3d_sampler]"    )
add_test(NAME unit_test::helide::InstanceNormals     COMMAND ${PROJECT_NAME} "[helide_instance]"           )
add_test(NAME unit_test::helide::InstanceIdArray     COMMAND ${PROJECT_NAME} "[helide_instance_id]"        )
add_test(NAME unit_test::helide::PrimitiveSampler    COMMAND ${PROJECT_NAME} "[helide_primitive_sampler]"  )
add_test(NAME unit_test::helide::Tf1dOpacity         COMMAND ${PROJECT_NAME} "[helide_tf1d_opacity]"       )
add_test(NAME unit_test::helide::TransformSampler    COMMAND ${PROJECT_NAME} "[helide_transform_sampler]"  )
add_test(NAME unit_test::helide::VolumeInterval      COMMAND ${PROJECT_NAME} "[helide_volume_interval]"    )
add_test(NAME unit_test::helide::VolumeInstances     COMMAND ${PROJECT_NAME} "[helide_volume_instances]"   )
add_test(NAME unit_test::helide::WaitQuery           COMMAND ${PROJECT_NAME} "[helide_wait_query]"         )
add_test(NAME unit_test::helide::WorldBounds         COMMAND ${PROJECT_NAME} "[helide_world_bounds]"       )

# A completion-callback or device-work regression hangs rather than fails.
set_tests_properties(unit_test::helium::FrameCallback
  unit_test::helium::WaitQuery unit_test::helide::WaitQuery
  PROPERTIES TIMEOUT 60)

## CTS conformance-harness catalog tests ##

add_executable(anariCatalogTests
  catch_main.cpp

  test_cts_builtin_truths.cpp
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
