//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "standardSurface.h"

#include <renderer/integrator/medium.h>
#include <renderer/materials/MaterialXCpp/nodes/helpers/mathHelpers.h>
#include <renderer/materials/MaterialXCpp/paramMap.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace mxcpp {

static const SlotName _kBase("base");
static const SlotName _kBaseColor("base_color");
static const SlotName _kDiffuseRoughness("diffuse_roughness");
static const SlotName _kMetalness("metalness");
static const SlotName _kSpecular("specular");
static const SlotName _kSpecularColor("specular_color");
static const SlotName _kSpecularRoughness("specular_roughness");
static const SlotName _kSpecularIOR("specular_IOR");
static const SlotName _kSpecularAnisotropy("specular_anisotropy");
static const SlotName _kSpecularRotation("specular_rotation");
static const SlotName _kTransmission("transmission");
static const SlotName _kTransmissionColor("transmission_color");
static const SlotName _kTransmissionDispersion("transmission_dispersion");
static const SlotName _kTransmissionDepth("transmission_depth");
static const SlotName _kTransmissionScatter("transmission_scatter");
static const SlotName _kTransmissionScatterAnisotropy(
    "transmission_scatter_anisotropy");
static const SlotName _kTransmissionExtraRoughness("transmission_extra_roughness");
static const SlotName _kSubsurface("subsurface");
static const SlotName _kSubsurfaceColor("subsurface_color");
static const SlotName _kSubsurfaceRadius("subsurface_radius");
static const SlotName _kSubsurfaceScale("subsurface_scale");
static const SlotName _kSubsurfaceAnisotropy("subsurface_anisotropy");
static const SlotName _kSheen("sheen");
static const SlotName _kSheenColor("sheen_color");
static const SlotName _kSheenRoughness("sheen_roughness");
static const SlotName _kCoat("coat");
static const SlotName _kCoatColor("coat_color");
static const SlotName _kCoatRoughness("coat_roughness");
static const SlotName _kCoatIOR("coat_IOR");
static const SlotName _kCoatAffectColor("coat_affect_color");
static const SlotName _kCoatAffectRoughness("coat_affect_roughness");
static const SlotName _kCoatAnisotropy("coat_anisotropy");
static const SlotName _kCoatRotation("coat_rotation");
static const SlotName _kCoatNormal("coat_normal");
static const SlotName _kThinFilmThickness("thin_film_thickness");
static const SlotName _kThinFilmIOR("thin_film_IOR");
static const SlotName _kEmission("emission");
static const SlotName _kEmissionColor("emission_color");
static const SlotName _kOpacity("opacity");
static const SlotName _kThinWalled("thin_walled");
static const SlotName _kNormal("normal");
static const SlotName _kTangent("tangent");

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

Vec3f
_LerpVec(const Vec3f& a, const Vec3f& b, const Vec3f& t)
{
    return Vec3f(
        a[0] * (1.0f - t[0]) + b[0] * t[0],
        a[1] * (1.0f - t[1]) + b[1] * t[1],
        a[2] * (1.0f - t[2]) + b[2] * t[2]);
}

Vec3f
_PowColorNonNegative(const Vec3f& value, float exponent)
{
    return Vec3f(
        std::pow(std::max(value[0], 0.0f), exponent),
        std::pow(std::max(value[1], 0.0f), exponent),
        std::pow(std::max(value[2], 0.0f), exponent));
}

Vec3f
_NormalizeOrFallback(const Vec3f& value, const Vec3f& fallback)
{
    const float length = value.length();
    if (length < 1.0e-6f) {
        return fallback;
    }
    return value / length;
}

Vec3f
_RotateTangent(
    const Vec3f& tangent,
    const Vec3f& axis,
    float anisotropy,
    float rotation)
{
    if (std::clamp(anisotropy, 0.0f, 1.0f) <= 0.0f) {
        return tangent;
    }

    return _NormalizeOrFallback(
        Rotate3d(tangent, rotation * 360.0f, axis),
        tangent);
}

Vec2f
_ComputeAnisotropicRoughness(float roughness, float anisotropy)
{
    const float clampedRoughness = _ClampRoughness(roughness);
    const float alphaRoughness =
        std::clamp(
            clampedRoughness * clampedRoughness,
            _kMinMicrofacetAlpha,
            1.0f);
    const float clampedAnisotropy = std::clamp(anisotropy, 0.0f, 0.98f);
    if (clampedAnisotropy <= 0.0f) {
        return Vec2f(alphaRoughness, alphaRoughness);
    }

    const float aspect = std::sqrt(1.0f - clampedAnisotropy);
    return Vec2f(
        std::min(alphaRoughness / aspect, 1.0f),
        std::clamp(alphaRoughness * aspect, _kMinMicrofacetAlpha, 1.0f));
}

struct _ArtisticIorData
{
    Vec3f ior = Vec3f(1.0f);
    Vec3f extinction = Vec3f(0.0f);
};

_ArtisticIorData
_ComputeArtisticIor(const Vec3f& reflectivity, const Vec3f& edgeColor)
{
    const Vec3f r = Vec3f(
        std::clamp(reflectivity[0], 0.0f, 0.99f),
        std::clamp(reflectivity[1], 0.0f, 0.99f),
        std::clamp(reflectivity[2], 0.0f, 0.99f));
    const Vec3f rSqrt(
        std::sqrt(r[0]),
        std::sqrt(r[1]),
        std::sqrt(r[2]));
    const Vec3f nMin =
        CompDiv(Vec3f(1.0f) - r, Vec3f(1.0f) + r);
    const Vec3f nMax =
        CompDiv(Vec3f(1.0f) + rSqrt, Vec3f(1.0f) - rSqrt);

    _ArtisticIorData data;
    data.ior = _LerpVec(
        nMax,
        nMin,
        _Saturate(edgeColor));

    const Vec3f np1 = data.ior + Vec3f(1.0f);
    const Vec3f nm1 = data.ior - Vec3f(1.0f);
    const Vec3f numerator =
        CompMul(np1, np1) * r - CompMul(nm1, nm1);
    const Vec3f denominator = Vec3f(
        std::max(1.0f - r[0], 1.0e-6f),
        std::max(1.0f - r[1], 1.0e-6f),
        std::max(1.0f - r[2], 1.0e-6f));
    const Vec3f k2 = CompDiv(numerator, denominator);
    data.extinction = Vec3f(
        std::sqrt(std::max(k2[0], 0.0f)),
        std::sqrt(std::max(k2[1], 0.0f)),
        std::sqrt(std::max(k2[2], 0.0f)));
    return data;
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

bool
_IsIdentityWeight(const Vec3f& weight)
{
    return std::abs(weight[0] - 1.0f) < 1.0e-6f &&
           std::abs(weight[1] - 1.0f) < 1.0e-6f &&
           std::abs(weight[2] - 1.0f) < 1.0e-6f;
}

Bsdf::NodeId
_AppendMultiply(Bsdf::ClosureTree* tree, Bsdf::NodeId input, const Vec3f& weight)
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

Bsdf::NodeId
_AppendMix(Bsdf::ClosureTree* tree, Bsdf::NodeId bg, Bsdf::NodeId fg, float mix)
{
    const float clampedMix = _Clamp01(mix);
    if (clampedMix <= 0.0f || !tree->IsValid(fg)) {
        return bg;
    }
    if (clampedMix >= 1.0f) {
        return fg;
    }
    if (!tree->IsValid(bg)) {
        return _AppendMultiply(tree, fg, Vec3f(clampedMix));
    }

    Bsdf::MixData mixData;
    mixData.bg = bg;
    mixData.fg = fg;
    mixData.mix = clampedMix;
    return tree->Add(mixData);
}

}  // namespace

SurfaceClosure
EvalStandardSurface(const ParamMap& params)
{
    SurfaceClosure c;

    const float base = Get<float>(params, _kBase, 1.0f);
    const Vec3f baseCol = Get<Vec3f>(params, _kBaseColor, Vec3f(0.8f));
    const float diffuseRoughness = Get<float>(params, _kDiffuseRoughness, 0.0f);

    c.baseColor = baseCol * base;
    const float specularRoughnessInput =
        Get<float>(params, _kSpecularRoughness, 0.2f);
    c.metallic = Get<float>(params, _kMetalness, 0.0f);

    const float spec = Get<float>(params, _kSpecular, 1.0f);
    c.specular = spec;
    c.specularColor = Get<Vec3f>(params, _kSpecularColor, Vec3f(1.0f));
    c.specularIor = Get<float>(params, _kSpecularIOR, 1.5f);

    const float specularAnisotropy =
        Get<float>(params, _kSpecularAnisotropy, 0.0f);
    const float specularRotation = Get<float>(params, _kSpecularRotation, 0.0f);

    c.transmission = Get<float>(params, _kTransmission, 0.0f);
    c.transmissionColor =
        Get<Vec3f>(params, _kTransmissionColor, Vec3f(1.0f));
    const float transmissionDepth =
        Get<float>(params, _kTransmissionDepth, 0.0f);
    const Vec3f transmissionScatter =
        Get<Vec3f>(params, _kTransmissionScatter, Vec3f(0.0f));
    const float transmissionScatterAnisotropy = Get<float>(
        params, _kTransmissionScatterAnisotropy, 0.0f);
    const float transmissionDispersionAbbe =
        std::max(Get<float>(params, _kTransmissionDispersion, 0.0f), 0.0f);
    c.interiorMedium = MakeTransmissionMedium(
        c.transmission,
        c.transmissionColor,
        transmissionDepth,
        transmissionScatter,
        transmissionScatterAnisotropy);

    c.subsurfaceWeight = Get<float>(params, _kSubsurface, 0.0f);
    c.subsurfaceColor = Get<Vec3f>(params, _kSubsurfaceColor, Vec3f(1.0f));
    c.subsurfaceRadius = Get<Vec3f>(params, _kSubsurfaceRadius, Vec3f(1.0f));
    c.subsurfaceRadiusScale = Vec3f(
        std::max(Get<float>(params, _kSubsurfaceScale, 1.0f), 0.0f));
    c.subsurfaceAnisotropy =
        Get<float>(params, _kSubsurfaceAnisotropy, 0.0f);

    c.coat = Get<float>(params, _kCoat, 0.0f);
    const Vec3f coatColor = Get<Vec3f>(params, _kCoatColor, Vec3f(1.0f));
    c.coatRoughness = Get<float>(params, _kCoatRoughness, 0.1f);
    c.coatIor = Get<float>(params, _kCoatIOR, 1.5f);
    const float coatAffectColor =
        Get<float>(params, _kCoatAffectColor, 0.0f);
    const float coatAffectRoughness =
        Get<float>(params, _kCoatAffectRoughness, 0.0f);
    const float coatAnisotropy = Get<float>(params, _kCoatAnisotropy, 0.0f);
    const float coatRotation = Get<float>(params, _kCoatRotation, 0.0f);
    const float thinFilmThicknessNm = std::max(
        Get<float>(params, _kThinFilmThickness, 0.0f),
        0.0f);
    const float thinFilmIor = std::max(
        Get<float>(params, _kThinFilmIOR, 1.5f),
        1.0f);
    const float transmissionExtraRoughness =
        Get<float>(params, _kTransmissionExtraRoughness, 0.0f);

    c.sheen = Get<float>(params, _kSheen, 0.0f);
    c.sheenColor = Get<Vec3f>(params, _kSheenColor, Vec3f(1.0f));
    c.sheenRoughness = Get<float>(params, _kSheenRoughness, 0.3f);

    const float emissionWeight = Get<float>(params, _kEmission, 0.0f);
    const Vec3f emissionCol = Get<Vec3f>(params, _kEmissionColor, Vec3f(1.0f));

    const Vec3f opacityVec = Get<Vec3f>(params, _kOpacity, Vec3f(1.0f));
    c.opacity = (opacityVec[0] + opacityVec[1] + opacityVec[2]) / 3.0f;

    c.thinWalled = Get<bool>(params, _kThinWalled, false);
    c.hasInteriorMedium = !c.thinWalled && !c.interiorMedium.IsVacuum();
    if (c.opacity == 1.0f) {
        c.opacity = Get<float>(params, _kOpacity, 1.0f);
    }
    c.presence = c.opacity;

    c.normal = Get<Vec3f>(params, _kNormal, Vec3f(0.0f, 0.0f, 1.0f));
    c.normalSpace = params.Find(_kNormal)
        ? SurfaceNormalSpace::World
        : SurfaceNormalSpace::None;
    const Vec3f tangent =
        Get<Vec3f>(params, _kTangent, Vec3f(1.0f, 0.0f, 0.0f));
    const bool hasCoatNormal = params.Find(_kCoatNormal) != nullptr;
    const Vec3f coatNormal =
        hasCoatNormal ? Get<Vec3f>(params, _kCoatNormal, c.normal) : c.normal;

    const float coatRoughnessMix = _Clamp01(
        _Clamp01(coatAffectRoughness) *
        _Clamp01(c.coat) *
        _Clamp01(c.coatRoughness));
    const float mainRoughnessInput = std::clamp(
        specularRoughnessInput * (1.0f - coatRoughnessMix) + coatRoughnessMix,
        0.0f,
        1.0f);
    const float transmissionRoughnessInput = std::clamp(
        std::clamp(
            specularRoughnessInput + transmissionExtraRoughness,
            0.0f,
            1.0f) *
            (1.0f - coatRoughnessMix) +
            coatRoughnessMix,
        0.0f,
        1.0f);
    c.roughness = mainRoughnessInput;

    const Vec2f specularRoughness =
        _ComputeAnisotropicRoughness(mainRoughnessInput, specularAnisotropy);
    const Vec2f transmissionRoughness =
        _ComputeAnisotropicRoughness(
            transmissionRoughnessInput,
            specularAnisotropy);
    const Vec2f coatRoughness =
        _ComputeAnisotropicRoughness(c.coatRoughness, coatAnisotropy);

    const float coatGamma =
        1.0f + _Clamp01(c.coat) * _Clamp01(coatAffectColor);
    const Vec3f coatAffectedBaseColor =
        _PowColorNonNegative(baseCol, coatGamma);
    const Vec3f coatAttenuation = _LerpVec(
        Vec3f(1.0f),
        _Saturate(coatColor),
        _Clamp01(c.coat));
    const Vec3f mainTangent = _RotateTangent(
        tangent,
        c.normal,
        specularAnisotropy,
        specularRotation);
    const Vec3f coatTangent = _RotateTangent(
        tangent,
        coatNormal,
        coatAnisotropy,
        coatRotation);

    // The full Standard Surface EDF under coat is directional, but the
    // current closure only stores a flat emission color.
    c.emissiveColor = CompMul(emissionCol * emissionWeight, coatAttenuation);

    Bsdf::ClosureTree tree;
    Bsdf::NodeId root = Bsdf::InvalidNodeId;

    if (_Clamp01(base) > 0.0f) {
        Bsdf::OrenNayarDiffuseData diffuse;
        diffuse.weight = _Clamp01(base);
        diffuse.color = coatAffectedBaseColor;
        diffuse.roughness = diffuseRoughness;
        diffuse.energyCompensation = true;
        root = tree.Add(diffuse);
    }

    if (c.HasSubsurfaceScattering()) {
        Bsdf::SubsurfaceData subsurface;
        subsurface.weight = _Clamp01(c.subsurfaceWeight);
        subsurface.color = _Saturate(c.subsurfaceColor);
        subsurface.radius = c.subsurfaceRadius;
        subsurface.anisotropy = c.subsurfaceAnisotropy;
        const Bsdf::NodeId subsurfaceId = tree.Add(subsurface);
        if (root == Bsdf::InvalidNodeId) {
            root = subsurfaceId;
        } else {
            root = _AppendMix(&tree, root, subsurfaceId, subsurface.weight);
        }
    }

    if (_Clamp01(c.sheen) > 0.0f) {
        Bsdf::SheenData sheen;
        sheen.weight = _Clamp01(c.sheen);
        sheen.color = _Saturate(c.sheenColor);
        sheen.roughness = _ClampRoughness(c.sheenRoughness);
        root = _AppendLayer(&tree, tree.Add(sheen), root);
    }

    const float transmissionMix = _Clamp01(c.transmission);
    const float clampedSpec = _Clamp01(spec);
    // Standard Surface retains its established reflection layer over a
    // separately mixed transmission lobe. Besides preserving compatibility,
    // this supports transmission_extra_roughness and keeps the OpenPBR
    // coupled-interface transport policy out of this material model.
    if (transmissionMix > 0.0f) {
        Bsdf::NodeId transmissionId = Bsdf::InvalidNodeId;
        if (c.thinWalled) {
            Bsdf::DielectricData transmission;
            transmission.weight = 1.0f;
            transmission.tint = _Saturate(c.transmissionColor);
            transmission.ior = 1.0f;
            transmission.dispersionAbbe = transmissionDispersionAbbe;
            transmission.roughness = transmissionRoughness;
            transmission.tangent = mainTangent;
            transmission.scatterMode = Bsdf::ScatterMode::Transmission;
            transmissionId = tree.Add(transmission);
        } else {
            Bsdf::DielectricData transmission;
            transmission.weight = 1.0f;
            transmission.tint = _Saturate(c.transmissionColor);
            transmission.ior = std::max(c.specularIor, 1.0f);
            transmission.dispersionAbbe = transmissionDispersionAbbe;
            transmission.roughness = transmissionRoughness;
            transmission.tangent = mainTangent;
            transmission.scatterMode = Bsdf::ScatterMode::Transmission;
            transmissionId = tree.Add(transmission);
        }
        root = _AppendMix(&tree, root, transmissionId, transmissionMix);
    }

    if (clampedSpec > 0.0f) {
        Bsdf::DielectricData dielectric;
        dielectric.weight = clampedSpec;
        dielectric.tint = _Saturate(c.specularColor);
        dielectric.ior = std::max(c.specularIor, 1.0f);
        dielectric.dispersionAbbe = transmissionDispersionAbbe;
        dielectric.roughness = specularRoughness;
        dielectric.tangent = mainTangent;
        dielectric.scatterMode = Bsdf::ScatterMode::Reflection;
        dielectric.thinFilmWeight = 1.0f;
        dielectric.thinFilmThickness = thinFilmThicknessNm;
        dielectric.thinFilmIor = thinFilmIor;
        root = _AppendLayer(&tree, tree.Add(dielectric), root);
    }

    const float metalMix = _Clamp01(c.metallic);
    if (metalMix > 0.0f) {
        Bsdf::NodeId metalId = Bsdf::InvalidNodeId;
        if (thinFilmThicknessNm > 0.0f) {
            Bsdf::GeneralizedSchlickData metal;
            metal.weight = 1.0f;
            metal.color0 = _Saturate(CompMul(c.baseColor, c.specularColor));
            metal.color82 = metal.color0;
            metal.color90 = Vec3f(1.0f);
            metal.exponent = 5.0f;
            metal.roughness = specularRoughness;
            metal.tangent = mainTangent;
            metal.scatterMode = Bsdf::ScatterMode::Reflection;
            metal.thinFilmWeight = 1.0f;
            metal.thinFilmThickness = thinFilmThicknessNm;
            metal.thinFilmIor = thinFilmIor;
            metalId = tree.Add(metal);
        } else {
            const _ArtisticIorData artisticIor = _ComputeArtisticIor(
                _Saturate(c.baseColor),
                _Saturate(c.specularColor * clampedSpec));
            Bsdf::ConductorData conductor;
            conductor.weight = 1.0f;
            conductor.ior = artisticIor.ior;
            conductor.extinction = artisticIor.extinction;
            conductor.roughness = specularRoughness;
            conductor.tangent = mainTangent;
            conductor.thinFilmWeight = 1.0f;
            conductor.thinFilmThickness = thinFilmThicknessNm;
            conductor.thinFilmIor = thinFilmIor;
            metalId = tree.Add(conductor);
        }
        root = _AppendMix(&tree, root, metalId, metalMix);
    }

    if (_Clamp01(c.coat) > 0.0f) {
        root = _AppendMultiply(&tree, root, coatAttenuation);

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

    tree.root = root;
    c.bsdfTree = std::move(tree);

    return c;
}

}  // namespace mxcpp
