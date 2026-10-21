## Copyright 2026 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

# MaterialX's Core and Format libraries (the document model and its XML
# reader; no shader generators), which the 'materialx' material subtype reads
# documents with (renderer/pathtracer/MaterialXDocument.*,
# docs/adr/0006-materialx-documents.md). Two targets:
# - halcyon_materialx: MaterialXCore and MaterialXFormat, static, their
#   symbols kept out of the device's dynamic symbols (--exclude-libs).
# - halcyon_materialx_definitions: MaterialX's data library (the nodedefs and
#   node graph implementations of stdlib, pbrlib, bxdf, nprlib, cmlib and
#   lights; no shader generator targets) embedded as strings, from the same
#   MaterialX as the code, so the device needs no search path at run time
#   (MaterialXLibrary.h).
#
# An installed MaterialX 1.39 with static libraries is used when
# find_package() finds one (OpenUSD's, say); otherwise MaterialX is fetched at
# the pinned commit and built here under target names of its own, so a
# configure that also finds another MaterialX (OpenUSD's, say) sees no clash.

project(halcyon_materialx LANGUAGES CXX)

# MaterialX 1.39.6 (main at the commit the ADR's prototype used; there is no
# 1.39.6 tag): the version OpenUSD 26.05 links
set(HALCYON_MATERIALX_COMMIT 38368ee04da84ce1f8837ecba7322dd6d81291f8)
set(HALCYON_MATERIALX_MD5 ce726bda1cc528ae17a0d07f2296c059)

# An installed one must be 1.39.6 or a later 1.39 (the pin's code and
# behaviour), and have its resources (the tests read its example documents)
find_package(MaterialX 1.39.6 CONFIG QUIET)
set(useInstalled OFF)
if (MaterialX_FOUND AND MaterialX_VERSION VERSION_LESS 1.40
    AND TARGET MaterialXCore AND TARGET MaterialXFormat
    AND EXISTS "${MATERIALX_RESOURCES_DIR}/Materials/Examples")
  get_target_property(coreType MaterialXCore TYPE)
  get_target_property(formatType MaterialXFormat TYPE)
  if (coreType STREQUAL "STATIC_LIBRARY"
      AND formatType STREQUAL "STATIC_LIBRARY")
    set(useInstalled ON)
  else()
    message(STATUS "MaterialX ${MaterialX_VERSION} found in ${MaterialX_DIR}"
      " is shared; fetching a static one")
  endif()
endif()

add_library(halcyon_materialx INTERFACE)

if (useInstalled)
  message(STATUS "Using MaterialX ${MaterialX_VERSION} from ${MaterialX_DIR}")
  set(materialxLibraries ${MATERIALX_STDLIB_DIR})
  set(materialxResources "${MATERIALX_RESOURCES_DIR}")
  target_link_libraries(halcyon_materialx INTERFACE
    MaterialXFormat MaterialXCore)
  # The imported targets are local to this directory, so name their files
  # here for --exclude-libs (below)
  set(excludedFiles)
  foreach (library MaterialXFormat MaterialXCore)
    get_target_property(configs ${library} IMPORTED_CONFIGURATIONS)
    set(properties IMPORTED_LOCATION)
    foreach (config ${configs})
      list(APPEND properties IMPORTED_LOCATION_${config})
    endforeach()
    foreach (property ${properties})
      get_target_property(location ${library} ${property})
      if (location)
        get_filename_component(name ${location} NAME)
        list(APPEND excludedFiles ${name})
      endif()
    endforeach()
  endforeach()
  list(REMOVE_DUPLICATES excludedFiles)
else()
  message(STATUS "MaterialX not found, fetching it")
  anari_sdk_fetch_project(
    NAME halcyon_materialx
    URL "https://github.com/AcademySoftwareFoundation/MaterialX/archive/${HALCYON_MATERIALX_COMMIT}.zip"
    MD5 ${HALCYON_MATERIALX_MD5}
  )
  set(src ${halcyon_materialx_LOCATION}/source)
  set(materialxLibraries ${halcyon_materialx_LOCATION}/libraries)
  set(materialxResources ${halcyon_materialx_LOCATION}/resources)

  # MaterialX's own CMakeLists.txt would define the targets MaterialXCore
  # etc., which OpenUSD's MaterialX package defines too; build the two
  # libraries from their sources instead. Its Generated.h, with a namespace
  # of halcyon's own (MATERIALX_NAMESPACE_SUFFIX), so even a non-hidden
  # symbol can't be confused with another MaterialX's.
  set(MATERIALX_MAJOR_VERSION 1)
  set(MATERIALX_MINOR_VERSION 39)
  set(MATERIALX_BUILD_VERSION 6)
  set(MATERIALX_NAMESPACE "MaterialX_halcyon_v1_39_6")
  set(generated ${CMAKE_CURRENT_BINARY_DIR}/include)
  configure_file(${src}/MaterialXCore/Generated.h.in
    ${generated}/MaterialXCore/Generated.h)

  file(GLOB coreSources ${src}/MaterialXCore/*.cpp)
  file(GLOB formatSources
    ${src}/MaterialXFormat/*.cpp
    ${src}/MaterialXFormat/External/PugiXML/*.cpp)
  add_library(halcyon_materialx_core STATIC ${coreSources})
  add_library(halcyon_materialx_format STATIC ${formatSources})
  foreach (target halcyon_materialx_core halcyon_materialx_format)
    target_include_directories(${target} SYSTEM PUBLIC ${src} ${generated})
    target_compile_features(${target} PUBLIC cxx_std_17)
    set_target_properties(${target} PROPERTIES
      CXX_VISIBILITY_PRESET hidden
      VISIBILITY_INLINES_HIDDEN ON)
  endforeach()
  target_link_libraries(halcyon_materialx_format PUBLIC
    halcyon_materialx_core ${CMAKE_DL_LIBS})
  target_link_libraries(halcyon_materialx INTERFACE halcyon_materialx_format)
  set(excludedFiles
    $<TARGET_LINKER_FILE_NAME:halcyon_materialx_format>
    $<TARGET_LINKER_FILE_NAME:halcyon_materialx_core>)
endif()

# Keep MaterialX's (and pugixml's) symbols out of the dynamic symbols of
# whatever links it, as Embree's tasking library's (external/embree)
if (UNIX AND NOT APPLE)
  foreach (file ${excludedFiles})
    target_link_options(halcyon_materialx INTERFACE
      "LINKER:--exclude-libs,${file}")
  endforeach()
endif()

# Where MaterialX's example documents are (resources/Materials/Examples), for
# the tests
set(HALCYON_MATERIALX_RESOURCES "${materialxResources}" PARENT_SCOPE)

## The data library, embedded ##

# Every definition and implementation graph file, without the shader
# generators' (gen*/) and targets/
file(GLOB_RECURSE definitionFiles RELATIVE ${materialxLibraries}
  ${materialxLibraries}/*.mtlx)
list(FILTER definitionFiles EXCLUDE REGEX "(^|/)(gen[a-z]*|targets)/")
list(SORT definitionFiles)
if (NOT definitionFiles)
  message(FATAL_ERROR "no MaterialX definitions in ${materialxLibraries}")
endif()

# One raw string literal per file, regenerated when a file changes
set(delimiter "halcyon_mtlx")
set(entries "")
foreach (file ${definitionFiles})
  set(path ${materialxLibraries}/${file})
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${path})
  file(READ ${path} content)
  string(FIND "${content}" ")${delimiter}\"" clash)
  if (NOT clash EQUAL -1)
    message(FATAL_ERROR "${path} contains the embedding's delimiter")
  endif()
  string(APPEND entries
    "    {\"${file}\",\n     R\"${delimiter}(${content})${delimiter}\"},\n")
endforeach()
list(LENGTH definitionFiles definitionCount)

set(generatedSource
  ${CMAKE_CURRENT_BINARY_DIR}/MaterialXDefinitions.cpp)
file(WRITE ${generatedSource}.tmp
"// Generated by external/materialx/CMakeLists.txt from
// ${materialxLibraries}: MaterialX's data library, one entry per file.

#include \"renderer/pathtracer/MaterialXDefinitions.h\"

namespace halcyon::pathtracer {

const MaterialXDefinitionFile MATERIALX_DEFINITION_FILES[] = {
${entries}};

const int MATERIALX_DEFINITION_FILE_COUNT = ${definitionCount};

} // namespace halcyon::pathtracer
")
# Only touched when it changes, so a reconfigure doesn't recompile it
configure_file(${generatedSource}.tmp ${generatedSource} COPYONLY)

add_library(halcyon_materialx_definitions STATIC ${generatedSource})
target_include_directories(halcyon_materialx_definitions PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/../..)
