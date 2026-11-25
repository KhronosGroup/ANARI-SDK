//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef PXR_IMAGING_PLUGIN_HDEMBREE_MATERIALXCPP_BSDF_LEGACYSURFACE_H
#define PXR_IMAGING_PLUGIN_HDEMBREE_MATERIALXCPP_BSDF_LEGACYSURFACE_H

#include "microfacet.h"

#include <renderer/materials/MaterialXCpp/materials/bsdf.h>

namespace mxcpp {
namespace Bsdf {
namespace detail {

/// Evaluates the legacy summary lobes stored directly on `closure`.
/// The normal selects the incident transport side; directions must be finite
/// unit vectors pointing away from the surface. Closure colors and weights
/// must be finite and non-negative;
/// roughness and weights are expected in [0,1], and IORs must be positive.
/// Returns finite non-negative RGB BSDF value. Invalid geometric
/// configurations contribute zero; this operation does not throw.
Vec3f EvalLegacySurface(
    const SurfaceClosure& closure, const SurfaceInteraction& interaction,
    const Vec3f& omegaInWld,
    BumpShadowingContext bumpContext);

/// Evaluates the legacy summary's mixture PDF.
/// The normal selects the incident transport side; directions must be finite
/// unit vectors pointing away from the surface, and `closure` must satisfy
/// `EvalLegacySurface`'s parameter
/// invariants. Returns a finite non-negative solid-angle density; an empty or
/// degenerate lobe mixture returns zero. This operation does not throw.
float PdfLegacySurface(
    const SurfaceClosure& closure, const SurfaceInteraction& interaction,
    const Vec3f& omegaInWld);

/// Samples one lobe from the legacy summary.
/// `normalShdWldOut` and `omegaOutWld` must be finite unit vectors pointing
/// away from the surface. `u1`, `u2`, and `uLobe` must be finite values in
/// [0,1). `closure` must satisfy `EvalLegacySurface`'s parameter invariants.
/// Returns a unit incident direction when valid. An empty or degenerate
/// mixture returns `pdfSolidAngle == 0`; callers must test it before division.
/// This operation does not throw.
Bsdf::BsdfSample SampleLegacySurface(
    const SurfaceClosure& closure, const SurfaceInteraction& interaction,
    float u1, float u2, float uLobe);

/// Clears every legacy BSDF summary field while retaining unrelated closure
/// state. `closure` must be non-null. This operation cannot fail.
void ClearLegacyBsdfSummary(SurfaceClosure* closure);

}  // namespace detail
}  // namespace Bsdf
}  // namespace mxcpp

#endif  // PXR_IMAGING_PLUGIN_HDEMBREE_MATERIALXCPP_BSDF_LEGACYSURFACE_H
