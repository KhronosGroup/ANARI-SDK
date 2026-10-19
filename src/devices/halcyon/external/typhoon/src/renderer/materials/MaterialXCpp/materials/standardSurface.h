//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef MXCPP_MATERIALS_STANDARD_SURFACE_H
#define MXCPP_MATERIALS_STANDARD_SURFACE_H

#include <renderer/materials/MaterialXCpp/paramMap.h>
#include <renderer/materials/MaterialXCpp/surfaceClosure.h>

namespace mxcpp {

/// Evaluate the Autodesk Standard Surface material model.
/// Maps Standard Surface parameters to a unified surface closure.
SurfaceClosure EvalStandardSurface(const ParamMap& params);

}  // namespace mxcpp

#endif
