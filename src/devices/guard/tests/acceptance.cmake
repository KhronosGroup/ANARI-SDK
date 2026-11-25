## Copyright 2026 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

# Correct frames: runs the SDK's unmodified anariRenderTests against a device
# loaded in-process and again through the guard device, and compares the
# images the two runs write.
#
#   cmake -DRENDER_TESTS=<anariRenderTests> -DDEVICE=<library> -P acceptance.cmake
#
# Works in ./acceptance_<library>, which it empties first and leaves behind.

if (NOT RENDER_TESTS OR NOT DEVICE)
  message(FATAL_ERROR "RENDER_TESTS and DEVICE are required")
endif()

# Differs between two in-process runs too, so it only has to be written.
set(UNSTABLE_IMAGES perf_spinning_cubes_0.png)
set(EXPECTED_IMAGE_COUNT 14)

set(WORK_DIR ${CMAKE_CURRENT_BINARY_DIR}/acceptance_${DEVICE})
file(REMOVE_RECURSE ${WORK_DIR})

function(render_into name)
  set(dir ${WORK_DIR}/${name})
  file(MAKE_DIRECTORY ${dir})
  execute_process(
    COMMAND ${ARGN}
    WORKING_DIRECTORY ${dir}
    RESULT_VARIABLE result
    OUTPUT_VARIABLE log
    ERROR_VARIABLE log
  )
  file(WRITE ${WORK_DIR}/${name}.log "${log}")
  if (NOT result EQUAL 0)
    message(FATAL_ERROR "${name} run ended with '${result}':\n${log}")
  endif()
  # anariRenderTests renders with helide when the library it was given does
  # not load, which would compare helide against itself.
  if (log MATCHES "falling back")
    message(FATAL_ERROR "${name} run did not use '${DEVICE}':\n${log}")
  endif()
  file(GLOB images RELATIVE ${dir} ${dir}/*.png)
  list(SORT images)
  set(${name}_images "${images}" PARENT_SCOPE)
endfunction()

render_into(inprocess ${RENDER_TESTS} --library ${DEVICE})
render_into(guarded
  ${CMAKE_COMMAND} -E env ANARI_GUARD_WRAPPED_DEVICE=${DEVICE}
  ${RENDER_TESTS} --library guard)

if (NOT "${inprocess_images}" STREQUAL "${guarded_images}")
  message(FATAL_ERROR "the two runs wrote different sets of images:\n"
    "  in-process: ${inprocess_images}\n"
    "  guarded:    ${guarded_images}")
endif()

list(LENGTH guarded_images count)
if (NOT count EQUAL EXPECTED_IMAGE_COUNT)
  message(FATAL_ERROR
    "expected ${EXPECTED_IMAGE_COUNT} images, found ${count}: ${guarded_images}")
endif()

foreach (image ${UNSTABLE_IMAGES})
  if (NOT image IN_LIST guarded_images)
    message(FATAL_ERROR "${image} was not written")
  endif()
endforeach()

set(differing "")
foreach (image ${guarded_images})
  if (image IN_LIST UNSTABLE_IMAGES)
    file(READ ${WORK_DIR}/guarded/${image} first_byte LIMIT 1 HEX)
    if (first_byte STREQUAL "")
      list(APPEND differing "${image} (empty)")
    endif()
  else()
    execute_process(
      COMMAND ${CMAKE_COMMAND} -E compare_files
        ${WORK_DIR}/inprocess/${image} ${WORK_DIR}/guarded/${image}
      RESULT_VARIABLE result
    )
    if (NOT result EQUAL 0)
      list(APPEND differing ${image})
    endif()
  endif()
endforeach()

if (differing)
  message(FATAL_ERROR
    "images differ from the in-process run: ${differing}\n"
    "  compare ${WORK_DIR}/inprocess with ${WORK_DIR}/guarded")
endif()

message(STATUS "${DEVICE}: ${count} images, all but ${UNSTABLE_IMAGES} "
  "byte-identical to the in-process run")
