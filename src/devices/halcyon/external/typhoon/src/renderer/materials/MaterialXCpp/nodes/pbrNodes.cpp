//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pbrNodes.h"

#include <renderer/materials/MaterialXCpp/nodeRegistry.h>
#include <renderer/materials/MaterialXCpp/nodes/helpers/spaceHelpers.h>
#include <renderer/materials/MaterialXCpp/paramMap.h>
#include <renderer/materials/MaterialXCpp/surfaceShaderUtils.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace mxcpp {

namespace {

const SlotName _kOut("out");
const SlotName _kWeight("weight");
const SlotName _kColor("color");
const SlotName _kColor0("color0");
const SlotName _kColor82("color82");
const SlotName _kColor90("color90");
const SlotName _kTint("tint");
const SlotName _kIor("ior");
const SlotName _kExtinction("extinction");
const SlotName _kExponent("exponent");
const SlotName _kGlossiness("glossiness");
const SlotName _kRoughness("roughness");
const SlotName _kRetroreflective("retroreflective");
const SlotName _kThinfilmThickness("thinfilm_thickness");
const SlotName _kThinfilmIor("thinfilm_ior");
const SlotName _kNormal("normal");
const SlotName _kTangent("tangent");
const SlotName _kScatterMode("scatter_mode");
const SlotName _kEnergyCompensation("energy_compensation");
const SlotName _kRadius("radius");
const SlotName _kAbsorption("absorption");
const SlotName _kScattering("scattering");
const SlotName _kAnisotropy("anisotropy");
const SlotName _kMode("mode");
const SlotName _kTop("top");
const SlotName _kBase("base");

static constexpr float _kFloatEps = 1e-6f;
static constexpr float _kMaterialXFloatEps = 1e-8f;

BsdfClosure
_MakeClosure(Bsdf::NodeData data)
{
    BsdfClosure closure;
    closure.tree.root = closure.tree.Add(std::move(data));
    return closure;
}

Bsdf::ScatterMode
_GetScatterMode(const ParamMap& inputs)
{
    const std::string mode = Get<std::string>(
        inputs, _kScatterMode, std::string("R"));
    if (mode == "T") {
        return Bsdf::ScatterMode::Transmission;
    }
    if (mode == "RT") {
        return Bsdf::ScatterMode::ReflectionTransmission;
    }
    return Bsdf::ScatterMode::Reflection;
}

template<typename T>
void
_ReadOptionalNormal(const ParamMap& inputs, T* data)
{
    if (inputs.Find(_kNormal)) {
        data->normal = Get<Vec3f>(
            inputs, _kNormal, Vec3f(0.0f, 0.0f, 1.0f));
        data->hasShadingNormal = true;
    }
}

Vec3f
_NormalizeOrZero(const Vec3f& value)
{
    if (value.length2() <= _kFloatEps * _kFloatEps) {
        return Vec3f(0.0f);
    }

    Vec3f result = value;
    result.normalize();
    return result;
}

Vec3f
_ComputeWorldPosition(const ShadingContext& ctx)
{
    Vec3f worldPosition = ctx.position;
    TransformNamedVec3(
        ctx, "object", "world",
        ShadingContext::TransformSpaceType::Point,
        ctx.position, &worldPosition);
    return worldPosition;
}

Vec3f
_ComputeWorldViewVector(const ShadingContext& ctx)
{
    return _NormalizeOrZero(ctx.viewPosition - _ComputeWorldPosition(ctx));
}

Vec3f
_GeneralizedSchlickEdfFactor(
    const Vec3f& color0,
    const Vec3f& color90,
    float exponent,
    float cosTheta)
{
    const float x = std::clamp(1.0f - cosTheta, 0.0f, 1.0f);
    return color0 + (color90 - color0) * std::pow(x, exponent);
}

float
_MaterialXRoughnessAlpha(float roughness)
{
    return std::clamp(
        roughness * roughness, _kMaterialXFloatEps, 1.0f);
}

Vec2f
_RoughnessAnisotropy(float roughness, float anisotropy)
{
    const float roughnessSqr = _MaterialXRoughnessAlpha(roughness);
    if (anisotropy <= 0.0f) {
        return Vec2f(roughnessSqr, roughnessSqr);
    }

    const float aspect = std::sqrt(
        1.0f - std::clamp(anisotropy, 0.0f, 0.98f));
    return Vec2f(
        std::min(roughnessSqr / aspect, 1.0f),
        roughnessSqr * aspect);
}

BsdfClosure
_MakeLayerClosure(
    const BsdfClosure& topClosure,
    const BsdfClosure& baseClosure)
{
    BsdfClosure closure;
    const auto applyInteriorMedium = [&]() {
        if (baseClosure.hasInteriorMedium) {
            closure.hasInteriorMedium = true;
            closure.interiorMedium = baseClosure.interiorMedium;
        } else if (topClosure.hasInteriorMedium) {
            closure.hasInteriorMedium = true;
            closure.interiorMedium = topClosure.interiorMedium;
        }
    };

    const Bsdf::NodeId top =
        AppendClosureTree(&closure.tree, topClosure.tree);
    const Bsdf::NodeId base =
        AppendClosureTree(&closure.tree, baseClosure.tree);

    if (!closure.tree.IsValid(top)) {
        closure.tree.root = base;
        applyInteriorMedium();
        return closure;
    }
    if (!closure.tree.IsValid(base)) {
        closure.tree.root = top;
        applyInteriorMedium();
        return closure;
    }

    Bsdf::LayerData layer;
    layer.top = top;
    layer.base = base;
    closure.tree.root = closure.tree.Add(layer);
    applyInteriorMedium();
    return closure;
}

BsdfClosure
_MakeLayerClosure(
    const BsdfClosure& topClosure,
    const VdfClosure& baseClosure)
{
    BsdfClosure closure;
    closure.tree = topClosure.tree;
    closure.hasInteriorMedium = topClosure.hasInteriorMedium;
    closure.interiorMedium = topClosure.interiorMedium;
    if (!baseClosure.medium.IsVacuum()) {
        closure.hasInteriorMedium = true;
        closure.interiorMedium = baseClosure.medium;
    }
    return closure;
}

void
_EvalOrenNayarDiffuseBsdf(
    const ParamMap& inputs,
    const ShadingContext&,
    NodeOutputMap* outputs)
{
    Bsdf::OrenNayarDiffuseData data;
    data.weight = Get<float>(inputs, _kWeight, 1.0f);
    data.color = Get<Vec3f>(inputs, _kColor, Vec3f(0.18f));
    data.roughness = Get<float>(inputs, _kRoughness, 0.0f);
    data.energyCompensation = Get<bool>(inputs, _kEnergyCompensation, false);
    _ReadOptionalNormal(inputs, &data);
    (*outputs)[_kOut] = Value(_MakeClosure(data));
}

void
_EvalBurleyDiffuseBsdf(
    const ParamMap& inputs,
    const ShadingContext&,
    NodeOutputMap* outputs)
{
    Bsdf::BurleyDiffuseData data;
    data.weight = Get<float>(inputs, _kWeight, 1.0f);
    data.color = Get<Vec3f>(inputs, _kColor, Vec3f(0.18f));
    data.roughness = Get<float>(inputs, _kRoughness, 0.0f);
    _ReadOptionalNormal(inputs, &data);
    (*outputs)[_kOut] = Value(_MakeClosure(data));
}

void
_EvalTranslucentBsdf(
    const ParamMap& inputs,
    const ShadingContext&,
    NodeOutputMap* outputs)
{
    Bsdf::TranslucentData data;
    data.weight = Get<float>(inputs, _kWeight, 1.0f);
    data.color = Get<Vec3f>(inputs, _kColor, Vec3f(1.0f));
    _ReadOptionalNormal(inputs, &data);
    (*outputs)[_kOut] = Value(_MakeClosure(data));
}

void
_EvalSubsurfaceBsdf(
    const ParamMap& inputs,
    const ShadingContext&,
    NodeOutputMap* outputs)
{
    Bsdf::SubsurfaceData data;
    data.weight = Get<float>(inputs, _kWeight, 1.0f);
    data.color = Get<Vec3f>(inputs, _kColor, Vec3f(0.18f));
    data.radius = Get<Vec3f>(inputs, _kRadius, Vec3f(1.0f));
    data.anisotropy = Get<float>(inputs, _kAnisotropy, 0.0f);
    _ReadOptionalNormal(inputs, &data);
    (*outputs)[_kOut] = Value(_MakeClosure(data));
}

void
_EvalRoughnessAnisotropy(
    const ParamMap& inputs,
    const ShadingContext&,
    NodeOutputMap* outputs)
{
    (*outputs)[_kOut] = Value(_RoughnessAnisotropy(
        Get<float>(inputs, _kRoughness, 0.0f),
        Get<float>(inputs, _kAnisotropy, 0.0f)));
}

void
_EvalRoughnessDual(
    const ParamMap& inputs,
    const ShadingContext&,
    NodeOutputMap* outputs)
{
    Vec2f roughness = Get<Vec2f>(inputs, _kRoughness, Vec2f(0.0f));
    if (roughness[1] < 0.0f) {
        roughness[1] = roughness[0];
    }
    (*outputs)[_kOut] = Value(Vec2f(
        _MaterialXRoughnessAlpha(roughness[0]),
        _MaterialXRoughnessAlpha(roughness[1])));
}

void
_EvalGlossinessAnisotropy(
    const ParamMap& inputs,
    const ShadingContext&,
    NodeOutputMap* outputs)
{
    const float glossiness = Get<float>(inputs, _kGlossiness, 1.0f);
    (*outputs)[_kOut] = Value(_RoughnessAnisotropy(
        1.0f - glossiness,
        Get<float>(inputs, _kAnisotropy, 0.0f)));
}

void
_EvalAbsorptionVdf(
    const ParamMap& inputs,
    const ShadingContext&,
    NodeOutputMap* outputs)
{
    VdfClosure closure;
    closure.medium.absorption = Get<Vec3f>(inputs, _kAbsorption, Vec3f(0.0f));
    (*outputs)[_kOut] = Value(closure);
}

void
_EvalAnisotropicVdf(
    const ParamMap& inputs,
    const ShadingContext&,
    NodeOutputMap* outputs)
{
    VdfClosure closure;
    closure.medium.absorption = Get<Vec3f>(inputs, _kAbsorption, Vec3f(0.0f));
    closure.medium.scattering = Get<Vec3f>(inputs, _kScattering, Vec3f(0.0f));
    closure.medium.anisotropy = Get<float>(inputs, _kAnisotropy, 0.0f);
    (*outputs)[_kOut] = Value(closure);
}

void
_EvalSheenBsdf(
    const ParamMap& inputs,
    const ShadingContext&,
    NodeOutputMap* outputs)
{
    Bsdf::SheenData data;
    data.weight = Get<float>(inputs, _kWeight, 1.0f);
    data.color = Get<Vec3f>(inputs, _kColor, Vec3f(1.0f));
    data.roughness = Get<float>(inputs, _kRoughness, 0.3f);

    const std::string mode = Get<std::string>(
        inputs, _kMode, std::string("conty_kulla"));
    data.mode = mode == "zeltner"
        ? Bsdf::SheenMode::Zeltner
        : Bsdf::SheenMode::ContyKulla;
    _ReadOptionalNormal(inputs, &data);

    (*outputs)[_kOut] = Value(_MakeClosure(data));
}

void
_EvalDielectricBsdf(
    const ParamMap& inputs,
    const ShadingContext&,
    NodeOutputMap* outputs)
{
    Bsdf::DielectricData data;
    data.weight = Get<float>(inputs, _kWeight, 1.0f);
    data.tint = Get<Vec3f>(inputs, _kTint, Vec3f(1.0f));
    data.ior = Get<float>(inputs, _kIor, 1.5f);
    data.roughness = Get<Vec2f>(inputs, _kRoughness, Vec2f(0.05f, 0.05f));
    data.retroreflective = Get<bool>(inputs, _kRetroreflective, false);
    data.thinFilmThickness = Get<float>(inputs, _kThinfilmThickness, 0.0f);
    data.thinFilmIor = Get<float>(inputs, _kThinfilmIor, 1.5f);
    data.tangent = Get<Vec3f>(inputs, _kTangent, Vec3f(1.0f, 0.0f, 0.0f));
    data.scatterMode = _GetScatterMode(inputs);
    _ReadOptionalNormal(inputs, &data);

    (*outputs)[_kOut] = Value(_MakeClosure(data));
}

void
_EvalConductorBsdf(
    const ParamMap& inputs,
    const ShadingContext&,
    NodeOutputMap* outputs)
{
    Bsdf::ConductorData data;
    data.weight = Get<float>(inputs, _kWeight, 1.0f);
    data.ior = Get<Vec3f>(inputs, _kIor, Vec3f(0.183f, 0.421f, 1.373f));
    data.extinction =
        Get<Vec3f>(inputs, _kExtinction, Vec3f(3.424f, 2.346f, 1.770f));
    data.roughness = Get<Vec2f>(inputs, _kRoughness, Vec2f(0.05f, 0.05f));
    data.retroreflective = Get<bool>(inputs, _kRetroreflective, false);
    data.thinFilmThickness = Get<float>(inputs, _kThinfilmThickness, 0.0f);
    data.thinFilmIor = Get<float>(inputs, _kThinfilmIor, 1.5f);
    data.tangent = Get<Vec3f>(inputs, _kTangent, Vec3f(1.0f, 0.0f, 0.0f));
    _ReadOptionalNormal(inputs, &data);

    (*outputs)[_kOut] = Value(_MakeClosure(data));
}

void
_EvalGeneralizedSchlickBsdf(
    const ParamMap& inputs,
    const ShadingContext&,
    NodeOutputMap* outputs)
{
    Bsdf::GeneralizedSchlickData data;
    data.weight = Get<float>(inputs, _kWeight, 1.0f);
    data.color0 = Get<Vec3f>(inputs, _kColor0, Vec3f(1.0f));
    data.color82 = Get<Vec3f>(inputs, _kColor82, Vec3f(1.0f));
    data.color90 = Get<Vec3f>(inputs, _kColor90, Vec3f(1.0f));
    data.exponent = Get<float>(inputs, _kExponent, 5.0f);
    data.roughness = Get<Vec2f>(inputs, _kRoughness, Vec2f(0.05f, 0.05f));
    data.retroreflective = Get<bool>(inputs, _kRetroreflective, false);
    data.thinFilmThickness = Get<float>(inputs, _kThinfilmThickness, 0.0f);
    data.thinFilmIor = Get<float>(inputs, _kThinfilmIor, 1.5f);
    data.tangent = Get<Vec3f>(inputs, _kTangent, Vec3f(1.0f, 0.0f, 0.0f));
    data.scatterMode = _GetScatterMode(inputs);
    _ReadOptionalNormal(inputs, &data);

    (*outputs)[_kOut] = Value(_MakeClosure(data));
}

void
_EvalGeneralizedSchlickEdf(
    const ParamMap& inputs,
    const ShadingContext& ctx,
    NodeOutputMap* outputs)
{
    UniformEdf base = Get<UniformEdf>(
        inputs, _kBase, UniformEdf{Vec3f(0.0f)});
    const Vec3f color0 = Get<Vec3f>(inputs, _kColor0, Vec3f(1.0f));
    const Vec3f color90 = Get<Vec3f>(inputs, _kColor90, Vec3f(1.0f));
    const float exponent = Get<float>(inputs, _kExponent, 5.0f);

    Vec3f viewVector = _ComputeWorldViewVector(ctx);
    if (viewVector.length2() <= _kFloatEps * _kFloatEps) {
        viewVector = Vec3f(0.0f, 0.0f, 1.0f);
    }

    Vec3f normal = _NormalizeOrZero(ctx.normal);
    if (normal.length2() <= _kFloatEps * _kFloatEps) {
        normal = Vec3f(0.0f, 0.0f, 1.0f);
    }
    if (normal.dot(viewVector) < 0.0f) {
        normal = -normal;
    }

    const float nDotV = std::clamp(
        normal.dot(viewVector), _kFloatEps, 1.0f);
    const Vec3f factor = _GeneralizedSchlickEdfFactor(
        color0, color90, exponent, nDotV);
    base.emittance = CompMul(base.emittance, factor);
    (*outputs)[_kOut] = Value(base);
}

void
_EvalLayerBsdf(
    const ParamMap& inputs,
    const ShadingContext&,
    NodeOutputMap* outputs)
{
    const BsdfClosure top = Get<BsdfClosure>(inputs, _kTop, BsdfClosure{});
    const BsdfClosure base = Get<BsdfClosure>(inputs, _kBase, BsdfClosure{});
    (*outputs)[_kOut] = Value(_MakeLayerClosure(top, base));
}

void
_EvalLayerVdf(
    const ParamMap& inputs,
    const ShadingContext&,
    NodeOutputMap* outputs)
{
    const BsdfClosure top = Get<BsdfClosure>(inputs, _kTop, BsdfClosure{});
    const VdfClosure base = Get<VdfClosure>(inputs, _kBase, VdfClosure{});
    (*outputs)[_kOut] = Value(_MakeLayerClosure(top, base));
}

void
_EvalChiangHairBsdf(
    const ParamMap&,
    const ShadingContext&,
    NodeOutputMap* outputs)
{
    Bsdf::UnsupportedData data;
    data.kind = Bsdf::UnsupportedNodeKind::ChiangHair;
    (*outputs)[_kOut] = Value(_MakeClosure(data));
}

}  // namespace

#define _REG(name, fn) reg.Register(name, fn)

void
RegisterPbrNodes(NodeRegistry& reg)
{
    _REG("ND_oren_nayar_diffuse_bsdf", &_EvalOrenNayarDiffuseBsdf);
    _REG("ND_burley_diffuse_bsdf", &_EvalBurleyDiffuseBsdf);
    _REG("ND_translucent_bsdf", &_EvalTranslucentBsdf);
    _REG("ND_subsurface_bsdf", &_EvalSubsurfaceBsdf);
    _REG("ND_roughness_anisotropy", &_EvalRoughnessAnisotropy);
    _REG("ND_roughness_dual", &_EvalRoughnessDual);
    _REG("ND_glossiness_anisotropy", &_EvalGlossinessAnisotropy);
    _REG("ND_absorption_vdf", &_EvalAbsorptionVdf);
    _REG("ND_anisotropic_vdf", &_EvalAnisotropicVdf);
    _REG("ND_sheen_bsdf", &_EvalSheenBsdf);
    _REG("ND_dielectric_bsdf", &_EvalDielectricBsdf);
    _REG("ND_conductor_bsdf", &_EvalConductorBsdf);
    _REG("ND_generalized_schlick_bsdf", &_EvalGeneralizedSchlickBsdf);
    _REG("ND_generalized_schlick_edf", &_EvalGeneralizedSchlickEdf);
    _REG("ND_layer_bsdf", &_EvalLayerBsdf);
    _REG("ND_layer_vdf", &_EvalLayerVdf);
    _REG("ND_chiang_hair_bsdf", &_EvalChiangHairBsdf);
}

#undef _REG

}  // namespace mxcpp
