//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "texture3dNodes.h"

#include <renderer/materials/MaterialXCpp/nodeRegistry.h>
#include <renderer/materials/MaterialXCpp/nodes/helpers/textureHelpers.h>

#include <array>
#include <cmath>
#include <string>
#include <type_traits>

namespace mxcpp {

namespace {

static const SlotName _kBlend("blend");
static const SlotName _kDefaultVal("default");
static const SlotName _kFileX("filex");
static const SlotName _kFileY("filey");
static const SlotName _kFileZ("filez");
static const SlotName _kFileXColorSpace("colorSpace:filex");
static const SlotName _kFileYColorSpace("colorSpace:filey");
static const SlotName _kFileZColorSpace("colorSpace:filez");
static const SlotName _kFilterType("filtertype");
static const SlotName _kFrameRange("framerange");
static const SlotName _kFrameOffset("frameoffset");
static const SlotName _kFrameEndAction("frameendaction");
static const SlotName _kLayerX("layerx");
static const SlotName _kLayerY("layery");
static const SlotName _kLayerZ("layerz");
static const SlotName _kNormal("normal");
static const SlotName _kOut("out");
static const SlotName _kPosition("position");
static const SlotName _kUpAxis("upaxis");

struct _TriplanarFootprint
{
    std::array<Vec2f, 3> st = {Vec2f(0.0f), Vec2f(0.0f), Vec2f(0.0f)};
    std::array<Vec2f, 3> dstdx = {Vec2f(0.0f), Vec2f(0.0f), Vec2f(0.0f)};
    std::array<Vec2f, 3> dstdy = {Vec2f(0.0f), Vec2f(0.0f), Vec2f(0.0f)};
};

static std::array<Vec2f, 3>
_ComputeTriplanarCoords(const Vec3f& position, const int upAxis)
{
    const float x = position[0];
    const float y = position[1];
    const float z = position[2];

    if (upAxis == 0) {
        return {
            Vec2f(z, y),
            Vec2f(z, x),
            Vec2f(-y, x)};
    }
    if (upAxis == 1) {
        return {
            Vec2f(z, y),
            Vec2f(x, z),
            Vec2f(x, y)};
    }
    return {
        Vec2f(y, z),
        Vec2f(x, z),
        Vec2f(x, y)};
}

static _TriplanarFootprint
_ComputeTriplanarFootprint(const ParamMap& inputs, const ShadingContext& ctx)
{
    const int upAxis = std::clamp(Get<int>(inputs, _kUpAxis, 2), 0, 2);
    const Vec3f position =
        EvaluateInput<Vec3f>(inputs, _kPosition, ctx, ctx.position);
    const std::array<Vec2f, 3> st = _ComputeTriplanarCoords(position, upAxis);

    const ShadingContext shiftedDx = OffsetContextDx(ctx);
    const Vec3f positionDx = EvaluateInput<Vec3f>(
        inputs, _kPosition, shiftedDx, shiftedDx.position);
    const std::array<Vec2f, 3> stDx =
        _ComputeTriplanarCoords(positionDx, upAxis);

    const ShadingContext shiftedDy = OffsetContextDy(ctx);
    const Vec3f positionDy = EvaluateInput<Vec3f>(
        inputs, _kPosition, shiftedDy, shiftedDy.position);
    const std::array<Vec2f, 3> stDy =
        _ComputeTriplanarCoords(positionDy, upAxis);

    _TriplanarFootprint footprint;
    for (int i = 0; i < 3; ++i) {
        footprint.st[i] = st[i];
        footprint.dstdx[i] = stDx[i] - st[i];
        footprint.dstdy[i] = stDy[i] - st[i];
    }
    return footprint;
}

static Vec3f
_ComputeTriplanarBlendWeights(const ParamMap& inputs, const ShadingContext& ctx)
{
    Vec3f normal = EvaluateInput<Vec3f>(
        inputs, _kNormal, ctx, GetDefaultObjectNormal(ctx));
    if (normal.length2() > _kFloatEps * _kFloatEps) {
        normal.normalize();
    } else {
        normal = Vec3f(0.0f);
    }

    const Vec3f absNormal(
        std::fabs(normal[0]),
        std::fabs(normal[1]),
        std::fabs(normal[2]));
    const float normalSum = absNormal[0] + absNormal[1] + absNormal[2];
    const Vec3f normalizedWeights =
        (normalSum > 0.0f) ? absNormal / normalSum : Vec3f(0.0f);

    const float blend = std::clamp(
        Get<float>(inputs, _kBlend, 1.0f), 0.03f, 1.0f);
    const float exponent = 1.0f / blend;
    const Vec3f poweredWeights(
        std::pow(normalizedWeights[0], exponent),
        std::pow(normalizedWeights[1], exponent),
        std::pow(normalizedWeights[2], exponent));
    const float poweredWeightSum =
        poweredWeights[0] + poweredWeights[1] + poweredWeights[2];
    if (poweredWeightSum > 0.0f) {
        return poweredWeights / poweredWeightSum;
    }
    return Vec3f(0.0f);
}

static const SlotName&
_GetTriplanarFileSlot(const int axis)
{
    if (axis == 0) {
        return _kFileX;
    }
    if (axis == 1) {
        return _kFileY;
    }
    return _kFileZ;
}

static const SlotName&
_GetTriplanarFileColorSpaceSlot(const int axis)
{
    if (axis == 0) {
        return _kFileXColorSpace;
    }
    if (axis == 1) {
        return _kFileYColorSpace;
    }
    return _kFileZColorSpace;
}

static const SlotName&
_GetTriplanarLayerSlot(const int axis)
{
    if (axis == 0) {
        return _kLayerX;
    }
    if (axis == 1) {
        return _kLayerY;
    }
    return _kLayerZ;
}

template<typename T, TextureDataRole DataRole>
static Vec4f
_SampleTriplanarTexture(const ParamMap& inputs,
                        const ShadingContext& ctx,
                        const SlotName& fileSlot,
                        const SlotName& fileColorSpaceSlot,
                        const SlotName& layerSlot,
                        const Vec2f& st,
                        const Vec2f& dstdx,
                        const Vec2f& dstdy,
                        const T& defaultValue)
{
    const Vec4f defaultColor = TextureValueTraits<T>::ToVec4(defaultValue);
    const std::string filePath =
        Get<std::string>(inputs, fileSlot, std::string());
    if (!ctx.textureSystem || filePath.empty()) {
        return defaultColor;
    }

    Texture2DRequest request;
    request.filePath = filePath;
    request.layerName = Get<std::string>(inputs, layerSlot, std::string());
    request.st = st;
    request.dstdx = dstdx;
    request.dstdy = dstdy;
    request.uAddressMode = TextureAddressMode::Periodic;
    request.vAddressMode = TextureAddressMode::Periodic;
    request.filterType = GetFilterType(inputs, _kFilterType);
    request.frameRange = Get<std::string>(inputs, _kFrameRange, std::string());
    request.frameOffset = Get<int>(inputs, _kFrameOffset, 0);
    request.frameEndAction =
        GetAddressMode(inputs, _kFrameEndAction, "constant");
    request.frame = ctx.frame;
    request.dataRole = DataRole;
    request.sourceColorSpace =
        Get<std::string>(inputs, fileColorSpaceSlot, std::string());
    request.channelCount = TextureValueTraits<T>::kChannelCount;
    request.defaultValue = defaultColor;
    if constexpr (std::is_same_v<T, Vec4f>) {
        request.channelFillValue =
            (DataRole == TextureDataRole::Color) ? 1.0f : 0.0f;
    } else {
        request.channelFillValue = TextureValueTraits<T>::kFillValue;
    }
    ApplyContextTextureBlur(ctx, &request);

    return ctx.textureSystem->Sample2D(request).value;
}

template<typename T, TextureDataRole DataRole>
static void
_EvalTriplanarProjectionNode(const ParamMap& inputs,
                             const ShadingContext& ctx,
                             NodeOutputMap* outputs)
{
    const T defaultValue = Get<T>(inputs, _kDefaultVal, Zero<T>());
    const _TriplanarFootprint footprint = _ComputeTriplanarFootprint(inputs, ctx);
    const Vec3f blendWeights = _ComputeTriplanarBlendWeights(inputs, ctx);

    Vec4f result(0.0f);
    for (int i = 0; i < 3; ++i) {
        const Vec4f sampled = _SampleTriplanarTexture<T, DataRole>(
            inputs, ctx,
            _GetTriplanarFileSlot(i),
            _GetTriplanarFileColorSpaceSlot(i),
            _GetTriplanarLayerSlot(i),
            footprint.st[i], footprint.dstdx[i], footprint.dstdy[i],
            defaultValue);
        result += sampled * blendWeights[i];
    }

    (*outputs)[_kOut] = Value(TextureValueTraits<T>::FromVec4(result));
}

}  // namespace

#define _REG(...) reg.Register(__VA_ARGS__)

void
RegisterTexture3dNodes(NodeRegistry& reg)
{
    _REG("ND_triplanarprojection_float",
         &_EvalTriplanarProjectionNode<float, TextureDataRole::NonColor>);
    _REG("ND_triplanarprojection_color3",
         &_EvalTriplanarProjectionNode<Vec3f, TextureDataRole::Color>);
    _REG("ND_triplanarprojection_color4",
         &_EvalTriplanarProjectionNode<Vec4f, TextureDataRole::Color>);
    _REG("ND_triplanarprojection_vector2",
         &_EvalTriplanarProjectionNode<Vec2f, TextureDataRole::NonColor>);
    _REG("ND_triplanarprojection_vector3",
         &_EvalTriplanarProjectionNode<Vec3f, TextureDataRole::NonColor>);
    _REG("ND_triplanarprojection_vector4",
         &_EvalTriplanarProjectionNode<Vec4f, TextureDataRole::NonColor>);
}

#undef _REG

}  // namespace mxcpp
