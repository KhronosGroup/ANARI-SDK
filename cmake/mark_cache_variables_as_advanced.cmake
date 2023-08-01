## Copyright 2023 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

function(mark_cache_variables_as_advanced)
  get_cmake_property(vars CACHE_VARIABLES)
  list (SORT vars)
  foreach (var ${vars})
    foreach (arg ${ARGN})
      unset(MATCHED)
      string(REGEX MATCH ${arg} MATCHED ${var})
      if (NOT MATCHED)
        continue()
      endif()
      mark_as_advanced(${var})
    endforeach()
  endforeach()
endfunction()

mark_cache_variables_as_advanced(
  "^Boost"
  "^CPM"
  "^EMBREE"
  "^FETCHCONTENT"
  "^NFD"
  "^PYBIND"
)
