## Copyright 2026 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

# cmake -DDEVICE=<device library>
#       -DSEARCH_DIRS=<directories to resolve dependencies in>
#       [-DNM=<nm>]
#       -P checkDeviceDependencies.cmake
#
# Fails if the device library depends, directly or through another library,
# on one named like OpenUSD (libusd_*, or an unprefixed build's libtf.so
# etc.), TBB, Python or Boost.Python, or if its RPATH or RUNPATH names a
# directory holding OpenUSD's libraries.
#
# With NM, also fails if the device exports a symbol of the libraries it links
# statically with hidden symbols: MaterialX (and its pugixml), stb_image,
# tinyexr and miniz, which another copy in the same process (OpenUSD's
# MaterialX, say) could otherwise interpose.

file(GET_RUNTIME_DEPENDENCIES
  LIBRARIES ${DEVICE}
  DIRECTORIES ${SEARCH_DIRS}
  RESOLVED_DEPENDENCIES_VAR resolved
  UNRESOLVED_DEPENDENCIES_VAR unresolved
)

set(found)
foreach (dependency ${resolved} ${unresolved})
  get_filename_component(name ${dependency} NAME)
  if (name MATCHES "^lib(usd_|tbb|python|boost_python)"
      OR name MATCHES "^lib(arch|tf|gf|vt|work|sdf|usd)\\.so")
    list(APPEND found ${dependency})
  endif()
endforeach()

# The search path the device carries: OpenUSD's library directory there
# would put OpenUSD before the app's own libraries
get_filename_component(deviceDir "${DEVICE}" DIRECTORY)
file(READ_ELF ${DEVICE} RPATH rpath RUNPATH runpath)
string(REPLACE ":" ";" searchPath "${rpath}:${runpath}")
foreach (entry ${searchPath})
  string(REPLACE "$ORIGIN" "${deviceDir}" dir "${entry}")
  if (NOT dir OR NOT IS_DIRECTORY "${dir}")
    continue()
  endif()
  get_filename_component(dir "${dir}" REALPATH)
  file(GLOB openusd "${dir}/libusd_tf.*" "${dir}/libtf.*" "${dir}/libusd_ms.*")
  if (openusd)
    list(APPEND found "search path ${entry}")
  endif()
endforeach()

if (unresolved)
  message("note: unresolved, so not followed: ${unresolved}")
endif()
if (found)
  message(FATAL_ERROR "FAIL ${DEVICE} depends on or searches ${found}")
endif()
message("PASS no OpenUSD, TBB or Python among the device's dependencies "
  "or in its search path")

if (NM)
  execute_process(COMMAND ${NM} -D --defined-only -C ${DEVICE}
    OUTPUT_VARIABLE symbols
    RESULT_VARIABLE result)
  if (NOT result EQUAL 0)
    message(FATAL_ERROR "FAIL ${NM} failed on ${DEVICE}")
  endif()
  string(REGEX MATCHALL
    "[^\n]*(MaterialX_|pugi::|stbi_|tinyexr|LoadEXR|mz_|tdefl_|tinfl_)[^\n]*"
    exported "${symbols}")
  if (exported)
    list(LENGTH exported count)
    list(GET exported 0 first)
    message(FATAL_ERROR "FAIL ${DEVICE} exports ${count} symbols of its "
      "statically linked MaterialX, pugixml, stb_image, tinyexr or miniz, "
      "e.g. ${first}")
  endif()
  message("PASS no MaterialX, pugixml, stb_image, tinyexr or miniz symbols "
    "among the device's exports")
endif()
