## Copyright 2025-2026 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

add_executable(halcyon_test_math test_math.cpp test_math.ispc)

# The tests intentionally gather/scatter through varying pointers
set_source_files_properties(test_math.ispc PROPERTIES COMPILE_OPTIONS "--woff")

target_include_directories(halcyon_test_math PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(halcyon_test_math PRIVATE anari::helium)

add_test(NAME halcyon::math COMMAND halcyon_test_math)

add_executable(halcyon_test_hitinfo test_hitinfo.cpp test_hitinfo.ispc)

# The test runs getHitInfo() over varying hits, which gathers by design
set_source_files_properties(test_hitinfo.ispc PROPERTIES COMPILE_OPTIONS "--woff")
target_include_directories(halcyon_test_hitinfo PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(halcyon_test_hitinfo PRIVATE embree anari::anari)

add_test(NAME halcyon::hitinfo COMMAND halcyon_test_hitinfo)

add_executable(halcyon_test_instance test_instance.cpp)
target_link_libraries(halcyon_test_instance PRIVATE anari::anari)
add_dependencies(halcyon_test_instance ${PROJECT_NAME})

# Load the device from the build tree, not an installed copy
add_test(NAME halcyon::instance COMMAND halcyon_test_instance)
set_tests_properties(halcyon::instance PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:${PROJECT_NAME}>:$ENV{LD_LIBRARY_PATH}"
)

# The derivative hooks the tests below link round like the device's
# (GeometryDerivatives.ispc)
set_source_files_properties(
  ${PROJECT_SOURCE_DIR}/scene/surface/geometry/GeometryDerivatives.ispc
  PROPERTIES COMPILE_OPTIONS "--opt=disable-fma")

# Links Geometry.ispc for the per-subtype vertex attribute hooks it installs
# (and GeometryDerivatives.ispc for the derivative hooks its setters install)
add_executable(halcyon_test_attributes
  test_attributes.cpp
  test_attributes.ispc
  ${PROJECT_SOURCE_DIR}/scene/surface/geometry/Geometry.ispc
  ${PROJECT_SOURCE_DIR}/scene/surface/geometry/GeometryDerivatives.ispc
)

# The test resolves attributes over varying hits, which gathers by design
set_source_files_properties(test_attributes.ispc PROPERTIES COMPILE_OPTIONS "--woff")
target_include_directories(halcyon_test_attributes PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(halcyon_test_attributes PRIVATE embree anari::anari)

add_test(NAME halcyon::attributes COMMAND halcyon_test_attributes)

add_executable(halcyon_test_idchannels test_idchannels.cpp)
target_link_libraries(halcyon_test_idchannels PRIVATE anari::anari)
add_dependencies(halcyon_test_idchannels ${PROJECT_NAME})

add_test(NAME halcyon::idchannels COMMAND halcyon_test_idchannels)
set_tests_properties(halcyon::idchannels PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:${PROJECT_NAME}>:$ENV{LD_LIBRARY_PATH}"
)

add_executable(halcyon_test_renderer test_renderer.cpp)
target_link_libraries(halcyon_test_renderer PRIVATE anari::anari)
add_dependencies(halcyon_test_renderer ${PROJECT_NAME})

add_test(NAME halcyon::renderer COMMAND halcyon_test_renderer)
set_tests_properties(halcyon::renderer PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:${PROJECT_NAME}>:$ENV{LD_LIBRARY_PATH}"
)

add_executable(halcyon_test_framebuffer test_framebuffer.cpp)
target_link_libraries(halcyon_test_framebuffer PRIVATE anari::anari)
add_dependencies(halcyon_test_framebuffer ${PROJECT_NAME})

add_test(NAME halcyon::framebuffer COMMAND halcyon_test_framebuffer)
set_tests_properties(halcyon::framebuffer PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:${PROJECT_NAME}>:$ENV{LD_LIBRARY_PATH}"
)

add_executable(halcyon_test_camera test_camera.cpp)
target_link_libraries(halcyon_test_camera PRIVATE anari::anari)
add_dependencies(halcyon_test_camera ${PROJECT_NAME})

add_test(NAME halcyon::camera COMMAND halcyon_test_camera)
set_tests_properties(halcyon::camera PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:${PROJECT_NAME}>:$ENV{LD_LIBRARY_PATH}"
)

add_executable(halcyon_test_frame test_frame.cpp)
target_link_libraries(halcyon_test_frame PRIVATE anari::anari)
add_dependencies(halcyon_test_frame ${PROJECT_NAME})

# A deadlock in the completion callback test shows up as a timeout
add_test(NAME halcyon::frame COMMAND halcyon_test_frame)
set_tests_properties(halcyon::frame PROPERTIES
  TIMEOUT 60
  ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:${PROJECT_NAME}>:$ENV{LD_LIBRARY_PATH}"
)

add_executable(halcyon_test_rendermodes test_rendermodes.cpp)
target_link_libraries(halcyon_test_rendermodes PRIVATE anari::anari)
add_dependencies(halcyon_test_rendermodes ${PROJECT_NAME})

add_test(NAME halcyon::rendermodes COMMAND halcyon_test_rendermodes)
set_tests_properties(halcyon::rendermodes PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:${PROJECT_NAME}>:$ENV{LD_LIBRARY_PATH}"
)

add_executable(halcyon_test_quad test_quad.cpp)
target_link_libraries(halcyon_test_quad PRIVATE anari::anari)
add_dependencies(halcyon_test_quad ${PROJECT_NAME})

add_test(NAME halcyon::quad COMMAND halcyon_test_quad)
set_tests_properties(halcyon::quad PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:${PROJECT_NAME}>:$ENV{LD_LIBRARY_PATH}"
)

add_executable(halcyon_test_face_varying test_face_varying.cpp)
target_link_libraries(halcyon_test_face_varying PRIVATE anari::anari)
add_dependencies(halcyon_test_face_varying ${PROJECT_NAME})

add_test(NAME halcyon::face_varying COMMAND halcyon_test_face_varying)
set_tests_properties(halcyon::face_varying PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:${PROJECT_NAME}>:$ENV{LD_LIBRARY_PATH}"
)

add_executable(halcyon_test_shading_normals test_shading_normals.cpp)
target_link_libraries(halcyon_test_shading_normals PRIVATE anari::anari)
add_dependencies(halcyon_test_shading_normals ${PROJECT_NAME})

add_test(NAME halcyon::shading_normals COMMAND halcyon_test_shading_normals)
set_tests_properties(halcyon::shading_normals PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:${PROJECT_NAME}>:$ENV{LD_LIBRARY_PATH}"
)

# Shading normals turned into Ng's hemisphere in both renderers: negated
# normals render alike, either winding alike, decided in world space
add_executable(halcyon_test_normal_winding test_normal_winding.cpp)
target_link_libraries(halcyon_test_normal_winding PRIVATE anari::anari)
add_dependencies(halcyon_test_normal_winding ${PROJECT_NAME})

add_test(NAME halcyon::normal_winding COMMAND halcyon_test_normal_winding)
set_tests_properties(halcyon::normal_winding PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:${PROJECT_NAME}>:$ENV{LD_LIBRARY_PATH}"
)

add_executable(halcyon_test_sphere test_sphere.cpp)
target_link_libraries(halcyon_test_sphere PRIVATE anari::anari)
add_dependencies(halcyon_test_sphere ${PROJECT_NAME})

add_test(NAME halcyon::sphere COMMAND halcyon_test_sphere)
set_tests_properties(halcyon::sphere PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:${PROJECT_NAME}>:$ENV{LD_LIBRARY_PATH}"
)

add_executable(halcyon_test_curve test_curve.cpp)
target_link_libraries(halcyon_test_curve PRIVATE anari::anari)
add_dependencies(halcyon_test_curve ${PROJECT_NAME})

add_test(NAME halcyon::curve COMMAND halcyon_test_curve)
set_tests_properties(halcyon::curve PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:${PROJECT_NAME}>:$ENV{LD_LIBRARY_PATH}"
)

# The curve's 'basis' (HALCYON_GEOMETRY_CURVE_BASIS): B-spline and Catmull-Rom
# shapes, attributes and radii along the basis, indexing, clamping, short
# strands, unknown bases; records warnings instead of failing
add_executable(halcyon_test_curve_basis test_curve_basis.cpp)
target_link_libraries(halcyon_test_curve_basis PRIVATE anari::anari)
add_dependencies(halcyon_test_curve_basis ${PROJECT_NAME})

add_test(NAME halcyon::curve_basis COMMAND halcyon_test_curve_basis)
set_tests_properties(halcyon::curve_basis PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:${PROJECT_NAME}>:$ENV{LD_LIBRARY_PATH}"
)

# Geometry validity and index conversion; records warnings instead of failing
add_executable(halcyon_test_geometry_validity test_geometry_validity.cpp)
target_link_libraries(halcyon_test_geometry_validity PRIVATE anari::anari)
add_dependencies(halcyon_test_geometry_validity ${PROJECT_NAME})

add_test(NAME halcyon::geometry_validity COMMAND halcyon_test_geometry_validity)
set_tests_properties(halcyon::geometry_validity PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:${PROJECT_NAME}>:$ENV{LD_LIBRARY_PATH}"
)

add_executable(halcyon_test_cylinder_cone test_cylinder_cone.cpp)
target_link_libraries(halcyon_test_cylinder_cone PRIVATE anari::anari)
add_dependencies(halcyon_test_cylinder_cone ${PROJECT_NAME})

add_test(NAME halcyon::cylinder_cone COMMAND halcyon_test_cylinder_cone)
set_tests_properties(halcyon::cylinder_cone PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:${PROJECT_NAME}>:$ENV{LD_LIBRARY_PATH}"
)

add_executable(halcyon_test_materials test_materials.cpp)
target_link_libraries(halcyon_test_materials PRIVATE anari::anari)
add_dependencies(halcyon_test_materials ${PROJECT_NAME})

add_test(NAME halcyon::materials COMMAND halcyon_test_materials)
set_tests_properties(halcyon::materials PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:${PROJECT_NAME}>:$ENV{LD_LIBRARY_PATH}"
)

add_executable(halcyon_test_indexed_vector test_indexed_vector.cpp)
target_include_directories(halcyon_test_indexed_vector PRIVATE ${PROJECT_SOURCE_DIR})

add_test(NAME halcyon::indexed_vector COMMAND halcyon_test_indexed_vector)

add_executable(halcyon_test_object_storage test_object_storage.cpp)
target_link_libraries(halcyon_test_object_storage PRIVATE anari::anari)
add_dependencies(halcyon_test_object_storage ${PROJECT_NAME})

add_test(NAME halcyon::object_storage COMMAND halcyon_test_object_storage)
set_tests_properties(halcyon::object_storage PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:${PROJECT_NAME}>:$ENV{LD_LIBRARY_PATH}"
)

add_executable(halcyon_test_samplers test_samplers.cpp)
target_link_libraries(halcyon_test_samplers PRIVATE anari::anari)
add_dependencies(halcyon_test_samplers ${PROJECT_NAME})

add_test(NAME halcyon::samplers COMMAND halcyon_test_samplers)
set_tests_properties(halcyon::samplers PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:${PROJECT_NAME}>:$ENV{LD_LIBRARY_PATH}"
)

# Links SpatialField.ispc to set up and sample fields directly; also loads the
# device for the field objects' validity
add_executable(halcyon_test_spatial_field
  test_spatial_field.cpp
  test_spatial_field.ispc
  ${PROJECT_SOURCE_DIR}/scene/volume/spatial_field/SpatialField.ispc
)
# The test samples scattered points, which gathers by design
set_source_files_properties(test_spatial_field.ispc PROPERTIES COMPILE_OPTIONS "--woff")
target_include_directories(halcyon_test_spatial_field PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(halcyon_test_spatial_field PRIVATE embree anari::anari)
add_dependencies(halcyon_test_spatial_field ${PROJECT_NAME})

add_test(NAME halcyon::spatial_field COMMAND halcyon_test_spatial_field)
set_tests_properties(halcyon::spatial_field PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:${PROJECT_NAME}>:$ENV{LD_LIBRARY_PATH}"
)

# Links SpatialField.ispc and Volume.ispc to ray march volumes directly; also
# loads the device to render them
add_executable(halcyon_test_volume
  test_volume.cpp
  test_volume.ispc
  ${PROJECT_SOURCE_DIR}/scene/volume/spatial_field/SpatialField.ispc
  ${PROJECT_SOURCE_DIR}/scene/volume/Volume.ispc
)
# Volumes are sampled at scattered points, which gathers by design
set_source_files_properties(test_volume.ispc PROPERTIES COMPILE_OPTIONS "--woff")
target_include_directories(halcyon_test_volume PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(halcyon_test_volume PRIVATE embree anari::anari)
add_dependencies(halcyon_test_volume ${PROJECT_NAME})

add_test(NAME halcyon::volume COMMAND halcyon_test_volume)
set_tests_properties(halcyon::volume PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:${PROJECT_NAME}>:$ENV{LD_LIBRARY_PATH}"
)

add_executable(halcyon_test_synchronization test_synchronization.cpp)
target_link_libraries(halcyon_test_synchronization PRIVATE anari::anari)
add_dependencies(halcyon_test_synchronization ${PROJECT_NAME})

# A deadlock shows up as a timeout
add_test(NAME halcyon::synchronization COMMAND halcyon_test_synchronization)
set_tests_properties(halcyon::synchronization PROPERTIES
  TIMEOUT 60
  ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:${PROJECT_NAME}>:$ENV{LD_LIBRARY_PATH}"
)

add_executable(halcyon_test_privatize test_privatize.cpp)
target_link_libraries(halcyon_test_privatize PRIVATE anari::anari)
add_dependencies(halcyon_test_privatize ${PROJECT_NAME})

# A deadlock shows up as a timeout
add_test(NAME halcyon::privatize COMMAND halcyon_test_privatize)
set_tests_properties(halcyon::privatize PROPERTIES
  TIMEOUT 60
  ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:${PROJECT_NAME}>:$ENV{LD_LIBRARY_PATH}"
)

# Links the device library and creates the device with anariNewHalcyonDevice()
# from the public header, as an application would
add_executable(halcyon_test_device test_device.cpp)
target_link_libraries(halcyon_test_device PRIVATE ${PROJECT_NAME} anari::anari)

add_test(NAME halcyon::device COMMAND halcyon_test_device)
# the build's library, not an installed copy on LD_LIBRARY_PATH (its RUNPATH
# comes after LD_LIBRARY_PATH)
set_tests_properties(halcyon::device PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:${PROJECT_NAME}>:$ENV{LD_LIBRARY_PATH}"
)

add_executable(halcyon_test_array_begin test_array_begin.cpp)
target_link_libraries(halcyon_test_array_begin PRIVATE anari::anari)
add_dependencies(halcyon_test_array_begin ${PROJECT_NAME})

add_test(NAME halcyon::array_begin COMMAND halcyon_test_array_begin)
set_tests_properties(halcyon::array_begin PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:${PROJECT_NAME}>:$ENV{LD_LIBRARY_PATH}"
)

add_executable(halcyon_test_transparency test_transparency.cpp)
target_link_libraries(halcyon_test_transparency PRIVATE anari::anari)
add_dependencies(halcyon_test_transparency ${PROJECT_NAME})

add_test(NAME halcyon::transparency COMMAND halcyon_test_transparency)
set_tests_properties(halcyon::transparency PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:${PROJECT_NAME}>:$ENV{LD_LIBRARY_PATH}"
)

add_executable(halcyon_test_ambient_occlusion test_ambient_occlusion.cpp)
target_link_libraries(halcyon_test_ambient_occlusion PRIVATE anari::anari)
add_dependencies(halcyon_test_ambient_occlusion ${PROJECT_NAME})

add_test(NAME halcyon::ambient_occlusion COMMAND halcyon_test_ambient_occlusion)
set_tests_properties(halcyon::ambient_occlusion PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:${PROJECT_NAME}>:$ENV{LD_LIBRARY_PATH}"
)

# Typhoon's own material library tests, built from the vendored sources
# (external/typhoon) as upstream builds them
set(TYPHOON_TESTS_DIR
  ${PROJECT_SOURCE_DIR}/external/typhoon/src/renderer/materials/MaterialXCpp/tests)
add_executable(halcyon_test_typhoon_materials
  ${TYPHOON_TESTS_DIR}/testMaterialXCppMain.cpp
  ${TYPHOON_TESTS_DIR}/testMaterialXCppGraph.cpp
  ${TYPHOON_TESTS_DIR}/testMaterialXCppMaterials.cpp
  ${TYPHOON_TESTS_DIR}/testMaterialXCppNodes.cpp
  typhoon_materials/unvendored_suites.cpp
)
target_link_libraries(halcyon_test_typhoon_materials
  PRIVATE halcyon_typhoon_materials)

add_test(NAME halcyon::typhoon_materials COMMAND halcyon_test_typhoon_materials)

add_executable(halcyon_test_typhoon_bsdl
  ${PROJECT_SOURCE_DIR}/external/typhoon/src/renderer/materials/BSDL/tests/smoke.cpp)
target_link_libraries(halcyon_test_typhoon_bsdl PRIVATE halcyon_bsdl)
# The smoke test checks with assert()
if (NOT MSVC)
  target_compile_options(halcyon_test_typhoon_bsdl PRIVATE -UNDEBUG)
endif()

add_test(NAME halcyon::typhoon_bsdl COMMAND halcyon_test_typhoon_bsdl)

# Whether native materials round exactly as the vendored C++ they port
# (tests/native_rounding.h). PathPbm.ispc is built without fused
# multiply-adds and with the system's math library, so it rounds as the C++
# does only where the C++ isn't contracted to FMAs or built with fast math:
# x86-64 without an ISA that has FMA (no -march=haswell/native, -mfma), or
# with -ffp-contract=off. Elsewhere (aarch64, where every target has FMA and
# GCC contracts by default; Apple clang on arm64; -march=native;
# -ffast-math) path_material, path_pbm and native_materials compare within
# rounding instead (HALCYON_NATIVE_BITWISE 0). The probes compile with
# CMAKE_CXX_FLAGS and the build type's flags, so they see what those enable
# (__FMA__, __FAST_MATH__); flags added per target or per configuration of a
# multi-config generator aren't seen, so HALCYON_NATIVE_CHECKS can force the
# answer: AUTO (the default) probes, BITWISE and TOLERANT decide.
set(HALCYON_NATIVE_CHECKS AUTO CACHE STRING
  "Native material checks: AUTO (probe the C++ flags), BITWISE or TOLERANT")
set_property(CACHE HALCYON_NATIVE_CHECKS PROPERTY STRINGS AUTO BITWISE TOLERANT)
include(CheckCXXSourceCompiles)
string(TOUPPER "${CMAKE_BUILD_TYPE}" HALCYON_BUILD_TYPE_UPPER)
set(HALCYON_EFFECTIVE_CXX_FLAGS
  "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${HALCYON_BUILD_TYPE_UPPER}}")
set(CMAKE_REQUIRED_FLAGS "${CMAKE_CXX_FLAGS_${HALCYON_BUILD_TYPE_UPPER}}")
check_cxx_source_compiles([[
#if !(defined(__x86_64__) || defined(_M_X64))
#error not x86-64
#endif
#if defined(__FAST_MATH__) || defined(_M_FP_FAST)
#error fast math
#endif
int main() { return 0; }
]] HALCYON_CXX_X86_64_NO_FAST_MATH)
check_cxx_source_compiles([[
#if defined(__FMA__) || defined(__FMA4__) || defined(_M_FP_CONTRACT)
#error the C++ may be contracted to FMAs
#endif
int main() { return 0; }
]] HALCYON_CXX_NO_FMA)
unset(CMAKE_REQUIRED_FLAGS)
# The parts of fast math that don't define __FAST_MATH__, and the last
# -ffp-contract, which wins
set(HALCYON_CXX_UNSAFE_MATH OFF)
if (HALCYON_EFFECTIVE_CXX_FLAGS MATCHES
    "-f(unsafe-math-optimizations|associative-math|reciprocal-math)")
  set(HALCYON_CXX_UNSAFE_MATH ON)
endif()
set(HALCYON_CXX_FP_CONTRACT "")
string(REGEX MATCHALL "-ffp-contract=[a-z]+" HALCYON_FP_CONTRACT_FLAGS
  "${HALCYON_EFFECTIVE_CXX_FLAGS}")
if (HALCYON_FP_CONTRACT_FLAGS)
  list(GET HALCYON_FP_CONTRACT_FLAGS -1 HALCYON_CXX_FP_CONTRACT)
endif()
if (HALCYON_NATIVE_CHECKS STREQUAL "BITWISE")
  set(HALCYON_NATIVE_BITWISE 1)
  message(STATUS "Native material checks: bitwise (HALCYON_NATIVE_CHECKS)")
elseif (HALCYON_NATIVE_CHECKS STREQUAL "TOLERANT")
  set(HALCYON_NATIVE_BITWISE 0)
  message(STATUS "Native material checks: within rounding (HALCYON_NATIVE_CHECKS)")
elseif (NOT HALCYON_NATIVE_CHECKS STREQUAL "AUTO")
  message(FATAL_ERROR "HALCYON_NATIVE_CHECKS is '${HALCYON_NATIVE_CHECKS}'; "
    "it must be AUTO, BITWISE or TOLERANT")
elseif (HALCYON_CXX_X86_64_NO_FAST_MATH AND NOT HALCYON_CXX_UNSAFE_MATH
    AND (HALCYON_CXX_NO_FMA OR HALCYON_CXX_FP_CONTRACT STREQUAL "-ffp-contract=off"))
  set(HALCYON_NATIVE_BITWISE 1)
  message(STATUS "Native material checks: bitwise")
else()
  set(HALCYON_NATIVE_BITWISE 0)
  message(STATUS "Native material checks: within rounding "
    "(the C++ may round differently: not x86-64, FMA or fast math)")
endif()

# The pathtracer's material interface (renderer/pathtracer/PathMaterial.*):
# ISPC kernels shading through it, compared with Typhoon's sequence in C++;
# run with --bench for the costs
# (links Sampler.ispc for the setters that fill the normal maps' samplers)
add_executable(halcyon_test_path_material
  test_path_material.cpp
  test_path_material.ispc
  ${PROJECT_SOURCE_DIR}/scene/surface/material/sampler/Sampler.ispc
)
target_include_directories(halcyon_test_path_material PRIVATE
  ${PROJECT_SOURCE_DIR}
)
# The kernels gather their points from an AoS array by design
set_source_files_properties(test_path_material.ispc
  PROPERTIES COMPILE_OPTIONS "--woff")
target_link_libraries(halcyon_test_path_material
  PRIVATE halcyon_path_materials Threads::Threads)
target_compile_definitions(halcyon_test_path_material
  PRIVATE HALCYON_NATIVE_BITWISE=${HALCYON_NATIVE_BITWISE})

add_test(NAME halcyon::path_material COMMAND halcyon_test_path_material)

# Native physicallyBased's lobes (renderer/pathtracer/PathPbm.isph) against
# the vendored MaterialXCpp functions they port
add_executable(halcyon_test_path_pbm
  test_path_pbm.cpp
  test_path_pbm.ispc
)
# The kernels gather their inputs from AoS arrays by design
set_source_files_properties(test_path_pbm.ispc
  PROPERTIES COMPILE_OPTIONS "--woff;--opt=disable-fma;--math-lib=system")
target_link_libraries(halcyon_test_path_pbm PRIVATE halcyon_path_materials)
target_compile_definitions(halcyon_test_path_pbm
  PRIVATE HALCYON_NATIVE_BITWISE=${HALCYON_NATIVE_BITWISE})

add_test(NAME halcyon::path_pbm COMMAND halcyon_test_path_pbm)

# halcyon's BSDF sampling of closures (renderer/pathtracer/ClosureSampling.*)
# against Typhoon's: bitwise but for a mix's delta samples, and lossless
# mixes' albedo by BSDF sampling
add_executable(halcyon_test_closure_sampling test_closure_sampling.cpp)
target_include_directories(halcyon_test_closure_sampling PRIVATE
  ${PROJECT_SOURCE_DIR}
)
target_link_libraries(halcyon_test_closure_sampling
  PRIVATE halcyon_path_materials)

add_test(NAME halcyon::closure_sampling COMMAND halcyon_test_closure_sampling)

# The pathtracer's material graphs (renderer/pathtracer/MaterialConverter.*),
# the geomprop bindings of the C++ materials they compile into, and their
# texture system (renderer/pathtracer/HalcyonTextureSystem.*); links
# Sampler.ispc for the setters that fill the samplers it samples
add_executable(halcyon_test_material_graph
  test_material_graph.cpp
  ${PROJECT_SOURCE_DIR}/scene/surface/material/sampler/Sampler.ispc
)
target_include_directories(halcyon_test_material_graph PRIVATE
  ${PROJECT_SOURCE_DIR}
)
target_link_libraries(halcyon_test_material_graph
  PRIVATE halcyon_path_materials anari::helium)
# The context groups' audit names the Typhoon commit it was made at
target_compile_definitions(halcyon_test_material_graph PRIVATE
  HALCYON_TYPHOON_PROVENANCE="${PROJECT_SOURCE_DIR}/external/typhoon/PROVENANCE.md"
)

add_test(NAME halcyon::material_graph COMMAND halcyon_test_material_graph)

# MaterialX's data library as the build embeds it
# (renderer/pathtracer/MaterialXLibrary.*, external/materialx)
add_executable(halcyon_test_materialx_library test_materialx_library.cpp)
target_include_directories(halcyon_test_materialx_library PRIVATE
  ${PROJECT_SOURCE_DIR}
)
target_link_libraries(halcyon_test_materialx_library
  PRIVATE halcyon_materialx_documents halcyon_materialx
  halcyon_materialx_definitions)

add_test(NAME halcyon::materialx_library COMMAND halcyon_test_materialx_library)

# Image files for the pathtracer's texture system
# (renderer/pathtracer/ImageFile.*, HalcyonTextureSystem::Sample2D())
add_executable(halcyon_test_image_files test_image_files.cpp)
target_include_directories(halcyon_test_image_files PRIVATE
  ${PROJECT_SOURCE_DIR}
)
target_link_libraries(halcyon_test_image_files
  PRIVATE halcyon_path_materials halcyon_image_loaders)

add_test(NAME halcyon::image_files COMMAND halcyon_test_image_files)

# MaterialX documents as MaterialXCpp graphs
# (renderer/pathtracer/MaterialXDocument.*), with MaterialX's example
# documents
add_executable(halcyon_test_materialx_document test_materialx_document.cpp)
target_include_directories(halcyon_test_materialx_document PRIVATE
  ${PROJECT_SOURCE_DIR}
)
target_link_libraries(halcyon_test_materialx_document
  PRIVATE halcyon_materialx_documents halcyon_path_materials)
target_compile_definitions(halcyon_test_materialx_document PRIVATE
  HALCYON_MATERIALX_RESOURCES="${HALCYON_MATERIALX_RESOURCES}"
)

add_test(NAME halcyon::materialx_document COMMAND halcyon_test_materialx_document)

add_executable(halcyon_test_pathtracer test_pathtracer.cpp)
target_link_libraries(halcyon_test_pathtracer PRIVATE anari::anari)
add_dependencies(halcyon_test_pathtracer ${PROJECT_NAME})

add_test(NAME halcyon::pathtracer COMMAND halcyon_test_pathtracer)
set_tests_properties(halcyon::pathtracer PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:${PROJECT_NAME}>:$ENV{LD_LIBRARY_PATH}"
)

# The pathtracer's progressive refinement and adaptive sampling: the sample
# limit, converged pixels, the frame's numSamples and refinementProgress
add_executable(halcyon_test_adaptive_sampling test_adaptive_sampling.cpp)
target_link_libraries(halcyon_test_adaptive_sampling PRIVATE anari::anari)
add_dependencies(halcyon_test_adaptive_sampling ${PROJECT_NAME})

add_test(NAME halcyon::adaptive_sampling COMMAND halcyon_test_adaptive_sampling)
set_tests_properties(halcyon::adaptive_sampling PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:${PROJECT_NAME}>:$ENV{LD_LIBRARY_PATH}"
  TIMEOUT 60
)

# The pathtracer's sampleLimit (VisRTX's name, which hdAnari reads): the
# declared default, precedence over convergedSamplesPerPixel, 0 (unbounded)
# and hdAnari's render loop
add_executable(halcyon_test_sample_limit test_sample_limit.cpp)
target_link_libraries(halcyon_test_sample_limit PRIVATE anari::anari)
add_dependencies(halcyon_test_sample_limit ${PROJECT_NAME})

add_test(NAME halcyon::sample_limit COMMAND halcyon_test_sample_limit)
set_tests_properties(halcyon::sample_limit PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:${PROJECT_NAME}>:$ENV{LD_LIBRARY_PATH}"
  TIMEOUT 60
)

# The extensions each renderer reports (introspection and its 'extension'
# property) and the device's list, their union
add_executable(halcyon_test_renderer_extensions test_renderer_extensions.cpp)
target_link_libraries(halcyon_test_renderer_extensions PRIVATE anari::anari)
add_dependencies(halcyon_test_renderer_extensions ${PROJECT_NAME})

add_test(NAME halcyon::renderer_extensions COMMAND halcyon_test_renderer_extensions)
set_tests_properties(halcyon::renderer_extensions PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:${PROJECT_NAME}>:$ENV{LD_LIBRARY_PATH}"
)

# The albedo and normal frame channels of both renderers
add_executable(halcyon_test_aovs test_aovs.cpp)
target_link_libraries(halcyon_test_aovs PRIVATE anari::anari)
add_dependencies(halcyon_test_aovs ${PROJECT_NAME})

add_test(NAME halcyon::aovs COMMAND halcyon_test_aovs)
set_tests_properties(halcyon::aovs PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:${PROJECT_NAME}>:$ENV{LD_LIBRARY_PATH}"
)

# matte and physicallyBased through the pathtracer, as MaterialXCpp graphs:
# constants, attributes, alpha modes, emission, samplers against the preview
# renderer's sampling
add_executable(halcyon_test_pathtracer_materials test_pathtracer_materials.cpp)
target_link_libraries(halcyon_test_pathtracer_materials PRIVATE anari::anari)
add_dependencies(halcyon_test_pathtracer_materials ${PROJECT_NAME})

add_test(NAME halcyon::pathtracer_materials COMMAND halcyon_test_pathtracer_materials)
set_tests_properties(halcyon::pathtracer_materials PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:${PROJECT_NAME}>:$ENV{LD_LIBRARY_PATH}"
)

# The 'materialx' material subtype through the device: documents, the
# properties, overrides in both renderers, and invalid documents
add_executable(halcyon_test_materialx test_materialx.cpp)
target_link_libraries(halcyon_test_materialx PRIVATE anari::anari)
target_compile_definitions(halcyon_test_materialx PRIVATE
  HALCYON_MATERIALX_RESOURCES="${HALCYON_MATERIALX_RESOURCES}"
)
add_dependencies(halcyon_test_materialx ${PROJECT_NAME})

add_test(NAME halcyon::materialx COMMAND halcyon_test_materialx)
set_tests_properties(halcyon::materialx PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:${PROJECT_NAME}>:$ENV{LD_LIBRARY_PATH}"
  TIMEOUT 120
)

# The pathtracer's native materials (matte) against their MaterialXCpp graphs
# on the same scenes and sample sequences ('nativeMaterials' true and false)
add_executable(halcyon_test_native_materials test_native_materials.cpp)
target_link_libraries(halcyon_test_native_materials PRIVATE anari::anari)
target_compile_definitions(halcyon_test_native_materials
  PRIVATE HALCYON_NATIVE_BITWISE=${HALCYON_NATIVE_BITWISE})
add_dependencies(halcyon_test_native_materials ${PROJECT_NAME})

add_test(NAME halcyon::native_materials COMMAND halcyon_test_native_materials)
set_tests_properties(halcyon::native_materials PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:${PROJECT_NAME}>:$ENV{LD_LIBRARY_PATH}"
)

# The pathtracer's path integrator against analytic values (multiple
# bounces, Russian roulette, MIS with sphere lights, transparent shadows,
# stochastic presence, clamping) and its convergence on a Cornell box
add_executable(halcyon_test_path_integrator test_path_integrator.cpp)
target_link_libraries(halcyon_test_path_integrator PRIVATE anari::anari)
add_dependencies(halcyon_test_path_integrator ${PROJECT_NAME})

add_test(NAME halcyon::path_integrator COMMAND halcyon_test_path_integrator)
set_tests_properties(halcyon::path_integrator PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:${PROJECT_NAME}>:$ENV{LD_LIBRARY_PATH}"
  TIMEOUT 300
)

# transferFunction1D volumes through the pathtracer: transmittance and alpha
# against the preview renderer, single scattering, ratio-tracked shadows and
# emitter-only segments against analytic values, overlapping volumes, the
# albedo and normal channels, depth and ids
add_executable(halcyon_test_pathtracer_volumes test_pathtracer_volumes.cpp)
target_link_libraries(halcyon_test_pathtracer_volumes PRIVATE anari::anari)
add_dependencies(halcyon_test_pathtracer_volumes ${PROJECT_NAME})

add_test(NAME halcyon::pathtracer_volumes COMMAND halcyon_test_pathtracer_volumes)
set_tests_properties(halcyon::pathtracer_volumes PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:${PROJECT_NAME}>:$ENV{LD_LIBRARY_PATH}"
  TIMEOUT 300
)

# The lights, through the pathtracer: a luminance test per light and
# brightness parameter, instancing, shadows, camera visibility
add_executable(halcyon_test_lights test_lights.cpp)
target_link_libraries(halcyon_test_lights PRIVATE anari::anari)
add_dependencies(halcyon_test_lights ${PROJECT_NAME})

add_test(NAME halcyon::lights COMMAND halcyon_test_lights)
set_tests_properties(halcyon::lights PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:${PROJECT_NAME}>:$ENV{LD_LIBRARY_PATH}"
)

# The hdri light, through the pathtracer: the map's orientation, furnace
# tests, a floor's irradiance under textured maps, 'visible', the warnings
add_executable(halcyon_test_hdri test_hdri.cpp)
target_link_libraries(halcyon_test_hdri PRIVATE anari::anari)
add_dependencies(halcyon_test_hdri ${PROJECT_NAME})

add_test(NAME halcyon::hdri COMMAND halcyon_test_hdri)
set_tests_properties(halcyon::hdri PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:${PROJECT_NAME}>:$ENV{LD_LIBRARY_PATH}"
)

# The pathtracer's background (what camera misses see, lighting nothing)
# and ambient light (a constant dome camera rays don't see): furnace tests,
# a floor's radiance, and both with visible and invisible hdri lights
add_executable(halcyon_test_background_ambient test_background_ambient.cpp)
target_link_libraries(halcyon_test_background_ambient PRIVATE anari::anari)
add_dependencies(halcyon_test_background_ambient ${PROJECT_NAME})

add_test(NAME halcyon::background_ambient COMMAND halcyon_test_background_ambient)
set_tests_properties(halcyon::background_ambient PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:${PROJECT_NAME}>:$ENV{LD_LIBRARY_PATH}"
)

# The pathtracer's random-walk subsurface scattering: black and white media,
# a slab's reflectance, bounce counting, the walk's owner and its scale
add_executable(halcyon_test_subsurface test_subsurface.cpp)
target_link_libraries(halcyon_test_subsurface PRIVATE anari::anari)
add_dependencies(halcyon_test_subsurface ${PROJECT_NAME})

add_test(NAME halcyon::subsurface COMMAND halcyon_test_subsurface)
set_tests_properties(halcyon::subsurface PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:${PROJECT_NAME}>:$ENV{LD_LIBRARY_PATH}"
)

# The pathtracer's homogeneous interior media: Beer-Lambert through
# volume-only boundaries and glass, one medium at a time, shadow rays, single
# scattering by Henyey-Greenstein, furnaces, grid volumes inside media and the
# albedo channel, against analytic values
add_executable(halcyon_test_media test_media.cpp)
target_link_libraries(halcyon_test_media PRIVATE anari::anari)
add_dependencies(halcyon_test_media ${PROJECT_NAME})

add_test(NAME halcyon::media COMMAND halcyon_test_media)
set_tests_properties(halcyon::media PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:${PROJECT_NAME}>:$ENV{LD_LIBRARY_PATH}"
)

# The pathtracer's hero-wavelength dispersion and caustics policy: a prism's
# dispersion, furnaces of dispersive glass, a mirror's caustic and its clamp,
# shadow rays through glass with caustics on and off
add_executable(halcyon_test_spectral_caustics test_spectral_caustics.cpp)
target_link_libraries(halcyon_test_spectral_caustics PRIVATE anari::anari)
add_dependencies(halcyon_test_spectral_caustics ${PROJECT_NAME})

add_test(NAME halcyon::spectral_caustics COMMAND halcyon_test_spectral_caustics)
set_tests_properties(halcyon::spectral_caustics PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:${PROJECT_NAME}>:$ENV{LD_LIBRARY_PATH}"
  TIMEOUT 600
)

# White furnaces of closures with a delta lobe inside a mix: smooth
# part-metallic physicallyBased, native and through its graph, and materialx
# documents, by BSDF sampling alone and with MIS
add_executable(halcyon_test_mix_delta test_mix_delta.cpp)
target_link_libraries(halcyon_test_mix_delta PRIVATE anari::anari)
target_compile_definitions(halcyon_test_mix_delta
  PRIVATE HALCYON_NATIVE_BITWISE=${HALCYON_NATIVE_BITWISE})
add_dependencies(halcyon_test_mix_delta ${PROJECT_NAME})

add_test(NAME halcyon::mix_delta COMMAND halcyon_test_mix_delta)
set_tests_properties(halcyon::mix_delta PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:${PROJECT_NAME}>:$ENV{LD_LIBRARY_PATH}"
  TIMEOUT 600
)

# The smooth normal's rounding under near-delta GGX lobes: a roughness-0.04
# metal sphere renders as a roughness-0.08 one
add_executable(halcyon_test_normal_rounding test_normal_rounding.cpp)
target_link_libraries(halcyon_test_normal_rounding PRIVATE anari::anari)
add_dependencies(halcyon_test_normal_rounding ${PROJECT_NAME})

add_test(NAME halcyon::normal_rounding COMMAND halcyon_test_normal_rounding)
set_tests_properties(halcyon::normal_rounding PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:${PROJECT_NAME}>:$ENV{LD_LIBRARY_PATH}"
  TIMEOUT 600
)

# The pathtracer's shading context, through its debug modes, against
# analytic values on simple shapes of every geometry subtype
add_executable(halcyon_test_mesh_tangents test_mesh_tangents.cpp)
target_link_libraries(halcyon_test_mesh_tangents PRIVATE anari::anari)
add_dependencies(halcyon_test_mesh_tangents ${PROJECT_NAME})

add_test(NAME halcyon::mesh_tangents COMMAND halcyon_test_mesh_tangents)
set_tests_properties(halcyon::mesh_tangents PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:${PROJECT_NAME}>:$ENV{LD_LIBRARY_PATH}"
)

add_executable(halcyon_test_shading_context test_shading_context.cpp)
target_link_libraries(halcyon_test_shading_context PRIVATE anari::anari)
add_dependencies(halcyon_test_shading_context ${PROJECT_NAME})

add_test(NAME halcyon::shading_context COMMAND halcyon_test_shading_context)
set_tests_properties(halcyon::shading_context PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:${PROJECT_NAME}>:$ENV{LD_LIBRARY_PATH}"
)

# The device's runtime dependencies include no OpenUSD, TBB or Python; its
# dynamic symbols none of the libraries it links with hidden symbols. ELF
# only (file(READ_ELF), nm -D, --exclude-libs)
if (UNIX AND NOT APPLE)
  # anari::anari is the installed library when halcyon builds alone, an
  # interface to the SDK's own 'anari' target in the SDK's tree
  if (TARGET anari)
    set(HALCYON_ANARI_LIBRARY anari)
  else()
    set(HALCYON_ANARI_LIBRARY anari::anari)
  endif()
  add_test(NAME halcyon::device_dependencies
    COMMAND ${CMAKE_COMMAND}
      -DDEVICE=$<TARGET_FILE:${PROJECT_NAME}>
      -DNM=${CMAKE_NM}
      "-DSEARCH_DIRS=$<TARGET_FILE_DIR:${PROJECT_NAME}>;$<TARGET_FILE_DIR:${HALCYON_ANARI_LIBRARY}>"
      -P ${CMAKE_CURRENT_LIST_DIR}/checkDeviceDependencies.cmake)
endif()
