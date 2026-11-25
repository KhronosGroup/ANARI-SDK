//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef MXCPP_MATERIALS_GLTF_PBR_H
#define MXCPP_MATERIALS_GLTF_PBR_H

#include <renderer/materials/MaterialXCpp/paramMap.h>
#include <renderer/materials/MaterialXCpp/surfaceClosure.h>

namespace mxcpp {

/// Evaluate the MaterialX glTF PBR surface model.
SurfaceClosure EvalGltfPbr(const ParamMap& params);

}  // namespace mxcpp

#endif
