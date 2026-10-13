// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Quad.h"

#include <pxr/imaging/hd/tokens.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace anari_hydra {

Quad::Quad(HydraGlobalState *s) : Geometry(s) {}

TfToken Quad::primType() const
{
  return HdPrimTypeTokens->mesh;
}

} // namespace anari_hydra
