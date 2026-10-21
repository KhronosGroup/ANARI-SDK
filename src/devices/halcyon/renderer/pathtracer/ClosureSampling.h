// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: LicenseRef-TOST-1.0

#pragma once

// BSDF sampling of evaluated closures: Typhoon's, with a mix node's delta
// samples weighed as its evaluation weighs them. The
// composite nodes of Typhoon's closure traversal are ported from MaterialXCpp's
// materials/bsdf/closureTraversal.cpp (vendored, unmodified, and not editable
// here); keep them in step with it when the pin moves.

#include <renderer/materials/MaterialXCpp/materials/bsdf.h>

namespace halcyon::pathtracer {

// Typhoon's Bsdf::SampleSurface(), except that a mix node passes a delta
// sample of the child it chose up unscaled: Typhoon divides it by the
// choice's probability (closureTraversal.cpp, SampleNode()'s MixData case),
// but a mix evaluates to the lerp of its children, whose leaves don't carry
// the mix weight, so its delta part is mix x the child's, which choosing the
// child with probability mix already estimates. Layers and adds, whose
// evaluation sums their children, keep the division. A subsurface sample
// keeps Typhoon's division at every mix (a maintainer decision, see
// AGENTS.md): Typhoon's OpenPBR and standard_surface give the subsurface leaf
// its own mix's weight, which that mix's division makes up for, but each mix
// enclosing that one (their metal and transmission mixes) divides again, and
// a subsurface leaf of weight 1 (disney_principled's, a document's) isn't
// made up for at all.
// Every sample that doesn't take a delta lobe through a mix is Typhoon's,
// bitwise: the traversal's choices don't depend on the values.
mxcpp::Bsdf::BsdfSample sampleSurface(const mxcpp::SurfaceClosure &closure,
    const mxcpp::SurfaceInteraction &interaction,
    float u1,
    float u2,
    float uLobe);

// SampleNode() of the subtree at 'nodeId' with the mix weighting above (the
// whole traversal, not only where Typhoon's sample would differ)
mxcpp::Bsdf::BsdfSample sampleNode(const mxcpp::Bsdf::ClosureTree &tree,
    mxcpp::Bsdf::NodeId nodeId,
    const mxcpp::SurfaceInteraction &interaction,
    float u1,
    float u2,
    float uChoice);

} // namespace halcyon::pathtracer
