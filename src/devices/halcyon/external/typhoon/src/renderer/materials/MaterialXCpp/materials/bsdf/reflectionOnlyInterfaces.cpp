//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "reflectionOnlyInterfaces.h"
#include "fresnel.h"
#include "thinFilm.h"

namespace mxcpp {
namespace Bsdf {
namespace detail {

Vec3f
ConductorReflectionFresnel(
    const Bsdf::ConductorData& data,
    float cosTheta)
{
    const Vec3f baseReflectance = FresnelConductor(
        cosTheta,
        MaxVec(data.ior, 0.0f),
        MaxVec(data.extinction, 0.0f));
    ThinFilmParams thinFilm;
    thinFilm.model = ThinFilmModel::Conductor;
    thinFilm.ior = MaxVec(data.ior, 0.0f);
    thinFilm.extinction = MaxVec(data.extinction, 0.0f);
    return ApplyThinFilm(
        baseReflectance,
        cosTheta,
        data.thinFilmWeight,
        data.thinFilmThickness,
        data.thinFilmIor,
        thinFilm);
}

Vec3f
GeneralizedSchlickReflectionFresnel(
    const Bsdf::GeneralizedSchlickData& data,
    float cosTheta)
{
    const Vec3f baseReflectance = GeneralizedSchlickFresnel(
        data.color0,
        data.color82,
        data.color90,
        data.exponent,
        cosTheta);
    ThinFilmParams thinFilm;
    thinFilm.model = ThinFilmModel::Schlick;
    thinFilm.F0 = Clamp01(data.color0);
    thinFilm.F82 = Clamp01(data.color82);
    thinFilm.F90 = Clamp01(data.color90);
    thinFilm.exponent = data.exponent;
    return ApplyThinFilm(
        baseReflectance,
        cosTheta,
        data.thinFilmWeight,
        data.thinFilmThickness,
        data.thinFilmIor,
        thinFilm);
}

}  // namespace detail
}  // namespace Bsdf
}  // namespace mxcpp
