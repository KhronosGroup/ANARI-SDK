// Copyright 2024-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "points.h"

#include "anariTokens.h"
#include "geometry.h"

// anari
#include <anari/frontend/anari_enums.h>
#include <anari/anari_cpp.hpp>
#include <anari/anari_cpp/anari_cpp_impl.hpp>
// pxr
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/staticData.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/array.h>
#include <pxr/base/vt/types.h>
#include <pxr/base/vt/value.h>
#include <pxr/imaging/hd/changeTracker.h>
#include <pxr/imaging/hd/enums.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hd/types.h>
// std
#include <algorithm>
#include <iterator>
#include <string>

using namespace std::string_literals;

PXR_NAMESPACE_OPEN_SCOPE

// HdAnariPoints definitions
// ////////////////////////////////////////////////////

HdAnariPoints::HdAnariPoints(
    anari::Device d, const SdfPath &id, const SdfPath &instancerId)
    : HdAnariGeometry(d, HdAnariTokens->sphere, id, instancerId)
{}

HdDirtyBits HdAnariPoints::GetInitialDirtyBitsMask() const
{
  auto mask = HdChangeTracker::Clean | HdChangeTracker::InitRepr
      | HdChangeTracker::DirtyExtent | HdChangeTracker::DirtyPoints
      | HdChangeTracker::DirtyPrimID | HdChangeTracker::DirtyPrimvar
      | HdChangeTracker::DirtyRepr | HdChangeTracker::DirtyMaterialId
      | HdChangeTracker::DirtyTransform | HdChangeTracker::DirtyVisibility
      | HdChangeTracker::DirtyWidths | HdChangeTracker::DirtyInstancer;

  return mask;
}

HdAnariGeometry::GeomSpecificPrimvars HdAnariPoints::GetGeomSpecificPrimvars(
    HdSceneDelegate *sceneDelegate,
    HdDirtyBits *dirtyBits,
    const TfToken::Set &allPrimvars,
    const VtVec3fArray &points,
    const SdfPath &geomsetId)
{
  GeomSpecificPrimvars primvars;

  // Sphere radii
  if (HdChangeTracker::IsDirty(HdChangeTracker::DirtyWidths)) {
    if (m_radiiArray) {
      anari::release(device_, m_radiiArray);
      m_radiiArray = {};
    }

    if (const auto &widthsVt = sceneDelegate->Get(GetId(), HdTokens->widths);
        widthsVt.IsHolding<VtFloatArray>()) {
      const auto &widths = widthsVt.UncheckedGet<VtFloatArray>();
      VtFloatArray radii;
      radii.reserve(std::size(widths));
      std::transform(std::cbegin(widths),
          std::cend(widths),
          std::back_inserter(radii),
          [](auto v) { return v / 2.0f; });

      m_radiiArray = _GetAttributeArray(VtValue(radii), ANARI_FLOAT32);
      primvars.push_back({HdAnariTokens->vertexRadius, m_radiiArray});
    }
  }
  return primvars;
}

HdAnariGeometry::PrimvarSource HdAnariPoints::UpdatePrimvarSource(
    HdSceneDelegate *sceneDelegate,
    HdInterpolation interpolation,
    const TfToken &attributeName,
    const VtValue &value)
{
  switch (interpolation) {
  case HdInterpolationConstant: {
    if (value.IsArrayValued()) {
      if (value.GetArraySize() == 0) {
        TF_RUNTIME_ERROR("Constant interpolation with no value.");
        return {};
      }
      if (value.GetArraySize() > 1) {
        TF_RUNTIME_ERROR("Constant interpolation with more than one value.");
      }
    }

    GfVec4f v;
    if (_GetVtValueAsAttribute(value, v)) {
      return v;
    } else {
      TF_RUNTIME_ERROR(
          "Error extracting value from primvar %s", attributeName.GetText());
    }
    break;
  }
  case HdInterpolationUniform:
  case HdInterpolationFaceVarying: {
    break;
  }
  case HdInterpolationVarying:
  case HdInterpolationVertex: {
    return _GetAttributeArray(value);
    break;
  }
  default:
    break;
  }
  return {};
}

void HdAnariPoints::Finalize(HdRenderParam *renderParam)
{
  anari::release(device_, m_radiiArray);

  HdAnariGeometry::Finalize(renderParam);
}

PXR_NAMESPACE_CLOSE_SCOPE
