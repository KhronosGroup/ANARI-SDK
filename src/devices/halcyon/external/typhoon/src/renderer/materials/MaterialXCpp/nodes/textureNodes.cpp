//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "textureNodes.h"

#include <renderer/materials/MaterialXCpp/nodeRegistry.h>
#include <renderer/materials/MaterialXCpp/nodes/helpers/textureHelpers.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <type_traits>

namespace mxcpp {

namespace {

static constexpr float _kFloatEps = 1e-6f;
static constexpr float _kHexSqrt3 = 1.7320508075688772935f;
static constexpr float _kHexSqrt3Times2 = 3.4641016151377545871f;
static constexpr float _kHexSkewX = -0.57735026918962576451f;
static constexpr float _kHexSkewY = 1.1547005383792515290f;
static constexpr float _kHexInvSkewY = 0.86602540378443864676f;

static const SlotName _kFile("file");
static const SlotName _kFileColorSpace("colorSpace:file");
static const SlotName _kLayer("layer");
static const SlotName _kTexcoord("texcoord");
static const SlotName _kSt("st");
static const SlotName _kViewdir("viewdir");
static const SlotName _kOut("out");
static const SlotName _kOutColor("outcolor");
static const SlotName _kOutA("outa");
static const SlotName _kDefaultVal("default");
static const SlotName _kFactor("factor");
static const SlotName _kColor("color");
static const SlotName _kGeomColor("geomcolor");
static const SlotName _kThicknessMin("thicknessMin");
static const SlotName _kThicknessMax("thicknessMax");
static const SlotName _kAnisotropyStrength("anisotropy_strength");
static const SlotName _kAnisotropyRotation("anisotropy_rotation");
static const SlotName _kAnisotropyStrengthOut("anisotropy_strength_out");
static const SlotName _kAnisotropyRotationOut("anisotropy_rotation_out");
static const SlotName _kFallback("fallback");
static const SlotName _kUAddressMode("uaddressmode");
static const SlotName _kVAddressMode("vaddressmode");
static const SlotName _kWrapS("wrapS");
static const SlotName _kWrapT("wrapT");
static const SlotName _kFilterType("filtertype");
static const SlotName _kRotation("rotation");
static const SlotName _kPivot("pivot");
static const SlotName _kRotate("rotate");
static const SlotName _kOperationOrder("operationorder");
static const SlotName _kStrength("strength");
static const SlotName _kFlipG("flip_g");
static const SlotName _kNormal("normal");
static const SlotName _kTangent("tangent");
static const SlotName _kBitangent("bitangent");
static const SlotName _kFrameRange("framerange");
static const SlotName _kFrameOffset("frameoffset");
static const SlotName _kFrameEndAction("frameendaction");
static const SlotName _kUvtiling("uvtiling");
static const SlotName _kUvoffset("uvoffset");
static const SlotName _kRealWorldImageSize("realworldimagesize");
static const SlotName _kRealWorldTileSize("realworldtilesize");
static const SlotName _kTiling("tiling");
static const SlotName _kRotationRange("rotationrange");
static const SlotName _kScale("scale");
static const SlotName _kBias("bias");
static const SlotName _kScaleRange("scalerange");
static const SlotName _kOffset("offset");
static const SlotName _kOffsetRange("offsetrange");
static const SlotName _kFalloff("falloff");
static const SlotName _kFalloffContrast("falloffcontrast");
static const SlotName _kLumaCoeffs("lumacoeffs");
static const SlotName _kSourceColorSpace("sourceColorSpace");
static const SlotName _kR("r");
static const SlotName _kG("g");
static const SlotName _kB("b");
static const SlotName _kA("a");
static const SlotName _kRgb("rgb");
static const SlotName _kRgba("rgba");

struct _HexTileFootprint
{
    std::array<Vec2f, 3> st = {Vec2f(0.0f), Vec2f(0.0f), Vec2f(0.0f)};
    std::array<Vec2f, 3> dstdx = {Vec2f(0.0f), Vec2f(0.0f), Vec2f(0.0f)};
    std::array<Vec2f, 3> dstdy = {Vec2f(0.0f), Vec2f(0.0f), Vec2f(0.0f)};
    std::array<float, 3> rotations = {0.0f, 0.0f, 0.0f};
    Vec3f weights = Vec3f(0.0f);
};

static Vec3f
_NormalizeWeights(const Vec3f& weights)
{
    const float sum = weights[0] + weights[1] + weights[2];
    if (std::abs(sum) <= _kFloatEps) {
        return Vec3f(1.0f / 3.0f);
    }
    return weights / sum;
}

static Vec2f
_HexTileHash(const Vec2f& p)
{
    Vec3f p3 = Fract(Vec3f(p[0], p[1], p[0]) *
                     Vec3f(0.1031f, 0.1030f, 0.0973f));
    const float foldedDot = Dot(
        p3,
        Vec3f(p3[1], p3[2], p3[0]) + Vec3f(33.33f));
    p3 += Vec3f(foldedDot);
    return Fract(
        Vec2f(p3[0] + p3[1], p3[0] + p3[2]) *
        Vec2f(p3[2], p3[1]));
}

static float
_SchlickGain(const float x, const float r)
{
    const float rr = std::clamp(r, 0.001f, 0.999f);
    const float a = (1.0f / rr - 2.0f) * (1.0f - 2.0f * x);
    return (x < 0.5f) ? x / (a + 1.0f) : (a - x) / (a - 1.0f);
}

static Vec3f
_ComputeHexBlendWeights(const Vec3f& luminanceWeights,
                        const Vec3f& tileWeights,
                        const float falloff)
{
    Vec3f weights(
        luminanceWeights[0] * std::pow(tileWeights[0], 7.0f),
        luminanceWeights[1] * std::pow(tileWeights[1], 7.0f),
        luminanceWeights[2] * std::pow(tileWeights[2], 7.0f));
    weights = _NormalizeWeights(weights);

    if (std::abs(falloff - 0.5f) > _kFloatEps) {
        weights[0] = _SchlickGain(weights[0], falloff);
        weights[1] = _SchlickGain(weights[1], falloff);
        weights[2] = _SchlickGain(weights[2], falloff);
        weights = _NormalizeWeights(weights);
    }

    return weights;
}

static _HexTileFootprint
_ComputeHexTileFootprint(const ParamMap& inputs, const ShadingContext& ctx)
{
    const Vec2f tiling = Get<Vec2f>(inputs, _kTiling, Vec2f(1.0f));
    const float rotationAmount = Get<float>(inputs, _kRotation, 1.0f);
    const Vec2f rotationRangeDegrees =
        Get<Vec2f>(inputs, _kRotationRange, Vec2f(0.0f, 360.0f));
    const float scaleAmount = Get<float>(inputs, _kScale, 1.0f);
    const Vec2f scaleRange =
        Get<Vec2f>(inputs, _kScaleRange, Vec2f(0.5f, 2.0f));
    const float offsetAmount = Get<float>(inputs, _kOffset, 1.0f);
    const Vec2f offsetRange =
        Get<Vec2f>(inputs, _kOffsetRange, Vec2f(0.0f, 1.0f));
    // Hex hashing and rotation operate in the texture system's T convention.
    // Enter that convention before the nonlinear transform, then convert the
    // generated coordinates and derivatives back before Sample2D flips them
    // at the backend boundary.

    const Vec2f baseTexcoord =
        EvaluateInput<Vec2f>(inputs, _kTexcoord, ctx, ctx.texcoord);
    const Vec2f coord(
        baseTexcoord[0] * tiling[0], 1.0f - baseTexcoord[1] * tiling[1]);

    const ShadingContext shiftedDx = OffsetContextDx(ctx);
    const Vec2f texcoordDx =
        EvaluateInput<Vec2f>(inputs, _kTexcoord, shiftedDx, shiftedDx.texcoord);
    const Vec2f coordDx(
        texcoordDx[0] * tiling[0], 1.0f - texcoordDx[1] * tiling[1]);

    const ShadingContext shiftedDy = OffsetContextDy(ctx);
    const Vec2f texcoordDy =
        EvaluateInput<Vec2f>(inputs, _kTexcoord, shiftedDy, shiftedDy.texcoord);
    const Vec2f coordDy(
        texcoordDy[0] * tiling[0], 1.0f - texcoordDy[1] * tiling[1]);

    const Vec2f baseDstdx = coordDx - coord;
    const Vec2f baseDstdy = coordDy - coord;

    const Vec2f st = coord * _kHexSqrt3Times2;
    const Vec2f stSkewed(
        st[0] + st[1] * _kHexSkewX,
        st[1] * _kHexSkewY);
    const Vec2f stFrac = Fract(stSkewed);
    const float z = 1.0f - stFrac[0] - stFrac[1];
    const float s = (-z >= 0.0f) ? 1.0f : 0.0f;
    const float s2 = 2.0f * s - 1.0f;

    const float w1 = -z * s2;
    const float w2 = s - stFrac[1] * s2;
    const float w3 = s - stFrac[0] * s2;

    const int baseIdX = static_cast<int>(std::floor(stSkewed[0]));
    const int baseIdY = static_cast<int>(std::floor(stSkewed[1]));
    const int si = static_cast<int>(s);
    const std::array<int, 3> idX = {
        baseIdX + si,
        baseIdX + si,
        baseIdX + 1 - si};
    const std::array<int, 3> idY = {
        baseIdY + si,
        baseIdY + 1 - si,
        baseIdY + si};

    const Vec2f rotationRange(
        rotationRangeDegrees[0] * kDegreesToRadians,
        rotationRangeDegrees[1] * kDegreesToRadians);
    const Vec2f seedOffset(0.12345f);

    _HexTileFootprint footprint;
    footprint.weights = Vec3f(w1, w2, w3);

    for (int i = 0; i < 3; ++i) {
        const Vec2f center(
            static_cast<float>(idX[i]) + static_cast<float>(idY[i]) * 0.5f,
            static_cast<float>(idY[i]) * _kHexInvSkewY);
        const Vec2f tileCenter = center / _kHexSqrt3Times2;
        const Vec2f randomValue = _HexTileHash(
            Vec2f(static_cast<float>(idX[i]), static_cast<float>(idY[i])) +
            seedOffset);
        const float tileRotation = Mix(
            rotationRange[0], rotationRange[1],
            randomValue[0] * rotationAmount);
        const float tileScale = Mix(
            1.0f,
            Mix(scaleRange[0], scaleRange[1], randomValue[1]),
            scaleAmount);
        const Vec2f tileOffset(
            Mix(offsetRange[0], offsetRange[1], randomValue[0] * offsetAmount),
            Mix(offsetRange[0], offsetRange[1], randomValue[1] * offsetAmount));

        const float sinRotation = std::sin(tileRotation);
        const float cosRotation = std::cos(tileRotation);
        const Vec2f delta = coord - tileCenter;

        footprint.rotations[i] = tileRotation;
        footprint.st[i] = Vec2f(
            (delta[0] * cosRotation - delta[1] * sinRotation) / tileScale +
                tileCenter[0] + tileOffset[0],
            1.0f -
                ((delta[0] * sinRotation + delta[1] * cosRotation) /
                     tileScale + tileCenter[1] + tileOffset[1]));
        footprint.dstdx[i] = Vec2f(
            (baseDstdx[0] * cosRotation - baseDstdx[1] * sinRotation) /
                tileScale,
            -(baseDstdx[0] * sinRotation + baseDstdx[1] * cosRotation) /
                tileScale);
        footprint.dstdy[i] = Vec2f(
            (baseDstdy[0] * cosRotation - baseDstdy[1] * sinRotation) /
                tileScale,
            -(baseDstdy[0] * sinRotation + baseDstdy[1] * cosRotation) /
                tileScale);
    }

    return footprint;
}

static Vec2f
_ComputeLatLongTexcoord(const Vec3f& viewdir, const float rotationDegrees)
{
    static constexpr float kInvTwoPi = -0.5f * kInvPi;
    static constexpr float kInv360 = 0.00277778f;

    const float angleXZ = std::atan2(viewdir[0], viewdir[2]);
    const float longitude = angleXZ * kInvTwoPi + 0.5f;
    const float offsetLongitude = longitude + rotationDegrees * kInv360;
    const float clampedY = std::clamp(viewdir[1], -1.0f, 1.0f);
    const float latitude = std::asin(clampedY) * kInvPi + 0.5f;

    return Vec2f(offsetLongitude, latitude);
}


static Vec2f
_ApplyTiledTransform(const ParamMap& inputs, const Vec2f& texcoord)
{
    const Vec2f uvtiling = Get<Vec2f>(inputs, _kUvtiling, Vec2f(1.0f));
    const Vec2f uvoffset = Get<Vec2f>(inputs, _kUvoffset, Vec2f(0.0f));
    const Vec2f imageSize =
        Get<Vec2f>(inputs, _kRealWorldImageSize, Vec2f(1.0f));
    const Vec2f tileSize =
        Get<Vec2f>(inputs, _kRealWorldTileSize, Vec2f(1.0f));

    const Vec2f tiled = CompMul(texcoord, uvtiling) - uvoffset;
    const Vec2f safeImageSize(
        (std::abs(imageSize[0]) > _kFloatEps) ? imageSize[0] : 1.0f,
        (std::abs(imageSize[1]) > _kFloatEps) ? imageSize[1] : 1.0f);

    return Vec2f(
        tiled[0] / safeImageSize[0] * tileSize[0],
        tiled[1] / safeImageSize[1] * tileSize[1]);
}

template<bool IsTiled>
static Vec2f
_ComputeSampleCoord(const ParamMap& inputs, const Vec2f& texcoord)
{
    if constexpr (IsTiled) {
        return _ApplyTiledTransform(inputs, texcoord);
    } else {
        return texcoord;
    }
}

template<bool IsTiled>
static void
_ComputeTextureFootprintForSlot(const ParamMap& inputs,
                                const SlotName& texcoordSlot,
                                const ShadingContext& ctx,
                                Vec2f* outSt,
                                Vec2f* outDstdx,
                                Vec2f* outDstdy)
{
    // The base coordinate uses the same shading context this node is being
    // evaluated with, so the connected input's cached value (computed during
    // the current graph pass) is already correct.  Only the dx/dy probes
    // below need the re-evaluation machinery, which runs the upstream
    // subgraph again per shifted context.
    const Vec2f baseTexcoord = Get<Vec2f>(inputs, texcoordSlot, ctx.texcoord);
    const Vec2f st = _ComputeSampleCoord<IsTiled>(inputs, baseTexcoord);

    const ShadingContext shiftedDx = OffsetContextDx(ctx);
    const Vec2f texcoordDx =
        EvaluateInput<Vec2f>(
            inputs, texcoordSlot, shiftedDx, shiftedDx.texcoord);
    const Vec2f stDx = _ComputeSampleCoord<IsTiled>(inputs, texcoordDx);

    const ShadingContext shiftedDy = OffsetContextDy(ctx);
    const Vec2f texcoordDy =
        EvaluateInput<Vec2f>(
            inputs, texcoordSlot, shiftedDy, shiftedDy.texcoord);
    const Vec2f stDy = _ComputeSampleCoord<IsTiled>(inputs, texcoordDy);

    if (outSt) {
        *outSt = st;
    }
    if (outDstdx) {
        *outDstdx = stDx - st;
    }
    if (outDstdy) {
        *outDstdy = stDy - st;
    }
}

template<bool IsTiled>
static void
_ComputeTextureFootprint(const ParamMap& inputs,
                         const ShadingContext& ctx,
                         Vec2f* outSt,
                         Vec2f* outDstdx,
                         Vec2f* outDstdy)
{
    _ComputeTextureFootprintForSlot<IsTiled>(
        inputs, _kTexcoord, ctx, outSt, outDstdx, outDstdy);
}

static void
_ComputeLatLongFootprint(const ParamMap& inputs,
                         const ShadingContext& ctx,
                         Vec2f* outSt,
                         Vec2f* outDstdx,
                         Vec2f* outDstdy)
{
    const float rotationDegrees = Get<float>(inputs, _kRotation, 0.0f);
    const Vec3f baseViewdir =
        EvaluateInput<Vec3f>(inputs, _kViewdir, ctx, Vec3f(0.0f, 0.0f, 1.0f));
    const Vec2f st = _ComputeLatLongTexcoord(baseViewdir, rotationDegrees);

    const ShadingContext shiftedDx = OffsetContextDx(ctx);
    const Vec3f viewdirDx = EvaluateInput<Vec3f>(
        inputs, _kViewdir, shiftedDx, Vec3f(0.0f, 0.0f, 1.0f));
    const Vec2f stDx = _ComputeLatLongTexcoord(viewdirDx, rotationDegrees);

    const ShadingContext shiftedDy = OffsetContextDy(ctx);
    const Vec3f viewdirDy = EvaluateInput<Vec3f>(
        inputs, _kViewdir, shiftedDy, Vec3f(0.0f, 0.0f, 1.0f));
    const Vec2f stDy = _ComputeLatLongTexcoord(viewdirDy, rotationDegrees);

    if (outSt) {
        *outSt = st;
    }
    if (outDstdx) {
        *outDstdx = stDx - st;
    }
    if (outDstdy) {
        *outDstdy = stDy - st;
    }
}

template<typename T>
static void
_EvalHexTiledImageNode(const ParamMap& inputs,
                       const ShadingContext& ctx,
                       NodeOutputMap* outputs)
{
    const T defaultValue = Get<T>(inputs, _kDefaultVal, Zero<T>());
    const Vec4f defaultColor = TextureValueTraits<T>::ToVec4(defaultValue);
    const std::string filePath = Get<std::string>(inputs, _kFile, std::string());

    if (!ctx.textureSystem || filePath.empty()) {
        (*outputs)[_kOut] = Value(defaultValue);
        return;
    }

    const _HexTileFootprint footprint = _ComputeHexTileFootprint(inputs, ctx);
    const float falloff = Get<float>(inputs, _kFalloff, 0.5f);
    const float falloffContrast = Get<float>(inputs, _kFalloffContrast, 0.5f);
    const Vec3f lumaCoeffs = Get<Vec3f>(
        inputs, _kLumaCoeffs,
        AcesCgLumaCoeffs());

    Texture2DRequest request;
    request.filePath = filePath;
    request.uAddressMode = TextureAddressMode::Periodic;
    request.vAddressMode = TextureAddressMode::Periodic;
    request.filterType = TextureFilterType::SmartBicubic;
    request.frame = ctx.frame;
    request.dataRole = TextureDataRole::Color;
    request.sourceColorSpace =
        Get<std::string>(inputs, _kFileColorSpace, std::string());
    request.channelCount = TextureValueTraits<T>::kChannelCount;
    request.defaultValue = defaultColor;
    if constexpr (std::is_same_v<T, Vec4f>) {
        request.channelFillValue = 1.0f;
    } else {
        request.channelFillValue = TextureValueTraits<T>::kFillValue;
    }
    ApplyContextTextureBlur(ctx, &request);

    std::array<Vec4f, 3> sampledValues = {
        Vec4f(0.0f), Vec4f(0.0f), Vec4f(0.0f)};
    Vec3f luminanceWeights(0.0f);

    for (int i = 0; i < 3; ++i) {
        request.st = footprint.st[i];
        request.dstdx = footprint.dstdx[i];
        request.dstdy = footprint.dstdy[i];

        const Texture2DResult sampled = ctx.textureSystem->Sample2D(request);
        sampledValues[i] = sampled.value;
        luminanceWeights[i] = Dot(
            Vec3f(sampled.value[0], sampled.value[1], sampled.value[2]),
            lumaCoeffs);
    }

    const Vec3f contrastWeights(
        Mix(1.0f, luminanceWeights[0], falloffContrast),
        Mix(1.0f, luminanceWeights[1], falloffContrast),
        Mix(1.0f, luminanceWeights[2], falloffContrast));
    const Vec3f blendWeights = _ComputeHexBlendWeights(
        contrastWeights, footprint.weights, falloff);

    if constexpr (std::is_same_v<T, Vec3f>) {
        const Vec3f result =
            Vec3f(sampledValues[0][0], sampledValues[0][1], sampledValues[0][2]) *
                blendWeights[0] +
            Vec3f(sampledValues[1][0], sampledValues[1][1], sampledValues[1][2]) *
                blendWeights[1] +
            Vec3f(sampledValues[2][0], sampledValues[2][1], sampledValues[2][2]) *
                blendWeights[2];
        (*outputs)[_kOut] = Value(result);
    } else {
        const Vec3f alphaWeights = _ComputeHexBlendWeights(
            Vec3f(1.0f), footprint.weights, falloff);
        Vec4f result(0.0f);
        result[0] = sampledValues[0][0] * blendWeights[0] +
                    sampledValues[1][0] * blendWeights[1] +
                    sampledValues[2][0] * blendWeights[2];
        result[1] = sampledValues[0][1] * blendWeights[0] +
                    sampledValues[1][1] * blendWeights[1] +
                    sampledValues[2][1] * blendWeights[2];
        result[2] = sampledValues[0][2] * blendWeights[0] +
                    sampledValues[1][2] * blendWeights[1] +
                    sampledValues[2][2] * blendWeights[2];
        result[3] = sampledValues[0][3] * alphaWeights[0] +
                    sampledValues[1][3] * alphaWeights[1] +
                    sampledValues[2][3] * alphaWeights[2];
        (*outputs)[_kOut] = Value(result);
    }
}
static Vec3f
_NormalToGradient(const Vec3f& normal, const Vec3f& perturbed)
{
    const float d = Dot(normal, perturbed);
    return (d * normal - perturbed) / std::max(1.0e-8f, std::abs(d));
}

static void
_EvalHexTiledNormalMap(const ParamMap& inputs,
                       const ShadingContext& ctx,
                       NodeOutputMap* outputs)
{
    const Vec3f normal = Get<Vec3f>(inputs, _kNormal, ctx.normal);
    const std::string filePath =
        Get<std::string>(inputs, _kFile, std::string());
    if (!ctx.textureSystem || filePath.empty()) {
        (*outputs)[_kOut] = Value(normal);
        return;
    }

    const Vec3f defaultValue =
        Get<Vec3f>(inputs, _kDefaultVal, Vec3f(0.5f, 0.5f, 1.0f));
    const Vec3f tangent = Get<Vec3f>(inputs, _kTangent, ctx.tangent);
    const Vec3f bitangent =
        Get<Vec3f>(inputs, _kBitangent, ctx.bitangent);
    const float strength = Get<float>(inputs, _kStrength, 1.0f);
    const bool flipG = Get<bool>(inputs, _kFlipG, false);
    const float falloff = Get<float>(inputs, _kFalloff, 0.5f);
    const _HexTileFootprint footprint =
        _ComputeHexTileFootprint(inputs, ctx);

    Texture2DRequest request;
    request.filePath = filePath;
    request.uAddressMode = TextureAddressMode::Periodic;
    request.vAddressMode = TextureAddressMode::Periodic;
    request.filterType = TextureFilterType::Linear;
    request.frame = ctx.frame;
    request.dataRole = TextureDataRole::NonColor;
    request.sourceColorSpace =
        Get<std::string>(inputs, _kFileColorSpace, std::string());
    request.channelCount = 3;
    request.channelFillValue = 0.0f;
    request.defaultValue = TextureValueTraits<Vec3f>::ToVec4(defaultValue);
    ApplyContextTextureBlur(ctx, &request);

    std::array<Vec3f, 3> tileNormals;
    for (int i = 0; i < 3; ++i) {
        request.st = footprint.st[i];
        request.dstdx = footprint.dstdx[i];
        request.dstdy = footprint.dstdy[i];
        const Vec4f sampled = ctx.textureSystem->Sample2D(request).value;
        Vec3f decoded(sampled[0], sampled[1], sampled[2]);
        if (flipG) {
            decoded[1] = 1.0f - decoded[1];
        }
        decoded = decoded * 2.0f - Vec3f(1.0f);

        Vec3f rotatedTangent = Rotate3d(
            tangent, -footprint.rotations[i] / kDegreesToRadians, normal);
        Vec3f rotatedBitangent = Rotate3d(
            bitangent, -footprint.rotations[i] / kDegreesToRadians, normal);
        rotatedTangent *= strength;
        rotatedBitangent *= strength;

        Vec3f orthogonalTangent =
            rotatedTangent - normal * Dot(rotatedTangent, normal);
        orthogonalTangent.normalize();
        Vec3f orthogonalBitangent =
            rotatedBitangent -
            normal * Dot(rotatedBitangent, normal) -
            orthogonalTangent * Dot(rotatedBitangent, orthogonalTangent);
        orthogonalBitangent.normalize();

        tileNormals[i] = (
            orthogonalTangent * decoded[0] +
            orthogonalBitangent * decoded[1] +
            normal * decoded[2]).normalized();
    }

    const Vec3f weights = _ComputeHexBlendWeights(
        Vec3f(1.0f), footprint.weights, falloff);
    const Vec3f gradient =
        _NormalToGradient(normal, tileNormals[0]) * weights[0] +
        _NormalToGradient(normal, tileNormals[1]) * weights[1] +
        _NormalToGradient(normal, tileNormals[2]) * weights[2];
    (*outputs)[_kOut] = Value((normal - gradient).normalized());
}

template<typename T, TextureDataRole DataRole, bool IsTiled>
static void
_EvalTextureNode(const ParamMap& inputs,
                 const ShadingContext& ctx,
                 NodeOutputMap* outputs)
{
    const T defaultValue = Get<T>(inputs, _kDefaultVal, Zero<T>());
    const Vec4f defaultColor = TextureValueTraits<T>::ToVec4(defaultValue);
    const std::string filePath = Get<std::string>(inputs, _kFile, std::string());

    if (!ctx.textureSystem || filePath.empty()) {
        (*outputs)[_kOut] = Value(defaultValue);
        return;
    }

    Vec2f st(0.0f);
    Vec2f dstdx(0.0f);
    Vec2f dstdy(0.0f);
    _ComputeTextureFootprint<IsTiled>(inputs, ctx, &st, &dstdx, &dstdy);

    const TextureAddressMode uAddressMode =
        GetAddressMode(inputs, _kUAddressMode, "periodic");
    const TextureAddressMode vAddressMode =
        GetAddressMode(inputs, _kVAddressMode, "periodic");
    if (UsesConstantDefaultOutside(uAddressMode, st[0]) ||
        UsesConstantDefaultOutside(vAddressMode, st[1])) {
        (*outputs)[_kOut] = Value(defaultValue);
        return;
    }

    Texture2DRequest request;
    request.filePath = filePath;
    request.layerName = Get<std::string>(inputs, _kLayer, std::string());
    request.st = st;
    request.dstdx = dstdx;
    request.dstdy = dstdy;
    request.uAddressMode = uAddressMode;
    request.vAddressMode = vAddressMode;
    request.filterType = GetFilterType(inputs, _kFilterType);
    request.frameRange = Get<std::string>(inputs, _kFrameRange, std::string());
    request.frameOffset = Get<int>(inputs, _kFrameOffset, 0);
    request.frameEndAction =
        GetAddressMode(inputs, _kFrameEndAction, "constant");
    request.frame = ctx.frame;
    request.dataRole = DataRole;
    request.sourceColorSpace =
        Get<std::string>(inputs, _kFileColorSpace, std::string());
    request.channelCount = TextureValueTraits<T>::kChannelCount;
    request.defaultValue = defaultColor;

    if constexpr (std::is_same_v<T, Vec4f>) {
        request.channelFillValue =
            (DataRole == TextureDataRole::Color) ? 1.0f : 0.0f;
    } else {
        request.channelFillValue = TextureValueTraits<T>::kFillValue;
    }
    ApplyContextTextureBlur(ctx, &request);

    const Texture2DResult sampled = ctx.textureSystem->Sample2D(request);
    (*outputs)[_kOut] = Value(TextureValueTraits<T>::FromVec4(sampled.value));
}
static Vec2f
_ComputeGltfImageCoord(const ParamMap& inputs,
                       const ShadingContext& ctx)
{
    const Vec2f texcoord =
        EvaluateInput<Vec2f>(inputs, _kTexcoord, ctx, ctx.texcoord);
    const Vec2f pivot =
        EvaluateInput<Vec2f>(inputs, _kPivot, ctx, Vec2f(0.0f, 1.0f));
    const Vec2f scale =
        EvaluateInput<Vec2f>(inputs, _kScale, ctx, Vec2f(1.0f));
    const float rotate =
        EvaluateInput<float>(inputs, _kRotate, ctx, 0.0f);
    const Vec2f offset =
        EvaluateInput<Vec2f>(inputs, _kOffset, ctx, Vec2f(0.0f));
    const int order =
        EvaluateInput<int>(inputs, _kOperationOrder, ctx, 0);

    Vec2f result = texcoord;
    result -= pivot;
    const Vec2f gltfOffset(-offset[0], offset[1]);
    if (order == 0) {
        result = CompMul(result, scale);
        result = Rotate2d(result, -rotate);
        result -= gltfOffset;
    } else {
        result -= gltfOffset;
        result = Rotate2d(result, -rotate);
        result = CompMul(result, scale);
    }
    result += pivot;
    return result;
}

template<
    typename T,
    TextureDataRole DataRole,
    bool ApplyFactor>
static void
_EvalGltfImage(const ParamMap& inputs,
               const ShadingContext& ctx,
               NodeOutputMap* outputs)
{
    const T defaultValue =
        EvaluateInput<T>(inputs, _kDefaultVal, ctx, T(0.0f));
    const std::string filePath =
        Get<std::string>(inputs, _kFile, std::string());
    if (!ctx.textureSystem || filePath.empty()) {
        T result = defaultValue;
        if constexpr (ApplyFactor) {
            const T factor =
                EvaluateInput<T>(inputs, _kFactor, ctx, T(1.0f));
            if constexpr (std::is_arithmetic_v<T>) {
                result *= factor;
            } else {
                result = CompMul(result, factor);
            }
        }
        (*outputs)[_kOut] = Value(result);
        return;
    }

    const Vec2f st =
        _ComputeGltfImageCoord(inputs, ctx);
    const Vec2f stDx =
        _ComputeGltfImageCoord(inputs, OffsetContextDx(ctx));
    const Vec2f stDy =
        _ComputeGltfImageCoord(inputs, OffsetContextDy(ctx));
    const TextureAddressMode uAddressMode =
        GetAddressMode(inputs, _kUAddressMode, "periodic");
    const TextureAddressMode vAddressMode =
        GetAddressMode(inputs, _kVAddressMode, "periodic");
    if (UsesConstantDefaultOutside(uAddressMode, st[0]) ||
        UsesConstantDefaultOutside(vAddressMode, st[1])) {
        T result = defaultValue;
        if constexpr (ApplyFactor) {
            const T factor =
                EvaluateInput<T>(inputs, _kFactor, ctx, T(1.0f));
            if constexpr (std::is_arithmetic_v<T>) {
                result *= factor;
            } else {
                result = CompMul(result, factor);
            }
        }
        (*outputs)[_kOut] = Value(result);
        return;
    }

    Texture2DRequest request;
    request.filePath = filePath;
    request.st = st;
    request.dstdx = stDx - st;
    request.dstdy = stDy - st;
    request.uAddressMode = uAddressMode;
    request.vAddressMode = vAddressMode;
    request.filterType = GetFilterType(inputs, _kFilterType);
    request.frame = ctx.frame;
    request.dataRole = DataRole;
    request.sourceColorSpace =
        Get<std::string>(inputs, _kFileColorSpace, std::string());
    request.channelCount = TextureValueTraits<T>::kChannelCount;
    if constexpr (DataRole == TextureDataRole::Color) {
        request.channelFillValue = 1.0f;
    } else {
        request.channelFillValue = TextureValueTraits<T>::kFillValue;
    }
    request.defaultValue = TextureValueTraits<T>::ToVec4(defaultValue);
    ApplyContextTextureBlur(ctx, &request);

    const Texture2DResult sampled = ctx.textureSystem->Sample2D(request);
    T result = TextureValueTraits<T>::FromVec4(sampled.value);
    if constexpr (ApplyFactor) {
        const T factor =
            EvaluateInput<T>(inputs, _kFactor, ctx, T(1.0f));
        if constexpr (std::is_arithmetic_v<T>) {
            result *= factor;
        } else {
            result = CompMul(result, factor);
        }
    }
    (*outputs)[_kOut] = Value(result);
}

static void
_EvalGltfImageVector3(const ParamMap& inputs,
                      const ShadingContext& ctx,
                      NodeOutputMap* outputs)
{
    _EvalGltfImage<Vec3f, TextureDataRole::NonColor, false>(
        inputs, ctx, outputs);
}

static void
_EvalGltfColorImage(const ParamMap& inputs,
                    const ShadingContext& ctx,
                    NodeOutputMap* outputs)
{
    NodeOutputMap imageOutputs;
    _EvalGltfImage<
        Vec4f, TextureDataRole::Color, false>(inputs, ctx, &imageOutputs);

    Vec4f image(0.0f);
    if (const Value* value = imageOutputs.Find(_kOut);
        value && ValueHolds<Vec4f>(*value)) {
        image = ValueGet<Vec4f>(*value);
    }
    const Vec4f color =
        EvaluateInput<Vec4f>(inputs, _kColor, ctx, Vec4f(1.0f));
    const Vec4f geomColor =
        EvaluateInput<Vec4f>(inputs, _kGeomColor, ctx, Vec4f(1.0f));
    const Vec4f result = CompMul(CompMul(image, color), geomColor);
    (*outputs)[_kOutColor] =
        Value(Vec3f(result[0], result[1], result[2]));
    (*outputs)[_kOutA] = Value(result[3]);
}

static void
_EvalGltfIridescenceThickness(const ParamMap& inputs,
                              const ShadingContext& ctx,
                              NodeOutputMap* outputs)
{
    NodeOutputMap imageOutputs;
    _EvalGltfImageVector3(inputs, ctx, &imageOutputs);

    float green = 0.0f;
    if (const Value* value = imageOutputs.Find(_kOut);
        value && ValueHolds<Vec3f>(*value)) {
        green = ValueGet<Vec3f>(*value)[1];
    }
    const float thicknessMin =
        EvaluateInput<float>(inputs, _kThicknessMin, ctx, 100.0f);
    const float thicknessMax =
        EvaluateInput<float>(inputs, _kThicknessMax, ctx, 400.0f);
    (*outputs)[_kOut] =
        Value(Mix(thicknessMax, thicknessMin, green));
}

static void
_EvalGltfAnisotropyImage(const ParamMap& inputs,
                         const ShadingContext& ctx,
                         NodeOutputMap* outputs)
{
    ParamMap imageInputs = inputs;
    if (!imageInputs.Find(_kDefaultVal)) {
        imageInputs[_kDefaultVal] = Value(Vec3f(1.0f, 0.5f, 1.0f));
    }
    NodeOutputMap imageOutputs;
    _EvalGltfImageVector3(imageInputs, ctx, &imageOutputs);

    Vec3f image(1.0f, 0.5f, 1.0f);
    if (const Value* value = imageOutputs.Find(_kOut);
        value && ValueHolds<Vec3f>(*value)) {
        image = ValueGet<Vec3f>(*value);
    }

    const float strength = EvaluateInput<float>(
        inputs, _kAnisotropyStrength, ctx, 1.0f);
    const float rotation = EvaluateInput<float>(
        inputs, _kAnisotropyRotation, ctx, 0.0f);
    const float directionX = image[0] * 2.0f - 1.0f;
    const float directionY = image[1] * 2.0f - 1.0f;

    (*outputs)[_kAnisotropyStrengthOut] = Value(strength * image[2]);
    (*outputs)[_kAnisotropyRotationOut] =
        Value(rotation + std::atan2(directionY, directionX));
}

static void
_EvalGltfNormalMap(const ParamMap& inputs,
                   const ShadingContext& ctx,
                   NodeOutputMap* outputs)
{
    NodeOutputMap imageOutputs;
    _EvalGltfImage<
        Vec3f, TextureDataRole::NonColor, false>(
            inputs, ctx, &imageOutputs);

    Vec3f value(0.0f);
    const Value* const imageOutput = imageOutputs.Find(_kOut);
    if (imageOutput && ValueHolds<Vec3f>(*imageOutput)) {
        value = ValueGet<Vec3f>(*imageOutput);
    }
    if (Dot(value, value) == 0.0f) {
        value = Vec3f(0.0f, 0.0f, 1.0f);
    } else {
        value = value * 2.0f - Vec3f(1.0f);
    }

    const Vec3f normal = ctx.normal;
    Vec3f tangent = ctx.dPdu - normal * Dot(ctx.dPdu, normal);
    if (Dot(tangent, tangent) < _kFloatEps * _kFloatEps) {
        tangent = ctx.tangent;
    } else {
        tangent.normalize();
    }

    // Match the generated OSL normalmap implementation: Bworld is dPdv,
    // then the normalmap function Gram-Schmidt orthogonalizes it against
    // Nworld and the normalized tangent.
    Vec3f bitangent =
        ctx.dPdv -
        normal * Dot(ctx.dPdv, normal) -
        tangent * Dot(ctx.dPdv, tangent);
    if (Dot(bitangent, bitangent) < _kFloatEps * _kFloatEps) {
        bitangent = ctx.bitangent;
    } else {
        bitangent.normalize();
    }

    Vec3f result =
        tangent * value[0] +
        bitangent * value[1] +
        normal * value[2];
    if (Dot(result, result) < _kFloatEps * _kFloatEps) {
        result = normal;
    } else {
        result.normalize();
    }
    (*outputs)[_kOut] = Value(result);
}

static void
_EvalLatLongImageNode(const ParamMap& inputs,
                      const ShadingContext& ctx,
                      NodeOutputMap* outputs)
{
    const Vec3f defaultValue = Get<Vec3f>(inputs, _kDefaultVal, Zero<Vec3f>());
    const Vec4f defaultColor = TextureValueTraits<Vec3f>::ToVec4(defaultValue);
    const std::string filePath = Get<std::string>(inputs, _kFile, std::string());

    if (!ctx.textureSystem || filePath.empty()) {
        (*outputs)[_kOut] = Value(defaultValue);
        return;
    }

    Vec2f st(0.0f);
    Vec2f dstdx(0.0f);
    Vec2f dstdy(0.0f);
    _ComputeLatLongFootprint(inputs, ctx, &st, &dstdx, &dstdy);

    Texture2DRequest request;
    request.filePath = filePath;
    request.st = st;
    request.dstdx = dstdx;
    request.dstdy = dstdy;
    request.uAddressMode = TextureAddressMode::Periodic;
    request.vAddressMode = TextureAddressMode::Mirror;
    request.filterType = TextureFilterType::Linear;
    request.frame = ctx.frame;
    request.dataRole = TextureDataRole::Color;
    request.sourceColorSpace =
        Get<std::string>(inputs, _kFileColorSpace, std::string());
    request.channelCount = TextureValueTraits<Vec3f>::kChannelCount;
    request.channelFillValue = TextureValueTraits<Vec3f>::kFillValue;
    request.defaultValue = defaultColor;
    ApplyContextTextureBlur(ctx, &request);

    const Texture2DResult sampled = ctx.textureSystem->Sample2D(request);
    (*outputs)[_kOut] = Value(TextureValueTraits<Vec3f>::FromVec4(sampled.value));
}

static Vec4f
_ApplyScaleBias(const Vec4f& value,
                const Vec4f& scale,
                const Vec4f& bias)
{
    return CompMul(value, scale) + bias;
}

static void
_SetUsdUvTextureOutputs(const Vec4f& value, NodeOutputMap* outputs)
{
    if (!outputs) {
        return;
    }

    (*outputs)[_kR] = Value(value[0]);
    (*outputs)[_kG] = Value(value[1]);
    (*outputs)[_kB] = Value(value[2]);
    (*outputs)[_kA] = Value(value[3]);
    (*outputs)[_kRgb] = Value(Vec3f(value[0], value[1], value[2]));
    (*outputs)[_kRgba] = Value(value);
}

static std::string
_ResolveUsdUvTextureSourceColorSpace(const std::string& sourceColorSpace)
{
    if (sourceColorSpace == "sRGB") {
        return "srgb_rec709_scene";
    }
    if (sourceColorSpace == "auto") {
        // Native UsdUVTexture defers this decision to the texture backend.
        return {};
    }
    return sourceColorSpace;
}

static std::string
_GetUsdSourceColorSpace(const ParamMap& inputs)
{
    const std::string explicitColorSpace =
        Get<std::string>(inputs, _kSourceColorSpace, std::string());
    if (!explicitColorSpace.empty()) {
        return explicitColorSpace;
    }

    return Get<std::string>(inputs, _kFileColorSpace, std::string());
}

static void
_EvalUsdUvTextureNode(const ParamMap& inputs,
                      const ShadingContext& ctx,
                      const bool enableAutomaticColorSpace,
                      NodeOutputMap* outputs)
{
    const Vec4f fallback =
        Get<Vec4f>(inputs, _kFallback, Vec4f(0.0f, 0.0f, 0.0f, 1.0f));
    const Vec4f scale = Get<Vec4f>(inputs, _kScale, Vec4f(1.0f));
    const Vec4f bias = Get<Vec4f>(inputs, _kBias, Vec4f(0.0f));
    const std::string filePath = Get<std::string>(inputs, _kFile, std::string());

    Vec4f sampledValue = fallback;

    if (ctx.textureSystem && !filePath.empty()) {
        Vec2f st(0.0f);
        Vec2f dstdx(0.0f);
        Vec2f dstdy(0.0f);
        _ComputeTextureFootprintForSlot<false>(
            inputs, _kSt, ctx, &st, &dstdx, &dstdy);

        Texture2DRequest request;
        request.filePath = filePath;
        request.st = st;
        request.dstdx = dstdx;
        request.dstdy = dstdy;
        request.uAddressMode = GetUsdAddressMode(inputs, _kWrapS, "useMetadata");
        request.vAddressMode = GetUsdAddressMode(inputs, _kWrapT, "useMetadata");
        request.filterType = TextureFilterType::Linear;
        request.frame = ctx.frame;
        request.dataRole = TextureDataRole::Color;
        const std::string sourceColorSpace = _GetUsdSourceColorSpace(inputs);
        request.sourceColorSpace =
            _ResolveUsdUvTextureSourceColorSpace(sourceColorSpace);
        request.inferSrgbFromFile =
            enableAutomaticColorSpace &&
            (sourceColorSpace.empty() || sourceColorSpace == "auto");
        request.channelCount = 4;
        request.channelFillValue = 1.0f;
        request.defaultValue = fallback;
        ApplyContextTextureBlur(ctx, &request);

        const Texture2DResult sampled = ctx.textureSystem->Sample2D(request);
        sampledValue = sampled.value;
    }

    _SetUsdUvTextureOutputs(
        _ApplyScaleBias(sampledValue, scale, bias), outputs);
}

static void
_EvalNativeUsdUvTextureNode(const ParamMap& inputs,
                            const ShadingContext& ctx,
                            NodeOutputMap* outputs)
{
    _EvalUsdUvTextureNode(
        inputs, ctx, /* enableAutomaticColorSpace = */ true, outputs);
}

static void
_EvalMaterialXUsdUvTextureNode(const ParamMap& inputs,
                               const ShadingContext& ctx,
                               NodeOutputMap* outputs)
{
    _EvalUsdUvTextureNode(
        inputs, ctx, /* enableAutomaticColorSpace = */ false, outputs);
}

}  // namespace

// ---- Registration --------------------------------------------------------

#define _REG(...) reg.Register(__VA_ARGS__)

void
RegisterTextureNodes(NodeRegistry& reg)
{
    _REG("ND_image_float",
         &_EvalTextureNode<float, TextureDataRole::NonColor, false>);
    _REG("ND_image_color3",
         &_EvalTextureNode<Vec3f, TextureDataRole::Color, false>);
    _REG("ND_image_color4",
         &_EvalTextureNode<Vec4f, TextureDataRole::Color, false>);
    _REG("ND_image_vector2",
         &_EvalTextureNode<Vec2f, TextureDataRole::NonColor, false>);
    _REG("ND_image_vector3",
         &_EvalTextureNode<Vec3f, TextureDataRole::NonColor, false>);
    _REG("ND_image_vector4",
         &_EvalTextureNode<Vec4f, TextureDataRole::NonColor, false>);

    _REG("ND_tiledimage_float",
         &_EvalTextureNode<float, TextureDataRole::NonColor, true>);
    _REG("ND_tiledimage_color3",
         &_EvalTextureNode<Vec3f, TextureDataRole::Color, true>);
    _REG("ND_tiledimage_color4",
         &_EvalTextureNode<Vec4f, TextureDataRole::Color, true>);
    _REG("ND_tiledimage_vector2",
         &_EvalTextureNode<Vec2f, TextureDataRole::NonColor, true>);
    _REG("ND_tiledimage_vector3",
         &_EvalTextureNode<Vec3f, TextureDataRole::NonColor, true>);
    _REG("ND_tiledimage_vector4",
         &_EvalTextureNode<Vec4f, TextureDataRole::NonColor, true>);

    _REG("ND_latlongimage", &_EvalLatLongImageNode);
    _REG("ND_hextiledimage_color3", &_EvalHexTiledImageNode<Vec3f>);
    _REG("ND_hextiledimage_color4", &_EvalHexTiledImageNode<Vec4f>);
    _REG("ND_hextilednormalmap_vector3", &_EvalHexTiledNormalMap);
    _REG("ND_gltf_colorimage", &_EvalGltfColorImage);
    _REG("ND_gltf_image_color3_color3_1_0",
         &_EvalGltfImage<Vec3f, TextureDataRole::Color, true>);
    _REG("ND_gltf_image_color4_color4_1_0",
         &_EvalGltfImage<Vec4f, TextureDataRole::Color, true>);
    _REG("ND_gltf_image_float_float_1_0",
         &_EvalGltfImage<float, TextureDataRole::NonColor, true>);
    _REG("ND_gltf_image_vector3_vector3_1_0", &_EvalGltfImageVector3);
    _REG("ND_gltf_iridescence_thickness_float_1_0",
         &_EvalGltfIridescenceThickness);
    _REG("ND_gltf_anisotropy_image", &_EvalGltfAnisotropyImage);
    _REG("ND_gltf_normalmap_vector3_1_0", &_EvalGltfNormalMap);
    _REG("ND_gltf_normalmap_vector3", &_EvalGltfNormalMap);
    _REG("ND_gltf_normalmap", &_EvalGltfNormalMap);

    _REG("UsdUVTexture", &_EvalNativeUsdUvTextureNode);
    _REG("ND_UsdUVTexture", &_EvalMaterialXUsdUvTextureNode);
    _REG("ND_UsdUVTexture_23", &_EvalMaterialXUsdUvTextureNode);
}

#undef _REG

}  // namespace mxcpp
