## Copyright 2020 Jefferson Amstutz
## SPDX-License-Identifier: Unlicense

include(FetchContent)

set(match3D_LOC ${CMAKE_BINARY_DIR}/match3D_src)

FetchContent_Populate(match3D_src
  URL https://github.com/jeffamstutz/match3D/archive/refs/heads/main.zip
  SOURCE_DIR ${match3D_LOC}
)
set(match3D_DIR ${match3D_LOC}/cmake)
mark_as_advanced(
  FETCHCONTENT_BASE_DIR
  FETCHCONTENT_FULLY_DISCONNECTED
  FETCHCONTENT_QUIET
  FETCHCONTENT_UPDATES_DISCONNECTED
)
