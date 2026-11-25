//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "geometricNodes.h"

#include <renderer/materials/MaterialXCpp/nodeRegistry.h>
#include <renderer/materials/MaterialXCpp/nodes/helpers/inputEvaluationHelpers.h>
#include <renderer/materials/MaterialXCpp/nodes/helpers/shadingContextHelpers.h>
#include <renderer/materials/MaterialXCpp/nodes/helpers/spaceHelpers.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace mxcpp {

static constexpr float _kFloatEps = 1e-6f;

static const SlotName _kIn("in");
static const SlotName _kHeight("height");
static const SlotName _kScale("scale");
static const SlotName _kTexcoord("texcoord");
static const SlotName _kNormal("normal");
static const SlotName _kTangent("tangent");
static const SlotName _kBitangent("bitangent");
static const SlotName _kOut("out");
static const SlotName _kGeomprop("geomprop");
static const SlotName _kDefault("default");
static const SlotName _kSpace("space");

static std::string
_GetSpace(const ParamMap& inputs)
{
    return NormalizeSpaceName(
        Get<std::string>(inputs, _kSpace, std::string("object")),
        std::string("object"));
}

static float
_SelectFiniteDifferenceStep(float dx, float dy)
{
    float step = 0.5f * (std::abs(dx) + std::abs(dy));
    if (step < 5.0e-4f) {
        step = 5.0e-4f;
    }
    return step;
}

static void
_BuildOrthonormalBasis(const Vec3f& normal,
                       const Vec3f& tangent,
                       const Vec3f& bitangent,
                       Vec3f* outNormal,
                       Vec3f* outTangent,
                       Vec3f* outBitangent)
{
    Vec3f n = normal;
    if (Dot(n, n) < _kFloatEps * _kFloatEps) {
        n = Vec3f(0.0f, 0.0f, 1.0f);
    } else {
        n.normalize();
    }

    Vec3f t = tangent - n * Dot(tangent, n);
    if (Dot(t, t) < _kFloatEps * _kFloatEps) {
        t = Cross(Vec3f(0.0f, 1.0f, 0.0f), n);
        if (Dot(t, t) < _kFloatEps * _kFloatEps) {
            t = Cross(Vec3f(1.0f, 0.0f, 0.0f), n);
        }
    }
    t.normalize();

    Vec3f b = bitangent - n * Dot(bitangent, n) - t * Dot(bitangent, t);
    if (Dot(b, b) < _kFloatEps * _kFloatEps) {
        b = Cross(n, t);
    }
    b.normalize();

    if (outNormal) {
        *outNormal = n;
    }
    if (outTangent) {
        *outTangent = t;
    }
    if (outBitangent) {
        *outBitangent = b;
    }
}

// Geometric nodes read from the shading context.

static void
_EvalPosition(const ParamMap& inputs, const ShadingContext& ctx,
              NodeOutputMap* outputs)
{
    const std::string space = _GetSpace(inputs);
    Vec3f result = ctx.position;
    TransformNamedVec3(
        ctx, "object", space,
        ShadingContext::TransformSpaceType::Point,
        ctx.position, &result);
    (*outputs)[_kOut] = Value(result);
}

static void
_EvalNormal(const ParamMap& inputs, const ShadingContext& ctx,
            NodeOutputMap* outputs)
{
    const std::string space = _GetSpace(inputs);
    Vec3f result = ctx.normal;
    TransformNamedVec3(
        ctx, "world", space,
        ShadingContext::TransformSpaceType::Normal,
        ctx.normal, &result);
    (*outputs)[_kOut] = Value(result);
}

static void
_EvalTangent(const ParamMap& inputs, const ShadingContext& ctx,
             NodeOutputMap* outputs)
{
    const std::string space = _GetSpace(inputs);
    // MaterialX hardware backends interpolate the vertex tangent into the
    // shading point. Use the renderer's matching interpolated frame instead
    // of the triangle-constant position derivative.
    Vec3f worldTangent = ctx.tangent;
    if (Dot(worldTangent, worldTangent) < _kFloatEps * _kFloatEps) {
        worldTangent = ctx.dPdu;
    }
    Vec3f result = worldTangent;
    TransformNamedVec3(
        ctx, "world", space,
        ShadingContext::TransformSpaceType::Vector,
        worldTangent, &result);
    if (Dot(result, result) > _kFloatEps * _kFloatEps) {
        result.normalize();
    }
    (*outputs)[_kOut] = Value(result);
}

static void
_EvalBitangent(const ParamMap& inputs, const ShadingContext& ctx,
               NodeOutputMap* outputs)
{
    const std::string space = _GetSpace(inputs);
    // Keep bitangent evaluation on the same interpolated shading frame as
    // tangent evaluation. The derivative is only a degenerate-frame fallback.
    Vec3f worldBitangent = ctx.bitangent;
    if (Dot(worldBitangent, worldBitangent) < _kFloatEps * _kFloatEps) {
        worldBitangent = ctx.dPdv;
    }
    Vec3f result = worldBitangent;
    TransformNamedVec3(
        ctx, "world", space,
        ShadingContext::TransformSpaceType::Vector,
        worldBitangent, &result);
    if (Dot(result, result) > _kFloatEps * _kFloatEps) {
        result.normalize();
    }
    (*outputs)[_kOut] = Value(result);
}

static void
_EvalTexcoordVector2(const ParamMap&, const ShadingContext& ctx,
                     NodeOutputMap* outputs)
{
    (*outputs)[_kOut] = Value(ctx.texcoord);
}

static void
_EvalTexcoordVector3(const ParamMap&, const ShadingContext& ctx,
                     NodeOutputMap* outputs)
{
    (*outputs)[_kOut] = Value(Vec3f(ctx.texcoord[0], ctx.texcoord[1], 0.0f));
}

static void
_EvalGeomcolorFloat(const ParamMap&, const ShadingContext& ctx,
                    NodeOutputMap* outputs)
{
    (*outputs)[_kOut] = Value(ctx.displayColor[0]);
}

static void
_EvalGeomcolorColor3(const ParamMap&, const ShadingContext& ctx,
                     NodeOutputMap* outputs)
{
    (*outputs)[_kOut] = Value(ctx.displayColor);
}

static void
_EvalGeomcolorColor4(const ParamMap&, const ShadingContext& ctx,
                     NodeOutputMap* outputs)
{
    (*outputs)[_kOut] = Value(Vec4f(
        ctx.displayColor[0], ctx.displayColor[1], ctx.displayColor[2],
        ctx.displayOpacity));
}

static void
_EvalBump(const ParamMap& inputs, const ShadingContext& ctx,
          NodeOutputMap* outputs)
{
    const float scale = Get<float>(inputs, _kScale, 1.0f);
    const float height = EvaluateInput<float>(inputs, _kHeight, ctx, 0.0f);
    const float du = _SelectFiniteDifferenceStep(ctx.dudx, ctx.dudy);
    const float dv = _SelectFiniteDifferenceStep(ctx.dvdx, ctx.dvdy);

    const float heightDu = EvaluateInput<float>(
        inputs, _kHeight, OffsetContextDu(ctx, du), 0.0f);
    const float heightDv = EvaluateInput<float>(
        inputs, _kHeight, OffsetContextDv(ctx, dv), 0.0f);

    const Vec3f inputNormal = Get<Vec3f>(inputs, _kNormal, ctx.normal);
    const Vec3f inputTangent = Get<Vec3f>(inputs, _kTangent, ctx.tangent);
    const Vec3f inputBitangent = Get<Vec3f>(inputs, _kBitangent, ctx.bitangent);

    Vec3f basisNormal;
    Vec3f basisTangent;
    Vec3f basisBitangent;
    _BuildOrthonormalBasis(
        inputNormal, inputTangent, inputBitangent,
        &basisNormal, &basisTangent, &basisBitangent);

    float tangentLength = std::sqrt(Dot(ctx.dPdu, ctx.dPdu));
    if (tangentLength < _kFloatEps) {
        tangentLength = 1.0f;
    }
    float bitangentLength = std::sqrt(Dot(ctx.dPdv, ctx.dPdv));
    if (bitangentLength < _kFloatEps) {
        bitangentLength = 1.0f;
    }

    const float dHdu = (heightDu - height) * scale / du;
    const float dHdv = (heightDv - height) * scale / dv;

    const Vec3f dpdu = basisTangent * tangentLength + basisNormal * dHdu;
    const Vec3f dpdv = basisBitangent * bitangentLength + basisNormal * dHdv;

    Vec3f worldNormal = Cross(dpdu, dpdv);
    if (Dot(worldNormal, worldNormal) < _kFloatEps * _kFloatEps) {
        (*outputs)[_kOut] = Value(basisNormal);
        return;
    }
    worldNormal.normalize();
    if (Dot(worldNormal, basisNormal) < 0.0f) {
        worldNormal = -worldNormal;
    }

    (*outputs)[_kOut] = Value(worldNormal);
}

static Vec3f
_EvalNormalMap(const ParamMap& inputs,
               const ShadingContext& ctx,
               const Vec2f& scale)
{
    Vec3f value = Get<Vec3f>(
        inputs, _kIn, Vec3f(0.5f, 0.5f, 1.0f));
    if (Dot(value, value) == 0.0f) {
        value = Vec3f(0.0f, 0.0f, 1.0f);
    } else {
        value = value * 2.0f - Vec3f(1.0f);
    }

    const Vec3f normal = Get<Vec3f>(inputs, _kNormal, ctx.normal);
    Vec3f defaultTangent =
        ctx.dPdu - normal * Dot(ctx.dPdu, normal);
    if (Dot(defaultTangent, defaultTangent) < _kFloatEps * _kFloatEps) {
        defaultTangent = ctx.tangent;
    } else {
        defaultTangent.normalize();
    }
    Vec3f defaultBitangent = Cross(normal, defaultTangent);
    if (Dot(defaultBitangent, defaultBitangent) <
        _kFloatEps * _kFloatEps) {
        defaultBitangent = ctx.bitangent;
    } else {
        defaultBitangent.normalize();
    }
    const Vec3f tangent =
        Get<Vec3f>(inputs, _kTangent, defaultTangent);
    const Vec3f bitangent =
        Get<Vec3f>(inputs, _kBitangent, defaultBitangent);
    Vec3f result =
        tangent * value[0] * scale[0] +
        bitangent * value[1] * scale[1] +
        normal * value[2];
    return result.normalized();
}

static void
_EvalNormalMapFloat(const ParamMap& inputs, const ShadingContext& ctx,
                    NodeOutputMap* outputs)
{
    const float scale = Get<float>(inputs, _kScale, 1.0f);
    (*outputs)[_kOut] = Value(
        _EvalNormalMap(inputs, ctx, Vec2f(scale)));
}

static void
_EvalNormalMapVector2(const ParamMap& inputs, const ShadingContext& ctx,
                      NodeOutputMap* outputs)
{
    const Vec2f scale = Get<Vec2f>(inputs, _kScale, Vec2f(1.0f));
    (*outputs)[_kOut] = Value(_EvalNormalMap(inputs, ctx, scale));
}

// ---- Geometric property nodes --------------------------------------------

template<typename T>
static void
_EvalGeomPropValue(const ParamMap& inputs, const ShadingContext& ctx,
                   NodeOutputMap* outputs)
{
    const int geomPropHandle = Get<int>(inputs, _kGeomprop, -1);
    if (geomPropHandle >= 0 && ctx.geomPropLookup) {
        Value v = ctx.geomPropLookup(
            ctx.geomPropUserData, geomPropHandle);
        if (ValueHolds<T>(v)) {
            (*outputs)[_kOut] = v;
            return;
        }
    }
    (*outputs)[_kOut] = Value(Get<T>(inputs, _kDefault, Zero<T>()));
}

static void
_EvalGeomPropValueUniformString(const ParamMap& inputs,
                                const ShadingContext& ctx,
                                NodeOutputMap* outputs)
{
    const int geomPropHandle = Get<int>(inputs, _kGeomprop, -1);
    const std::string defaultVal =
        Get<std::string>(inputs, _kDefault, std::string());
    if (geomPropHandle >= 0 && ctx.uniformProps &&
        static_cast<size_t>(geomPropHandle) < ctx.uniformProps->size()) {
        const Value& value = (*ctx.uniformProps)[geomPropHandle];
        if (ValueHolds<std::string>(value)) {
            (*outputs)[_kOut] = value;
            return;
        }
    }
    (*outputs)[_kOut] = Value(defaultVal);
}

// ---- Registration --------------------------------------------------------

#define _REG(name, fn) reg.Register(name, fn)

void
RegisterGeometricNodes(NodeRegistry& reg)
{
    _REG("ND_position_vector3",  &_EvalPosition);
    _REG("ND_normal_vector3",    &_EvalNormal);
    _REG("ND_tangent_vector3",   &_EvalTangent);
    _REG("ND_bitangent_vector3", &_EvalBitangent);
    _REG("ND_texcoord_vector2",  &_EvalTexcoordVector2);
    _REG("ND_texcoord_vector3",  &_EvalTexcoordVector3);
    _REG("ND_geomcolor_float",   &_EvalGeomcolorFloat);
    _REG("ND_geomcolor_color3",  &_EvalGeomcolorColor3);
    _REG("ND_geomcolor_color4",  &_EvalGeomcolorColor4);
    _REG("ND_bump_vector3", &_EvalBump);
    _REG("ND_normalmap_float", &_EvalNormalMapFloat);
    _REG("ND_normalmap_vector2", &_EvalNormalMapVector2);

    // geompropvalue (per-sample varying)
    _REG("ND_geompropvalue_integer", &_EvalGeomPropValue<int>);
    _REG("ND_geompropvalue_boolean", &_EvalGeomPropValue<bool>);
    _REG("ND_geompropvalue_float",   &_EvalGeomPropValue<float>);
    _REG("ND_geompropvalue_color3",  &_EvalGeomPropValue<Vec3f>);
    _REG("ND_geompropvalue_color4",  &_EvalGeomPropValue<Vec4f>);
    _REG("ND_geompropvalue_vector2", &_EvalGeomPropValue<Vec2f>);
    _REG("ND_geompropvalue_vector3", &_EvalGeomPropValue<Vec3f>);
    _REG("ND_geompropvalue_vector4", &_EvalGeomPropValue<Vec4f>);
    _REG("ND_geompropvalue_matrix44", &_EvalGeomPropValue<Mat4f>);

    // geompropvalueuniform (per-mesh uniform)
    _REG("ND_geompropvalueuniform_string",   &_EvalGeomPropValueUniformString);
    _REG("ND_geompropvalueuniform_filename", &_EvalGeomPropValueUniformString);
}

#undef _REG

}  // namespace mxcpp
