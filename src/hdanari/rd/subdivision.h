// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <pxr/base/vt/value.h>
#include <pxr/imaging/hd/enums.h>
#include <pxr/imaging/hd/meshTopology.h>
#include <pxr/imaging/pxOsd/refinerFactory.h>
#include <pxr/pxr.h>

#include <memory>

PXR_NAMESPACE_OPEN_SCOPE

// Uniform OpenSubdiv refinement of a mesh, driven by the USD complexity-derived
// refine level (HdDisplayStyle::refineLevel). Refines the subdivision control
// cage into a denser polygonal mesh and interpolates primvars so they stay
// consistent with the refined vertices and faces.
//
// v1 limitations: uniform refinement only; face-varying primvars are not
// refined (Refine returns an empty VtValue for them); meshes carrying geom
// subsets are not refined (Create returns null).
class HdAnariSubdivision
{
 public:
  // Returns null when the mesh should not be refined: non-positive level, no
  // subdivision scheme, or geom subsets present.
  static std::unique_ptr<HdAnariSubdivision> Create(
      const HdMeshTopology &topology, int refineLevel);

  // Refined polygonal topology (subdivision scheme cleared to "none").
  const HdMeshTopology &refinedTopology() const { return refinedTopology_; }

  // Interpolate a coarse primvar value onto the refined mesh. Constant values
  // pass through unchanged; face-varying (and unsupported element types) return
  // an empty VtValue.
  VtValue Refine(const VtValue &value, HdInterpolation interpolation) const;

 private:
  HdAnariSubdivision() = default;

  PxOsdTopologyRefinerSharedPtr refiner_;
  HdMeshTopology refinedTopology_;
  int level_ = 0;
};

PXR_NAMESPACE_CLOSE_SCOPE
