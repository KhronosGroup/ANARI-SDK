// Copyright 2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <pxr/pxr.h>
#include <pxr/imaging/hd/meshUtil.h>

#include <pxr/base/vt/array.h>
#include <pxr/base/vt/value.h>
#include <pxr/base/vt/types.h>

#include <pxr/base/tf/token.h>
#include <pxr/usd/sdf/path.h>

PXR_NAMESPACE_OPEN_SCOPE

class HdAnariMeshUtil : public HdMeshUtil {
public:
  using  HdMeshUtil::HdMeshUtil;

  // Gather per face primvar after a triangulation or quadrangulation
  bool GatherPerFacePrimvar(const SdfPath& id, const TfToken& pvname, VtValue value, VtIntArray primitiveParameters, VtValue *mappedPrimvar) const;

private:
// Gather per face primvar after a triangulation or quadrangulation
  template <typename T> static
  void GatherPerFacePrimVar(VtIntArray const& primitiveParams, void const* sourceUntyped, VtValue *mappedPrimvar);

};

PXR_NAMESPACE_CLOSE_SCOPE