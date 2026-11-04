## Copyright 2026 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

## MPI tests for the ptc device (wrapping helide) ##
#
# Each test runs one of the test programs under mpiexec. In ptcSmoke every rank
# renders one sphere through ptc; rank 0 checks the composited image and all
# ranks exit non-zero if any check failed. The tests set LD_LIBRARY_PATH
# themselves, so no manual environment setup is needed. Test names start with
# ptc_, so they do not match the SDK's own test filters (unit_test,
# render_test).

if (NOT MPIEXEC_EXECUTABLE)
  message(WARNING "ptc: mpiexec not found, skipping the ptc tests")
  return()
endif()

# ptc wraps helide in the tests: the SDK's own (BUILD_HELIDE_DEVICE), or,
# when ptc is built on its own, the one installed with the found SDK.
if (TARGET anari_library_helide)
  set(PTC_TEST_HELIDE anari_library_helide)
else()
  set(PTC_TEST_HELIDE anari::anari_library_helide)
endif()
message(STATUS "ptc: testing with ${MPIEXEC_EXECUTABLE}")

# The tests check that ptc reports its own version
set(PTC_TEST_VERSION_DEFINITIONS
  PTC_VERSION_MAJOR=${PTC_VERSION_MAJOR}
  PTC_VERSION_MINOR=${PTC_VERSION_MINOR}
  PTC_VERSION_PATCH=${PTC_VERSION_PATCH})

add_executable(ptcSmoke ptcSmoke.cpp SmokeHarness.cpp SmokeChecks.cpp
  SmokeCallbacks.cpp SmokeDiagnostics.cpp)
target_link_libraries(ptcSmoke PRIVATE anari::anari MPI::MPI_CXX)
target_compile_definitions(ptcSmoke PRIVATE ${PTC_TEST_VERSION_DEFINITIONS})
# ptc and helide are loaded at runtime by anariLoadLibrary, so make sure they
# are built
add_dependencies(ptcSmoke anari_library_ptc ${PTC_TEST_HELIDE})

set(PTC_TEST_RANKS 1 2 3)
set(PTC_TEST_COLORS ufixed8 float32)

# Open MPI refuses to start more ranks than the host has slots (cores) unless
# told to oversubscribe, so the 3- and 4-rank tests would fail on small CI
# runners. MPICH and others oversubscribe without being asked.
execute_process(COMMAND ${MPIEXEC_EXECUTABLE} --version
  OUTPUT_VARIABLE PTC_MPIEXEC_VERSION
  ERROR_VARIABLE PTC_MPIEXEC_VERSION)
set(PTC_MPIEXEC_OVERSUBSCRIBE "")
if (PTC_MPIEXEC_VERSION MATCHES "Open MPI|OpenRTE|open-mpi")
  set(PTC_MPIEXEC_OVERSUBSCRIBE --oversubscribe)
  message(STATUS "ptc: mpiexec is Open MPI, multi-rank tests pass --oversubscribe")
endif()

# The mpiexec command line that starts <ranks> ranks of what follows it.
function(ptc_mpiexec out ranks)
  set(flags "")
  if (ranks GREATER 1)
    set(flags ${PTC_MPIEXEC_OVERSUBSCRIBE})
  endif()
  set(${out}
    ${MPIEXEC_EXECUTABLE} ${MPIEXEC_NUMPROC_FLAG} ${ranks} ${flags}
    ${MPIEXEC_PREFLAGS}
    PARENT_SCOPE)
endfunction()

# Run ptcSmoke on <ranks> ranks with the given extra arguments.
function(ptc_smoke_command out ranks)
  ptc_mpiexec(mpiexec ${ranks})
  set(${out}
    ${mpiexec} $<TARGET_FILE:ptcSmoke> ${MPIEXEC_POSTFLAGS}
    --expect-library $<TARGET_FILE:anari_library_ptc> ${ARGN}
    PARENT_SCOPE)
endfunction()

# The freshly built ptc and the helide it wraps must come first on the search
# path, ahead of any other installed copies. ptcSmoke also checks which ptc
# library it actually loaded (--expect-library).
# PTC_ANARI_LIBRARY is helide, or the optional third argument.
function(ptc_set_test_environment test ranks)
  set(envLibrary helide)
  if (ARGC GREATER 2)
    set(envLibrary ${ARGV2})
  endif()
  set(libdirs
    "$<TARGET_FILE_DIR:anari_library_ptc>"
    "$<TARGET_FILE_DIR:${PTC_TEST_HELIDE}>")
  set_tests_properties(${test} PROPERTIES
    TIMEOUT 120
    PROCESSORS ${ranks}
    ENVIRONMENT "PTC_ANARI_LIBRARY=${envLibrary}")
  if (CMAKE_VERSION VERSION_GREATER_EQUAL 3.22)
    # each prepend goes in front of the previous one, so add in reverse
    set(mods "")
    list(REVERSE libdirs)
    foreach(dir ${libdirs})
      list(APPEND mods "LD_LIBRARY_PATH=path_list_prepend:${dir}")
    endforeach()
    set_tests_properties(${test} PROPERTIES ENVIRONMENT_MODIFICATION "${mods}")
  else()
    # Note: this captures LD_LIBRARY_PATH at configure time.
    string(REPLACE ";" ":" path "${libdirs}")
    if (DEFINED ENV{LD_LIBRARY_PATH} AND NOT "$ENV{LD_LIBRARY_PATH}" STREQUAL "")
      string(APPEND path ":$ENV{LD_LIBRARY_PATH}")
    endif()
    set_property(TEST ${test} APPEND PROPERTY ENVIRONMENT
      "LD_LIBRARY_PATH=${path}")
  endif()
endfunction()

# Add ptc_smoke_<name>_<ranks>ranks for each rank count in RANKS (default
# PTC_TEST_RANKS), with PTC_ANARI_LIBRARY=<ENV_LIBRARY> (default helide).
function(ptc_add_smoke_tests name)
  cmake_parse_arguments(PARSE_ARGV 1 arg "" "ENV_LIBRARY" "RANKS")
  if (NOT arg_RANKS)
    set(arg_RANKS ${PTC_TEST_RANKS})
  endif()
  foreach(ranks ${arg_RANKS})
    set(test ptc_smoke_${name}_${ranks}ranks)
    ptc_smoke_command(cmd ${ranks} ${arg_UNPARSED_ARGUMENTS})
    add_test(NAME ${test} COMMAND ${cmd})
    ptc_set_test_environment(${test} ${ranks} ${arg_ENV_LIBRARY})
  endforeach()
endfunction()

# Each color runs twice: without channel.depth (mapping depth must be refused)
# and with it (--depth: composited depth must be correct).
foreach(color ${PTC_TEST_COLORS})
  ptc_add_smoke_tests(${color} --color ${color})
  ptc_add_smoke_tests(${color}_depth --color ${color} --depth)
endforeach()

# Frame parameters changing between two frames. Each also flips
# channel.depth between the frames.
foreach(color ${PTC_TEST_COLORS})
  ptc_add_smoke_tests(${color}_resize --color ${color} --resize)
endforeach()
ptc_add_smoke_tests(retype_float32_to_ufixed8
  --first-color float32 --color ufixed8 --depth)
ptc_add_smoke_tests(retype_ufixed8_to_float32
  --first-color ufixed8 --color float32 --depth)
# same pixel size
ptc_add_smoke_tests(retype_ufixed8_linear_to_ufixed8
  --first-color ufixed8-linear --color ufixed8)
# no channel.color: nothing to map, but depth still composites
ptc_add_smoke_tests(nocolor_depth --color none --depth)
ptc_add_smoke_tests(unset_color --first-color ufixed8 --color none --depth)
ptc_add_smoke_tests(unset_all
  --unset-all --resize --first-color ufixed8-linear --color ufixed8)
ptc_add_smoke_tests(unset_all_depth --unset-all --color ufixed8 --depth)
foreach(color ${PTC_TEST_COLORS})
  ptc_add_smoke_tests(${color}_uncommitted --color ${color} --uncommitted)
endforeach()

# Blending: every rank's background differs. Where all ranks miss,
# the composited pixel must match premultiplied "over" of the backgrounds,
# computed in linear space, for every color type ptc composites. With opaque
# backgrounds, only the front one shows and must round-trip unchanged.
foreach(color ufixed8 ufixed8-linear float32)
  ptc_add_smoke_tests(${color}_blend --color ${color} --background blend
    --depth)
  ptc_add_smoke_tests(${color}_opaque --color ${color} --background opaque)
endforeach()

# ID channels: each pixel's primitiveId/objectId/instanceId is the
# nearest fragment's. Also with no color, with ids enabled or disabled between
# frames, and after unset-all (ids_split is below). Every test checks
# that channels ptc does not composite (normal, albedo) are refused.
ptc_add_smoke_tests(ids --color ufixed8 --ids)
ptc_add_smoke_tests(nocolor_ids --color none --ids)
ptc_add_smoke_tests(ids_resize --color float32 --resize --ids)
ptc_add_smoke_tests(ids_unset_all --color ufixed8 --unset-all --ids)

# A color type ptc cannot composite: committing it reports an ERROR with
# ANARI_STATUS_INVALID_ARGUMENT, rendering then reports an ERROR through the
# status callback on every rank (no exception escapes anariRenderFrame), and
# there is nothing to map.
ptc_add_smoke_tests(unsupported_color --color float32-vec3)
# The same on the last rank only: the ranks agree that the render
# failed instead of the others hanging in the compositor, so every rank
# reports an ERROR and returns.
ptc_add_smoke_tests(fail_last_rank --color ufixed8 --fail-last-rank
  RANKS 2 3)
ptc_add_smoke_tests(fail_last_rank_ids --color ufixed8 --ids --depth
  --fail-last-rank RANKS 2 3)
# The frame completion callback still runs after a failed render, once the
# ERROR has been reported.
ptc_add_smoke_tests(unsupported_color_callback --color float32-vec3 --callback
  RANKS 1 2)
ptc_add_smoke_tests(fail_last_rank_callback --color ufixed8 --fail-last-rank
  --callback RANKS 2 3)

# Parameters ptc tracks, set with the wrong type, warn at commit.
ptc_add_smoke_tests(wrong_types --color ufixed8 --wrong-types RANKS 1 2)

# Frame completion callback: called once per render with the ptc
# handles, after compositing; not called once unset.
ptc_add_smoke_tests(callback --color ufixed8 --callback)
ptc_add_smoke_tests(callback_resize --color ufixed8 --resize --depth --callback)
ptc_add_smoke_tests(callback_unset_all --color ufixed8 --unset-all --callback)
# Rendering or committing the frame from inside its callback is refused.
ptc_add_smoke_tests(callback_reenter --color ufixed8 --callback --reenter
  RANKS 1 2)

# mpiCommunicator: MPI_COMM_WORLD split into two halves, each
# compositing its own image to its own rank 0. Also switching communicators
# between frames: back to 'world' (unsetting the parameter), to a congruent
# but different communicator (kept), and to different ranks (torn down).
set(PTC_SPLIT_RANKS RANKS 2 4)
foreach(color ${PTC_TEST_COLORS})
  ptc_add_smoke_tests(${color}_split --color ${color} --comm split --depth
    ${PTC_SPLIT_RANKS})
endforeach()
ptc_add_smoke_tests(world_to_split --first-comm world --comm split --depth
  ${PTC_SPLIT_RANKS})
ptc_add_smoke_tests(split_to_world --first-comm split --comm world --depth
  ${PTC_SPLIT_RANKS})
ptc_add_smoke_tests(split_to_split --first-comm split --comm split --resize
  --depth ${PTC_SPLIT_RANKS})
ptc_add_smoke_tests(split_unset_all --first-comm split --comm world --unset-all
  --depth ${PTC_SPLIT_RANKS})
ptc_add_smoke_tests(split_callback --comm split --callback ${PTC_SPLIT_RANKS})
# on 2 ranks, halves and even/odd ranks are the same split
ptc_add_smoke_tests(split_to_interleaved --first-comm split
  --comm interleaved --depth RANKS 4)
# each half blends its own ranks' backgrounds
ptc_add_smoke_tests(ufixed8_blend_split --color ufixed8 --background blend
  --comm split ${PTC_SPLIT_RANKS})
# each half composites its own ids
ptc_add_smoke_tests(ids_split --color ufixed8 --comm split --ids
  ${PTC_SPLIT_RANKS})

# Wrapped library selection. The device parameter
# 'wrappedLibrary' wins over PTC_ANARI_LIBRARY, which here names a library
# that does not exist: loading helide through the parameter must not warn,
# and the image must be right.
set(PTC_TEST_MISSING_LIBRARY ptcTestNoSuchLibrary)
ptc_add_smoke_tests(wrapped_library_parameter --color ufixed8 --depth
  --wrapped-library helide ENV_LIBRARY ${PTC_TEST_MISSING_LIBRARY})

# Wrapped library load failure and fallback: a missing
# 'wrappedLibrary' is a FATAL_ERROR after which no API call crashes; a missing
# PTC_ANARI_LIBRARY falls back to helide with a WARNING.
add_executable(ptcWrapped ptcWrapped.cpp)
target_link_libraries(ptcWrapped PRIVATE anari::anari MPI::MPI_CXX)
target_compile_definitions(ptcWrapped PRIVATE ${PTC_TEST_VERSION_DEFINITIONS})
add_dependencies(ptcWrapped anari_library_ptc ${PTC_TEST_HELIDE})
foreach(scenario missing fallback)
  if (scenario STREQUAL "missing")
    set(envLibrary helide)
  else()
    set(envLibrary ${PTC_TEST_MISSING_LIBRARY})
  endif()
  foreach(ranks 1 2)
    set(test ptc_wrapped_${scenario}_${ranks}ranks)
    ptc_mpiexec(mpiexec ${ranks})
    add_test(NAME ${test} COMMAND
      ${mpiexec} $<TARGET_FILE:ptcWrapped> ${MPIEXEC_POSTFLAGS}
      --expect-library $<TARGET_FILE:anari_library_ptc>
      --scenario ${scenario})
    ptc_set_test_environment(${test} ${ranks} ${envLibrary})
  endforeach()
endforeach()

# MPI lifetime: with and without the app initializing MPI, ptc
# keeps MPI initialized until its last device is released, and finalizes it
# only if it initialized it. Device release is collective, so every rank
# releases the same devices in the same order.
add_executable(ptcLifetime ptcLifetime.cpp)
target_link_libraries(ptcLifetime PRIVATE anari::anari MPI::MPI_CXX)
add_dependencies(ptcLifetime anari_library_ptc ${PTC_TEST_HELIDE})
foreach(scenario two-devices single query-release)
  foreach(mpi app ptc)
    set(extra "")
    if (mpi STREQUAL "app")
      set(extra --app-mpi)
    endif()
    foreach(ranks 1 2)
      set(test ptc_lifetime_${scenario}_${mpi}_mpi_${ranks}ranks)
      ptc_mpiexec(mpiexec ${ranks})
      add_test(NAME ${test} COMMAND
        ${mpiexec} $<TARGET_FILE:ptcLifetime> ${MPIEXEC_POSTFLAGS}
        --expect-library $<TARGET_FILE:anari_library_ptc>
        --scenario ${scenario} ${extra})
      ptc_set_test_environment(${test} ${ranks})
    endforeach()
  endforeach()
endforeach()

# Failure path: with inverted expectations the pixel check must fail and
# every rank must exit non-zero.
set(ranks 2)
set(test ptc_smoke_detects_failure_${ranks}ranks)
ptc_smoke_command(cmd ${ranks} --invert-check)
string(REPLACE ";" "$<SEMICOLON>" cmd "${cmd}")
add_test(NAME ${test}
  COMMAND ${CMAKE_COMMAND} -DRANKS=${ranks} -DCOMMAND=${cmd}
          -P ${CMAKE_CURRENT_SOURCE_DIR}/ExpectFailure.cmake)
ptc_set_test_environment(${test} ${ranks})

# Quiet device: a normal 2-rank run with every channel and the
# callback prints nothing to stdout but the harness's own lines; everything
# the device has to say goes through the status callback.
set(ranks 2)
set(test ptc_smoke_quiet_${ranks}ranks)
ptc_smoke_command(cmd ${ranks} --color ufixed8 --depth --ids --callback)
string(REPLACE ";" "$<SEMICOLON>" cmd "${cmd}")
add_test(NAME ${test}
  COMMAND ${CMAKE_COMMAND} -DCOMMAND=${cmd}
          -P ${CMAKE_CURRENT_SOURCE_DIR}/ExpectQuiet.cmake)
ptc_set_test_environment(${test} ${ranks})
