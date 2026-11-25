//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "openPbr.h"

#include <renderer/integrator/medium.h>
#include <renderer/materials/MaterialXCpp/paramMap.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace mxcpp {

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

constexpr float _kMinMicrofacetAlpha = 1.0e-6f;

float
_Clamp01(float x)
{
    return std::clamp(x, 0.0f, 1.0f);
}

float
_ClampRoughness(float roughness)
{
    return std::clamp(roughness, 0.001f, 1.0f);
}

float
_ClampAlpha(float alpha)
{
    return std::clamp(alpha, _kMinMicrofacetAlpha, 1.0f);
}

Vec3f
_Saturate(const Vec3f& value)
{
    return Vec3f(
        _Clamp01(value[0]),
        _Clamp01(value[1]),
        _Clamp01(value[2]));
}

Vec3f
_LerpVec(const Vec3f& a, const Vec3f& b, float t)
{
    return a * (1.0f - t) + b * t;
}

Vec2f
_ComputeAnisotropicRoughness(float roughness, float anisotropy)
{
    const float clampedRoughness = _ClampRoughness(roughness);
    const float clampedAnisotropy = _Clamp01(anisotropy);
    const float alphaRoughness = clampedRoughness * clampedRoughness;
    const float oneMinusAnisotropy = 1.0f - clampedAnisotropy;
    const float alphaX = alphaRoughness * std::sqrt(
        2.0f /
        std::max(oneMinusAnisotropy * oneMinusAnisotropy + 1.0f, 1.0e-6f));
    const float alphaY = oneMinusAnisotropy * alphaX;
    return Vec2f(
        _ClampAlpha(alphaX),
        _ClampAlpha(alphaY));
}

float
_ComputeEffectiveSpecularRoughness(float specularRoughness,
                                   float coatRoughness,
                                   float coatWeight)
{
    const float specularRoughness4 = std::pow(specularRoughness, 4.0f);
    const float coatRoughness4 = std::pow(coatRoughness, 4.0f);
    const float coatAffectedRoughness = std::pow(
        std::min(
            1.0f,
            2.0f * coatRoughness4 + specularRoughness4),
        0.25f);
    const float clampedCoatWeight = _Clamp01(coatWeight);
    return specularRoughness * (1.0f - clampedCoatWeight) +
        coatAffectedRoughness * clampedCoatWeight;
}

float
_ComputeEffectiveSpecularIor(float specularIor,
                             float specularWeight,
                             float coatIor,
                             float coatWeight)
{
    const float iorSpecular = std::max(specularIor, 1.0f);
    const float iorCoat = std::max(coatIor, 1.0f);
    const float specularToCoat = iorSpecular / iorCoat;
    const float coatToSpecular = iorCoat / iorSpecular;
    const float coatRelativeIor = std::max(specularToCoat, coatToSpecular);
    const float clampedCoatWeight = _Clamp01(coatWeight);
    const float iorEffective = iorSpecular * (1.0f - clampedCoatWeight) +
                               coatRelativeIor * clampedCoatWeight;

    const float epsilon = (iorEffective - 1.0f) / (iorEffective + 1.0f);
    const float scaledF0 = std::clamp(
        specularWeight * epsilon * epsilon,
        0.0f,
        0.99999f);
    const float modulatedEpsilon =
        std::copysign(std::sqrt(scaledF0), iorEffective - 1.0f);
    return (1.0f + modulatedEpsilon) /
        std::max(1.0f - modulatedEpsilon, 1.0e-6f);
}

Bsdf::NodeId
_AppendLayer(Bsdf::ClosureTree* tree, Bsdf::NodeId top, Bsdf::NodeId base)
{
    if (!tree->IsValid(top)) {
        return base;
    }
    if (!tree->IsValid(base)) {
        return top;
    }

    Bsdf::LayerData layer;
    layer.top = top;
    layer.base = base;
    return tree->Add(layer);
}

Bsdf::NodeId
_AppendMix(Bsdf::ClosureTree* tree,
           Bsdf::NodeId bg,
           Bsdf::NodeId fg,
           float mix)
{
    const float clampedMix = _Clamp01(mix);
    if (clampedMix <= 0.0f || !tree->IsValid(fg)) {
        return bg;
    }
    if (clampedMix >= 1.0f || !tree->IsValid(bg)) {
        return fg;
    }

    Bsdf::MixData mixData;
    mixData.bg = bg;
    mixData.fg = fg;
    mixData.mix = clampedMix;
    return tree->Add(mixData);
}

bool
_IsIdentityWeight(const Vec3f& weight)
{
    return std::abs(weight[0] - 1.0f) < 1.0e-6f &&
           std::abs(weight[1] - 1.0f) < 1.0e-6f &&
           std::abs(weight[2] - 1.0f) < 1.0e-6f;
}

Bsdf::NodeId
_AppendMultiply(Bsdf::ClosureTree* tree,
                Bsdf::NodeId input,
                const Vec3f& weight)
{
    if (!tree->IsValid(input)) {
        return input;
    }

    const Vec3f saturatedWeight = _Saturate(weight);
    if (_IsIdentityWeight(saturatedWeight)) {
        return input;
    }

    Bsdf::MultiplyData multiply;
    multiply.input = input;
    multiply.weight = saturatedWeight;
    return tree->Add(multiply);
}

Vec3f
_ComputeCoatDarkeningFactor(const Vec3f& baseColor,
                            float metallic,
                            float specularWeight,
                            const Vec3f& subsurfaceColor,
                            float subsurfaceWeight,
                            float coatIor,
                            float coatWeight,
                            float coatDarkening)
{
    const float clampedCoatIor = std::max(coatIor, 1.0f);
    const float coatF0 = std::pow(
        (clampedCoatIor - 1.0f) / (clampedCoatIor + 1.0f), 2.0f);
    const float kCoat =
        1.0f - (1.0f - coatF0) / (clampedCoatIor * clampedCoatIor);

    const Vec3f eMetal = _Saturate(baseColor) * _Clamp01(specularWeight);
    const Vec3f eDielectric = _LerpVec(
        _Saturate(baseColor),
        _Saturate(subsurfaceColor),
        _Clamp01(subsurfaceWeight));
    const Vec3f eBase = _LerpVec(
        eDielectric,
        eMetal,
        _Clamp01(metallic));

    const Vec3f numerator(1.0f - kCoat);
    const Vec3f denominator = Vec3f(1.0f) - eBase * kCoat;
    Vec3f baseDarkening(1.0f);
    for (int i = 0; i < 3; ++i) {
        baseDarkening[i] = numerator[i] /
            std::max(denominator[i], 1.0e-5f);
    }

    return _LerpVec(
        Vec3f(1.0f),
        _Saturate(baseDarkening),
        _Clamp01(coatWeight) * _Clamp01(coatDarkening));
}

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

}  // namespace

SurfaceClosure
EvalOpenPbr(const ParamMap& params)
{
    SurfaceClosure c;

    const float baseWeight = Get<float>(params, _kBaseWeight, 1.0f);
    const Vec3f baseColor = Get<Vec3f>(params, _kBaseColor, Vec3f(0.8f));
    const float baseRoughness = _GetWithFallback<float>(
        params,
        _kBaseDiffuseRoughness,
        _kLegacyBaseRoughness,
        0.0f);

    c.baseColor = baseColor * baseWeight;
    c.roughness = Get<float>(params, _kSpecularRoughness, 0.3f);
    c.metallic = Get<float>(params, _kBaseMetalness, 0.0f);

    c.specular = Get<float>(params, _kSpecularWeight, 1.0f);
    c.specularColor = Get<Vec3f>(params, _kSpecularColor, Vec3f(1.0f));
    c.specularIor = Get<float>(params, _kSpecularIor, 1.5f);

    const float specularAnisotropy =
        _GetWithFallback<float>(
            params,
            _kSpecularRoughnessAnisotropy,
            _kLegacySpecularAnisotropy,
            0.0f);

    c.transmission = Get<float>(params, _kTransmissionWeight, 0.0f);
    c.transmissionColor =
        Get<Vec3f>(params, _kTransmissionColor, Vec3f(1.0f));
    const float transmissionDepth =
        Get<float>(params, _kTransmissionDepth, 0.0f);
    const Vec3f transmissionScatter =
        Get<Vec3f>(params, _kTransmissionScatter, Vec3f(0.0f));
    const float transmissionScatterAnisotropy = Get<float>(
        params, _kTransmissionScatterAnisotropy, 0.0f);
    const float transmissionDispersionScale = _Clamp01(
        Get<float>(params, _kTransmissionDispersionScale, 0.0f));
    const float transmissionDispersionAbbe = std::max(
        Get<float>(params, _kTransmissionDispersionAbbeNumber, 20.0f),
        0.0f);
    const float effectiveDispersionAbbe =
        transmissionDispersionScale > 0.0f
        ? transmissionDispersionAbbe / transmissionDispersionScale
        : 0.0f;
    c.interiorMedium = MakeTransmissionMedium(
        c.transmission,
        c.transmissionColor,
        transmissionDepth,
        transmissionScatter,
        transmissionScatterAnisotropy);

    c.subsurfaceWeight = Get<float>(params, _kSubsurfaceWeight, 0.0f);
    c.subsurfaceColor = Get<Vec3f>(params, _kSubsurfaceColor, Vec3f(0.8f));
    // OpenPBR defines `subsurface_radius` as a *scalar* (float), broadcast to
    // per-channel via the separate `subsurface_radius_scale` color3.
    // Reading it as Vec3f here would silently fall back to the default,
    // giving a 1 m base mfp regardless of the scene value.
    const float subsurfaceRadiusScalar =
        Get<float>(params, _kSubsurfaceRadius, 1.0f);
    c.subsurfaceRadius = Vec3f(subsurfaceRadiusScalar);
    c.subsurfaceRadiusScale = Get<Vec3f>(
        params, _kSubsurfaceRadiusScale, Vec3f(1.0f, 0.5f, 0.25f));
    c.subsurfaceAnisotropy = Get<float>(
        params, _kSubsurfaceScatterAnisotropy, 0.0f);

    c.coat = Get<float>(params, _kCoatWeight, 0.0f);
    const Vec3f coatColor = Get<Vec3f>(params, _kCoatColor, Vec3f(1.0f));
    c.coatRoughness = Get<float>(params, _kCoatRoughness, 0.0f);
    const float coatAnisotropy =
        Get<float>(params, _kCoatRoughnessAnisotropy, 0.0f);
    const Vec2f coatRoughness =
        _ComputeAnisotropicRoughness(c.coatRoughness, coatAnisotropy);
    c.coatIor = Get<float>(params, _kCoatIor, 1.6f);
    const float coatDarkening = Get<float>(params, _kCoatDarkening, 1.0f);
    const float effectiveSpecularRoughness =
        _ComputeEffectiveSpecularRoughness(
            c.roughness,
            c.coatRoughness,
            c.coat);
    const Vec2f specularRoughness =
        _ComputeAnisotropicRoughness(
            effectiveSpecularRoughness,
            specularAnisotropy);
    const float thinFilmWeight = _Clamp01(
        Get<float>(params, _kThinFilmWeight, 0.0f));
    const float thinFilmThicknessNm = std::max(
        Get<float>(params, _kThinFilmThickness, 0.5f) * 1000.0f,
        0.0f);
    const float thinFilmIor = std::max(
        Get<float>(params, _kThinFilmIor, 1.4f),
        1.0f);

    c.sheen = Get<float>(params, _kFuzzWeight, 0.0f);
    c.sheenColor = Get<Vec3f>(params, _kFuzzColor, Vec3f(1.0f));
    c.sheenRoughness = Get<float>(params, _kFuzzRoughness, 0.5f);

    const float emissionLum = Get<float>(params, _kEmissionLuminance, 0.0f);
    const Vec3f emissionColor = Get<Vec3f>(params, _kEmissionColor, Vec3f(1.0f));
    c.emissiveColor = emissionColor * emissionLum;

    const Vec3f opacityVec = Get<Vec3f>(params, _kGeometryOpacity, Vec3f(1.0f));
    c.opacity = (opacityVec[0] + opacityVec[1] + opacityVec[2]) / 3.0f;
    if (c.opacity == 1.0f) {
        c.opacity = Get<float>(params, _kGeometryOpacity, 1.0f);
    }
    c.presence = c.opacity;

    c.thinWalled = Get<bool>(params, _kGeometryThinWalled, false);
    c.hasInteriorMedium = !c.thinWalled && !c.interiorMedium.IsVacuum();
    const bool hasGeometryNormal =
        params.Find(_kGeometryNormal) || params.Find(_kLegacyNormal);
    c.normal = _GetWithFallback<Vec3f>(
        params,
        _kGeometryNormal,
        _kLegacyNormal,
        Vec3f(0.0f, 0.0f, 1.0f));
    c.normalSpace = hasGeometryNormal
        ? SurfaceNormalSpace::World
        : SurfaceNormalSpace::None;
    const Vec3f tangent = _GetWithFallback<Vec3f>(
        params,
        _kGeometryTangent,
        _kLegacyTangent,
        Vec3f(1.0f, 0.0f, 0.0f));
    const Vec3f coatTangent = _GetWithFallback<Vec3f>(
        params,
        _kGeometryCoatTangent,
        _kGeometryTangent,
        tangent);
    const bool hasCoatNormal =
        params.Find(_kGeometryCoatNormal) || params.Find(_kCoatNormal);
    const Vec3f coatNormal = hasCoatNormal
        ? _GetWithFallback<Vec3f>(
              params,
              _kGeometryCoatNormal,
              _kCoatNormal,
              c.normal)
        : c.normal;

    Bsdf::ClosureTree tree;
    Bsdf::NodeId opaqueBase = Bsdf::InvalidNodeId;

    const float diffuseWeight = _Clamp01(baseWeight);
    if (diffuseWeight > 0.0f) {
        Bsdf::OrenNayarDiffuseData diffuse;
        diffuse.weight = diffuseWeight;
        diffuse.color = baseColor;
        diffuse.roughness = baseRoughness;
        diffuse.energyCompensation = true;
        opaqueBase = tree.Add(diffuse);
    }

    if (c.HasSubsurfaceScattering()) {
        Bsdf::SubsurfaceData subsurface;
        subsurface.weight = _Clamp01(c.subsurfaceWeight);
        subsurface.color = _Saturate(c.subsurfaceColor);
        subsurface.radius = c.subsurfaceRadius;
        subsurface.anisotropy = c.subsurfaceAnisotropy;
        const Bsdf::NodeId subsurfaceId = tree.Add(subsurface);
        if (opaqueBase == Bsdf::InvalidNodeId) {
            opaqueBase = subsurfaceId;
        } else {
            opaqueBase = _AppendMix(
                &tree, opaqueBase, subsurfaceId, subsurface.weight);
        }
    }

    const float metalMix = _Clamp01(c.metallic);
    const float specularWeight = _Clamp01(c.specular);
    const float effectiveSpecularIor = _ComputeEffectiveSpecularIor(
        c.specularIor,
        c.specular,
        c.coatIor,
        c.coat);
    Bsdf::NodeId dielectricSubstrate = opaqueBase;

    const float transmissionWeight = _Clamp01(c.transmission);
    const bool useCombinedDielectricInterface =
        transmissionWeight > 0.0f;
    if (transmissionWeight > 0.0f) {
        Bsdf::DielectricInterfaceData interface;
        interface.reflectionWeight = 1.0f;
        interface.reflectionTint = _Saturate(c.specularColor);
        interface.transmissionWeight = transmissionWeight;
        // Regular OpenPBR volumes carry transmission_color through the
        // interior medium; tinting the surface BTDF would double-color it.
        interface.transmissionTint = c.hasInteriorMedium
            ? Vec3f(1.0f)
            : _Saturate(c.transmissionColor);
        interface.ior = effectiveSpecularIor;
        interface.dispersionAbbe = effectiveDispersionAbbe;
        interface.roughness = specularRoughness;
        interface.tangent = tangent;
        interface.thinFilmWeight = thinFilmWeight;
        interface.thinFilmThickness = thinFilmThicknessNm;
        interface.thinFilmIor = thinFilmIor;
        interface.thinWalled = c.thinWalled;
        interface.compensateCoupledDielectric = true;

        dielectricSubstrate = _AppendMultiply(
            &tree,
            dielectricSubstrate,
            Vec3f(1.0f - transmissionWeight));
        dielectricSubstrate = _AppendLayer(
            &tree, tree.Add(interface), dielectricSubstrate);
    }

    Bsdf::NodeId dielectricBase = dielectricSubstrate;
    if (specularWeight > 0.0f && !useCombinedDielectricInterface) {
        Bsdf::DielectricData dielectric;
        dielectric.weight = 1.0f;
        dielectric.tint = _Saturate(c.specularColor);
        dielectric.ior = effectiveSpecularIor;
        dielectric.dispersionAbbe = effectiveDispersionAbbe;
        dielectric.roughness = specularRoughness;
        dielectric.tangent = tangent;
        dielectric.scatterMode = Bsdf::ScatterMode::Reflection;
        dielectric.thinFilmWeight = thinFilmWeight;
        dielectric.thinFilmThickness = thinFilmThicknessNm;
        dielectric.thinFilmIor = thinFilmIor;
        const Bsdf::NodeId dielectricId = tree.Add(dielectric);
        dielectricBase = _AppendLayer(&tree, dielectricId, dielectricBase);
    }

    Bsdf::NodeId root = dielectricBase;
    if (metalMix > 0.0f) {
        Bsdf::GeneralizedSchlickData metal;
        metal.weight = specularWeight;
        metal.color0 = _Saturate(baseColor * baseWeight);
        metal.color82 = _Saturate(c.specularColor);
        metal.color90 = Vec3f(1.0f);
        metal.exponent = 5.0f;
        metal.roughness = specularRoughness;
        metal.tangent = tangent;
        metal.scatterMode = Bsdf::ScatterMode::Reflection;
        metal.thinFilmWeight = thinFilmWeight;
        metal.thinFilmThickness = thinFilmThicknessNm;
        metal.thinFilmIor = thinFilmIor;

        const Bsdf::NodeId metalId = tree.Add(metal);
        root = _AppendMix(&tree, dielectricBase, metalId, metalMix);
    }

    if (_Clamp01(c.coat) > 0.0f) {
        root = _AppendMultiply(
            &tree,
            root,
            _ComputeCoatDarkeningFactor(
                baseColor,
                metalMix,
                specularWeight,
                c.subsurfaceColor,
                c.subsurfaceWeight,
                c.coatIor,
                c.coat,
                coatDarkening));
        root = _AppendMultiply(
            &tree,
            root,
            _LerpVec(
                Vec3f(1.0f),
                _Saturate(coatColor),
                _Clamp01(c.coat)));

        Bsdf::DielectricData coat;
        coat.weight = _Clamp01(c.coat);
        coat.tint = Vec3f(1.0f);
        coat.ior = std::max(c.coatIor, 1.0f);
        coat.roughness = coatRoughness;
        coat.normal = coatNormal;
        coat.hasShadingNormal = hasCoatNormal;
        coat.tangent = coatTangent;
        coat.scatterMode = Bsdf::ScatterMode::Reflection;
        root = _AppendLayer(&tree, tree.Add(coat), root);
    }

    if (_Clamp01(c.sheen) > 0.0f) {
        Bsdf::SheenData fuzz;
        fuzz.weight = _Clamp01(c.sheen);
        fuzz.color = _Saturate(c.sheenColor);
        fuzz.roughness = _ClampRoughness(c.sheenRoughness);
        fuzz.mode = Bsdf::SheenMode::Zeltner;
        root = _AppendLayer(&tree, tree.Add(fuzz), root);
    }

    tree.root = root;
    c.bsdfTree = std::move(tree);

    return c;
}

}  // namespace mxcpp
