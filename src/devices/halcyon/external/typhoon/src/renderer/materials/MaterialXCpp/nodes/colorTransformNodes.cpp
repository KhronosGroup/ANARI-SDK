//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "colorTransformNodes.h"

#include <renderer/materials/MaterialXCpp/nodeRegistry.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace mxcpp {

static const SlotName _kIn("in");
static const SlotName _kOut("out");

static constexpr float _kAdobeRgbGamma = 563.0f / 256.0f;

static const Mat3f _kAcescgToLinRec709Matrix(
    1.705050992658f, -0.130256417507f, -0.024003356805f,
   -0.621792120657f,  1.140804736575f, -0.128968976065f,
   -0.083258872001f, -0.010548319068f,  1.15297233287f);
static const Mat3f _kLinAdobeRgbToLinRec709Matrix(
    1.39835574f,  0.0f,         0.0f,
   -0.398355744f, 1.0f,         -0.0429289893f,
    0.0f,         0.0f,          1.04292899f);
static const Mat3f _kLinDisplayP3ToLinRec709Matrix(
    1.22493029f, -0.04205868f, -0.01964128f,
   -0.22492968f, 1.04205894f, -0.07864794f,
    0.00000006f, -0.00000001f, 1.09828925f);

static bool
_TryExternalColorTransform(const ShadingContext& ctx,
                           const char* sourceColorSpace,
                           const char* targetColorSpace,
                           const Vec3f& in,
                           Vec3f* out)
{
    return ctx.colorTransform &&
           ctx.colorTransform(
               ctx.colorTransformUserData,
               sourceColorSpace,
               targetColorSpace,
               in,
               out);
}

static Vec3f
_ClampNonNegative(const Vec3f& in)
{
    return Vec3f(
        std::max(in[0], 0.0f),
        std::max(in[1], 0.0f),
        std::max(in[2], 0.0f));
}

static Vec3f
_ApplyGammaToLinear(const Vec3f& in, const float gamma)
{
    return Vec3f(
        std::pow(in[0], gamma),
        std::pow(in[1], gamma),
        std::pow(in[2], gamma));
}

static float
_ApplySrgbTextureToLinearRec709(const float in)
{
    if (in > 0.04045f) {
        return std::pow(std::max(in + 0.055f, 0.0f) / 1.055f, 2.4f);
    }
    return in / 12.92f;
}

static Vec3f
_ApplySrgbTextureToLinearRec709(const Vec3f& in)
{
    return Vec3f(
        _ApplySrgbTextureToLinearRec709(in[0]),
        _ApplySrgbTextureToLinearRec709(in[1]),
        _ApplySrgbTextureToLinearRec709(in[2]));
}

static Vec3f
_ApplyMatrix(const Vec3f& in, const Mat3f& matrix)
{
    Vec3f result(0.0f);
    for (int i = 0; i < 3; ++i) {
        result[i] =
            in[0] * matrix[0][i] +
            in[1] * matrix[1][i] +
            in[2] * matrix[2][i];
    }
    return result;
}

static Vec3f
_TransformG18Rec709ToLinRec709(const ShadingContext& ctx, const Vec3f& in)
{
    const Vec3f clamped = _ClampNonNegative(in);
    Vec3f result;
    if (_TryExternalColorTransform(
            ctx, "g18_rec709", "lin_rec709", clamped, &result)) {
        return result;
    }
    return _ApplyGammaToLinear(clamped, 1.8f);
}

static Vec3f
_TransformG22Rec709ToLinRec709(const ShadingContext& ctx, const Vec3f& in)
{
    const Vec3f clamped = _ClampNonNegative(in);
    Vec3f result;
    if (_TryExternalColorTransform(
            ctx, "g22_rec709", "lin_rec709", clamped, &result)) {
        return result;
    }
    return _ApplyGammaToLinear(clamped, 2.2f);
}

static Vec3f
_TransformRec709DisplayToLinRec709(
    const ShadingContext& ctx, const Vec3f& in)
{
    const Vec3f clamped = _ClampNonNegative(in);
    Vec3f result;
    if (_TryExternalColorTransform(
            ctx, "rec709_display", "lin_rec709", clamped, &result)) {
        return result;
    }
    return _ApplyGammaToLinear(clamped, 2.4f);
}

static Vec3f
_TransformAcescgToLinRec709(const ShadingContext& ctx, const Vec3f& in)
{
    Vec3f result;
    if (_TryExternalColorTransform(
            ctx, "acescg", "lin_rec709", in, &result)) {
        return result;
    }
    return _ApplyMatrix(in, _kAcescgToLinRec709Matrix);
}

static Vec3f
_TransformG22Ap1ToLinRec709(const ShadingContext& ctx, const Vec3f& in)
{
    const Vec3f clamped = _ClampNonNegative(in);
    Vec3f result;
    if (_TryExternalColorTransform(
            ctx, "g22_ap1", "lin_rec709", clamped, &result)) {
        return result;
    }
    return _ApplyMatrix(
        _ApplyGammaToLinear(clamped, 2.2f),
        _kAcescgToLinRec709Matrix);
}

static Vec3f
_TransformSrgbTextureToLinRec709(const ShadingContext& ctx, const Vec3f& in)
{
    Vec3f result;
    if (_TryExternalColorTransform(
            ctx, "srgb_texture", "lin_rec709", in, &result)) {
        return result;
    }
    return _ApplySrgbTextureToLinearRec709(in);
}

static Vec3f
_TransformLinAdobeRgbToLinRec709(const ShadingContext& ctx, const Vec3f& in)
{
    Vec3f result;
    if (_TryExternalColorTransform(
            ctx, "lin_adobergb", "lin_rec709", in, &result)) {
        return result;
    }
    return _ApplyMatrix(in, _kLinAdobeRgbToLinRec709Matrix);
}

static Vec3f
_TransformAdobeRgbToLinRec709(const ShadingContext& ctx, const Vec3f& in)
{
    const Vec3f clamped = _ClampNonNegative(in);
    Vec3f result;
    if (_TryExternalColorTransform(
            ctx, "adobergb", "lin_rec709", clamped, &result)) {
        return result;
    }
    return _ApplyMatrix(
        _ApplyGammaToLinear(clamped, _kAdobeRgbGamma),
        _kLinAdobeRgbToLinRec709Matrix);
}

static Vec3f
_TransformSrgbDisplayP3ToLinRec709(const ShadingContext& ctx, const Vec3f& in)
{
    Vec3f result;
    if (_TryExternalColorTransform(
            ctx, "srgb_displayp3", "lin_rec709", in, &result)) {
        return result;
    }
    return _ApplyMatrix(
        _ApplySrgbTextureToLinearRec709(in),
        _kLinDisplayP3ToLinRec709Matrix);
}

static Vec3f
_TransformLinDisplayP3ToLinRec709(const ShadingContext& ctx, const Vec3f& in)
{
    Vec3f result;
    if (_TryExternalColorTransform(
            ctx, "lin_displayp3", "lin_rec709", in, &result)) {
        return result;
    }
    return _ApplyMatrix(in, _kLinDisplayP3ToLinRec709Matrix);
}

template<Vec3f (*Transform)(const ShadingContext&, const Vec3f&)>
static void
_EvalColorTransformColor3(const ParamMap& inputs, const ShadingContext& ctx,
                          NodeOutputMap* outputs)
{
    const Vec3f in = Get<Vec3f>(inputs, _kIn, Vec3f(0.0f));
    (*outputs)[_kOut] =
        Value(ctx.bypassColorTransforms ? in : Transform(ctx, in));
}

template<Vec3f (*Transform)(const ShadingContext&, const Vec3f&)>
static void
_EvalColorTransformColor4(const ParamMap& inputs, const ShadingContext& ctx,
                          NodeOutputMap* outputs)
{
    const Vec4f in = Get<Vec4f>(inputs, _kIn, Vec4f(0.0f));
    const Vec3f rgb(in[0], in[1], in[2]);
    const Vec3f transformed =
        ctx.bypassColorTransforms ? rgb : Transform(ctx, rgb);
    (*outputs)[_kOut] = Value(
        Vec4f(transformed[0], transformed[1], transformed[2], in[3]));
}

#define _REG(name, fn) reg.Register(name, fn)

void
RegisterColorTransformNodes(NodeRegistry& reg)
{
    _REG("ND_g18_rec709_to_lin_rec709_color3",
         &_EvalColorTransformColor3<_TransformG18Rec709ToLinRec709>);
    _REG("ND_g18_rec709_to_lin_rec709_color4",
         &_EvalColorTransformColor4<_TransformG18Rec709ToLinRec709>);
    _REG("ND_g22_rec709_to_lin_rec709_color3",
         &_EvalColorTransformColor3<_TransformG22Rec709ToLinRec709>);
    _REG("ND_g22_rec709_to_lin_rec709_color4",
         &_EvalColorTransformColor4<_TransformG22Rec709ToLinRec709>);
    _REG("ND_rec709_display_to_lin_rec709_color3",
         &_EvalColorTransformColor3<_TransformRec709DisplayToLinRec709>);
    _REG("ND_rec709_display_to_lin_rec709_color4",
         &_EvalColorTransformColor4<_TransformRec709DisplayToLinRec709>);
    _REG("ND_acescg_to_lin_rec709_color3",
         &_EvalColorTransformColor3<_TransformAcescgToLinRec709>);
    _REG("ND_acescg_to_lin_rec709_color4",
         &_EvalColorTransformColor4<_TransformAcescgToLinRec709>);
    _REG("ND_g22_ap1_to_lin_rec709_color3",
         &_EvalColorTransformColor3<_TransformG22Ap1ToLinRec709>);
    _REG("ND_g22_ap1_to_lin_rec709_color4",
         &_EvalColorTransformColor4<_TransformG22Ap1ToLinRec709>);
    _REG("ND_srgb_texture_to_lin_rec709_color3",
         &_EvalColorTransformColor3<_TransformSrgbTextureToLinRec709>);
    _REG("ND_srgb_texture_to_lin_rec709_color4",
         &_EvalColorTransformColor4<_TransformSrgbTextureToLinRec709>);
    _REG("ND_lin_adobergb_to_lin_rec709_color3",
         &_EvalColorTransformColor3<_TransformLinAdobeRgbToLinRec709>);
    _REG("ND_lin_adobergb_to_lin_rec709_color4",
         &_EvalColorTransformColor4<_TransformLinAdobeRgbToLinRec709>);
    _REG("ND_adobergb_to_lin_rec709_color3",
         &_EvalColorTransformColor3<_TransformAdobeRgbToLinRec709>);
    _REG("ND_adobergb_to_lin_rec709_color4",
         &_EvalColorTransformColor4<_TransformAdobeRgbToLinRec709>);
    _REG("ND_srgb_displayp3_to_lin_rec709_color3",
         &_EvalColorTransformColor3<_TransformSrgbDisplayP3ToLinRec709>);
    _REG("ND_srgb_displayp3_to_lin_rec709_color4",
         &_EvalColorTransformColor4<_TransformSrgbDisplayP3ToLinRec709>);
    _REG("ND_lin_displayp3_to_lin_rec709_color3",
         &_EvalColorTransformColor3<_TransformLinDisplayP3ToLinRec709>);
    _REG("ND_lin_displayp3_to_lin_rec709_color4",
         &_EvalColorTransformColor4<_TransformLinDisplayP3ToLinRec709>);
}

#undef _REG

}  // namespace mxcpp
