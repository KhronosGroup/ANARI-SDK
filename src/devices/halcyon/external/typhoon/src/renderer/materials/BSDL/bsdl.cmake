# Copyright Contributors to the Open Shading Language project.
# SPDX-License-Identifier: BSD-3-Clause
# https://github.com/AcademySoftwareFoundation/OpenShadingLanguage

function(ADD_BSDL_LIBRARY NAME)
    cmake_parse_arguments(PARSE_ARGV 1 bsdl "" "SUBDIR" "SPECTRAL_COLOR_SPACES")

    set(_bsdl_source_dir "${CMAKE_CURRENT_SOURCE_DIR}")
    set(_bsdl_binary_dir "${CMAKE_CURRENT_BINARY_DIR}")
    if(bsdl_SUBDIR)
        set(_bsdl_source_dir "${_bsdl_source_dir}/${bsdl_SUBDIR}")
        set(_bsdl_binary_dir "${_bsdl_binary_dir}/${bsdl_SUBDIR}")
    endif()

    set(_bsdl_generated_include_dir "${_bsdl_binary_dir}/generated/include")
    set(_bsdl_generated_bsdl_dir "${_bsdl_generated_include_dir}/BSDL")

    set(_bsdl_lut_headers
        SPI/microfacet_tools_luts.h
        SPI/bsdf_clearcoat_luts.h
        SPI/bsdf_dielectric_front_luts.h
        SPI/bsdf_dielectric_back_luts.h
        SPI/bsdf_thinlayer_luts.h
        MTX/bsdf_contysheen_luts.h
        MTX/bsdf_zeltnersheen_luts.h
        MTX/bsdf_dielectric_reflfront_luts.h
        MTX/bsdf_dielectric_bothfront_luts.h
        MTX/bsdf_dielectric_bothback_luts.h)
    set(_bsdl_generated_lut_headers)
    foreach(_bsdl_lut_header ${_bsdl_lut_headers})
        list(APPEND _bsdl_generated_lut_headers
             "${_bsdl_generated_bsdl_dir}/${_bsdl_lut_header}")
    endforeach()

    add_library(${NAME}_BOOTSTRAP INTERFACE)
    target_include_directories(${NAME}_BOOTSTRAP INTERFACE
        "${_bsdl_source_dir}/include")
    target_link_libraries(${NAME}_BOOTSTRAP INTERFACE Imath::Imath)
    target_compile_features(${NAME}_BOOTSTRAP INTERFACE cxx_std_17)

    add_executable(${NAME}_genluts "${_bsdl_source_dir}/src/genluts.cpp")
    target_link_libraries(${NAME}_genluts
        PRIVATE ${NAME}_BOOTSTRAP Threads::Threads)

    add_custom_command(
        OUTPUT ${_bsdl_generated_lut_headers}
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "${_bsdl_generated_bsdl_dir}/SPI"
                "${_bsdl_generated_bsdl_dir}/MTX"
        COMMAND $<TARGET_FILE:${NAME}_genluts> "${_bsdl_generated_bsdl_dir}"
        DEPENDS ${NAME}_genluts
        USES_TERMINAL
        VERBATIM
        COMMENT "Generating BSDL lookup tables")

    add_custom_target(${NAME}_generate_luts ALL
        DEPENDS ${_bsdl_generated_lut_headers})

    if(DEFINED bsdl_SPECTRAL_COLOR_SPACES)
        add_executable(${NAME}_jakobhanika_luts
            "${_bsdl_source_dir}/src/jakobhanika_luts.cpp")
        target_link_libraries(${NAME}_jakobhanika_luts PRIVATE Threads::Threads)
        foreach(CS ${bsdl_SPECTRAL_COLOR_SPACES})
            set(JACOBHANIKA_${CS} "${_bsdl_binary_dir}/jakobhanika_${CS}.cpp")
            list(APPEND BSDL_LUTS_CPP "${JACOBHANIKA_${CS}}")
            add_custom_command(
                OUTPUT "${JACOBHANIKA_${CS}}"
                USES_TERMINAL
                COMMAND $<TARGET_FILE:${NAME}_jakobhanika_luts>
                        64 "${JACOBHANIKA_${CS}}" ${CS}
                DEPENDS ${NAME}_jakobhanika_luts
                COMMENT "Generating Jakob-Hanika RGB-Spectrum ${CS} tables")
        endforeach()
        set(${NAME}_LUTS_CPP ${BSDL_LUTS_CPP} PARENT_SCOPE)
    endif()

    add_library(${NAME} INTERFACE)
    target_include_directories(${NAME} INTERFACE
        $<BUILD_INTERFACE:${_bsdl_source_dir}/include>
        $<BUILD_INTERFACE:${_bsdl_generated_include_dir}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
    target_link_libraries(${NAME} INTERFACE Imath::Imath)
    target_compile_features(${NAME} INTERFACE cxx_std_17)
    add_dependencies(${NAME} ${NAME}_generate_luts)

    set(${NAME}_GENERATED_LUT_HEADERS
        ${_bsdl_generated_lut_headers}
        PARENT_SCOPE)
    set(BSDL_GENERATED_INCLUDE_DIR
        "${_bsdl_generated_include_dir}"
        PARENT_SCOPE)
endfunction()
