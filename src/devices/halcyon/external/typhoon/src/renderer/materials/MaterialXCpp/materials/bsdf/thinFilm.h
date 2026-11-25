//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef PXR_IMAGING_PLUGIN_HDEMBREE_MATERIALXCPP_BSDF_THINFILM_H
#define PXR_IMAGING_PLUGIN_HDEMBREE_MATERIALXCPP_BSDF_THINFILM_H

#include <renderer/materials/MaterialXCpp/mathTypes.h>

namespace mxcpp {
namespace Bsdf {
namespace detail {

/// Returns whether a thin-film layer should be evaluated.
/// `weight`, `thickness`, and `ior` must be finite. The current policy tests
/// positive weight and thickness against `kEpsilon`; `ior` is reserved for
/// policy validation and currently does not affect the result. Cannot fail.
bool HasThinFilm(float weight, float thickness, float ior);

/// Fresnel model used for the film/substrate interface.
enum class ThinFilmModel
{
    Dielectric,
    Conductor,
    Schlick
};

/// Optical parameters for the substrate beneath a non-absorbing film.
struct ThinFilmParams
{
    ThinFilmModel model = ThinFilmModel::Dielectric;
    Vec3f ior = Vec3f(1.0f);
    Vec3f extinction = Vec3f(0.0f);
    Vec3f tint = Vec3f(1.0f);
    Vec3f F0 = Vec3f(0.0f);
    Vec3f F82 = Vec3f(0.0f);
    Vec3f F90 = Vec3f(1.0f);
    float exponent = 5.0f;
};

/// Evaluates polarized dielectric Fresnel power reflectance.
/// `cosTheta` must be finite and is clamped to [0,1]. `ior` is the positive
/// transmitted/incident relative IOR. Returns `(Rp, Rs)`; each component is
/// finite for valid IOR input. Total internal reflection returns `(1,1)`.
/// This operation cannot otherwise fail.
Vec2f FresnelDielectricPolarized(float cosTheta, float ior);

/// Evaluates unpolarized conductor Fresnel power reflectance.
/// `cosTheta` must be finite and is clamped to [0,1]. Every component of `ior`
/// and `extinction` must be finite and non-negative. Returns finite RGB
/// reflectance clamped to [0,1]. This operation cannot fail.
Vec3f FresnelConductor(
    float cosTheta, const Vec3f& ior, const Vec3f& extinction);

/// Blends the base reflectance with Airy thin-film interference.
/// All inputs must be finite. `baseReflectance`, `thinFilmWeight`, and
/// `cosTheta` represent values in [0,1]; weight and cosine are clamped when the
/// film is active. `thinFilmThickness` is nanometres and must be non-negative;
/// `thinFilmIor` and the IOR fields in `params` must be positive. Extinction
/// fields must be non-negative. Returns `baseReflectance` unchanged when the
/// layer is inactive, otherwise finite RGB reflectance clamped to [0,1].
/// This operation cannot fail.
Vec3f ApplyThinFilm(
    const Vec3f& baseReflectance, float cosTheta, float thinFilmWeight,
    float thinFilmThickness, float thinFilmIor,
    const ThinFilmParams& params);

}  // namespace detail
}  // namespace Bsdf
}  // namespace mxcpp

#endif  // PXR_IMAGING_PLUGIN_HDEMBREE_MATERIALXCPP_BSDF_THINFILM_H
