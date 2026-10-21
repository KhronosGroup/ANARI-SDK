// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "PathMaterial.isph"

// Exported so that ispc writes the records and flags into PathMaterial_ispc.h,
// which the C++ side (PathMaterial.h) reads them from
export void PathMaterial_layouts(uniform PathShadeFlag,
    uniform PathMaterialFlag,
    uniform PathSampleFlag,
    uniform PathSoADerivative,
    uniform PathContextGroup,
    uniform MaterialKind,
    uniform Attribute,
    uniform PathMaterialContext *uniform,
    uniform PathShadeInSoA *uniform,
    uniform PathShadeOutSoA *uniform,
    uniform PathEvalInSoA *uniform,
    uniform PathEvalOutSoA *uniform,
    uniform PathSubsurfaceInSoA *uniform,
    uniform PathSubsurfaceOutSoA *uniform,
    uniform PathHandleSoA *uniform,
    uniform PathMediumSoA *uniform,
    uniform PathShadowOutSoA *uniform)
{}

