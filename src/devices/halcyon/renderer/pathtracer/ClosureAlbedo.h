// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// The pathtracer's albedo of an evaluated closure (KHR_FRAME_CHANNEL_ALBEDO).
// Typhoon has no albedo output; this is halcyon's own summary of a closure's
// BSDF tree (the tree, not the legacy summary fields, which ND_surface graphs
// such as matte's leave cleared): a reflectance colour per lobe at normal
// incidence, combined as the tree combines its lobes. See AGENTS.md ("Frame
// channels") for the rules.

#include <renderer/materials/MaterialXCpp/mathTypes.h>

namespace mxcpp {
struct SurfaceClosure;
}

namespace halcyon::pathtracer {

struct ClosureAlbedo
{
  // In [0, 1] per component; 0 for a closure without a BSDF tree
  mxcpp::Vec3f albedo{0.f};
  // Every lobe that contributes is a delta (perfectly specular) lobe, and
  // there is at least one: the albedo channel looks past this hit, along the
  // path's specular sample
  bool specularOnly{false};
};

ClosureAlbedo closureAlbedo(const mxcpp::SurfaceClosure &closure);

} // namespace halcyon::pathtracer
