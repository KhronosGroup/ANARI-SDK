# Copyright 2026 The Khronos Group
# SPDX-License-Identifier: Apache-2.0

# Runs COMMAND (a list) and passes only if it exits zero and every line it
# wrote to stdout is one of ptcSmoke's own: PASS/FAIL lines, its "callback"
# progress lines and its summary. Anything else came from the device.

execute_process(COMMAND ${COMMAND}
  RESULT_VARIABLE result
  OUTPUT_VARIABLE out
  ERROR_VARIABLE err)
message("${out}${err}")

if (NOT result EQUAL 0)
  message(FATAL_ERROR "expected a zero exit, got ${result}")
endif()

string(REPLACE "\n" ";" lines "${out}")
set(foreign "")
foreach(line IN LISTS lines)
  if (line STREQUAL "")
    continue()
  endif()
  if (line MATCHES "^(PASS|FAIL): " OR line MATCHES "^SMOKE (OK|FAILED)"
      OR line MATCHES "^callback [0-9]+: " OR line MATCHES "^rank [0-9]+ exiting")
    continue()
  endif()
  string(APPEND foreign "  ${line}\n")
endforeach()
if (NOT foreign STREQUAL "")
  message(FATAL_ERROR "the device wrote to stdout:\n${foreign}")
endif()
