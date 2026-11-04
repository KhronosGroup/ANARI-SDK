# Copyright 2026 The Khronos Group
# SPDX-License-Identifier: Apache-2.0

# Runs COMMAND (a list) and passes only if it exits non-zero and each of the
# RANKS ranks reported that it is exiting with a failure.

execute_process(COMMAND ${COMMAND}
  RESULT_VARIABLE result
  OUTPUT_VARIABLE out
  ERROR_VARIABLE err)
message("${out}${err}")

if (result EQUAL 0)
  message(FATAL_ERROR "expected a non-zero exit, got 0")
endif()
math(EXPR last "${RANKS} - 1")
foreach(r RANGE ${last})
  if (NOT out MATCHES "rank ${r} exiting with failure")
    message(FATAL_ERROR "rank ${r} did not report a failure exit")
  endif()
endforeach()
if (NOT out MATCHES "FAIL: rank [0-9]+ sphere")
  message(FATAL_ERROR "failure did not come from the pixel check")
endif()
