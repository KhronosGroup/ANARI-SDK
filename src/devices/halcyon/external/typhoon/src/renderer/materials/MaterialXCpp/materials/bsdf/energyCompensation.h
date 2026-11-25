//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef PXR_IMAGING_PLUGIN_HDEMBREE_MATERIALXCPP_BSDF_ENERGYCOMPENSATION_H
#define PXR_IMAGING_PLUGIN_HDEMBREE_MATERIALXCPP_BSDF_ENERGYCOMPENSATION_H

#include <renderer/materials/MaterialXCpp/materials/bsdf.h>

namespace mxcpp {
namespace Bsdf {
namespace detail {

/// Lower and upper absolute-IOR coordinates baked into the BSDL LUTs.
inline constexpr float kBsdlDielectricIorMin = 1.001f;
inline constexpr float kBsdlDielectricIorMax = 5.0f;

/// Interpolates directional GGX single-scatter missing energy.
/// `cosTheta` and `alphaRoughness` must be finite and are clamped to [0,1].
/// Returns a finite missing-energy fraction in [0,1]. Cannot fail.
float LookupGgxMissingEnergy(float cosTheta, float alphaRoughness);

/// Atomically selects the rough-dielectric layer-throughput approximation used
/// by later BSDF evaluations. `mode` must be a valid enum value. This operation
/// cannot fail.
void SetDielectricThroughputModeState(
    Bsdf::DielectricLayerThroughputMode mode);

/// Returns the current rough-dielectric layer-throughput approximation.
/// The stored value must have been written through
/// `SetDielectricThroughputModeState`; this operation cannot fail.
Bsdf::DielectricLayerThroughputMode GetDielectricThroughputModeState();

/// Interpolates the BSDL directional reflection filter.
/// `cosTheta` and `perceptualRoughness` must be finite; values are clamped to
/// [0,1]. `ior` must be finite and positive; it is clamped to the LUT domain.
/// Returns a finite filter in [0,1]. This operation cannot fail.
float LookupBsdlDielectricReflFrontFilter(
    float cosTheta, float perceptualRoughness, float ior);

/// Interpolates BSDL's directional dielectric transmission
/// single-scatter albedo.
/// `cosTheta` and `perceptualRoughness` must be finite and are clamped to
/// [0,1]. `ior` must be finite and positive and is clamped to the LUT domain.
/// `backfacing` selects incidence from inside. Returns finite albedo in [0,1].
float LookupBsdlDielectricTransmissionSingleScatterAlbedo(
    float cosTheta, float perceptualRoughness, float ior, bool backfacing);

/// Computes BSDL's coupled-dielectric missing energy.
/// `cosThetaO` and `perceptualRoughness` must be finite and are clamped to
/// [0,1]. `ior` must be finite and positive and is clamped to the LUT domain.
/// `backfacing` selects transport from inside the dielectric. Returns the
/// finite missing-energy fraction in [0,1], or zero below its roughness
/// threshold. Cannot fail.
float BsdlCoupledDielectricCompensation(
    float cosThetaO, float perceptualRoughness, float ior, bool backfacing);

/// Returns the Turquin GGX multiple-scattering scale for one outgoing
/// direction. `alphaRoughness` and `cosThetaO` must be finite and are clamped
/// to [0,1]. `fresnel` must be finite and non-negative. Returns one below the
/// roughness threshold; otherwise returns a finite non-negative RGB scale. This
/// operation cannot fail.
Vec3f TurquinMicrofacetMsScale(
    float alphaRoughness, float cosThetaO, const Vec3f& fresnel);

/// Estimates the reflectance removed from a closure below a dielectric layer.
/// `alphaRoughness` and `cosThetaO` must be finite and are clamped to [0,1].
/// `fresnel` must be finite and non-negative. Returns finite RGB reflectance
/// clamped to [0,1]; this operation cannot fail.
Vec3f LayerThroughputReflectance(
    float alphaRoughness, float cosThetaO, const Vec3f& fresnel);

/// Evaluates MaterialX GLSL's scalar dielectric-layer reflectance estimate.
/// `alphaRoughness` and `cosThetaO` must be finite and are clamped to [0,1].
/// `ior` must be finite and positive. Returns finite RGB reflectance in [0,1];
/// this operation cannot fail.
Vec3f MaterialXGlslDielectricLayerReflectance(
    float alphaRoughness, float cosThetaO, float ior);

}  // namespace detail
}  // namespace Bsdf
}  // namespace mxcpp

#endif  // PXR_IMAGING_PLUGIN_HDEMBREE_MATERIALXCPP_BSDF_ENERGYCOMPENSATION_H
