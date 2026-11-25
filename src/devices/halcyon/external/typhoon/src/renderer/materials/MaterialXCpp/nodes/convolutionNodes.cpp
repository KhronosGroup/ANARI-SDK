//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "convolutionNodes.h"

#include <renderer/materials/MaterialXCpp/nodeRegistry.h>
#include <renderer/materials/MaterialXCpp/nodes/helpers/inputEvaluationHelpers.h>
#include <renderer/materials/MaterialXCpp/nodes/helpers/mathHelpers.h>
#include <renderer/materials/MaterialXCpp/nodes/helpers/shadingContextHelpers.h>

#include <cmath>

namespace mxcpp {

namespace {

static const SlotName _kIn("in");
static const SlotName _kOut("out");
static const SlotName _kScale("scale");
static const SlotName _kSize("size");
static const SlotName _kTexcoord("texcoord");

static constexpr float _kBlurSizeEps = 1e-6f;
static constexpr float _kHeightToNormalFrameEps = 1e-8f;
static constexpr float _kSobelScaleFactor = 1.0f / 16.0f;

template<typename T>
static T
_EvaluateBlurInput(const ParamMap& inputs,
                   const ShadingContext& ctx,
                   const T& defaultValue)
{
    Value value;
    if (inputs.Evaluate(_kIn, ctx, &value) && ValueHolds<T>(value)) {
        return ValueGet<T>(value);
    }
    return Get<T>(inputs, _kIn, defaultValue);
}

template<typename T>
static void
_EvalBlur(const ParamMap& inputs,
          const ShadingContext& ctx,
          NodeOutputMap* outputs)
{
    const T defaultValue = Get<T>(inputs, _kIn, Zero<T>());
    const float size = EvaluateInput<float>(inputs, _kSize, ctx, 0.0f);
    if (size <= _kBlurSizeEps) {
        (*outputs)[_kOut] = Value(_EvaluateBlurInput(inputs, ctx, defaultValue));
        return;
    }

    ShadingContext blurCtx = ctx;
    blurCtx.textureBlur += Vec2f(size);
    (*outputs)[_kOut] = Value(_EvaluateBlurInput(inputs, blurCtx, defaultValue));
}

static Vec3f
_ComputeHeightToNormalEncoded(const ParamMap& inputs,
                              const SlotName& heightSlot,
                              const ShadingContext& ctx)
{
    const float scale = Get<float>(inputs, _kScale, 1.0f);
    const ShadingContext shiftedDx = OffsetContextDx(ctx);
    const ShadingContext shiftedDy = OffsetContextDy(ctx);
    const ShadingContext shiftedMinusDx = OffsetContextDx(ctx, -1.0f);
    const ShadingContext shiftedMinusDy = OffsetContextDy(ctx, -1.0f);

    const float heightDx =
        EvaluateInput<float>(inputs, heightSlot, shiftedDx, 0.0f) -
        EvaluateInput<float>(inputs, heightSlot, shiftedMinusDx, 0.0f);
    const float heightDy =
        EvaluateInput<float>(inputs, heightSlot, shiftedDy, 0.0f) -
        EvaluateInput<float>(inputs, heightSlot, shiftedMinusDy, 0.0f);

    const Vec2f texcoordDx =
        EvaluateInput<Vec2f>(inputs, _kTexcoord, shiftedDx, shiftedDx.texcoord) -
        EvaluateInput<Vec2f>(
            inputs, _kTexcoord, shiftedMinusDx, shiftedMinusDx.texcoord);
    const Vec2f texcoordDy =
        EvaluateInput<Vec2f>(inputs, _kTexcoord, shiftedDy, shiftedDy.texcoord) -
        EvaluateInput<Vec2f>(
            inputs, _kTexcoord, shiftedMinusDy, shiftedMinusDy.texcoord);

    const Vec2f dHdS =
        Vec2f(heightDx, heightDy) * scale * _kSobelScaleFactor;
    const Vec2f dUdS(texcoordDx[0], texcoordDy[0]);
    const Vec2f dVdS(texcoordDx[1], texcoordDy[1]);

    const Vec3f tangent(dUdS[0], dVdS[0], dHdS[0]);
    const Vec3f bitangent(dUdS[1], dVdS[1], dHdS[1]);
    Vec3f normal = Cross(tangent, bitangent);

    // Ray differentials may be scaled for the sampling rate, so detect a
    // degenerate frame relative to its tangent lengths rather than its area.
    const float frameMagnitude =
        std::sqrt(Dot(tangent, tangent)) *
        std::sqrt(Dot(bitangent, bitangent));
    if (!(frameMagnitude > 0.0f) || !std::isfinite(frameMagnitude)) {
        normal = Vec3f(0.0f, 0.0f, 1.0f);
    } else {
        normal *= 1.0f / frameMagnitude;
        const float relativeNormalLengthSquared = Dot(normal, normal);
        if (!std::isfinite(relativeNormalLengthSquared) ||
            relativeNormalLengthSquared <
                _kHeightToNormalFrameEps * _kHeightToNormalFrameEps) {
            normal = Vec3f(0.0f, 0.0f, 1.0f);
        } else if (normal[2] < 0.0f) {
            normal = -normal;
        }
        normal.normalize();
    }

    return normal * 0.5f + Vec3f(0.5f, 0.5f, 0.5f);
}

static void
_EvalHeightToNormal(const ParamMap& inputs, const ShadingContext& ctx,
                    NodeOutputMap* outputs)
{
    (*outputs)[_kOut] = Value(
        _ComputeHeightToNormalEncoded(inputs, _kIn, ctx));
}

}  // namespace

#define _REG(name, fn) reg.Register(name, fn)

void
RegisterConvolutionNodes(NodeRegistry& reg)
{
    _REG("ND_blur_float",   &_EvalBlur<float>);
    _REG("ND_blur_color3",  &_EvalBlur<Vec3f>);
    _REG("ND_blur_color4",  &_EvalBlur<Vec4f>);
    _REG("ND_blur_vector2", &_EvalBlur<Vec2f>);
    _REG("ND_blur_vector3", &_EvalBlur<Vec3f>);
    _REG("ND_blur_vector4", &_EvalBlur<Vec4f>);
    _REG("ND_heighttonormal_vector3", &_EvalHeightToNormal);
}

#undef _REG

}  // namespace mxcpp
