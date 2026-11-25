//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "usdPreviewSurface.h"

#include <renderer/materials/MaterialXCpp/paramMap.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace mxcpp {

static const SlotName _kDiffuseColor("diffuseColor");
static const SlotName _kEmissiveColor("emissiveColor");
static const SlotName _kUseSpecularWorkflow("useSpecularWorkflow");
static const SlotName _kSpecularColor("specularColor");
static const SlotName _kMetallic("metallic");
static const SlotName _kRoughness("roughness");
static const SlotName _kClearcoat("clearcoat");
static const SlotName _kClearcoatRoughness("clearcoatRoughness");
static const SlotName _kOpacity("opacity");
static const SlotName _kOpacityMode("opacityMode");
static const SlotName _kOpacityThreshold("opacityThreshold");
static const SlotName _kIor("ior");
static const SlotName _kNormal("normal");
static const SlotName _kDisplacement("displacement");
static const SlotName _kOcclusion("occlusion");

namespace {

enum class _OpacityMode {
    Transparent,
    Presence
};

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

Vec2f
_ComputeIsotropicAlpha(float roughness)
{
    const float clampedRoughness = _ClampRoughness(roughness);
    const float alpha = std::clamp(clampedRoughness * clampedRoughness,
                                   1.0e-5f, 1.0f);
    return Vec2f(alpha, alpha);
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
_Lerp(const Vec3f& a, const Vec3f& b, float t)
{
    return a * (1.0f - t) + b * t;
}

float
_DielectricF0FromIor(float ior)
{
    const float denom = std::max(std::abs(ior + 1.0f), 1.0e-6f);
    float f0 = (1.0f - ior) / denom;
    return f0 * f0;
}

Bsdf::NodeId
_AppendAdd(Bsdf::ClosureTree* tree, Bsdf::NodeId lhs, Bsdf::NodeId rhs)
{
    if (!tree->IsValid(lhs)) {
        return rhs;
    }
    if (!tree->IsValid(rhs)) {
        return lhs;
    }

    Bsdf::AddData add;
    add.in1 = lhs;
    add.in2 = rhs;
    return tree->Add(add);
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
_AppendMultiply(Bsdf::ClosureTree* tree, Bsdf::NodeId input, const Vec3f& weight)
{
    if (!tree->IsValid(input)) {
        return input;
    }

    const Vec3f saturatedWeight = _Saturate(weight);
    if (saturatedWeight[0] >= 1.0f &&
        saturatedWeight[1] >= 1.0f &&
        saturatedWeight[2] >= 1.0f) {
        return input;
    }

    Bsdf::MultiplyData multiply;
    multiply.input = input;
    multiply.weight = saturatedWeight;
    return tree->Add(multiply);
}

_OpacityMode
_GetOpacityMode(const ParamMap& params)
{
    const Value* const value = params.Find(_kOpacityMode);
    if (!value) {
        return _OpacityMode::Transparent;
    }

    if (ValueHolds<int>(*value)) {
        return ValueGet<int>(*value) == 1
            ? _OpacityMode::Presence
            : _OpacityMode::Transparent;
    }

    if (ValueHolds<std::string>(*value)) {
        return ValueGet<std::string>(*value) == "presence"
            ? _OpacityMode::Presence
            : _OpacityMode::Transparent;
    }

    return _OpacityMode::Transparent;
}

}  // namespace

SurfaceClosure
EvalUsdPreviewSurface(const ParamMap& params)
{
    SurfaceClosure c;

    c.baseColor = Get<Vec3f>(params, _kDiffuseColor, Vec3f(0.18f));
    c.emissiveColor = Get<Vec3f>(params, _kEmissiveColor, Vec3f(0.0f));

    const int useSpecWf = Get<int>(params, _kUseSpecularWorkflow, 0);
    if (useSpecWf) {
        c.specularColor = Get<Vec3f>(params, _kSpecularColor, Vec3f(0.0f));
        c.metallic = 0.0f;
        c.specular = 1.0f;
    } else {
        c.metallic = Get<float>(params, _kMetallic, 0.0f);
        c.specularColor = Vec3f(1.0f);
        c.specular = 1.0f;
    }

    c.roughness = Get<float>(params, _kRoughness, 0.5f);
    c.coat = Get<float>(params, _kClearcoat, 0.0f);
    c.coatRoughness = Get<float>(params, _kClearcoatRoughness, 0.01f);
    c.specularIor = Get<float>(params, _kIor, 1.5f);
    c.coatIor = c.specularIor;

    const float authoredOpacity =
        std::clamp(Get<float>(params, _kOpacity, 1.0f), 0.0f, 1.0f);
    const float opacityThreshold = Get<float>(params, _kOpacityThreshold, 0.0f);
    const bool hasCutoutThreshold = opacityThreshold > 0.0f;
    const float cutoutOpacity =
        authoredOpacity >= opacityThreshold ? 1.0f : 0.0f;
    const _OpacityMode opacityMode = _GetOpacityMode(params);

    if (hasCutoutThreshold) {
        c.opacity = cutoutOpacity;
        c.presence = cutoutOpacity;
        c.transmission = 0.0f;
    } else if (opacityMode == _OpacityMode::Presence) {
        c.opacity = authoredOpacity;
        c.presence = authoredOpacity;
        c.transmission = 0.0f;
    } else {
        c.opacity = authoredOpacity;
        c.presence = 1.0f;
        c.transmission = 1.0f - authoredOpacity;
    }

    c.normal = Get<Vec3f>(params, _kNormal, Vec3f(0.0f, 0.0f, 1.0f));
    c.normalSpace = params.Find(_kNormal)
        ? SurfaceNormalSpace::Tangent
        : SurfaceNormalSpace::None;

    c.transmissionColor = Vec3f(1.0f);
    c.sheen = 0.0f;
    c.thinWalled = false;

    Bsdf::ClosureTree tree;
    Bsdf::NodeId root = Bsdf::InvalidNodeId;

    // Metalness-workflow transparency: reflectivity, refraction, and total
    // internal reflection are all governed by the single authored ior, which
    // is exactly the coupled dielectric interface's parameterization.  Build
    // the same interface-over-attenuated-base structure OpenPBR uses so
    // transparent UsdPreviewSurface and OpenPBR shade identically.  The
    // specular workflow (arbitrary specularColor F0 decoupled from ior) and
    // metallic transparency have no such physical mapping and stay on the
    // paired Schlick-reflection + transmission closure below.
    const bool useDielectricInterface =
        !useSpecWf && c.transmission > 0.0f && _Clamp01(c.metallic) <= 0.0f;
    if (useDielectricInterface) {
        Bsdf::NodeId substrate = Bsdf::InvalidNodeId;
        const float substrateWeight = _Clamp01(1.0f - c.transmission);
        if (substrateWeight > 0.0f) {
            Bsdf::OrenNayarDiffuseData diffuse;
            diffuse.weight = 1.0f;
            diffuse.color = c.baseColor;
            diffuse.roughness = 0.0f;
            substrate = _AppendMultiply(
                &tree, tree.Add(diffuse), Vec3f(substrateWeight));
        }

        Bsdf::DielectricInterfaceData interface;
        interface.reflectionWeight = 1.0f;
        interface.reflectionTint = Vec3f(1.0f);
        interface.transmissionWeight = _Clamp01(c.transmission);
        interface.transmissionTint = _Saturate(c.transmissionColor);
        interface.ior = std::max(c.specularIor, 1.0f);
        interface.roughness = _ComputeIsotropicAlpha(c.roughness);
        interface.compensateCoupledDielectric = true;
        root = _AppendLayer(&tree, tree.Add(interface), substrate);

        if (c.coat > 0.0f) {
            Bsdf::DielectricData coat;
            coat.weight = _Clamp01(c.coat);
            coat.tint = Vec3f(1.0f);
            coat.ior = c.coatIor;
            coat.roughness = _ComputeIsotropicAlpha(c.coatRoughness);
            coat.scatterMode = Bsdf::ScatterMode::Reflection;
            root = _AppendLayer(&tree, tree.Add(coat), root);
        }

        tree.root = root;
        c.bsdfTree = std::move(tree);
        return c;
    }

    const float diffuseWeight =
        _Clamp01((1.0f - c.metallic) * (1.0f - c.transmission));
    if (diffuseWeight > 0.0f) {
        Bsdf::OrenNayarDiffuseData diffuse;
        diffuse.weight = diffuseWeight;
        diffuse.color = c.baseColor;
        diffuse.roughness = 0.0f;
        root = _AppendAdd(&tree, root, tree.Add(diffuse));
    }

    const Vec2f specularRoughness(
        _ClampRoughness(c.roughness),
        _ClampRoughness(c.roughness));
    if (useSpecWf) {
        Bsdf::GeneralizedSchlickData specular;
        specular.weight = 1.0f;
        specular.color0 = _Saturate(c.specularColor);
        specular.color82 = Vec3f(1.0f);
        specular.color90 = Vec3f(1.0f);
        specular.exponent = 5.0f;
        specular.roughness = specularRoughness;
        specular.scatterMode = Bsdf::ScatterMode::Reflection;
        root = _AppendAdd(&tree, root, tree.Add(specular));
    } else {
        const float metallic = _Clamp01(c.metallic);
        const Vec3f albedo = _Saturate(c.baseColor);
        const Vec3f dielectricF0(_DielectricF0FromIor(c.specularIor));

        Bsdf::GeneralizedSchlickData specular;
        specular.weight = 1.0f;
        specular.color0 = _Lerp(dielectricF0, albedo, metallic);
        specular.color82 = Vec3f(1.0f);
        specular.color90 = _Lerp(Vec3f(1.0f), albedo, metallic);
        specular.exponent = 5.0f;
        specular.roughness = specularRoughness;
        specular.scatterMode = Bsdf::ScatterMode::Reflection;
        root = _AppendAdd(&tree, root, tree.Add(specular));
    }

    if (c.transmission > 0.0f) {
        Bsdf::DielectricData transmission;
        transmission.weight = _Clamp01(c.transmission);
        transmission.tint = c.transmissionColor;
        transmission.ior = std::max(c.specularIor, 1.0f);
        transmission.roughness = specularRoughness;
        transmission.scatterMode = Bsdf::ScatterMode::Transmission;
        root = _AppendAdd(&tree, root, tree.Add(transmission));
    }

    if (c.coat > 0.0f) {
        Bsdf::DielectricData coat;
        coat.weight = _Clamp01(c.coat);
        coat.tint = Vec3f(1.0f);
        coat.ior = c.coatIor;
        coat.roughness = _ComputeIsotropicAlpha(c.coatRoughness);
        coat.scatterMode = Bsdf::ScatterMode::Reflection;
        root = _AppendLayer(&tree, tree.Add(coat), root);
    }

    tree.root = root;
    c.bsdfTree = std::move(tree);

    return c;
}

}  // namespace mxcpp
