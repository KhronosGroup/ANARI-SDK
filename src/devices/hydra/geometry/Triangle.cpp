// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Triangle.h"

#include <pxr/imaging/hd/tokens.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace anari_hydra {

Triangle::Triangle(HydraGlobalState *s) : Geometry(s) {}

TfToken Triangle::primType() const
{
  return HdPrimTypeTokens->mesh;
}

} // namespace anari_hydra
