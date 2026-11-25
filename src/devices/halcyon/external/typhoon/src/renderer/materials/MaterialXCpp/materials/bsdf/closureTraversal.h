//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef PXR_IMAGING_PLUGIN_HDEMBREE_MATERIALXCPP_BSDF_CLOSURETRAVERSAL_H
#define PXR_IMAGING_PLUGIN_HDEMBREE_MATERIALXCPP_BSDF_CLOSURETRAVERSAL_H

#include "microfacet.h"

#include <renderer/materials/MaterialXCpp/materials/bsdf.h>

namespace mxcpp {
namespace Bsdf {
namespace detail {

/// Returns a copy of `tree` with caustic-class leaves removed and surviving
/// node references remapped. `tree` must contain only valid child references.
/// An entirely removed tree is returned with an invalid root; allocation
/// failure may propagate as `std::bad_alloc`.
Bsdf::ClosureTree PruneCausticClassLobes(const Bsdf::ClosureTree& tree);

/// Prepares every validated surface leaf normal once for later traversals.
/// Un-authored leaves inherit finite unit incident-side `normalShdWldOut`;
/// authored leaves must already contain finite unit exterior normals validated
/// against the graph normal and are faced to the same side. Glossy and
/// subsurface-entry normals are then raised toward finite unit incident-side
/// `normalGeomWldOut` when needed to keep their deterministic mirror direction
/// above the geometric surface. This matches Cycles for both delta and
/// finite-roughness microfacet closures; generated directions are still
/// validated at sampling. `tree` must be non-null. This is a one-shot operation
/// on a tree whose authored leaves have already been validated. The tree must
/// not be mutated afterwards. Allocation-free and does not throw.
void PrepareShadingNormals(
    Bsdf::ClosureTree* tree,
    const Vec3f& normalShdWldOut,
    const Vec3f& normalGeomWldOut,
    const Vec3f& omegaOutWld);

/// Evaluates the closure subtree rooted at `nodeId`.
/// `nodeId` may be invalid, in which case zero is returned. `interaction`
/// satisfies `SurfaceInteraction`'s contract and `omegaInWld` is a finite unit
/// direction pointing away from the surface. The tree must have been prepared.
/// Returns a finite non-negative RGB BSDF value; malformed or degenerate nodes
/// contribute zero. Does not throw.
Vec3f EvalNode(
    const Bsdf::ClosureTree& tree, Bsdf::NodeId nodeId,
    const SurfaceInteraction& interaction, const Vec3f& omegaInWld,
    BumpShadowingContext bumpContext);

/// EvalNode with each surface leaf multiplied by its own absolute incident
/// cosine before composite nodes combine the values.
Vec3f EvalNodeCosine(
    const Bsdf::ClosureTree& tree, Bsdf::NodeId nodeId,
    const SurfaceInteraction& interaction, const Vec3f& omegaInWld,
    BumpShadowingContext bumpContext);

/// Evaluates the solid-angle PDF of the closure subtree rooted at `nodeId`.
/// `nodeId` may be invalid, in which case zero is returned. `interaction` and
/// `omegaInWld` satisfy `EvalNode`'s invariants. Returns a finite non-negative
/// density; degenerate nodes return zero. Does not throw.
float PdfNode(
    const Bsdf::ClosureTree& tree, Bsdf::NodeId nodeId,
    const SurfaceInteraction& interaction, const Vec3f& omegaInWld);

/// Samples the closure subtree rooted at `nodeId`.
/// `nodeId` may be invalid. `interaction` satisfies `SurfaceInteraction`'s
/// contract and its geometric normal owns reflection/transmission validity.
/// `u1`, `u2`, and `uChoice` must be finite values in [0,1). Returns a sample
/// whose direction is unit length when valid.
/// Failure, a degenerate subtree, or a selected wrong-side direction returns
/// zero BSDF and PDF without resampling. Callers must test the PDF before
/// division. Does not throw.
Bsdf::BsdfSample SampleNode(
    const Bsdf::ClosureTree& tree, Bsdf::NodeId nodeId,
    const SurfaceInteraction& interaction, float u1, float u2,
    float uChoice);

}  // namespace detail
}  // namespace Bsdf
}  // namespace mxcpp

#endif  // PXR_IMAGING_PLUGIN_HDEMBREE_MATERIALXCPP_BSDF_CLOSURETRAVERSAL_H
