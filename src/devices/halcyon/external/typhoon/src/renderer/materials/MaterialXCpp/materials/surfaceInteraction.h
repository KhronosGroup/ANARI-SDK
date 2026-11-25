//
// Per-hit state shared by surface-closure traversal.
//
#ifndef MXCPP_MATERIALS_SURFACE_INTERACTION_H
#define MXCPP_MATERIALS_SURFACE_INTERACTION_H

#include <renderer/materials/MaterialXCpp/mathTypes.h>

namespace mxcpp {

/// Per-hit state that is constant for one closure traversal. All normals and
/// `omegaOutWld` are finite unit vectors on the incident transport side.
/// `frontFacing` is immutable geometric state and must not be inferred from a
/// shading normal.
struct SurfaceInteraction
{
    /// Whole-material graph normal. Read only by the legacy summary path and
    /// deliberately unprepared standalone leaf helpers. Closure-tree
    /// traversal reads each leaf's prepared normal instead.
    Vec3f normalShdWldOut;
    Vec3f normalSrfWldOut;
    Vec3f normalGeomWldOut;
    Vec3f omegaOutWld;
    float heroWavelengthNm = 0.0f;
    bool frontFacing = true;
};

}  // namespace mxcpp

#endif  // MXCPP_MATERIALS_SURFACE_INTERACTION_H
