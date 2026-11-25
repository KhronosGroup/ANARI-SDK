## Copyright 2026 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

# Every test runs the guard device from the build tree, against helide unless
# it says otherwise, and fails if it has not finished in 60 seconds.
set(GUARD_TEST_ENVIRONMENT
  "LD_LIBRARY_PATH=path_list_prepend:${CMAKE_BINARY_DIR}"
  "ANARI_GUARD_WRAPPED_DEVICE=set:helide"
)
# Under ThreadSanitizer the adapter and the host, which inherits this, leave
# out the reports that are not the guard device's. ThreadSanitizer does not
# start on a kernel that randomises addresses as widely as recent ones do, so
# each test is started with that off, where setarch is there to do it.
set(GUARD_TEST_LAUNCHER)
if (GUARD_SANITIZE_THREAD)
  list(APPEND GUARD_TEST_ENVIRONMENT
    "TSAN_OPTIONS=set:suppressions=${CMAKE_CURRENT_LIST_DIR}/tsan.supp")
  find_program(GUARD_SETARCH setarch)
  if (GUARD_SETARCH)
    set(GUARD_TEST_LAUNCHER ${GUARD_SETARCH} ${CMAKE_SYSTEM_PROCESSOR} -R)
  endif()
endif()

# guard_test(<name> [SOURCES ...] [COMMAND ...] [LABELS ...]
#   [WRAPPED <library>] [CEILING <bytes>] [FAULTS])
# FAULTS runs the test over guardTestHost, which dies where the test says.
# CEILING sets the array ceiling, which is otherwise the default.
function(guard_test name)
  cmake_parse_arguments(ARG
    "FAULTS" "WRAPPED;CEILING" "SOURCES;COMMAND;LABELS" ${ARGN})
  if (ARG_SOURCES)
    add_executable(${name} ${ARG_SOURCES})
    target_link_libraries(${name} PRIVATE anari::anari Threads::Threads)
  endif()
  if (NOT ARG_COMMAND)
    set(ARG_COMMAND ${name})
  endif()
  # Behind a launcher CTest no longer takes a target's name for its file.
  list(GET ARG_COMMAND 0 program)
  if (TARGET ${program})
    list(REMOVE_AT ARG_COMMAND 0)
    list(INSERT ARG_COMMAND 0 $<TARGET_FILE:${program}>)
  endif()
  add_test(NAME ${name}
    COMMAND ${GUARD_TEST_LAUNCHER} ${ARG_COMMAND}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  )
  set(environment ${GUARD_TEST_ENVIRONMENT})
  if (ARG_WRAPPED)
    list(APPEND environment "ANARI_GUARD_WRAPPED_DEVICE=set:${ARG_WRAPPED}")
  endif()
  if (ARG_CEILING)
    list(APPEND environment "ANARI_GUARD_INLINE_THRESHOLD=set:${ARG_CEILING}")
  endif()
  if (ARG_FAULTS)
    list(APPEND environment "ANARI_GUARD_HOST=set:$<TARGET_FILE:guardTestHost>")
  endif()
  set_tests_properties(${name} PROPERTIES
    ENVIRONMENT_MODIFICATION "${environment}"
    LABELS "${ARG_LABELS}"
    TIMEOUT 60
  )
endfunction()

## The protocol, message by message ##

# The recording device: wrapped by tests that check what the wrapped device is
# given, call by call.
add_library(anari_library_guard_recorder MODULE RecordingDevice.cpp)
target_link_libraries(anari_library_guard_recorder PRIVATE anari::anari)

guard_test(guard_calls SOURCES calls.cpp LABELS unit)
# With a ceiling low enough that its larger arrays are above it
guard_test(guard_calls_low_ceiling
  COMMAND guard_calls CEILING 16384 LABELS unit)

guard_test(guard_extensions SOURCES extensions.cpp LABELS unit)

guard_test(guard_misuse SOURCES misuse.cpp LABELS unit)

# Released handles left in an object array: what the wrapped device is given
guard_test(guard_unchanged_elements SOURCES unchanged_elements.cpp LABELS unit)
guard_test(guard_unchanged_elements_low_ceiling
  COMMAND guard_unchanged_elements CEILING 16384 LABELS unit)

guard_test(guard_creation_messages SOURCES creation_messages.cpp LABELS unit)

guard_test(guard_release_in_callback SOURCES release_in_callback.cpp LABELS unit)

guard_test(guard_wire SOURCES wire.cpp LABELS unit)
target_link_libraries(guard_wire PRIVATE anari_guard_common)

## Traffic counters and tuning variables ##

guard_test(guard_counters SOURCES counters.cpp LABELS unit)
guard_test(guard_inline_threshold COMMAND guard_counters threshold LABELS unit)
guard_test(guard_probe_period COMMAND guard_counters probe LABELS unit)

## Bulk data ##

# At the default array ceiling, which every array of its scenes is below, and
# at one that the larger of them are above
guard_test(guard_regions SOURCES regions.cpp LABELS data)
guard_test(guard_regions_low_ceiling
  COMMAND guard_regions CEILING 4096 LABELS data)

# Released handles left in an object array: the same pixels as in-process
guard_test(guard_released_elements SOURCES released_elements.cpp LABELS data)
guard_test(guard_released_elements_low_ceiling
  COMMAND guard_released_elements CEILING 16384 LABELS data)

## Frame completion ##

guard_test(guard_frame_completion SOURCES frame_completion.cpp LABELS frames)
# Over the recording device as one without the completion callback extension
guard_test(guard_frame_completion_timeout
  COMMAND guard_frame_completion timeout LABELS frames)

## Several threads of the application ##

# Part of the gate: 'acceptance' as well as 'threads'.
guard_test(guard_threads_objects
  SOURCES threads_objects.cpp LABELS threads acceptance)
# With a ceiling its mapped parameters are above, so that messages that carry
# a descriptor are among those the threads send
guard_test(guard_threads_objects_low_ceiling
  COMMAND guard_threads_objects CEILING 16384 LABELS threads acceptance)

## Device death ##

guard_test(guard_death_outside_kill
  SOURCES death_outside_kill.cpp LABELS death)
guard_test(guard_death_message_held
  SOURCES death_message_held.cpp LABELS death)
guard_test(guard_death_regions
  SOURCES death_regions.cpp LABELS death)

# The host with a fault hook: it dies at the call named by
# ANARI_GUARD_HOST_FAULT. Never installed.
add_executable(guardTestHost TestHost.cpp)
target_link_libraries(guardTestHost PRIVATE anari_guard_host_core)

# One test for each point at which the host can die.
guard_test(guard_death_idle SOURCES death_idle.cpp LABELS death FAULTS)

guard_test(guard_death_blocked_caller
  SOURCES death_blocked_caller.cpp LABELS death FAULTS)

guard_test(guard_death_frame_wait
  SOURCES death_frame_wait.cpp LABELS death FAULTS)

guard_test(guard_death_frame_in_flight
  SOURCES death_frame_in_flight.cpp LABELS death FAULTS)

guard_test(guard_death_mapped SOURCES death_mapped.cpp LABELS death FAULTS)

guard_test(guard_death_during_release
  SOURCES death_during_release.cpp LABELS death FAULTS)

# Several threads calling at the moment of death
guard_test(guard_death_threads
  SOURCES death_threads.cpp LABELS death threads FAULTS)

# A thread waiting on a frame, another calling, and the frame's callback
# calling on it: while it renders, and at death
guard_test(guard_wait_and_callers
  SOURCES wait_and_callers.cpp LABELS death threads FAULTS)

# How the application is told: after the wrapped device's last messages, and
# under its own call, so that it may throw or exit from its callback.
guard_test(guard_death_last_messages
  SOURCES death_last_messages.cpp LABELS death FAULTS)
guard_test(guard_death_callback_ends
  SOURCES death_callback_ends.cpp LABELS death FAULTS)

# The fatal message says how the host ended.
add_executable(guard_death_how death_how.cpp)
target_link_libraries(guard_death_how PRIVATE anari::anari Threads::Threads)
guard_test(guard_death_abort
  COMMAND guard_death_how abort "killed by signal 6" LABELS death FAULTS)
guard_test(guard_death_exit
  COMMAND guard_death_how exit "exited with code 1" LABELS death FAULTS)
# A host that is alive and cannot be reached: the adapter ends it.
guard_test(guard_death_unreachable
  COMMAND guard_death_how unreachable "connection to the host running 'helide' was lost"
  LABELS death FAULTS)

# Devices that never come to exist, which is not device death.
guard_test(guard_creation_failures
  SOURCES creation_failures.cpp LABELS death FAULTS)

# Runs a program and kills its host from outside
add_executable(guardKillHostOf kill_host_of.cpp)
target_link_libraries(guardKillHostOf PRIVATE Threads::Threads)

## The SDK's unmodified render tests ##

# Built in the SDK's tree, anariRenderTests is a target of the same build; on
# its own, the device looks for the installed program.
if (STANDALONE_SOURCE_TREE)
  find_program(GUARD_RENDER_TESTS anariRenderTests)
else()
  set(GUARD_RENDER_TESTS $<TARGET_FILE:anariRenderTests>)
endif()

# Not under ThreadSanitizer, whose runtime cannot be loaded into a program
# that was built without it.
if (GUARD_RENDER_TESTS AND NOT GUARD_SANITIZE_THREAD)
  # Correct frames: the same images as the device loaded in-process.
  guard_test(guard_acceptance_helide
    COMMAND ${CMAKE_COMMAND}
      -DRENDER_TESTS=${GUARD_RENDER_TESTS} -DDEVICE=helide
      -P ${CMAKE_CURRENT_LIST_DIR}/acceptance.cmake
    LABELS acceptance)

  # The unmodified program loses its host mid-run and ends by its own choice:
  # anariRenderTests calls exit() from its status callback, which the death
  # message runs on the program's own thread.
  guard_test(guard_render_tests_outside_kill
    COMMAND guardKillHostOf 300 ${GUARD_RENDER_TESTS} --library guard
    LABELS death unmodified)

  # The same, with the host crashing at the third render.
  add_executable(guardRunFaulted run_faulted.cpp)
  target_link_libraries(guardRunFaulted PRIVATE Threads::Threads)
  target_include_directories(guardRunFaulted PRIVATE
    $<TARGET_PROPERTY:anari::anari,INTERFACE_INCLUDE_DIRECTORIES>)
  guard_test(guard_render_tests_faulted
    COMMAND guardRunFaulted renderFrame:3:after:segv
      ${GUARD_RENDER_TESTS} --library guard
    LABELS death unmodified FAULTS)
  endif()
