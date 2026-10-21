//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef PXR_IMAGING_PLUGIN_HDEMBREE_MATERIALXCPP_BSDF_REFLECTIONONLYINTERFACES_H
#define PXR_IMAGING_PLUGIN_HDEMBREE_MATERIALXCPP_BSDF_REFLECTIONONLYINTERFACES_H

#include "mathPrimitives.h"
#include "shadingFrame.h"

#include <renderer/materials/MaterialXCpp/materials/bsdf.h>

#include <algorithm>
#include <cmath>

namespace mxcpp {
namespace Bsdf {
namespace detail {

/// Evaluates conductor Fresnel, including optional thin-film interference.
/// `data` optical vectors must be finite; IOR and extinction are non-negative,
/// thin-film weight is in [0,1], thickness is non-negative, and film IOR is
/// positive. `cosTheta` must be finite and represents an absolute surface
/// cosine in [0,1]. Returns finite RGB reflectance in [0,1]. Cannot fail.
Vec3f ConductorReflectionFresnel(
    const Bsdf::ConductorData& data, float cosTheta);

/// Evaluates generalized-Schlick Fresnel, including optional thin film.
/// `data` colors and exponent must be finite, thin-film weight is in [0,1],
/// thickness is non-negative, and film IOR is positive. `cosTheta` must be
/// finite in [0,1]. Returns finite RGB reflectance in [0,1]. Cannot fail.
Vec3f GeneralizedSchlickReflectionFresnel(
    const Bsdf::GeneralizedSchlickData& data, float cosTheta);

/// Samples the conductor's discrete mirror direction.
/// `normalShdLobeWldOut` and `omegaOutWld` must be finite unit vectors, with
/// the normal facing the active lobe. `data` must satisfy
/// `ConductorReflectionFresnel`'s invariants and have finite non-negative
/// weight. Returns a unit incident direction, `pdfSolidAngle == 1`, and
/// `isSpecular == true`. Cannot fail for valid inputs.
inline Bsdf::BsdfSample
SampleDeltaConductorReflection(
    const Bsdf::ConductorData& data, const Vec3f& normalShdLobeWldOut,
    const Vec3f& omegaOutWld)
{
    const float cosTheta =
        std::max(std::abs(Dot(normalShdLobeWldOut, omegaOutWld)), kEpsilon);
    return SampleDeltaReflection(
        ConductorReflectionFresnel(data, cosTheta), data.weight,
        normalShdLobeWldOut, omegaOutWld);
}

/// Samples the generalized-Schlick interface's discrete mirror direction.
/// The normal and outgoing direction must be finite unit vectors, with the
/// normal facing the active lobe. `data` must satisfy
/// `GeneralizedSchlickReflectionFresnel`'s invariants and have finite
/// non-negative weight. Returns a unit direction, `pdfSolidAngle == 1`, and
/// `isSpecular == true`. Cannot fail for valid inputs.
inline Bsdf::BsdfSample
SampleDeltaGeneralizedSchlickReflection(
    const Bsdf::GeneralizedSchlickData& data,
    const Vec3f& normalShdLobeWldOut, const Vec3f& omegaOutWld)
{
    const float cosTheta =
        std::max(std::abs(Dot(normalShdLobeWldOut, omegaOutWld)), kEpsilon);
    return SampleDeltaReflection(
        GeneralizedSchlickReflectionFresnel(data, cosTheta), data.weight,
        normalShdLobeWldOut, omegaOutWld);
}

}  // namespace detail
}  // namespace Bsdf
}  // namespace mxcpp

#endif  // PXR_IMAGING_PLUGIN_HDEMBREE_MATERIALXCPP_BSDF_REFLECTIONONLYINTERFACES_H
