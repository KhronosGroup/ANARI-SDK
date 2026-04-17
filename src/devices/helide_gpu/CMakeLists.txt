## Copyright 2026 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.11)

if (${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_CURRENT_LIST_DIR})
  set(STANDALONE_SOURCE_TREE TRUE)
else()
  set(STANDALONE_SOURCE_TREE FALSE)
endif()

if (STANDALONE_SOURCE_TREE)
  message(STATUS "CMake version: ${CMAKE_VERSION}")
endif()

## Language setup ##

set(CMAKE_DISABLE_SOURCE_CHANGES ON)
set(CMAKE_DISABLE_IN_SOURCE_BUILD ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(CMAKE_BUILD_TYPE_INIT Release)

set(CMAKE_INSTALL_RPATH "$ORIGIN")
set(CMAKE_INSTALL_MESSAGE LAZY)

## Establish project ##

project(anari_library_helide_gpu LANGUAGES CXX)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

include(GNUInstallDirs)

## Dependencies ##

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/../rtx/cmake")

find_package(anari REQUIRED)
find_package(SDL3 REQUIRED COMPONENTS SDL3)
find_program(GLSLANG_VALIDATOR glslangValidator REQUIRED)
if(APPLE)
  find_program(SPIRV_CROSS spirv-cross REQUIRED)
else()
  find_program(SPIRV_CROSS spirv-cross)
endif()
if(SPIRV_CROSS)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
endif()
## GLM (fetched) ##
set(GLM_BUILD_TESTS   OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_INSTALL OFF CACHE BOOL "" FORCE)
anari_sdk_fetch_project(
  NAME anari_helide_gpu_glm
  URL "https://github.com/g-truc/glm/archive/refs/tags/1.0.1.zip"
  MD5 78e541dadeee540ff9cedb5b618df821
  ADD_SUBDIR
)

## Shader compilation: GLSL -> SPIR-V -> C header ##

function(compile_shader_to_header stage source out_spv out_header sym)
  get_filename_component(source_abs "${source}" ABSOLUTE)
  add_custom_command(
    OUTPUT "${out_spv}"
    COMMAND ${GLSLANG_VALIDATOR} -V --target-env vulkan1.0
            -S ${stage} "${source_abs}" -o "${out_spv}"
    DEPENDS "${source_abs}"
    COMMENT "Compiling ${source} to SPIR-V"
  )
  add_custom_command(
    OUTPUT "${out_header}"
    COMMAND ${CMAKE_COMMAND}
            -DINPUT_FILE="${out_spv}"
            -DOUTPUT_FILE="${out_header}"
            -DSYM_NAME="${sym}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/bin2header.cmake"
    DEPENDS "${out_spv}"
    COMMENT "Generating C header from SPIR-V (${sym})"
  )
endfunction()

## Optional: SPIR-V -> MSL cross-compilation via spirv-cross ##

function(compile_msl_header source_spv out_msl out_header sym)
  if(NOT SPIRV_CROSS)
    return()
  endif()
  set(out_msl_raw "${out_msl}.raw")
  add_custom_command(
    OUTPUT "${out_msl_raw}"
    COMMAND ${SPIRV_CROSS}
            --msl "${source_spv}"
            --msl-version 20200
            ${ARGN}
            --output "${out_msl_raw}"
    DEPENDS "${source_spv}"
    COMMENT "Cross-compiling SPIR-V to MSL (${sym})"
  )
  # SDL3_gpu requires uniform buffers (constant) before storage buffers (device)
  # in [[buffer(N)]] slots, but spirv-cross may interleave them. Fix up the order.
  add_custom_command(
    OUTPUT "${out_msl}"
    COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/fix_msl_buffers.py"
            "${out_msl_raw}" "${out_msl}"
    DEPENDS "${out_msl_raw}" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/fix_msl_buffers.py"
    COMMENT "Fixing MSL buffer order (${sym})"
  )
  add_custom_command(
    OUTPUT "${out_header}"
    COMMAND ${CMAKE_COMMAND}
            -DINPUT_FILE="${out_msl}"
            -DOUTPUT_FILE="${out_header}"
            -DSYM_NAME="${sym}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/str2header.cmake"
    DEPENDS "${out_msl}"
    COMMENT "Generating C header from MSL (${sym})"
  )
endfunction()

compile_shader_to_header(vert
  ${CMAKE_CURRENT_SOURCE_DIR}/shaders/triangle.vert
  ${CMAKE_CURRENT_BINARY_DIR}/triangle.vert.spv
  ${CMAKE_CURRENT_BINARY_DIR}/triangle_vert_spv.h
  triangle_vert_spv)

compile_shader_to_header(vert
  ${CMAKE_CURRENT_SOURCE_DIR}/shaders/triangle_mesh.vert
  ${CMAKE_CURRENT_BINARY_DIR}/triangle_mesh.vert.spv
  ${CMAKE_CURRENT_BINARY_DIR}/triangle_mesh_vert_spv.h
  triangle_mesh_vert_spv)

compile_shader_to_header(frag
  ${CMAKE_CURRENT_SOURCE_DIR}/shaders/triangle.frag
  ${CMAKE_CURRENT_BINARY_DIR}/triangle.frag.spv
  ${CMAKE_CURRENT_BINARY_DIR}/triangle_frag_spv.h
  triangle_frag_spv)

compile_shader_to_header(vert
  ${CMAKE_CURRENT_SOURCE_DIR}/shaders/fullscreen.vert
  ${CMAKE_CURRENT_BINARY_DIR}/fullscreen.vert.spv
  ${CMAKE_CURRENT_BINARY_DIR}/fullscreen_vert_spv.h
  fullscreen_vert_spv)

compile_shader_to_header(frag
  ${CMAKE_CURRENT_SOURCE_DIR}/shaders/ssao.frag
  ${CMAKE_CURRENT_BINARY_DIR}/ssao.frag.spv
  ${CMAKE_CURRENT_BINARY_DIR}/ssao_frag_spv.h
  ssao_frag_spv)

compile_shader_to_header(frag
  ${CMAKE_CURRENT_SOURCE_DIR}/shaders/blur.frag
  ${CMAKE_CURRENT_BINARY_DIR}/blur.frag.spv
  ${CMAKE_CURRENT_BINARY_DIR}/blur_frag_spv.h
  blur_frag_spv)

compile_shader_to_header(frag
  ${CMAKE_CURRENT_SOURCE_DIR}/shaders/composite.frag
  ${CMAKE_CURRENT_BINARY_DIR}/composite.frag.spv
  ${CMAKE_CURRENT_BINARY_DIR}/composite_frag_spv.h
  composite_frag_spv)

compile_shader_to_header(vert
  ${CMAKE_CURRENT_SOURCE_DIR}/shaders/sphere.vert
  ${CMAKE_CURRENT_BINARY_DIR}/sphere.vert.spv
  ${CMAKE_CURRENT_BINARY_DIR}/sphere_vert_spv.h
  sphere_vert_spv)

compile_shader_to_header(frag
  ${CMAKE_CURRENT_SOURCE_DIR}/shaders/sphere.frag
  ${CMAKE_CURRENT_BINARY_DIR}/sphere.frag.spv
  ${CMAKE_CURRENT_BINARY_DIR}/sphere_frag_spv.h
  sphere_frag_spv)

compile_shader_to_header(vert
  ${CMAKE_CURRENT_SOURCE_DIR}/shaders/volume.vert
  ${CMAKE_CURRENT_BINARY_DIR}/volume.vert.spv
  ${CMAKE_CURRENT_BINARY_DIR}/volume_vert_spv.h
  volume_vert_spv)

compile_shader_to_header(frag
  ${CMAKE_CURRENT_SOURCE_DIR}/shaders/volume.frag
  ${CMAKE_CURRENT_BINARY_DIR}/volume.frag.spv
  ${CMAKE_CURRENT_BINARY_DIR}/volume_frag_spv.h
  volume_frag_spv)

## MSL cross-compilation for each shader ##

compile_msl_header(
  ${CMAKE_CURRENT_BINARY_DIR}/triangle.vert.spv
  ${CMAKE_CURRENT_BINARY_DIR}/triangle.vert.msl
  ${CMAKE_CURRENT_BINARY_DIR}/triangle_vert_msl.h
  triangle_vert_msl)

compile_msl_header(
  ${CMAKE_CURRENT_BINARY_DIR}/triangle_mesh.vert.spv
  ${CMAKE_CURRENT_BINARY_DIR}/triangle_mesh.vert.msl
  ${CMAKE_CURRENT_BINARY_DIR}/triangle_mesh_vert_msl.h
  triangle_mesh_vert_msl)

compile_msl_header(
  ${CMAKE_CURRENT_BINARY_DIR}/triangle.frag.spv
  ${CMAKE_CURRENT_BINARY_DIR}/triangle.frag.msl
  ${CMAKE_CURRENT_BINARY_DIR}/triangle_frag_msl.h
  triangle_frag_msl)

compile_msl_header(
  ${CMAKE_CURRENT_BINARY_DIR}/fullscreen.vert.spv
  ${CMAKE_CURRENT_BINARY_DIR}/fullscreen.vert.msl
  ${CMAKE_CURRENT_BINARY_DIR}/fullscreen_vert_msl.h
  fullscreen_vert_msl)

compile_msl_header(
  ${CMAKE_CURRENT_BINARY_DIR}/ssao.frag.spv
  ${CMAKE_CURRENT_BINARY_DIR}/ssao.frag.msl
  ${CMAKE_CURRENT_BINARY_DIR}/ssao_frag_msl.h
  ssao_frag_msl
  --msl-decoration-binding)

compile_msl_header(
  ${CMAKE_CURRENT_BINARY_DIR}/blur.frag.spv
  ${CMAKE_CURRENT_BINARY_DIR}/blur.frag.msl
  ${CMAKE_CURRENT_BINARY_DIR}/blur_frag_msl.h
  blur_frag_msl
  --msl-decoration-binding)

compile_msl_header(
  ${CMAKE_CURRENT_BINARY_DIR}/composite.frag.spv
  ${CMAKE_CURRENT_BINARY_DIR}/composite.frag.msl
  ${CMAKE_CURRENT_BINARY_DIR}/composite_frag_msl.h
  composite_frag_msl)

compile_msl_header(
  ${CMAKE_CURRENT_BINARY_DIR}/sphere.vert.spv
  ${CMAKE_CURRENT_BINARY_DIR}/sphere.vert.msl
  ${CMAKE_CURRENT_BINARY_DIR}/sphere_vert_msl.h
  sphere_vert_msl)

compile_msl_header(
  ${CMAKE_CURRENT_BINARY_DIR}/sphere.frag.spv
  ${CMAKE_CURRENT_BINARY_DIR}/sphere.frag.msl
  ${CMAKE_CURRENT_BINARY_DIR}/sphere_frag_msl.h
  sphere_frag_msl)

compile_msl_header(
  ${CMAKE_CURRENT_BINARY_DIR}/volume.vert.spv
  ${CMAKE_CURRENT_BINARY_DIR}/volume.vert.msl
  ${CMAKE_CURRENT_BINARY_DIR}/volume_vert_msl.h
  volume_vert_msl)

compile_msl_header(
  ${CMAKE_CURRENT_BINARY_DIR}/volume.frag.spv
  ${CMAKE_CURRENT_BINARY_DIR}/volume.frag.msl
  ${CMAKE_CURRENT_BINARY_DIR}/volume_frag_msl.h
  volume_frag_msl)

## Shader build target ##

set(SHADER_DEPS
  ${CMAKE_CURRENT_BINARY_DIR}/triangle_vert_spv.h
  ${CMAKE_CURRENT_BINARY_DIR}/triangle_mesh_vert_spv.h
  ${CMAKE_CURRENT_BINARY_DIR}/triangle_frag_spv.h
  ${CMAKE_CURRENT_BINARY_DIR}/fullscreen_vert_spv.h
  ${CMAKE_CURRENT_BINARY_DIR}/ssao_frag_spv.h
  ${CMAKE_CURRENT_BINARY_DIR}/blur_frag_spv.h
  ${CMAKE_CURRENT_BINARY_DIR}/composite_frag_spv.h
  ${CMAKE_CURRENT_BINARY_DIR}/sphere_vert_spv.h
  ${CMAKE_CURRENT_BINARY_DIR}/sphere_frag_spv.h
  ${CMAKE_CURRENT_BINARY_DIR}/volume_vert_spv.h
  ${CMAKE_CURRENT_BINARY_DIR}/volume_frag_spv.h
)

if(SPIRV_CROSS)
  list(APPEND SHADER_DEPS
    ${CMAKE_CURRENT_BINARY_DIR}/triangle_vert_msl.h
    ${CMAKE_CURRENT_BINARY_DIR}/triangle_mesh_vert_msl.h
    ${CMAKE_CURRENT_BINARY_DIR}/triangle_frag_msl.h
    ${CMAKE_CURRENT_BINARY_DIR}/fullscreen_vert_msl.h
    ${CMAKE_CURRENT_BINARY_DIR}/ssao_frag_msl.h
    ${CMAKE_CURRENT_BINARY_DIR}/blur_frag_msl.h
    ${CMAKE_CURRENT_BINARY_DIR}/composite_frag_msl.h
    ${CMAKE_CURRENT_BINARY_DIR}/sphere_vert_msl.h
    ${CMAKE_CURRENT_BINARY_DIR}/sphere_frag_msl.h
    ${CMAKE_CURRENT_BINARY_DIR}/volume_vert_msl.h
    ${CMAKE_CURRENT_BINARY_DIR}/volume_frag_msl.h
  )
endif()

add_custom_target(helide_gpu_shaders ALL DEPENDS ${SHADER_DEPS})

## Build device target ##

project_add_library(SHARED
  GLContextInterface.cpp
  Object.cpp
  HelideGPUDevice.cpp
  HelideGPUDeviceGlobalState.cpp
  HelideGPULibrary.cpp
  camera/Camera.cpp
  camera/OrthographicCamera.cpp
  camera/PerspectiveCamera.cpp
  frame/Frame.cpp
  geometry/Cone.cpp
  geometry/Curve.cpp
  geometry/Cylinder.cpp
  geometry/Geometry.cpp
  geometry/Sphere.cpp
  geometry/Triangle.cpp
  gpu/GPUBuffer.cpp
  gpu/GPUTexture.cpp
  gpu/sdl3_gpu_device.cpp
  light/Light.cpp
  material/Material.cpp
  material/Matte.cpp
  renderer/Renderer.cpp
  sampler/Image1D.cpp
  sampler/Image2D.cpp
  sampler/Image3D.cpp
  sampler/ImageSampler.cpp
  sampler/PrimitiveSampler.cpp
  sampler/Sampler.cpp
  sampler/TransformSampler.cpp
  scene/Group.cpp
  scene/Instance.cpp
  scene/World.cpp
  spatial_field/SpatialField.cpp
  spatial_field/StructuredRegular.cpp
  surface/Surface.cpp
  volume/TransferFunction1D.cpp
  volume/Volume.cpp
)

add_dependencies(${PROJECT_NAME} helide_gpu_shaders)

if(SPIRV_CROSS)
  target_compile_definitions(${PROJECT_NAME} PRIVATE HELIDE_GPU_HAS_MSL_SHADERS)
endif()

# GLM 1.0+ requires explicit opt-in for GTX extensions (e.g. component_wise).
target_compile_definitions(${PROJECT_NAME} PRIVATE GLM_ENABLE_EXPERIMENTAL)

project_link_libraries(PRIVATE anari::helium glm::glm SDL3::SDL3)

include(GenerateExportHeader)
generate_export_header(${PROJECT_NAME} EXPORT_MACRO_NAME "HELIDE_GPU_EXPORT")

project_include_directories(
PRIVATE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/gpu>
)

## Code generation ##

anari_generate_queries(
  DEVICE_TARGET ${PROJECT_NAME}
  CPP_NAMESPACE helide_gpu
  JSON_DEFINITIONS_FILE ${CMAKE_CURRENT_SOURCE_DIR}/HelideGPUDefinitions.json
)

## Install ##

install(TARGETS ${PROJECT_NAME}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  # on Windows put the dlls into bin
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  # ... and the import lib into the devel package
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
