## Copyright 2026 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

find_package(anari REQUIRED)

# The tests dlopen the device through libanari, which would otherwise find a
# previously installed copy next to itself; put the freshly built device (and
# the USD libraries) first on the loader path.
set(_anari_hydra_test_libpath
  "$<TARGET_FILE_DIR:anari_library_hydra>:${pxr_DIR}/lib")

# The device makes its own GL context (EGL headless, else GLX, else none), so
# the tests run without a display. Tests that need GL (Storm) run through
# hydra_gl_gate, which exits with the ctest skip code only when the machine has
# no GL context at all; tests of CPU delegates (Embree) always run.
add_executable(hydra_gl_gate gl_gate.cpp)
target_link_libraries(hydra_gl_gate PRIVATE anari::anari)

# Test and target names carry a hydra_ prefix so they cannot collide with the
# SDK's own targets or match its CI test filters (unit_test, render_test).

# anari_hydra_add_test(<name> <executable> [args...]): register test
# hydra_<name> running target hydra_<executable>. Tests whose delegate is
# storm (or the default, which forwards to Storm) need GL.
function(anari_hydra_add_test name exe)
  set(_launcher)
  if(NOT ARGN OR "${ARGV2}" STREQUAL "storm")
    set(_launcher $<TARGET_FILE:hydra_gl_gate>)
  endif()
  add_test(NAME hydra_${name}
    COMMAND ${_launcher} $<TARGET_FILE:hydra_${exe}> ${ARGN})
  set_tests_properties(hydra_${name} PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${_anari_hydra_test_libpath}"
    SKIP_RETURN_CODE 77
    TIMEOUT 180)
endfunction()

# anari_hydra_add_executable_test(<executable> <delegate>...): build
# <executable>.cpp as hydra_<executable> and register
# hydra_<executable>_<delegate> for each delegate.
function(anari_hydra_add_executable_test exe)
  add_executable(hydra_${exe} ${exe}.cpp)
  target_link_libraries(hydra_${exe} PRIVATE anari::anari)
  foreach(delegate ${ARGN})
    anari_hydra_add_test(${exe}_${delegate} ${exe} ${delegate})
  endforeach()
endfunction()

anari_hydra_add_executable_test(render_triangle storm embree)
anari_hydra_add_executable_test(render_geometry storm)
anari_hydra_add_executable_test(render_geometry_edit storm embree)
anari_hydra_add_executable_test(render_material storm)
anari_hydra_add_executable_test(render_instancing storm)
anari_hydra_add_executable_test(render_lights storm)
anari_hydra_add_executable_test(render_instanced_lights storm)
anari_hydra_add_executable_test(render_sampler storm)
anari_hydra_add_executable_test(render_sampler_transforms storm)
anari_hydra_add_executable_test(render_attribute storm)
anari_hydra_add_executable_test(render_instance_override storm embree)
anari_hydra_add_executable_test(render_instance_array storm)
anari_hydra_add_executable_test(render_normal_map storm)
anari_hydra_add_executable_test(render_hdri storm embree)
anari_hydra_add_executable_test(render_ambient storm embree)
anari_hydra_add_executable_test(render_background storm)
anari_hydra_add_executable_test(render_settings embree)
anari_hydra_add_executable_test(render_aov storm embree)
anari_hydra_add_executable_test(render_aov_named embree)
anari_hydra_add_executable_test(reflect_settings storm embree)
anari_hydra_add_executable_test(query_extensions storm embree)
anari_hydra_add_executable_test(query_default_delegate)
anari_hydra_add_test(query_default_delegate query_default_delegate)
anari_hydra_add_executable_test(frame_duration storm embree)
anari_hydra_add_executable_test(render_normals storm)
anari_hydra_add_executable_test(render_picking storm embree)
anari_hydra_add_executable_test(render_spheres storm embree)
anari_hydra_add_executable_test(render_camera storm embree)
anari_hydra_add_executable_test(query_bounds storm embree)
anari_hydra_add_executable_test(query_geometry_params storm embree)
anari_hydra_add_executable_test(render_image_precision storm)
anari_hydra_add_executable_test(render_sampler_derived storm)
anari_hydra_add_executable_test(render_sampler_border storm)
set_tests_properties(hydra_render_sampler_border_storm PROPERTIES TIMEOUT 300)
anari_hydra_add_executable_test(render_normal_channel storm embree)
anari_hydra_add_executable_test(query_object_info storm embree)
anari_hydra_add_executable_test(frame_callback storm embree)
anari_hydra_add_executable_test(render_background_image storm embree)
anari_hydra_add_executable_test(render_volume storm embree)
anari_hydra_add_executable_test(render_isosurface storm embree)

# GL context selection (ANARI_HYDRA_GL) and the clean refusal of a storm
# device without GL. Not gated: it covers the no-GL case itself.
add_executable(hydra_device_gl_mode device_gl_mode.cpp)
target_link_libraries(hydra_device_gl_mode PRIVATE anari::anari)
add_test(NAME hydra_device_gl_mode_embree COMMAND hydra_device_gl_mode)
set_tests_properties(hydra_device_gl_mode_embree PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${_anari_hydra_test_libpath}"
  TIMEOUT 180)

# A GL app (like Vela) with its own GLX context current, before and after the
# device's creation. Skips without an X display.
find_package(X11)
find_package(OpenGL COMPONENTS GLX)
if(X11_FOUND AND TARGET OpenGL::GLX)
  add_executable(hydra_render_app_gl render_app_gl.cpp)
  target_link_libraries(hydra_render_app_gl PRIVATE
    anari::anari OpenGL::GL OpenGL::GLX X11::X11)
  add_test(NAME hydra_render_app_gl_app_first_storm
    COMMAND hydra_render_app_gl app-first)
  add_test(NAME hydra_render_app_gl_device_first_storm
    COMMAND hydra_render_app_gl device-first)
  set_tests_properties(
    hydra_render_app_gl_app_first_storm hydra_render_app_gl_device_first_storm
    PROPERTIES
      ENVIRONMENT "LD_LIBRARY_PATH=${_anari_hydra_test_libpath}"
      SKIP_RETURN_CODE 77
      TIMEOUT 180)
endif()

# White-box guard of the generated normal-mapping surface against OpenUSD
# drift: it reads Sdr/Hio directly and shares the device's shader source header.
find_package(pxr REQUIRED)
anari_hydra_add_executable_test(guard_normal_map_shader storm)
target_include_directories(hydra_guard_normal_map_shader PRIVATE
  ${PROJECT_SOURCE_DIR}/scene)
target_link_libraries(hydra_guard_normal_map_shader PRIVATE sdr hio sdf tf vt gf)
target_compile_features(hydra_guard_normal_map_shader PRIVATE cxx_std_17)

anari_hydra_add_test(render_sampler_facevarying_storm
  render_sampler storm faceVarying)
anari_hydra_add_test(render_sampler_transforms_facevarying_storm
  render_sampler_transforms storm faceVarying)
anari_hydra_add_test(render_normals_facevarying_storm
  render_normals storm faceVarying)

# A library directory shared with other packages: a copy of the device beside
# its anari_hydra/ manifest, plus another package's top-level plugInfo.json (a
# decoy that would hijack the "anari" image type if loaded). The device must
# register only its own manifest directory, so textures still resolve.
set(_anari_hydra_shared_lib "${CMAKE_CURRENT_BINARY_DIR}/shared_prefix/lib")
add_custom_target(hydra_shared_prefix_layout ALL
  COMMAND ${CMAKE_COMMAND} -E make_directory
    ${_anari_hydra_shared_lib}/anari_hydra
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:anari_library_hydra> ${_anari_hydra_shared_lib}/
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE_DIR:anari_library_hydra>/anari_hydra/plugInfo.json
    ${_anari_hydra_shared_lib}/anari_hydra/
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_CURRENT_SOURCE_DIR}/decoy_plugInfo.json
    ${_anari_hydra_shared_lib}/plugInfo.json
  VERBATIM)
add_dependencies(hydra_shared_prefix_layout anari_library_hydra)
anari_hydra_add_test(render_sampler_shared_prefix_storm render_sampler storm)
set_tests_properties(hydra_render_sampler_shared_prefix_storm PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${_anari_hydra_shared_lib}:${pxr_DIR}/lib")

if(TARGET anari_hydra_nanovdb)
  # The nanovdb section builds its grids with the same NanoVDB headers.
  target_link_libraries(hydra_render_volume PRIVATE anari_hydra_nanovdb)
endif()

# The *_embree tests need USD's hdEmbree render delegate plugin, which is an
# optional part of a USD build; without it they fail (or pass vacuously), so
# keep them registered but disabled.
if(NOT EXISTS "${pxr_DIR}/plugin/usd/hdEmbree/resources/plugInfo.json")
  message(STATUS "hdEmbree plugin not found in ${pxr_DIR}; disabling *_embree tests")
  get_directory_property(_anari_hydra_tests TESTS)
  list(FILTER _anari_hydra_tests INCLUDE REGEX "_embree$")
  set_tests_properties(${_anari_hydra_tests} PROPERTIES DISABLED TRUE)
endif()
