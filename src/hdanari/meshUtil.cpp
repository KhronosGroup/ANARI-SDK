// Copyright 2024-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "meshUtil.h"

// pxr
#include <pxr/base/gf/vec2d.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec3d.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec4d.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/trace/staticKeyData.h>
#include <pxr/base/trace/trace.h>
#include <pxr/base/vt/array.h>
#include <pxr/base/vt/types.h>
#include <pxr/base/vt/value.h>
#include <pxr/imaging/hd/perfLog.h>
#include <pxr/imaging/hd/types.h>
#include <pxr/imaging/hd/vtBufferSource.h>
#include <pxr/pxr.h>
#include <pxr/usd/sdf/path.h>

PXR_NAMESPACE_OPEN_SCOPE

template <typename T>
void HdAnariMeshUtil::GatherPerFacePrimVar(VtIntArray const &primitiveParams,
    void const *sourceUntyped,
    VtValue *mappedPrimvar)
{
  T const *source = static_cast<T const *>(sourceUntyped);

  VtArray<T> results;
  results.reserve(primitiveParams.size());
  for (auto pp : primitiveParams) {
    int faceIndex = HdMeshUtil::DecodeFaceIndexFromCoarseFaceParam(pp);
    results.push_back(source[faceIndex]);
  }

  *mappedPrimvar = results;
}

bool GatherGeomSubsetTopology(VtIntArray geomSubsetIndices,
    VtIntArray primitiveParameters,
    VtVec3iArray *triangleIndices)
{
  if (triangleIndices == nullptr) {
    TF_CODING_ERROR(
        "No output buffer provided for trianglulated geomsubset topology generation");
    return false;
  }

  VtVec3iArray result;
  // At least as many triangles as requested indices. It will be more in case
  // of a non triangle mesh.
  result.reserve(geomSubsetIndices.size());

  for (auto index : geomSubsetIndices) {
  }

  return true;
}

bool HdAnariMeshUtil::GatherPerFacePrimvar(const SdfPath &id,
    const TfToken &pvname,
    VtValue value,
    VtIntArray primitiveParameters,
    VtValue *mappedPrimvar)
{
  HD_TRACE_FUNCTION();

  if (mappedPrimvar == nullptr) {
    TF_CODING_ERROR("No output buffer provided for triangulation");
    return false;
  }

  HdVtBufferSource buffer(pvname, value);

  switch (buffer.GetTupleType().type) {
  case HdTypeFloat:
    GatherPerFacePrimVar<float>(
        primitiveParameters, buffer.GetData(), mappedPrimvar);
    break;
  case HdTypeFloatVec2:
    GatherPerFacePrimVar<GfVec2f>(
        primitiveParameters, buffer.GetData(), mappedPrimvar);
    break;
  case HdTypeFloatVec3:
    GatherPerFacePrimVar<GfVec3f>(
        primitiveParameters, buffer.GetData(), mappedPrimvar);
    break;
  case HdTypeFloatVec4:
    GatherPerFacePrimVar<GfVec4f>(
        primitiveParameters, buffer.GetData(), mappedPrimvar);
    break;
  case HdTypeDouble:
    GatherPerFacePrimVar<double>(
        primitiveParameters, buffer.GetData(), mappedPrimvar);
    break;
  case HdTypeDoubleVec2:
    GatherPerFacePrimVar<GfVec2d>(
        primitiveParameters, buffer.GetData(), mappedPrimvar);
    break;
  case HdTypeDoubleVec3:
    GatherPerFacePrimVar<GfVec3d>(
        primitiveParameters, buffer.GetData(), mappedPrimvar);
    break;
  case HdTypeDoubleVec4:
    GatherPerFacePrimVar<GfVec4d>(
        primitiveParameters, buffer.GetData(), mappedPrimvar);
    break;
  default:
    TF_CODING_ERROR("Unsupported primvar type for triangle mapping [%s.%s]",
        id.GetText(),
        pvname.GetText());
    return false;
  }

  return true;
}

PXR_NAMESPACE_CLOSE_SCOPE
