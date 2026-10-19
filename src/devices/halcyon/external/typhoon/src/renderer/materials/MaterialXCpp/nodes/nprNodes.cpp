//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "nprNodes.h"

#include <renderer/materials/MaterialXCpp/nodeRegistry.h>
#include <renderer/materials/MaterialXCpp/nodes/helpers/shadingContextHelpers.h>
#include <renderer/materials/MaterialXCpp/nodes/helpers/spaceHelpers.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace mxcpp {

namespace {

static constexpr float _kFloatEps = 1e-6f;

static const SlotName _kNormal("normal");
static const SlotName _kOut("out");
static const SlotName _kSpace("space");

static std::string
_GetViewSpace(const ParamMap& inputs)
{
    return NormalizeSpaceName(
        Get<std::string>(inputs, _kSpace, std::string("world")),
        std::string("world"));
}

static Vec3f
_ComputeWorldViewDirection(const ShadingContext& ctx)
{
    Vec3f worldPosition = ctx.position;
    TransformNamedVec3(
        ctx, "object", "world",
        ShadingContext::TransformSpaceType::Point,
        ctx.position, &worldPosition);

    Vec3f result = worldPosition - ctx.viewPosition;
    if (result.length2() > _kFloatEps * _kFloatEps) {
        result.normalize();
    }
    return result;
}

static Vec3f
_NormalizeOrZero(const Vec3f& value)
{
    if (value.length2() <= _kFloatEps * _kFloatEps) {
        return Vec3f(0.0f);
    }

    Vec3f result = value;
    result.normalize();
    return result;
}

static void
_EvalViewDirection(const ParamMap& inputs, const ShadingContext& ctx,
                   NodeOutputMap* outputs)
{
    const std::string space = _GetViewSpace(inputs);
    Vec3f result = _ComputeWorldViewDirection(ctx);
    TransformNamedVec3(
        ctx, "world", space,
        ShadingContext::TransformSpaceType::Vector,
        result, &result);
    if (result.length2() > _kFloatEps * _kFloatEps) {
        result.normalize();
    }
    (*outputs)[_kOut] = Value(result);
}

static void
_EvalFacingRatio(const ParamMap& inputs, const ShadingContext& ctx,
                 NodeOutputMap* outputs)
{
    const Vec3f viewDirection = Get<Vec3f>(
        inputs, "viewdirection", _ComputeWorldViewDirection(ctx));
    const Vec3f normal = Get<Vec3f>(inputs, _kNormal, ctx.normal);
    const bool faceForward = Get<bool>(inputs, "faceforward", true);
    const bool invert = Get<bool>(inputs, "invert", false);

    const float dot = Dot(viewDirection, normal);
    const float facing = faceForward ? std::fabs(dot) : -dot;
    (*outputs)[_kOut] = Value(invert ? (1.0f - facing) : facing);
}

static void
_EvalGoochShade(const ParamMap& inputs, const ShadingContext& ctx,
                NodeOutputMap* outputs)
{
    const Vec3f warmColor = Get<Vec3f>(
        inputs, "warm_color", Vec3f(0.8f, 0.8f, 0.7f));
    const Vec3f coolColor = Get<Vec3f>(
        inputs, "cool_color", Vec3f(0.3f, 0.3f, 0.8f));
    const float specularIntensity =
        Get<float>(inputs, "specular_intensity", 1.0f);
    const float shininess = Get<float>(inputs, "shininess", 64.0f);
    const Vec3f lightDirection = Get<Vec3f>(
        inputs, "light_direction", Vec3f(1.0f, -0.5f, -0.5f));

    const Vec3f unitNormal = _NormalizeOrZero(ctx.normal);
    const Vec3f unitViewDirection =
        _NormalizeOrZero(_ComputeWorldViewDirection(ctx));
    const Vec3f unitLightDirection = _NormalizeOrZero(lightDirection);

    const float nDotL = Dot(unitNormal, unitLightDirection);
    const float coolIntensity = 0.5f * (1.0f + nDotL);
    const Vec3f diffuse = warmColor + (coolColor - warmColor) * coolIntensity;

    const Vec3f viewReflect =
        unitViewDirection - 2.0f * Dot(unitViewDirection, unitNormal) * unitNormal;
    const float vDotR = Dot(-unitLightDirection, viewReflect);
    const float specularHighlight = std::pow(std::max(vDotR, 0.0f), shininess);
    const float specular = specularHighlight * specularIntensity;

    (*outputs)[_kOut] = Value(diffuse + Vec3f(specular));
}

}  // namespace

#define _REG(name, fn) reg.Register(name, fn)

void
RegisterNprNodes(NodeRegistry& reg)
{
    _REG("ND_viewdirection_vector3", &_EvalViewDirection);
    _REG("ND_facingratio_float", &_EvalFacingRatio);
    _REG("ND_gooch_shade", &_EvalGoochShade);
}

#undef _REG

}  // namespace mxcpp
