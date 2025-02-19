// Copyright 2024-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "geometry.h"
// anari
#include <anari/anari_cpp.hpp>
// pxr
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/types.h>
#include <pxr/base/vt/value.h>
#include <pxr/imaging/hd/enums.h>
#include <pxr/imaging/hd/geomSubset.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/imaging/hd/types.h>
#include <pxr/imaging/hf/perfLog.h>
#include <pxr/pxr.h>
#include <pxr/usd/sdf/path.h>

PXR_NAMESPACE_OPEN_SCOPE

class HdAnariPoints final : public HdAnariGeometry
{
 public:
  HF_MALLOC_TAG_NEW("new HdAnariPoints");
  HdAnariPoints(anari::Device d,
      const SdfPath &id,
      const SdfPath &instancerId = SdfPath());

  HdDirtyBits GetInitialDirtyBitsMask() const override;

 protected:
  HdGeomSubsets GetGeomSubsets(
      HdSceneDelegate *sceneDelegate, HdDirtyBits *dirtyBits) override
  {
    return {};
  }

  HdAnariGeometry::GeomSpecificPrimvars GetGeomSpecificPrimvars(
      HdSceneDelegate *sceneDelegate,
      HdDirtyBits *dirtyBits,
      const TfToken::Set &allPrimvars,
      const VtVec3fArray &points,
      const SdfPath &geomsetId) override;
  HdAnariGeometry::PrimvarSource UpdatePrimvarSource(
      HdSceneDelegate *sceneDelegate,
      HdInterpolation interpolation,
      const TfToken &attributeName,
      const VtValue &value) override;

  void Finalize(HdRenderParam *renderParam) override;

 private:
  HdAnariPoints(const HdAnariPoints &) = delete;
  HdAnariPoints &operator=(const HdAnariPoints &) = delete;

  anari::Array1D m_radiiArray{};
};

PXR_NAMESPACE_CLOSE_SCOPE