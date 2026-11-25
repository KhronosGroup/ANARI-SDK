// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Curve.h"

#include <pxr/imaging/hd/tokens.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace anari_hydra {

Curve::Curve(HydraGlobalState *s) : Geometry(s) {}

TfToken Curve::primType() const
{
  return HdPrimTypeTokens->basisCurves;
}

} // namespace anari_hydra
