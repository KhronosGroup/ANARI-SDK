//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "gltfPbr.h"

#include <renderer/materials/MaterialXCpp/nodes/helpers/mathHelpers.h>
#include <renderer/materials/MaterialXCpp/paramMap.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace mxcpp {

static const SlotName _kBaseColor("base_color");
static const SlotName _kMetallic("metallic");
static const SlotName _kRoughness("roughness");
static const SlotName _kNormal("normal");
static const SlotName _kTangent("tangent");
static const SlotName _kTransmission("transmission");
static const SlotName _kSpecular("specular");
static const SlotName _kSpecularColor("specular_color");
static const SlotName _kIor("ior");
static const SlotName _kAlpha("alpha");
static const SlotName _kAlphaMode("alpha_mode");
static const SlotName _kAlphaCutoff("alpha_cutoff");
static const SlotName _kIridescence("iridescence");
static const SlotName _kIridescenceIor("iridescence_ior");
static const SlotName _kIridescenceThickness("iridescence_thickness");
static const SlotName _kSheenColor("sheen_color");
static const SlotName _kSheenRoughness("sheen_roughness");
static const SlotName _kClearcoat("clearcoat");
static const SlotName _kClearcoatRoughness("clearcoat_roughness");
static const SlotName _kClearcoatNormal("clearcoat_normal");
static const SlotName _kEmissive("emissive");
static const SlotName _kEmissiveStrength("emissive_strength");
static const SlotName _kThickness("thickness");
static const SlotName _kAnisotropyStrength("anisotropy_strength");
static const SlotName _kAnisotropyRotation("anisotropy_rotation");
static const SlotName _kDispersion("dispersion");

namespace {

enum class _AlphaMode
{
    Opaque = 0,
    Mask = 1,
    Blend = 2
};

float
_Clamp01(float x)
{
    return std::clamp(x, 0.0f, 1.0f);
}

float
_ClampEpsilon(float x)
{
    return std::clamp(x, 1.0e-5f, 1.0f);
}

Vec3f
_Saturate(const Vec3f& value)
{
    return Vec3f(
        _Clamp01(value[0]),
        _Clamp01(value[1]),
        _Clamp01(value[2]));
}

Vec2f
_ComputeBaseRoughness(float roughness, float anisotropyStrength)
{
    const float alphaRoughness = _ClampEpsilon(roughness * roughness);
    const float strength2 = _Clamp01(anisotropyStrength) * _Clamp01(anisotropyStrength);
    const float at = std::clamp(
        alphaRoughness * (1.0f - strength2) + strength2, 1.0e-5f, 1.0f);
    return Vec2f(at, alphaRoughness);
}

Vec2f
_ComputeIsotropicRoughness(float roughness)
{
    const float alphaRoughness = _ClampEpsilon(roughness * roughness);
    return Vec2f(alphaRoughness, alphaRoughness);
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
    float anisotropyStrength,
    float rotationRadians)
{
    if (std::clamp(anisotropyStrength, 0.0f, 1.0f) <= 0.0f ||
        std::abs(rotationRadians) <= 1.0e-6f) {
        return tangent;
    }

    constexpr float kRadiansToDegrees = 180.0f / kPi;
    return _NormalizeOrFallback(
        Rotate3d(tangent, -rotationRadians * kRadiansToDegrees, axis),
        tangent);
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

float
_MaxComponent(const Vec3f& value)
{
    return std::max(value[0], std::max(value[1], value[2]));
}

}  // namespace

SurfaceClosure
EvalGltfPbr(const ParamMap& params)
{
    SurfaceClosure c;

    const Vec3f baseColor = Get<Vec3f>(params, _kBaseColor, Vec3f(1.0f));
    const float metallic = Get<float>(params, _kMetallic, 1.0f);
    const float roughness = Get<float>(params, _kRoughness, 1.0f);
    const Vec3f tangent = Get<Vec3f>(params, _kTangent, Vec3f(1.0f, 0.0f, 0.0f));
    const float transmission = Get<float>(params, _kTransmission, 0.0f);
    const float specular = Get<float>(params, _kSpecular, 1.0f);
    const Vec3f specularColor = Get<Vec3f>(params, _kSpecularColor, Vec3f(1.0f));
    const float ior = Get<float>(params, _kIor, 1.5f);
    const float alpha = Get<float>(params, _kAlpha, 1.0f);
    const int alphaModeValue = Get<int>(params, _kAlphaMode, 0);
    const float alphaCutoff = Get<float>(params, _kAlphaCutoff, 0.5f);
    const float iridescence = Get<float>(params, _kIridescence, 0.0f);
    const float iridescenceIor = Get<float>(params, _kIridescenceIor, 1.3f);
    const float iridescenceThickness =
        Get<float>(params, _kIridescenceThickness, 100.0f);
    const Vec3f sheenColor = Get<Vec3f>(params, _kSheenColor, Vec3f(0.0f));
    const float sheenRoughness = Get<float>(params, _kSheenRoughness, 0.0f);
    const float clearcoat = Get<float>(params, _kClearcoat, 0.0f);
    const float clearcoatRoughness = Get<float>(params, _kClearcoatRoughness, 0.0f);
    const Vec3f normal = Get<Vec3f>(params, _kNormal, Vec3f(0.0f, 0.0f, 1.0f));
    const Vec3f clearcoatNormal = Get<Vec3f>(params, _kClearcoatNormal, normal);
    const Vec3f emissive = Get<Vec3f>(params, _kEmissive, Vec3f(0.0f));
    const float emissiveStrength = Get<float>(params, _kEmissiveStrength, 1.0f);
    const float thickness = Get<float>(params, _kThickness, 0.0f);
    const float anisotropyStrength = Get<float>(params, _kAnisotropyStrength, 0.0f);
    const float anisotropyRotation = Get<float>(params, _kAnisotropyRotation, 0.0f);
    const float dispersion = std::max(Get<float>(params, _kDispersion, 0.0f), 0.0f);
    const Vec3f mainTangent = _RotateTangent(
        tangent,
        normal,
        anisotropyStrength,
        anisotropyRotation);

    c.baseColor = baseColor;
    c.roughness = roughness;
    c.metallic = metallic;
    c.specular = specular;
    c.specularColor = specularColor;
    c.specularIor = ior;
    c.transmission = transmission;
    c.transmissionColor = baseColor;
    c.sheenColor = sheenColor;
    c.sheen = _MaxComponent(sheenColor);
    c.sheenRoughness = sheenRoughness * sheenRoughness;
    c.coat = clearcoat;
    c.coatRoughness = clearcoatRoughness;
    c.coatIor = 1.5f;
    c.emissiveColor = emissive * emissiveStrength;
    c.normal = normal;
    c.normalSpace = params.Find(_kNormal)
        ? SurfaceNormalSpace::World
        : SurfaceNormalSpace::None;
    c.thinWalled = thickness <= 0.0f;

    const _AlphaMode alphaMode = static_cast<_AlphaMode>(alphaModeValue);
    const float authoredAlpha = _Clamp01(alpha);
    if (alphaMode == _AlphaMode::Opaque) {
        c.opacity = 1.0f;
        c.presence = 1.0f;
    } else if (alphaMode == _AlphaMode::Mask) {
        const float masked = authoredAlpha >= alphaCutoff ? 1.0f : 0.0f;
        c.opacity = masked;
        c.presence = masked;
    } else {
        c.opacity = authoredAlpha;
        c.presence = authoredAlpha;
    }

    Bsdf::ClosureTree tree;
    const Vec2f baseRoughness = _ComputeBaseRoughness(roughness, anisotropyStrength);

    Bsdf::OrenNayarDiffuseData diffuse;
    diffuse.color = baseColor;
    const Bsdf::NodeId diffuseId = tree.Add(diffuse);

    Bsdf::DielectricData transmissionData;
    transmissionData.weight = 1.0f;
    transmissionData.tint = baseColor;
    transmissionData.ior = std::max(ior, 1.0f);
    // KHR_materials_dispersion stores dispersion strength as 20 / Abbe.
    // Zero disables wavelength-dependent refraction.
    transmissionData.dispersionAbbe =
        dispersion > 0.0f ? 20.0f / dispersion : 0.0f;
    transmissionData.roughness = baseRoughness;
    transmissionData.tangent = mainTangent;
    transmissionData.scatterMode = Bsdf::ScatterMode::Transmission;

    Bsdf::MixData transmissionMix;
    transmissionMix.bg = diffuseId;
    transmissionMix.fg = tree.Add(transmissionData);
    transmissionMix.mix = _Clamp01(transmission);
    const Bsdf::NodeId transmissionMixId = tree.Add(transmissionMix);

    const float oneMinusIor = 1.0f - ior;
    const float onePlusIor = 1.0f + ior;
    const float dielectricF0Scalar =
        (oneMinusIor * oneMinusIor) / std::max(onePlusIor * onePlusIor, 1.0e-5f);
    const Vec3f dielectricF0 =
        _Saturate(specularColor * dielectricF0Scalar) * _Clamp01(specular);
    const Vec3f dielectricF90 = Vec3f(_Clamp01(specular));

    Bsdf::GeneralizedSchlickData reflection;
    reflection.color0 = dielectricF0;
    reflection.color82 = Vec3f(1.0f);
    reflection.color90 = dielectricF90;
    reflection.roughness = baseRoughness;
    reflection.tangent = mainTangent;
    reflection.scatterMode = Bsdf::ScatterMode::Reflection;
    reflection.thinFilmWeight = _Clamp01(iridescence);
    reflection.thinFilmIor = iridescenceIor;
    reflection.thinFilmThickness = iridescenceThickness;

    const Bsdf::NodeId reflectionId = tree.Add(reflection);

    Bsdf::LayerData dielectricLayer;
    dielectricLayer.top = reflectionId;
    dielectricLayer.base = transmissionMixId;
    const Bsdf::NodeId dielectricLayerId = tree.Add(dielectricLayer);

    Bsdf::GeneralizedSchlickData metal;
    metal.color0 = baseColor;
    metal.color82 = Vec3f(1.0f);
    metal.color90 = Vec3f(1.0f);
    metal.roughness = baseRoughness;
    metal.tangent = mainTangent;
    metal.thinFilmWeight = _Clamp01(iridescence);
    metal.thinFilmIor = iridescenceIor;
    metal.thinFilmThickness = iridescenceThickness;
    const Bsdf::NodeId metalBranchId = tree.Add(metal);

    Bsdf::MixData baseMix;
    baseMix.bg = dielectricLayerId;
    baseMix.fg = metalBranchId;
    baseMix.mix = _Clamp01(metallic);
    Bsdf::NodeId root = tree.Add(baseMix);

    const float sheenIntensity = _Clamp01(_MaxComponent(sheenColor));
    if (sheenIntensity > 0.0f) {
        Bsdf::SheenData sheen;
        sheen.weight = sheenIntensity;
        sheen.color = sheenColor / sheenIntensity;
        sheen.roughness = sheenRoughness * sheenRoughness;
        root = _AppendLayer(&tree, tree.Add(sheen), root);
    }

    if (_Clamp01(clearcoat) > 0.0f) {
        Bsdf::DielectricData clearcoatData;
        clearcoatData.weight = _Clamp01(clearcoat);
        clearcoatData.ior = 1.5f;
        clearcoatData.roughness = _ComputeIsotropicRoughness(clearcoatRoughness);
        clearcoatData.tangent = tangent;
        clearcoatData.normal = clearcoatNormal;
        clearcoatData.hasShadingNormal =
            params.Find(_kClearcoatNormal) || params.Find(_kNormal);
        root = _AppendLayer(&tree, tree.Add(clearcoatData), root);
    }

    tree.root = root;
    c.bsdfTree = std::move(tree);
    return c;
}

}  // namespace mxcpp
