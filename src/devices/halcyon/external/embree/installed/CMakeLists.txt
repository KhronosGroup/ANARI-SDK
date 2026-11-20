## Copyright 2026 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

# Defines anari_embree on an installed Embree 4.3 or newer and TBB, when both
# are found and the Embree has the features the devices use; otherwise
# defines nothing, and ../CMakeLists.txt builds Embree from source.
#
# A directory of its own so that nothing a passed-over package leaves behind
# reaches the source build: neither the variables Embree's package sets
# (EMBREE_STATIC_LIB, EMBREE_TASKING_SYSTEM, ...) nor its imported 'embree'
# target, which would clash with the one Embree's source build adds. Imported
# targets stay in this directory unless made global, which only a usable
# install's are.

find_package(TBB QUIET COMPONENTS tbb)
find_package(embree 4.3 QUIET)

if (NOT embree_FOUND)
  return()
endif()

# The features the devices use, which an installed Embree may have left out
# (packages before Embree 4.4 don't record instance arrays)
set(missing "")
foreach (feature
    EMBREE_FILTER_FUNCTION
    EMBREE_GEOMETRY_CURVE
    EMBREE_GEOMETRY_INSTANCE
    EMBREE_GEOMETRY_INSTANCE_ARRAY
    EMBREE_GEOMETRY_POINT
    EMBREE_GEOMETRY_QUAD
    EMBREE_GEOMETRY_TRIANGLE)
  if (DEFINED ${feature} AND NOT ${feature})
    list(APPEND missing ${feature})
  endif()
endforeach()
if (missing)
  message(STATUS "Embree ${embree_VERSION} (${embree_DIR}) lacks "
    "${missing}; building Embree from source instead")
  return()
endif()

if (NOT TBB_FOUND)
  message(STATUS "Found Embree ${embree_VERSION} (${embree_DIR}) but not "
    "TBB, which the devices' parallel loops then use; building Embree from "
    "source instead")
  return()
endif()

message(STATUS "Using Embree ${embree_VERSION} (${embree_DIR}) with "
  "TBB ${TBB_VERSION}")

# The devices (and halcyon's tests, which link 'embree') link the packages'
# targets from their own directories. All of each export set's targets go
# global, as a later find_package() elsewhere (ptc's TBB) fails on finding
# only some of them.
get_directory_property(importedTargets IMPORTED_TARGETS)
foreach (target ${importedTargets})
  if (target STREQUAL "embree" OR target MATCHES "^TBB::")
    set_target_properties(${target} PROPERTIES IMPORTED_GLOBAL TRUE)
  endif()
endforeach()

add_library(anari_embree INTERFACE)
target_link_libraries(anari_embree INTERFACE embree TBB::tbb)
target_compile_definitions(anari_embree INTERFACE ANARI_EMBREE_TBB)
target_include_directories(anari_embree
  INTERFACE ${CMAKE_CURRENT_LIST_DIR}/../include)
