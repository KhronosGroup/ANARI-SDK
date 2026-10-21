//
// Adobe OpenPBR adapter for MaterialXCpp.
//
#include "adobeOpenPbr.h"
#include "openPbr.h"

#include <renderer/integrator/medium.h>
#include <renderer/materials/MaterialXCpp/materials/bsdf/closureTraversal.h>
#include <renderer/materials/MaterialXCpp/materials/bsdf/microfacet.h>
#include <renderer/materials/MaterialXCpp/nodes/helpers/mathHelpers.h>
#include <renderer/materials/MaterialXCpp/paramMap.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <utility>
#include <variant>

#ifdef PXR_HDEMBREE_ENABLE_ADOBE_OPENPBR
#include <glm/glm.hpp>
#include <openpbr/openpbr.h>
#endif

namespace mxcpp {

#ifdef PXR_HDEMBREE_ENABLE_ADOBE_OPENPBR

struct AdobeOpenPbrPreparedSurfaceState
{
    OpenPBR_PreparedBsdf prepared;
    Bsdf::AdobeOpenPbrData data;
    Vec3f normalShdLobeWldOut = Vec3f(0.0f, 0.0f, 1.0f);
    Vec3f normalSrfWldOut = Vec3f(0.0f, 0.0f, 1.0f);
    Vec3f normalGeomWldOut = Vec3f(0.0f, 0.0f, 1.0f);
    Vec3f omegaOutWld = Vec3f(0.0f, 0.0f, 1.0f);
    float presence = 1.0f;
    bool frontFacing = true;
};

static const SlotName _kBaseWeight("base_weight");
static const SlotName _kBaseColor("base_color");
static const SlotName _kBaseDiffuseRoughness("base_diffuse_roughness");
static const SlotName _kLegacyBaseRoughness("base_roughness");
static const SlotName _kBaseMetalness("base_metalness");
static const SlotName _kSpecularWeight("specular_weight");
static const SlotName _kSpecularColor("specular_color");
static const SlotName _kSpecularRoughness("specular_roughness");
static const SlotName _kSpecularIor("specular_ior");
static const SlotName _kSpecularRoughnessAnisotropy(
    "specular_roughness_anisotropy");
static const SlotName _kLegacySpecularAnisotropy("specular_anisotropy");
static const SlotName _kTransmissionWeight("transmission_weight");
static const SlotName _kTransmissionColor("transmission_color");
static const SlotName _kTransmissionDispersionScale(
    "transmission_dispersion_scale");
static const SlotName _kTransmissionDispersionAbbeNumber(
    "transmission_dispersion_abbe_number");
static const SlotName _kTransmissionDepth("transmission_depth");
static const SlotName _kTransmissionScatter("transmission_scatter");
static const SlotName _kTransmissionScatterAnisotropy(
    "transmission_scatter_anisotropy");
static const SlotName _kSubsurfaceWeight("subsurface_weight");
static const SlotName _kSubsurfaceColor("subsurface_color");
static const SlotName _kSubsurfaceRadius("subsurface_radius");
static const SlotName _kSubsurfaceRadiusScale("subsurface_radius_scale");
static const SlotName _kSubsurfaceScatterAnisotropy(
    "subsurface_scatter_anisotropy");
static const SlotName _kCoatWeight("coat_weight");
static const SlotName _kCoatColor("coat_color");
static const SlotName _kCoatRoughness("coat_roughness");
static const SlotName _kCoatRoughnessAnisotropy("coat_roughness_anisotropy");
static const SlotName _kCoatIor("coat_ior");
static const SlotName _kCoatDarkening("coat_darkening");
static const SlotName _kThinFilmWeight("thin_film_weight");
static const SlotName _kThinFilmThickness("thin_film_thickness");
static const SlotName _kThinFilmIor("thin_film_ior");
static const SlotName _kGeometryCoatNormal("geometry_coat_normal");
static const SlotName _kCoatNormal("coat_normal");
static const SlotName _kFuzzWeight("fuzz_weight");
static const SlotName _kFuzzColor("fuzz_color");
static const SlotName _kFuzzRoughness("fuzz_roughness");
static const SlotName _kEmissionLuminance("emission_luminance");
static const SlotName _kEmissionColor("emission_color");
static const SlotName _kGeometryOpacity("geometry_opacity");
static const SlotName _kGeometryThinWalled("geometry_thin_walled");
static const SlotName _kGeometryNormal("geometry_normal");
static const SlotName _kGeometryTangent("geometry_tangent");
static const SlotName _kGeometryCoatTangent("geometry_coat_tangent");
static const SlotName _kLegacyNormal("normal");
static const SlotName _kLegacyTangent("tangent");

namespace {

constexpr float _kEpsilon = 1.0e-7f;

template <typename T>
T
_GetWithFallback(const ParamMap& params,
                 const SlotName& primary,
                 const SlotName& fallback,
                 const T& defaultValue)
{
    if (params.Find(primary)) {
        return Get<T>(params, primary, defaultValue);
    }
    return Get<T>(params, fallback, defaultValue);
}

float
_AverageOpacity(const ParamMap& params)
{
    const Vec3f opacityVec = Get<Vec3f>(
        params, _kGeometryOpacity, Vec3f(1.0f));
    float opacity =
        (opacityVec[0] + opacityVec[1] + opacityVec[2]) / 3.0f;
    if (opacity == 1.0f) {
        opacity = Get<float>(params, _kGeometryOpacity, 1.0f);
    }
    return std::clamp(opacity, 0.0f, 1.0f);
}

Bsdf::AdobeOpenPbrData
_MakeAdobeOpenPbrData(const ParamMap& params)
{
    Bsdf::AdobeOpenPbrData data;

    data.baseWeight = Get<float>(params, _kBaseWeight, data.baseWeight);
    data.baseColor = Get<Vec3f>(params, _kBaseColor, data.baseColor);
    data.baseDiffuseRoughness = _GetWithFallback<float>(
        params,
        _kBaseDiffuseRoughness,
        _kLegacyBaseRoughness,
        data.baseDiffuseRoughness);
    data.baseMetalness =
        Get<float>(params, _kBaseMetalness, data.baseMetalness);

    data.subsurfaceWeight =
        Get<float>(params, _kSubsurfaceWeight, data.subsurfaceWeight);
    data.subsurfaceColor =
        Get<Vec3f>(params, _kSubsurfaceColor, data.subsurfaceColor);
    data.subsurfaceRadius =
        Get<float>(params, _kSubsurfaceRadius, data.subsurfaceRadius);
    data.subsurfaceRadiusScale = Get<Vec3f>(
        params, _kSubsurfaceRadiusScale, data.subsurfaceRadiusScale);
    data.subsurfaceScatterAnisotropy = Get<float>(
        params,
        _kSubsurfaceScatterAnisotropy,
        data.subsurfaceScatterAnisotropy);

    data.specularWeight =
        Get<float>(params, _kSpecularWeight, data.specularWeight);
    data.specularColor =
        Get<Vec3f>(params, _kSpecularColor, data.specularColor);
    data.specularRoughness =
        Get<float>(params, _kSpecularRoughness, data.specularRoughness);
    data.specularRoughnessAnisotropy = _GetWithFallback<float>(
        params,
        _kSpecularRoughnessAnisotropy,
        _kLegacySpecularAnisotropy,
        data.specularRoughnessAnisotropy);
    data.specularIor =
        Get<float>(params, _kSpecularIor, data.specularIor);

    data.coatWeight = Get<float>(params, _kCoatWeight, data.coatWeight);
    data.coatColor = Get<Vec3f>(params, _kCoatColor, data.coatColor);
    data.coatRoughness =
        Get<float>(params, _kCoatRoughness, data.coatRoughness);
    data.coatRoughnessAnisotropy = Get<float>(
        params, _kCoatRoughnessAnisotropy, data.coatRoughnessAnisotropy);
    data.coatIor = Get<float>(params, _kCoatIor, data.coatIor);
    data.coatDarkening =
        Get<float>(params, _kCoatDarkening, data.coatDarkening);

    data.fuzzWeight = Get<float>(params, _kFuzzWeight, data.fuzzWeight);
    data.fuzzColor = Get<Vec3f>(params, _kFuzzColor, data.fuzzColor);
    data.fuzzRoughness =
        Get<float>(params, _kFuzzRoughness, data.fuzzRoughness);

    data.transmissionWeight =
        Get<float>(params, _kTransmissionWeight, data.transmissionWeight);
    data.transmissionColor =
        Get<Vec3f>(params, _kTransmissionColor, data.transmissionColor);
    data.transmissionDepth =
        Get<float>(params, _kTransmissionDepth, data.transmissionDepth);
    data.transmissionScatter =
        Get<Vec3f>(params, _kTransmissionScatter, data.transmissionScatter);
    data.transmissionScatterAnisotropy = Get<float>(
        params,
        _kTransmissionScatterAnisotropy,
        data.transmissionScatterAnisotropy);
    data.transmissionDispersionScale = Get<float>(
        params,
        _kTransmissionDispersionScale,
        data.transmissionDispersionScale);
    data.transmissionDispersionAbbeNumber = Get<float>(
        params,
        _kTransmissionDispersionAbbeNumber,
        data.transmissionDispersionAbbeNumber);

    data.thinFilmWeight =
        Get<float>(params, _kThinFilmWeight, data.thinFilmWeight);
    data.thinFilmThickness =
        Get<float>(params, _kThinFilmThickness, data.thinFilmThickness);
    data.thinFilmIor = Get<float>(params, _kThinFilmIor, data.thinFilmIor);

    data.emissionLuminance =
        Get<float>(params, _kEmissionLuminance, data.emissionLuminance);
    data.emissionColor =
        Get<Vec3f>(params, _kEmissionColor, data.emissionColor);

    data.geometryOpacity = _AverageOpacity(params);
    data.geometryThinWalled =
        Get<bool>(params, _kGeometryThinWalled, data.geometryThinWalled);
    data.geometryNormal = _GetWithFallback<Vec3f>(
        params,
        _kGeometryNormal,
        _kLegacyNormal,
        data.geometryNormal);
    data.geometryTangent = _GetWithFallback<Vec3f>(
        params,
        _kGeometryTangent,
        _kLegacyTangent,
        data.geometryTangent);

    const bool hasCoatNormal =
        params.Find(_kGeometryCoatNormal) || params.Find(_kCoatNormal);
    data.geometryCoatNormal = hasCoatNormal
        ? _GetWithFallback<Vec3f>(
              params,
              _kGeometryCoatNormal,
              _kCoatNormal,
              data.geometryNormal)
        : data.geometryNormal;
    data.geometryCoatTangent = _GetWithFallback<Vec3f>(
        params,
        _kGeometryCoatTangent,
        _kGeometryTangent,
        data.geometryTangent);

    return data;
}

Vec3f
_CleanNonnegative(const Vec3f& value)
{
    Vec3f result = value;
    for (int i = 0; i < 3; ++i) {
        if (!std::isfinite(result[i]) || result[i] < 0.0f) {
            result[i] = 0.0f;
        }
    }
    return result;
}

Vec3f
_NormalizeOrFallback(const Vec3f& value, const Vec3f& fallback)
{
    if (value.length2() > _kEpsilon * _kEpsilon) {
        return value.normalized();
    }
    return fallback.normalized();
}

vec3
_ToOpenPbr(const Vec3f& value)
{
    return vec3(value[0], value[1], value[2]);
}

Vec3f
_FromOpenPbr(const vec3& value)
{
    return Vec3f(value.x, value.y, value.z);
}

OpenPBR_Basis
_MakeBasis(const Vec3f& normalShdWldOut, const Vec3f& tangentWld)
{
    const Vec3f normalNormalized =
        _NormalizeOrFallback(normalShdWldOut, Vec3f(0.0f, 0.0f, 1.0f));
    const Vec3f projectedTangent =
        tangentWld - normalNormalized * Dot(tangentWld, normalNormalized);
    if (projectedTangent.length2() <= _kEpsilon * _kEpsilon) {
        return openpbr_make_basis(_ToOpenPbr(normalNormalized));
    }
    return openpbr_make_basis(_ToOpenPbr(normalNormalized),
                              _ToOpenPbr(projectedTangent.normalized()), 1.0f);
}

OpenPBR_ResolvedInputs
_MakeResolvedInputs(const Bsdf::AdobeOpenPbrData& data,
                    const Vec3f& normalShdWldOut)
{
    OpenPBR_ResolvedInputs inputs = openpbr_make_default_resolved_inputs();

    inputs.base_weight = data.baseWeight;
    inputs.base_color = _ToOpenPbr(data.baseColor);
    inputs.base_diffuse_roughness = data.baseDiffuseRoughness;
    inputs.base_metalness = data.baseMetalness;

    inputs.subsurface_weight = data.subsurfaceWeight;
    inputs.subsurface_color = _ToOpenPbr(data.subsurfaceColor);
    inputs.subsurface_radius = data.subsurfaceRadius;
    inputs.subsurface_radius_scale = _ToOpenPbr(data.subsurfaceRadiusScale);
    inputs.subsurface_scatter_anisotropy =
        data.subsurfaceScatterAnisotropy;

    inputs.specular_weight = data.specularWeight;
    inputs.specular_color = _ToOpenPbr(data.specularColor);
    inputs.specular_roughness = data.specularRoughness;
    inputs.specular_roughness_anisotropy =
        data.specularRoughnessAnisotropy;
    inputs.specular_ior = data.specularIor;

    inputs.coat_weight = data.coatWeight;
    inputs.coat_color = _ToOpenPbr(data.coatColor);
    inputs.coat_roughness = data.coatRoughness;
    inputs.coat_roughness_anisotropy = data.coatRoughnessAnisotropy;
    inputs.coat_ior = data.coatIor;
    inputs.coat_darkening = data.coatDarkening;

    inputs.fuzz_weight = data.fuzzWeight;
    inputs.fuzz_color = _ToOpenPbr(data.fuzzColor);
    inputs.fuzz_roughness = data.fuzzRoughness;

    inputs.transmission_weight = data.transmissionWeight;
    inputs.transmission_color = _ToOpenPbr(data.transmissionColor);
    inputs.transmission_depth = data.transmissionDepth;
    inputs.transmission_scatter = _ToOpenPbr(data.transmissionScatter);
    inputs.transmission_scatter_anisotropy =
        data.transmissionScatterAnisotropy;
    inputs.transmission_dispersion_scale =
        data.transmissionDispersionScale;
    inputs.transmission_dispersion_abbe_number =
        data.transmissionDispersionAbbeNumber;

    inputs.thin_film_weight = data.thinFilmWeight;
    inputs.thin_film_thickness = data.thinFilmThickness;
    inputs.thin_film_ior = data.thinFilmIor;

    inputs.emission_luminance = data.emissionLuminance;
    inputs.emission_color = _ToOpenPbr(data.emissionColor);

    inputs.geometry_opacity = data.geometryOpacity;
    inputs.geometry_thin_walled = data.geometryThinWalled;
    inputs.geometry_basis = _MakeBasis(normalShdWldOut, data.geometryTangent);
    inputs.geometry_coat_basis =
        _MakeBasis(normalShdWldOut, data.geometryCoatTangent);

    return inputs;
}

OpenPBR_PreparedBsdf
_Prepare(const Bsdf::AdobeOpenPbrData& data, const Vec3f& normalShdWldOut,
         const Vec3f& omegaOutWld)
{
    const OpenPBR_ResolvedInputs inputs =
        _MakeResolvedInputs(data, normalShdWldOut);
    const Vec3f omegaOutWldNormalized =
        _NormalizeOrFallback(omegaOutWld, normalShdWldOut);
    return openpbr_prepare(inputs, vec3(1.0f), OpenPBR_BaseRgbWavelengths_nm,
                           OpenPBR_VacuumIor,
                           _ToOpenPbr(omegaOutWldNormalized));
}

OpenPBR_HomogeneousVolume
_MakeHomogeneousVolume(const MediumProperties& medium)
{
    if (medium.adobeOpenPbrVolume.valid) {
        return openpbr_make_volume_from_extinction_coefficient_and_albedo_and_anisotropy(
            _ToOpenPbr(
                _CleanNonnegative(
                    medium.adobeOpenPbrVolume.extinctionCoefficient)),
            _ToOpenPbr(_CleanNonnegative(medium.adobeOpenPbrVolume.albedo)),
            std::clamp(
                medium.adobeOpenPbrVolume.anisotropy,
                -0.999f,
                0.999f));
    }
    return openpbr_make_volume_from_absorption_and_scattering_coefficients_and_anisotropy(
        _ToOpenPbr(_CleanNonnegative(medium.absorption)),
        _ToOpenPbr(_CleanNonnegative(medium.scattering)),
        std::clamp(medium.anisotropy, -0.999f, 0.999f));
}

Vec3f
_CleanThroughput(const Vec3f& value)
{
    Vec3f result = _CleanNonnegative(value);
    for (int i = 0; i < 3; ++i) {
        if (!std::isfinite(result[i])) {
            result[i] = 0.0f;
        }
    }
    return result;
}

float
_Clamp01(float value)
{
    if (!std::isfinite(value)) {
        return 0.0f;
    }
    return std::clamp(value, 0.0f, 1.0f);
}

bool
_IsEffectivelyZero(float value)
{
    return std::abs(value) <= _kEpsilon;
}

bool
_HasPositiveSubsurfaceRadius(const Bsdf::AdobeOpenPbrData& data)
{
    const Vec3f radius = data.subsurfaceRadiusScale * data.subsurfaceRadius;
    return radius[0] > _kEpsilon ||
           radius[1] > _kEpsilon ||
           radius[2] > _kEpsilon;
}

bool
_IsPureThickSubsurface(const Bsdf::AdobeOpenPbrData& data)
{
    return !data.geometryThinWalled &&
           data.subsurfaceWeight > _kEpsilon &&
           data.baseMetalness < 1.0f - _kEpsilon &&
           _IsEffectivelyZero(data.transmissionWeight) &&
           _HasPositiveSubsurfaceRadius(data);
}

struct _AdobeSurfaceSampleResult
{
    Bsdf::BsdfSample sample{
        Vec3f(0.0f),
        Vec3f(0.0f),
        0.0f,
        false
    };
    Vec3f diffuseBsdfValue = Vec3f(0.0f);
    Vec3f specularBsdfValue = Vec3f(0.0f);
    Vec3f throughputWeight = Vec3f(0.0f);
    OpenPBR_BsdfLobeType sampledType = OpenPBR_BsdfLobeTypeNone;
    bool valid = false;
};

_AdobeSurfaceSampleResult
_SamplePreparedAdobeOpenPbrSurfaceRaw(
    const AdobeOpenPbrPreparedSurfaceState& state,
    float u1,
    float u2,
    float uLobe)
{
    _AdobeSurfaceSampleResult result;

    vec3 lightDirection(0.0f);
    OpenPBR_DiffuseSpecular weight;
    float pdfSolidAngle = 0.0f;
    OpenPBR_BsdfLobeType sampledType = OpenPBR_BsdfLobeTypeNone;
    openpbr_sample(state.prepared, vec3(u1, u2, uLobe), lightDirection, weight,
                   pdfSolidAngle, sampledType);

    if (!std::isfinite(pdfSolidAngle) || pdfSolidAngle <= 0.0f) {
        return result;
    }

    const Vec3f omegaInWld = _NormalizeOrFallback(_FromOpenPbr(lightDirection),
                                                  state.normalShdLobeWldOut);
    const Vec3f diffuseWeight =
        _CleanNonnegative(_FromOpenPbr(weight.diffuse));
    const Vec3f specularWeight =
        _CleanNonnegative(_FromOpenPbr(weight.specular));
    const Vec3f weightSum = diffuseWeight + specularWeight;
    const bool isSpecular =
        (sampledType & OpenPBR_BsdfLobeTypeSpecular) != 0;

    Vec3f bsdfValue(0.0f);
    if (isSpecular) {
        bsdfValue = weightSum;
    } else {
        const float cosThetaI =
            std::abs(Dot(state.normalShdLobeWldOut, omegaInWld));
        if (cosThetaI <= _kEpsilon) {
            return result;
        }
        const float weightToBsdf =
            state.presence * pdfSolidAngle / cosThetaI;
        result.diffuseBsdfValue = diffuseWeight * weightToBsdf;
        result.specularBsdfValue = specularWeight * weightToBsdf;
        bsdfValue =
            result.diffuseBsdfValue + result.specularBsdfValue;
    }

    result.sample = Bsdf::BsdfSample{omegaInWld, _CleanNonnegative(bsdfValue),
                                     pdfSolidAngle, isSpecular};
    result.sample.isDiffuseLike =
        (sampledType & OpenPBR_BsdfLobeTypeDiffuse) != 0;
    result.sample.isTransmission =
        (sampledType & OpenPBR_BsdfLobeTypeTransmission) != 0;
    result.throughputWeight = weightSum;
    result.sampledType = sampledType;
    result.valid = true;

    const bool isSpecularTransmission =
        isSpecular &&
        ((sampledType & OpenPBR_BsdfLobeTypeTransmission) != 0);
    if (isSpecularTransmission) {
        const float ior = std::max(state.data.specularIor, 1.0f);
        result.sample.eta = state.frontFacing ? (1.0f / ior) : ior;
    }

    return result;
}

Bsdf::BsdfSample
_MakeSubsurfaceMarker(float weight, const Vec3f& normalShdLobeWldOut)
{
    Bsdf::BsdfSample sample{
        Vec3f(0.0f),
        Vec3f(std::max(weight, 0.0f)),
        1.0f,
        false
    };
    sample.isSubsurface = sample.bsdfValue[0] > 0.0f ||
                          sample.bsdfValue[1] > 0.0f ||
                          sample.bsdfValue[2] > 0.0f;
    sample.isDiffuseLike = sample.isSubsurface;
    sample.normalShdLobeWldOut = normalShdLobeWldOut;
    return sample;
}

Bsdf::BsdfSample
_SamplePureSubsurfaceFallback(
    const AdobeOpenPbrPreparedSurfaceState& state,
    float u1,
    float u2,
    float uLobe)
{
    const float sssProbability = _Clamp01(state.data.subsurfaceWeight);
    if (sssProbability <= _kEpsilon) {
        return Bsdf::BsdfSample{Vec3f(0.0f), Vec3f(0.0f), 0.0f, false};
    }

    if (sssProbability >= 1.0f - _kEpsilon || uLobe < sssProbability) {
        return _MakeSubsurfaceMarker(
            state.data.subsurfaceWeight /
                std::max(sssProbability, _kEpsilon),
            state.normalShdLobeWldOut);
    }

    Bsdf::AdobeOpenPbrData surfaceOnlyData = state.data;
    surfaceOnlyData.subsurfaceWeight = 0.0f;

    AdobeOpenPbrPreparedSurfaceState surfaceOnlyState;
    surfaceOnlyState.data = surfaceOnlyData;
    surfaceOnlyState.normalShdLobeWldOut = state.normalShdLobeWldOut;
    surfaceOnlyState.normalSrfWldOut = state.normalSrfWldOut;
    surfaceOnlyState.normalGeomWldOut = state.normalGeomWldOut;
    surfaceOnlyState.omegaOutWld = state.omegaOutWld;
    surfaceOnlyState.presence = state.presence;
    surfaceOnlyState.frontFacing = state.frontFacing;
    surfaceOnlyState.prepared =
        _Prepare(surfaceOnlyData, state.normalShdLobeWldOut, state.omegaOutWld);

    const float surfaceProbability = 1.0f - sssProbability;
    const float remappedLobe =
        (uLobe - sssProbability) / std::max(surfaceProbability, _kEpsilon);
    _AdobeSurfaceSampleResult surfaceSample =
        _SamplePreparedAdobeOpenPbrSurfaceRaw(
            surfaceOnlyState,
            u1,
            u2,
            remappedLobe);
    if (!surfaceSample.valid) {
        return Bsdf::BsdfSample{Vec3f(0.0f), Vec3f(0.0f), 0.0f, false};
    }
    surfaceSample.sample.bsdfValue *=
        1.0f / std::max(surfaceProbability, _kEpsilon);
    return surfaceSample.sample;
}

}  // namespace

#endif  // PXR_HDEMBREE_ENABLE_ADOBE_OPENPBR

SurfaceClosure
EvalAdobeOpenPbr(const ParamMap& params)
{
#ifndef PXR_HDEMBREE_ENABLE_ADOBE_OPENPBR
    return EvalOpenPbr(params);
#else
    const Bsdf::AdobeOpenPbrData data = _MakeAdobeOpenPbrData(params);

    SurfaceClosure closure;
    closure.baseColor = data.baseColor * data.baseWeight;
    closure.roughness = data.specularRoughness;
    closure.metallic = data.baseMetalness;
    closure.specular = data.specularWeight;
    closure.specularColor = data.specularColor;
    closure.specularIor = data.specularIor;
    closure.emissiveColor = data.emissionColor * data.emissionLuminance;
    closure.transmission = data.transmissionWeight;
    closure.transmissionColor = data.transmissionColor;
    closure.opacity = data.geometryOpacity;
    closure.presence = data.geometryOpacity;
    closure.coat = data.coatWeight;
    closure.coatRoughness = data.coatRoughness;
    closure.coatIor = data.coatIor;
    closure.sheen = data.fuzzWeight;
    closure.sheenColor = data.fuzzColor;
    closure.sheenRoughness = data.fuzzRoughness;
    closure.normal = data.geometryNormal;
    closure.normalSpace =
        (params.Find(_kGeometryNormal) || params.Find(_kLegacyNormal))
        ? SurfaceNormalSpace::World
        : SurfaceNormalSpace::None;
    closure.thinWalled = data.geometryThinWalled;
    closure.subsurfaceWeight = data.subsurfaceWeight;
    closure.subsurfaceColor = data.subsurfaceColor;
    closure.subsurfaceRadius = Vec3f(data.subsurfaceRadius);
    closure.subsurfaceRadiusScale = data.subsurfaceRadiusScale;
    closure.subsurfaceAnisotropy = data.subsurfaceScatterAnisotropy;
    const MediumProperties adobeMedium =
        MakeAdobeOpenPbrInteriorMedium(data);
    if (_IsPureThickSubsurface(data)) {
        closure.precomputedSubsurfaceMedium = adobeMedium;
        closure.hasPrecomputedSubsurfaceMedium = !adobeMedium.IsVacuum();
    } else {
        closure.interiorMedium = adobeMedium;
        closure.hasInteriorMedium =
            !data.geometryThinWalled && !closure.interiorMedium.IsVacuum();
    }

    Bsdf::ClosureTree tree;
    tree.root = tree.Add(data);
    closure.bsdfTree = std::move(tree);
    return closure;
#endif
}

SurfaceClosure
EvalAdobeOpenPbrVisibility(const ParamMap& params)
{
#ifndef PXR_HDEMBREE_ENABLE_ADOBE_OPENPBR
    return EvalOpenPbr(params);
#else
    const Bsdf::AdobeOpenPbrData data = _MakeAdobeOpenPbrData(params);

    SurfaceClosure closure;
    closure.transmission = data.transmissionWeight;
    closure.transmissionColor = data.transmissionColor;
    closure.opacity = data.geometryOpacity;
    closure.presence = data.geometryOpacity;
    closure.thinWalled = data.geometryThinWalled;
    if (!_IsPureThickSubsurface(data)) {
        closure.interiorMedium = MakeAdobeOpenPbrInteriorMedium(data);
        closure.hasInteriorMedium =
            !data.geometryThinWalled && !closure.interiorMedium.IsVacuum();
    }
    return closure;
#endif
}

Vec3f
EvalAdobeOpenPbr(const Bsdf::AdobeOpenPbrData& data,
                 const Vec3f& normalShdWldOut,
                 const Vec3f& normalSrfWldOut,
                 const Vec3f& omegaInWld,
                 const Vec3f& omegaOutWld)
{
#ifndef PXR_HDEMBREE_ENABLE_ADOBE_OPENPBR
    (void)data;
    (void)normalShdWldOut;
    (void)normalSrfWldOut;
    (void)omegaInWld;
    (void)omegaOutWld;
    return Vec3f(0.0f);
#else
    if (!Bsdf::detail::BumpHemisphereAgreement(
            normalSrfWldOut, normalShdWldOut, omegaInWld)) {
        return Vec3f(0.0f);
    }
    const float cosThetaI = std::abs(Dot(normalShdWldOut, omegaInWld));
    if (cosThetaI <= _kEpsilon) {
        return Vec3f(0.0f);
    }

    const OpenPBR_PreparedBsdf prepared =
        _Prepare(data, normalShdWldOut, omegaOutWld);
    const OpenPBR_DiffuseSpecular valueWithCos = openpbr_eval(
        prepared,
        _ToOpenPbr(_NormalizeOrFallback(omegaInWld, normalShdWldOut)));
    const float bumpShadowing = Bsdf::detail::BumpShadowingTerm(
        normalSrfWldOut, normalShdWldOut, omegaInWld, true,
        Bsdf::detail::BumpShadowingContext::Evaluation);
    const Vec3f value = _FromOpenPbr(
        valueWithCos.diffuse * bumpShadowing + valueWithCos.specular);
    return _CleanNonnegative(value * (1.0f / cosThetaI));
#endif
}

AdobeOpenPbrPreparedSurface
PrepareAdobeOpenPbrSurface(const SurfaceClosure& closure,
                           const SurfaceInteraction& interaction)
{
#ifndef PXR_HDEMBREE_ENABLE_ADOBE_OPENPBR
    (void)closure;
    (void)interaction;
    return AdobeOpenPbrPreparedSurface{};
#else
    const Bsdf::Node* root = closure.bsdfTree.Get(closure.bsdfTree.root);
    if (!root) {
        return AdobeOpenPbrPreparedSurface{};
    }

    const auto* data = std::get_if<Bsdf::AdobeOpenPbrData>(&root->data);
    if (!data) {
        return AdobeOpenPbrPreparedSurface{};
    }

    const Vec3f normalShdLobeWldOut = Bsdf::detail::ResolveShadingNormal(
        *data, interaction.normalShdWldOut);
    auto state = std::make_shared<AdobeOpenPbrPreparedSurfaceState>();
    state->data = *data;
    state->normalShdLobeWldOut = normalShdLobeWldOut;
    state->normalSrfWldOut = interaction.normalSrfWldOut;
    state->normalGeomWldOut = interaction.normalGeomWldOut;
    state->frontFacing = interaction.frontFacing;
    state->omegaOutWld = interaction.omegaOutWld;
    state->presence = closure.presence;
    state->prepared = _Prepare(
        *data, normalShdLobeWldOut, interaction.omegaOutWld);

    AdobeOpenPbrPreparedSurface preparedSurface;
    preparedSurface.state = std::move(state);
    preparedSurface.valid = true;
    return preparedSurface;
#endif
}

AdobeOpenPbrEvalPdfResult
EvalPdfAdobeOpenPbr(const Bsdf::AdobeOpenPbrData& data,
                    const Vec3f& normalShdWldOut, const Vec3f& omegaInWld,
                    const Vec3f& omegaOutWld)
{
#ifndef PXR_HDEMBREE_ENABLE_ADOBE_OPENPBR
    (void)data;
    (void)normalShdWldOut;
    (void)omegaInWld;
    (void)omegaOutWld;
    return AdobeOpenPbrEvalPdfResult{};
#else
    auto state = std::make_shared<AdobeOpenPbrPreparedSurfaceState>();
    state->data = data;
    state->normalShdLobeWldOut = normalShdWldOut;
    state->normalSrfWldOut = normalShdWldOut;
    state->normalGeomWldOut = normalShdWldOut;
    state->omegaOutWld = omegaOutWld;
    state->prepared = _Prepare(data, normalShdWldOut, omegaOutWld);

    AdobeOpenPbrPreparedSurface preparedSurface;
    preparedSurface.state = std::move(state);
    preparedSurface.valid = true;
    return EvalPdfPreparedAdobeOpenPbrSurface(preparedSurface, omegaInWld);
#endif
}

AdobeOpenPbrEvalPdfResult
EvalPdfPreparedAdobeOpenPbrSurface(
    const AdobeOpenPbrPreparedSurface& preparedSurface, const Vec3f& omegaInWld)
{
#ifndef PXR_HDEMBREE_ENABLE_ADOBE_OPENPBR
    (void)preparedSurface;
    (void)omegaInWld;
    return AdobeOpenPbrEvalPdfResult{};
#else
    AdobeOpenPbrEvalPdfResult result;
    if (!preparedSurface.valid || !preparedSurface.state) {
        return result;
    }
    const AdobeOpenPbrPreparedSurfaceState& state = *preparedSurface.state;
    result.evaluated = true;

    const float cosThetaI = std::abs(Dot(state.normalShdLobeWldOut, omegaInWld));
    if (cosThetaI <= _kEpsilon) {
        return result;
    }

    const vec3 lightDirection =
        _ToOpenPbr(_NormalizeOrFallback(omegaInWld, state.normalShdLobeWldOut));
    const OpenPBR_DiffuseSpecular valueWithCos =
        openpbr_eval(state.prepared, lightDirection);
    if (Bsdf::detail::BumpHemisphereAgreement(
            state.normalSrfWldOut, state.normalShdLobeWldOut, omegaInWld)) {
        const float bumpShadowing = Bsdf::detail::BumpShadowingTerm(
            state.normalSrfWldOut, state.normalShdLobeWldOut, omegaInWld,
            true, Bsdf::detail::BumpShadowingContext::Evaluation);
        const Vec3f value = _FromOpenPbr(
            valueWithCos.diffuse * bumpShadowing + valueWithCos.specular);
        result.valueCosine =
            _CleanNonnegative(value * state.presence);
        result.value = result.valueCosine * (1.0f / cosThetaI);
    }

    const float pdfSolidAngle = openpbr_pdf(state.prepared, lightDirection);
    result.pdfSolidAngle =
        std::isfinite(pdfSolidAngle) ? std::max(pdfSolidAngle, 0.0f) : 0.0f;
    return result;
#endif
}

AdobeOpenPbrEvalPdfResult
TryEvalPdfAdobeOpenPbrSurface(const SurfaceClosure& closure,
                              const SurfaceInteraction& interaction,
                              const Vec3f& omegaInWld)
{
    const Bsdf::Node* root = closure.bsdfTree.Get(closure.bsdfTree.root);
    if (!root) {
        return AdobeOpenPbrEvalPdfResult{};
    }

    const auto* data = std::get_if<Bsdf::AdobeOpenPbrData>(&root->data);
    if (!data) {
        return AdobeOpenPbrEvalPdfResult{};
    }

    const AdobeOpenPbrPreparedSurface preparedSurface =
        PrepareAdobeOpenPbrSurface(closure, interaction);
    return EvalPdfPreparedAdobeOpenPbrSurface(preparedSurface, omegaInWld);
}

float
PdfAdobeOpenPbr(const Bsdf::AdobeOpenPbrData& data,
                const Vec3f& normalShdWldOut, const Vec3f& omegaInWld,
                const Vec3f& omegaOutWld)
{
#ifndef PXR_HDEMBREE_ENABLE_ADOBE_OPENPBR
    (void)data;
    (void)normalShdWldOut;
    (void)omegaInWld;
    (void)omegaOutWld;
    return 0.0f;
#else
    const OpenPBR_PreparedBsdf prepared =
        _Prepare(data, normalShdWldOut, omegaOutWld);
    const float pdfSolidAngle = openpbr_pdf(
        prepared,
        _ToOpenPbr(_NormalizeOrFallback(omegaInWld, normalShdWldOut)));
    return std::isfinite(pdfSolidAngle) ? std::max(pdfSolidAngle, 0.0f) : 0.0f;
#endif
}

Bsdf::BsdfSample
SampleAdobeOpenPbr(const Bsdf::AdobeOpenPbrData& data,
                   const Vec3f& normalShdLobeWldOut,
                   const SurfaceInteraction& interaction,
                   float u1, float u2, float uLobe)
{
#ifndef PXR_HDEMBREE_ENABLE_ADOBE_OPENPBR
    (void)data;
    (void)normalShdLobeWldOut;
    (void)interaction;
    (void)u1;
    (void)u2;
    (void)uLobe;
    return Bsdf::BsdfSample{Vec3f(0.0f), Vec3f(0.0f), 0.0f, false};
#else
    auto state = std::make_shared<AdobeOpenPbrPreparedSurfaceState>();
    state->data = data;
    state->normalShdLobeWldOut = normalShdLobeWldOut;
    state->normalSrfWldOut = interaction.normalSrfWldOut;
    state->normalGeomWldOut = interaction.normalGeomWldOut;
    state->frontFacing = interaction.frontFacing;
    state->omegaOutWld = interaction.omegaOutWld;
    state->prepared = _Prepare(
        data, normalShdLobeWldOut, interaction.omegaOutWld);

    AdobeOpenPbrPreparedSurface preparedSurface;
    preparedSurface.state = std::move(state);
    preparedSurface.valid = true;
    return SamplePreparedAdobeOpenPbrSurface(
        preparedSurface,
        u1,
        u2,
        uLobe);
#endif
}

Bsdf::BsdfSample
SamplePreparedAdobeOpenPbrSurface(
    const AdobeOpenPbrPreparedSurface& preparedSurface,
    float u1,
    float u2,
    float uLobe)
{
#ifndef PXR_HDEMBREE_ENABLE_ADOBE_OPENPBR
    (void)preparedSurface;
    (void)u1;
    (void)u2;
    (void)uLobe;
    return Bsdf::BsdfSample{Vec3f(0.0f), Vec3f(0.0f), 0.0f, false};
#else
    if (!preparedSurface.valid || !preparedSurface.state) {
        return Bsdf::BsdfSample{Vec3f(0.0f), Vec3f(0.0f), 0.0f, false};
    }
    const AdobeOpenPbrPreparedSurfaceState& state = *preparedSurface.state;
    if (Dot(state.normalShdLobeWldOut, state.omegaOutWld) <= 0.0f) {
        return Bsdf::BsdfSample{
            Vec3f(0.0f), Vec3f(0.0f), 0.0f, false};
    }

    const bool pureThickSubsurface = _IsPureThickSubsurface(state.data);
    if (pureThickSubsurface && _IsEffectivelyZero(state.data.specularWeight)) {
        return _SamplePureSubsurfaceFallback(state, u1, u2, uLobe);
    }

    _AdobeSurfaceSampleResult result =
        _SamplePreparedAdobeOpenPbrSurfaceRaw(state, u1, u2, uLobe);
    if (!result.valid) {
        if (pureThickSubsurface) {
            return _SamplePureSubsurfaceFallback(state, u1, u2, uLobe);
        }
        return Bsdf::BsdfSample{Vec3f(0.0f), Vec3f(0.0f), 0.0f, false};
    }

    const bool sampledTransmission =
        (result.sampledType & OpenPBR_BsdfLobeTypeTransmission) != 0;
    if (pureThickSubsurface && sampledTransmission) {
        if (!Bsdf::detail::SampledDirectionIsValid(
                true, state.normalShdLobeWldOut,
                state.normalGeomWldOut, result.sample.omegaInWld)) {
            return Bsdf::BsdfSample{
                Vec3f(0.0f), Vec3f(0.0f), 0.0f, false};
        }
        Bsdf::BsdfSample sample{
            result.sample.omegaInWld,
            _CleanNonnegative(result.throughputWeight * state.presence), 1.0f,
            false};
        sample.isSubsurface = true;
        sample.hasSubsurfaceEntryDirection = true;
        sample.isDiffuseLike = true;
        sample.isTransmission = true;
        sample.normalShdLobeWldOut = state.normalShdLobeWldOut;
        return sample;
    }

    const bool diffuseFamily =
        (result.sampledType & OpenPBR_BsdfLobeTypeDiffuse) != 0;
    bool directionValid = Bsdf::detail::SampledDirectionIsValid(
        sampledTransmission, state.normalShdLobeWldOut,
        state.normalGeomWldOut, result.sample.omegaInWld);
    if (diffuseFamily) {
        const float bumpShadowing = Bsdf::detail::BumpShadowingTerm(
            state.normalSrfWldOut, state.normalShdLobeWldOut,
            result.sample.omegaInWld, true,
            Bsdf::detail::BumpShadowingContext::Sampling);
        directionValid = directionValid && bumpShadowing > 0.0f;
        result.sample.bsdfValue =
            result.diffuseBsdfValue * bumpShadowing +
            result.specularBsdfValue;
    }
    if (!directionValid) {
        result.sample.bsdfValue = Vec3f(0.0f);
        result.sample.bsdfValueCosine = Vec3f(0.0f);
        result.sample.pdfSolidAngle = 0.0f;
    } else if (!result.sample.isSpecular) {
        result.sample.bsdfValueCosine = result.sample.bsdfValue *
            std::abs(Dot(state.normalShdLobeWldOut,
                         result.sample.omegaInWld));
    }

    return result.sample;
#endif
}

MediumProperties
MakeAdobeOpenPbrInteriorMedium(const Bsdf::AdobeOpenPbrData& data)
{
#ifndef PXR_HDEMBREE_ENABLE_ADOBE_OPENPBR
    (void)data;
    return MediumProperties();
#else
    if (data.geometryThinWalled) {
        return MediumProperties();
    }

    const OpenPBR_ResolvedInputs inputs =
        _MakeResolvedInputs(data, Vec3f(0.0f, 0.0f, 1.0f));
    OpenPBR_PreparedBsdf prepared{};
    OpenPBR_VolumeDerivedProps volumeDerivedProps{};
    openpbr_prepare_volume(
        inputs,
        volumeDerivedProps,
        prepared,
        true);
    if (!openpbr_is_homogeneous_volume(prepared.volume)) {
        return MediumProperties();
    }

    const Vec3f extinction =
        _CleanNonnegative(_FromOpenPbr(prepared.volume.extinction_coefficient));
    Vec3f albedo = _FromOpenPbr(prepared.volume.albedo);
    for (int i = 0; i < 3; ++i) {
        albedo[i] = std::clamp(albedo[i], 0.0f, 1.0f);
    }

    MediumProperties medium;
    medium.scattering = CompMul(extinction, albedo);
    medium.absorption = CompMul(extinction, Vec3f(1.0f) - albedo);
    medium.anisotropy = std::clamp(
        prepared.volume.anisotropy,
        -0.999f,
        0.999f);
    medium.transportModel = MediumTransportModel::AdobeOpenPBR;
    medium.adobeOpenPbrVolume.extinctionCoefficient = extinction;
    medium.adobeOpenPbrVolume.albedo = albedo;
    medium.adobeOpenPbrVolume.anisotropy = medium.anisotropy;
    medium.adobeOpenPbrVolume.valid = true;
    return medium;
#endif
}

Vec3f
AdobeOpenPbrEvalVolumeTransmittance(
    const MediumProperties& medium,
    float distance)
{
#ifndef PXR_HDEMBREE_ENABLE_ADOBE_OPENPBR
    return EvalBeerTransmittance(medium, distance);
#else
    if (distance <= 0.0f || medium.IsVacuum()) {
        return Vec3f(1.0f);
    }

    const OpenPBR_HomogeneousVolume volume = _MakeHomogeneousVolume(medium);
    if (!openpbr_is_homogeneous_volume(volume)) {
        return Vec3f(1.0f);
    }
    return _CleanNonnegative(
        _FromOpenPbr(openpbr_calculate_transmittance_at_distance(
            volume,
            distance)));
#endif
}

float
AdobeOpenPbrSampleVolumeEventDistance(const MediumProperties& medium,
                                      const Vec3f& throughputRgb, float u)
{
#ifndef PXR_HDEMBREE_ENABLE_ADOBE_OPENPBR
    (void)throughputRgb;
    return SampleFreeFlight(medium, u);
#else
    if (medium.IsVacuum()) {
        return std::numeric_limits<float>::infinity();
    }

    const OpenPBR_HomogeneousVolume volume = _MakeHomogeneousVolume(medium);
    if (!openpbr_is_homogeneous_volume(volume)) {
        return std::numeric_limits<float>::infinity();
    }

    float distance = std::numeric_limits<float>::infinity();
    openpbr_sample_event_distance(
        volume, _ToOpenPbr(_CleanThroughput(throughputRgb)),
        std::clamp(u, 0.0f, 1.0f - _kEpsilon), distance);
    return distance;
#endif
}

Vec3f
AdobeOpenPbrCalculateVolumeEventWeight(const MediumProperties& medium,
                                       const Vec3f& throughputRgb,
                                       float distance)
{
#ifndef PXR_HDEMBREE_ENABLE_ADOBE_OPENPBR
    (void)throughputRgb;
    return EvalFreeFlightScatterWeight(medium, distance);
#else
    if (distance < 0.0f || medium.IsVacuum()) {
        return Vec3f(0.0f);
    }

    const OpenPBR_HomogeneousVolume volume = _MakeHomogeneousVolume(medium);
    if (!openpbr_is_homogeneous_volume(volume)) {
        return Vec3f(0.0f);
    }
    return _CleanNonnegative(
        _FromOpenPbr(openpbr_calculate_weight_for_event_at_distance(
            volume, _ToOpenPbr(_CleanThroughput(throughputRgb)), distance)));
#endif
}

Vec3f
AdobeOpenPbrCalculateVolumeSurfaceWeight(const MediumProperties& medium,
                                         const Vec3f& throughputRgb,
                                         float distance)
{
#ifndef PXR_HDEMBREE_ENABLE_ADOBE_OPENPBR
    (void)throughputRgb;
    return EvalBeerTransmittance(medium, distance);
#else
    if (distance <= 0.0f || medium.IsVacuum()) {
        return Vec3f(1.0f);
    }

    const OpenPBR_HomogeneousVolume volume = _MakeHomogeneousVolume(medium);
    if (!openpbr_is_homogeneous_volume(volume)) {
        return Vec3f(1.0f);
    }
    return _CleanNonnegative(
        _FromOpenPbr(openpbr_calculate_weight_for_surface_at_distance(
            volume, _ToOpenPbr(_CleanThroughput(throughputRgb)), distance)));
#endif
}

Vec3f
AdobeOpenPbrSampleVolumePhase(const MediumProperties& medium,
                              const Vec3f& omegaOutWld, float u1, float u2)
{
#ifndef PXR_HDEMBREE_ENABLE_ADOBE_OPENPBR
    return SampleHenyeyGreenstein(omegaOutWld, medium.anisotropy, u1, u2);
#else
    const OpenPBR_HomogeneousVolume volume = _MakeHomogeneousVolume(medium);
    const Vec3f omegaOutWldSafe =
        _NormalizeOrFallback(omegaOutWld, Vec3f(0.0f, 0.0f, 1.0f));
    return _NormalizeOrFallback(
        _FromOpenPbr(openpbr_sample_anisotropic_phase_function(
            volume, _ToOpenPbr(omegaOutWldSafe),
            vec2(std::clamp(u1, 0.0f, 1.0f - _kEpsilon),
                 std::clamp(u2, 0.0f, 1.0f - _kEpsilon)))),
        -omegaOutWldSafe);
#endif
}

float
AdobeOpenPbrEvalVolumePhasePdf(const MediumProperties& medium,
                               const Vec3f& omegaInWld,
                               const Vec3f& omegaOutWld)
{
#ifndef PXR_HDEMBREE_ENABLE_ADOBE_OPENPBR
    return PdfHenyeyGreenstein(omegaInWld, omegaOutWld, medium.anisotropy);
#else
    const OpenPBR_HomogeneousVolume volume = _MakeHomogeneousVolume(medium);
    const float pdfSolidAngle =
        openpbr_calculate_anisotropic_phase_function_pdf(
            volume,
            _ToOpenPbr(
                _NormalizeOrFallback(omegaOutWld, Vec3f(0.0f, 0.0f, 1.0f))),
            _ToOpenPbr(
                _NormalizeOrFallback(omegaInWld, Vec3f(0.0f, 0.0f, 1.0f))));
    return std::isfinite(pdfSolidAngle) ? std::max(pdfSolidAngle, 0.0f) : 0.0f;
#endif
}

}  // namespace mxcpp
