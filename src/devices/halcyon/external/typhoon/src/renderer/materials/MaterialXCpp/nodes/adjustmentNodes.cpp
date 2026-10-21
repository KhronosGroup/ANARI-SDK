//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "adjustmentNodes.h"

#include <renderer/materials/MaterialXCpp/nodeRegistry.h>
#include <renderer/materials/MaterialXCpp/nodes/helpers/colorHelpers.h>
#include <renderer/materials/MaterialXCpp/nodes/helpers/mathHelpers.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace mxcpp {

static const SlotName _kIn("in");
static const SlotName _kLow("low");
static const SlotName _kHigh("high");
static const SlotName _kInlow("inlow");
static const SlotName _kInhigh("inhigh");
static const SlotName _kOutlow("outlow");
static const SlotName _kOuthigh("outhigh");
static const SlotName _kOut("out");
static const SlotName _kAmount("amount");
static const SlotName _kCenter("center");
static const SlotName _kPivot("pivot");
static const SlotName _kHue("hue");
static const SlotName _kSaturation("saturation");
static const SlotName _kGamma("gamma");
static const SlotName _kLift("lift");
static const SlotName _kGain("gain");
static const SlotName _kContrast("contrast");
static const SlotName _kContrastpivot("contrastpivot");
static const SlotName _kExposure("exposure");
static const SlotName _kLumacoeffs("lumacoeffs");
static const SlotName _kDoclamp("doclamp");

// ---- Remap / Smoothstep --------------------------------------------------

template<typename T>
static T
_RemapComponents(const T& v, const T& inLo, const T& inHi,
                 const T& outLo, const T& outHi)
{
    T result;
    for (unsigned int i = 0; i < T::dimensions(); ++i) {
        result[i] = Remap(v[i], inLo[i], inHi[i], outLo[i], outHi[i]);
    }
    return result;
}

static void
_EvalRemapFloat(const ParamMap& inputs, const ShadingContext&,
                NodeOutputMap* outputs)
{
    float v  = Get<float>(inputs, _kIn,     0.0f);
    float il = Get<float>(inputs, _kInlow,  0.0f);
    float ih = Get<float>(inputs, _kInhigh, 1.0f);
    float ol = Get<float>(inputs, _kOutlow, 0.0f);
    float oh = Get<float>(inputs, _kOuthigh, 1.0f);
    (*outputs)[_kOut] = Value(Remap(v, il, ih, ol, oh));
}

template<typename T>
static void
_EvalRemap(const ParamMap& inputs, const ShadingContext&,
           NodeOutputMap* outputs)
{
    const T v = Get<T>(inputs, _kIn, Zero<T>());
    const T inLo = Get<T>(inputs, _kInlow, Zero<T>());
    const T inHi = Get<T>(inputs, _kInhigh, One<T>());
    const T outLo = Get<T>(inputs, _kOutlow, Zero<T>());
    const T outHi = Get<T>(inputs, _kOuthigh, One<T>());
    (*outputs)[_kOut] = Value(
        _RemapComponents(v, inLo, inHi, outLo, outHi));
}

template<typename T>
static void
_EvalRemapFA(const ParamMap& inputs, const ShadingContext&,
             NodeOutputMap* outputs)
{
    const T v = Get<T>(inputs, _kIn, Zero<T>());
    const float inLo = Get<float>(inputs, _kInlow, 0.0f);
    const float inHi = Get<float>(inputs, _kInhigh, 1.0f);
    const float outLo = Get<float>(inputs, _kOutlow, 0.0f);
    const float outHi = Get<float>(inputs, _kOuthigh, 1.0f);
    (*outputs)[_kOut] = Value(_RemapComponents(
        v, T(inLo), T(inHi), T(outLo), T(outHi)));
}

template<typename T>
static T
_SmoothstepComponents(const T& lo, const T& hi, const T& v)
{
    T result;
    for (unsigned int i = 0; i < T::dimensions(); ++i) {
        result[i] = Smoothstep(lo[i], hi[i], v[i]);
    }
    return result;
}

static void
_EvalSmoothstepFloat(const ParamMap& inputs, const ShadingContext&,
                     NodeOutputMap* outputs)
{
    const float v = Get<float>(inputs, _kIn, 0.0f);
    const float lo = Get<float>(inputs, _kLow, 0.0f);
    const float hi = Get<float>(inputs, _kHigh, 1.0f);
    (*outputs)[_kOut] = Value(Smoothstep(lo, hi, v));
}

template<typename T>
static void
_EvalSmoothstep(const ParamMap& inputs, const ShadingContext&,
                NodeOutputMap* outputs)
{
    const T v = Get<T>(inputs, _kIn, Zero<T>());
    const T lo = Get<T>(inputs, _kLow, Zero<T>());
    const T hi = Get<T>(inputs, _kHigh, One<T>());
    (*outputs)[_kOut] = Value(_SmoothstepComponents(lo, hi, v));
}

template<typename T>
static void
_EvalSmoothstepFA(const ParamMap& inputs, const ShadingContext&,
                  NodeOutputMap* outputs)
{
    const T v = Get<T>(inputs, _kIn, Zero<T>());
    const float lo = Get<float>(inputs, _kLow, 0.0f);
    const float hi = Get<float>(inputs, _kHigh, 1.0f);
    (*outputs)[_kOut] = Value(_SmoothstepComponents(T(lo), T(hi), v));
}

// ---- Contrast ------------------------------------------------------------

template<typename T>
static T
_GetContrastPivot(const ParamMap& inputs, const T& defaultValue)
{
    if (inputs.Find(_kPivot)) {
        return Get<T>(inputs, _kPivot, defaultValue);
    }
    return Get<T>(inputs, _kCenter, defaultValue);
}

static float
_GetContrastPivotFloat(const ParamMap& inputs, float defaultValue)
{
    if (inputs.Find(_kPivot)) {
        return Get<float>(inputs, _kPivot, defaultValue);
    }
    return Get<float>(inputs, _kCenter, defaultValue);
}

template<typename T>
static void
_EvalContrast(const ParamMap& inputs, const ShadingContext&,
              NodeOutputMap* outputs)
{
    T v      = Get<T>(inputs, _kIn,     Zero<T>());
    T amount = Get<T>(inputs, _kAmount, One<T>());
    T pivot  = _GetContrastPivot<T>(inputs, T(0.5f));
    // contrast = pivot + (v - pivot) * amount
    (*outputs)[_kOut] = Value(
        pivot + CompMul(v - pivot, amount));
}

template<typename T>
static void
_EvalContrastFA(const ParamMap& inputs, const ShadingContext&,
                NodeOutputMap* outputs)
{
    const T v = Get<T>(inputs, _kIn, Zero<T>());
    const float amount = Get<float>(inputs, _kAmount, 1.0f);
    const float pivot = _GetContrastPivotFloat(inputs, 0.5f);
    (*outputs)[_kOut] = Value(T(pivot) + (v - T(pivot)) * amount);
}

static void
_EvalContrastFloat(const ParamMap& inputs, const ShadingContext&,
                   NodeOutputMap* outputs)
{
    float v      = Get<float>(inputs, _kIn,     0.0f);
    float amount = Get<float>(inputs, _kAmount, 1.0f);
    float pivot  = _GetContrastPivotFloat(inputs, 0.5f);
    (*outputs)[_kOut] = Value(pivot + (v - pivot) * amount);
}

// ---- Saturate ------------------------------------------------------------

static void
_EvalSaturateColor3(const ParamMap& inputs, const ShadingContext&,
                    NodeOutputMap* outputs)
{
    Vec3f c = Get<Vec3f>(inputs, _kIn, Vec3f(0.0f));
    float amount = Get<float>(inputs, _kAmount, 1.0f);
    Vec3f luma = Get<Vec3f>(inputs, _kLumacoeffs, AcesCgLumaCoeffs());
    float gray = luma[0] * c[0] + luma[1] * c[1] + luma[2] * c[2];
    Vec3f g(gray);
    (*outputs)[_kOut] = Value(g + (c - g) * amount);
}

static void
_EvalSaturateColor4(const ParamMap& inputs, const ShadingContext&,
                    NodeOutputMap* outputs)
{
    Vec4f c = Get<Vec4f>(inputs, _kIn, Vec4f(0.0f));
    float amount = Get<float>(inputs, _kAmount, 1.0f);
    Vec3f luma = Get<Vec3f>(inputs, _kLumacoeffs, AcesCgLumaCoeffs());
    float gray = luma[0] * c[0] + luma[1] * c[1] + luma[2] * c[2];
    float r = gray + (c[0] - gray) * amount;
    float g = gray + (c[1] - gray) * amount;
    float b = gray + (c[2] - gray) * amount;
    (*outputs)[_kOut] = Value(Vec4f(r, g, b, c[3]));
}

static float
_Luminance(const Vec3f& c, const Vec3f& lumaCoeffs)
{
    return lumaCoeffs[0] * c[0] +
           lumaCoeffs[1] * c[1] +
           lumaCoeffs[2] * c[2];
}

static void
_EvalLuminanceColor3(const ParamMap& inputs, const ShadingContext&,
                     NodeOutputMap* outputs)
{
    const Vec3f c = Get<Vec3f>(inputs, _kIn, Vec3f(0.0f));
    const Vec3f lumaCoeffs = Get<Vec3f>(
        inputs, _kLumacoeffs, Rec709LumaCoeffs());
    const float luma = _Luminance(c, lumaCoeffs);
    (*outputs)[_kOut] = Value(Vec3f(luma));
}

static void
_EvalLuminanceColor4(const ParamMap& inputs, const ShadingContext&,
                     NodeOutputMap* outputs)
{
    const Vec4f c = Get<Vec4f>(inputs, _kIn, Vec4f(0.0f));
    const Vec3f lumaCoeffs = Get<Vec3f>(
        inputs, _kLumacoeffs, Rec709LumaCoeffs());
    const float luma = _Luminance(Vec3f(c[0], c[1], c[2]), lumaCoeffs);
    (*outputs)[_kOut] = Value(Vec4f(luma, luma, luma, c[3]));
}

static void
_EvalRgbToHsv(const ParamMap& inputs, const ShadingContext&,
              NodeOutputMap* outputs)
{
    const Vec3f rgb = Get<Vec3f>(inputs, _kIn, Vec3f(0.0f));
    (*outputs)[_kOut] = Value(RgbToHsv(rgb));
}

static void
_EvalRgbToHsvColor4(const ParamMap& inputs, const ShadingContext&,
                    NodeOutputMap* outputs)
{
    const Vec4f rgba = Get<Vec4f>(inputs, _kIn, Vec4f(0.0f));
    const Vec3f hsv = RgbToHsv(Vec3f(rgba[0], rgba[1], rgba[2]));
    (*outputs)[_kOut] = Value(Vec4f(hsv[0], hsv[1], hsv[2], rgba[3]));
}

static void
_EvalHsvToRgb(const ParamMap& inputs, const ShadingContext&,
              NodeOutputMap* outputs)
{
    const Vec3f hsv = Get<Vec3f>(inputs, _kIn, Vec3f(0.0f));
    (*outputs)[_kOut] = Value(HsvToRgb(hsv));
}

static void
_EvalHsvToRgbColor4(const ParamMap& inputs, const ShadingContext&,
                    NodeOutputMap* outputs)
{
    const Vec4f hsva = Get<Vec4f>(inputs, _kIn, Vec4f(0.0f));
    const Vec3f rgb = HsvToRgb(Vec3f(hsva[0], hsva[1], hsva[2]));
    (*outputs)[_kOut] = Value(Vec4f(rgb[0], rgb[1], rgb[2], hsva[3]));
}

// ---- HSV Adjust ----------------------------------------------------------

static void
_EvalHsvadjustColor3(const ParamMap& inputs, const ShadingContext&,
                     NodeOutputMap* outputs)
{
    Vec3f c = Get<Vec3f>(inputs, _kIn, Vec3f(0.0f));
    Vec3f amount = Get<Vec3f>(inputs, _kAmount, Vec3f(0.0f, 1.0f, 1.0f));
    Vec3f hsv = RgbToHsv(c);
    // amount = (hue_add, sat_mul, val_mul)
    hsv[0] = std::fmod(hsv[0] + amount[0], 1.0f);
    if (hsv[0] < 0.0f) hsv[0] += 1.0f;
    hsv[1] *= amount[1];
    hsv[2] *= amount[2];
    (*outputs)[_kOut] = Value(HsvToRgb(hsv));
}

static void
_EvalHsvadjustColor4(const ParamMap& inputs, const ShadingContext&,
                     NodeOutputMap* outputs)
{
    Vec4f c = Get<Vec4f>(inputs, _kIn, Vec4f(0.0f));
    Vec3f amount = Get<Vec3f>(inputs, _kAmount, Vec3f(0.0f, 1.0f, 1.0f));
    Vec3f hsv = RgbToHsv(Vec3f(c[0], c[1], c[2]));
    hsv[0] = std::fmod(hsv[0] + amount[0], 1.0f);
    if (hsv[0] < 0.0f) hsv[0] += 1.0f;
    hsv[1] *= amount[1];
    hsv[2] *= amount[2];
    Vec3f rgb = HsvToRgb(hsv);
    (*outputs)[_kOut] = Value(Vec4f(rgb[0], rgb[1], rgb[2], c[3]));
}

// ---- Color Correct -------------------------------------------------------

static Vec3f
_ColorCorrectRgb(Vec3f c, float hue, float saturation, float gamma,
                 float lift, float gain, float contrast,
                 float contrastpivot, float exposure)
{
    // Match NG_colorcorrect_color3 in the MaterialX standard library.
    // Hue is an HSV adjustment, while saturation is the separate
    // luminance-based saturate node.
    Vec3f hsv = RgbToHsv(c);
    hsv[0] = std::fmod(hsv[0] + hue, 1.0f);
    if (hsv[0] < 0.0f) hsv[0] += 1.0f;
    c = HsvToRgb(hsv);

    const Vec3f luma = AcesCgLumaCoeffs();
    const float gray = Dot(c, luma);
    c = Vec3f(gray) + (c - Vec3f(gray)) * saturation;

    const float invGamma = 1.0f / gamma;
    for (int channel = 0; channel < 3; ++channel) {
        c[channel] =
            c[channel] > 0.0f && invGamma != 0.0f
            ? std::pow(c[channel], invGamma)
            : 0.0f;
    }

    c = c * (Vec3f(1.0f) - Vec3f(lift)) + Vec3f(lift);
    c *= gain;
    c = Vec3f(contrastpivot) + (c - Vec3f(contrastpivot)) * contrast;
    c *= std::pow(2.0f, exposure);

    return c;
}

static void
_EvalColorcorrectColor3(const ParamMap& inputs, const ShadingContext&,
                        NodeOutputMap* outputs)
{
    Vec3f c              = Get<Vec3f>(inputs, _kIn, Vec3f(1.0f));
    float hue            = Get<float>(inputs, _kHue, 0.0f);
    float saturation     = Get<float>(inputs, _kSaturation, 1.0f);
    float gamma          = Get<float>(inputs, _kGamma, 1.0f);
    float lift           = Get<float>(inputs, _kLift, 0.0f);
    float gain           = Get<float>(inputs, _kGain, 1.0f);
    float contrast       = Get<float>(inputs, _kContrast, 1.0f);
    float contrastpivot  = Get<float>(inputs, _kContrastpivot, 0.5f);
    float exposure       = Get<float>(inputs, _kExposure, 0.0f);

    (*outputs)[_kOut] = Value(
        _ColorCorrectRgb(c, hue, saturation, gamma,
                         lift, gain, contrast, contrastpivot, exposure));
}

static void
_EvalColorcorrectColor4(const ParamMap& inputs, const ShadingContext&,
                        NodeOutputMap* outputs)
{
    Vec4f c              = Get<Vec4f>(inputs, _kIn, Vec4f(1.0f, 1.0f, 1.0f, 0.0f));
    float hue            = Get<float>(inputs, _kHue, 0.0f);
    float saturation     = Get<float>(inputs, _kSaturation, 1.0f);
    float gamma          = Get<float>(inputs, _kGamma, 1.0f);
    float lift           = Get<float>(inputs, _kLift, 0.0f);
    float gain           = Get<float>(inputs, _kGain, 1.0f);
    float contrast       = Get<float>(inputs, _kContrast, 1.0f);
    float contrastpivot  = Get<float>(inputs, _kContrastpivot, 0.5f);
    float exposure       = Get<float>(inputs, _kExposure, 0.0f);

    Vec3f rgb = _ColorCorrectRgb(
        Vec3f(c[0], c[1], c[2]), hue, saturation, gamma,
        lift, gain, contrast, contrastpivot, exposure);
    (*outputs)[_kOut] = Value(Vec4f(rgb[0], rgb[1], rgb[2], c[3]));
}

// ---- Range ---------------------------------------------------------------

// Component-wise safe power: pow(max(v, 0), exponent).
static float _SafePow(float v, float e) {
    return (v > 0.0f && e != 0.0f) ? std::pow(v, e) : 0.0f;
}

template<typename T>
static T _RangeImpl(const T& v, const T& inLo, const T& inHi,
                    const T& gamma, const T& outLo, const T& outHi);

template<>
float
_RangeImpl<float>(const float& v, const float& inLo, const float& inHi,
                  const float& gamma, const float& outLo, const float& outHi)
{
    if (inHi == inLo) return outLo;
    float t = (v - inLo) / (inHi - inLo);
    t = (gamma != 1.0f) ? _SafePow(t, 1.0f / gamma) : t;
    return outLo + t * (outHi - outLo);
}

template<>
Vec2f
_RangeImpl<Vec2f>(const Vec2f& v, const Vec2f& inLo, const Vec2f& inHi,
                  const Vec2f& gamma, const Vec2f& outLo, const Vec2f& outHi)
{
    return Vec2f(
        _RangeImpl<float>(v[0], inLo[0], inHi[0], gamma[0], outLo[0], outHi[0]),
        _RangeImpl<float>(v[1], inLo[1], inHi[1], gamma[1], outLo[1], outHi[1]));
}

template<>
Vec3f
_RangeImpl<Vec3f>(const Vec3f& v, const Vec3f& inLo, const Vec3f& inHi,
                  const Vec3f& gamma, const Vec3f& outLo, const Vec3f& outHi)
{
    return Vec3f(
        _RangeImpl<float>(v[0], inLo[0], inHi[0], gamma[0], outLo[0], outHi[0]),
        _RangeImpl<float>(v[1], inLo[1], inHi[1], gamma[1], outLo[1], outHi[1]),
        _RangeImpl<float>(v[2], inLo[2], inHi[2], gamma[2], outLo[2], outHi[2]));
}

template<>
Vec4f
_RangeImpl<Vec4f>(const Vec4f& v, const Vec4f& inLo, const Vec4f& inHi,
                  const Vec4f& gamma, const Vec4f& outLo, const Vec4f& outHi)
{
    return Vec4f(
        _RangeImpl<float>(v[0], inLo[0], inHi[0], gamma[0], outLo[0], outHi[0]),
        _RangeImpl<float>(v[1], inLo[1], inHi[1], gamma[1], outLo[1], outHi[1]),
        _RangeImpl<float>(v[2], inLo[2], inHi[2], gamma[2], outLo[2], outHi[2]),
        _RangeImpl<float>(v[3], inLo[3], inHi[3], gamma[3], outLo[3], outHi[3]));
}

// Full-type range (all parameters match T).
template<typename T>
static void
_EvalRange(const ParamMap& inputs, const ShadingContext&,
           NodeOutputMap* outputs)
{
    T v     = Get<T>(inputs, _kIn,      Zero<T>());
    T inLo  = Get<T>(inputs, _kInlow,   Zero<T>());
    T inHi  = Get<T>(inputs, _kInhigh,  One<T>());
    T gamma = Get<T>(inputs, _kGamma,   One<T>());
    T outLo = Get<T>(inputs, _kOutlow,  Zero<T>());
    T outHi = Get<T>(inputs, _kOuthigh, One<T>());
    bool doClamp = Get<bool>(inputs, _kDoclamp, false);

    T result = _RangeImpl<T>(v, inLo, inHi, gamma, outLo, outHi);
    if (doClamp) {
        result = ClampValue(result, outLo, outHi);
    }
    (*outputs)[_kOut] = Value(result);
}

// Float-args (FA) range: range parameters are float, applied uniformly.
template<typename T>
static void
_EvalRangeFA(const ParamMap& inputs, const ShadingContext&,
             NodeOutputMap* outputs)
{
    T v         = Get<T>(inputs, _kIn,      Zero<T>());
    float inLo  = Get<float>(inputs, _kInlow,   0.0f);
    float inHi  = Get<float>(inputs, _kInhigh,  1.0f);
    float gamma = Get<float>(inputs, _kGamma,   1.0f);
    float outLo = Get<float>(inputs, _kOutlow,  0.0f);
    float outHi = Get<float>(inputs, _kOuthigh, 1.0f);
    bool doClamp = Get<bool>(inputs, _kDoclamp, false);

    T result = _RangeImpl<T>(v, T(inLo), T(inHi), T(gamma),
                             T(outLo), T(outHi));
    if (doClamp) {
        result = ClampValue(result, T(outLo), T(outHi));
    }
    (*outputs)[_kOut] = Value(result);
}

// ---- Registration --------------------------------------------------------

#define _REG(name, fn) reg.Register(name, fn)

void
RegisterAdjustmentNodes(NodeRegistry& reg)
{
    _REG("ND_remap_float",     &_EvalRemapFloat);
    _REG("ND_remap_color3",    &_EvalRemap<Vec3f>);
    _REG("ND_remap_color4",    &_EvalRemap<Vec4f>);
    _REG("ND_remap_vector2",   &_EvalRemap<Vec2f>);
    _REG("ND_remap_vector3",   &_EvalRemap<Vec3f>);
    _REG("ND_remap_vector4",   &_EvalRemap<Vec4f>);
    _REG("ND_remap_color3FA",  &_EvalRemapFA<Vec3f>);
    _REG("ND_remap_color4FA",  &_EvalRemapFA<Vec4f>);
    _REG("ND_remap_vector2FA", &_EvalRemapFA<Vec2f>);
    _REG("ND_remap_vector3FA", &_EvalRemapFA<Vec3f>);
    _REG("ND_remap_vector4FA", &_EvalRemapFA<Vec4f>);

    _REG("ND_smoothstep_float",     &_EvalSmoothstepFloat);
    _REG("ND_smoothstep_color3",    &_EvalSmoothstep<Vec3f>);
    _REG("ND_smoothstep_color4",    &_EvalSmoothstep<Vec4f>);
    _REG("ND_smoothstep_vector2",   &_EvalSmoothstep<Vec2f>);
    _REG("ND_smoothstep_vector3",   &_EvalSmoothstep<Vec3f>);
    _REG("ND_smoothstep_vector4",   &_EvalSmoothstep<Vec4f>);
    _REG("ND_smoothstep_color3FA",  &_EvalSmoothstepFA<Vec3f>);
    _REG("ND_smoothstep_color4FA",  &_EvalSmoothstepFA<Vec4f>);
    _REG("ND_smoothstep_vector2FA", &_EvalSmoothstepFA<Vec2f>);
    _REG("ND_smoothstep_vector3FA", &_EvalSmoothstepFA<Vec3f>);
    _REG("ND_smoothstep_vector4FA", &_EvalSmoothstepFA<Vec4f>);

    _REG("ND_contrast_float",  &_EvalContrastFloat);
    _REG("ND_contrast_color3", &_EvalContrast<Vec3f>);
    _REG("ND_contrast_color4", &_EvalContrast<Vec4f>);
    _REG("ND_contrast_vector2", &_EvalContrast<Vec2f>);
    _REG("ND_contrast_vector3", &_EvalContrast<Vec3f>);
    _REG("ND_contrast_vector4", &_EvalContrast<Vec4f>);
    _REG("ND_contrast_color3FA", &_EvalContrastFA<Vec3f>);
    _REG("ND_contrast_color4FA", &_EvalContrastFA<Vec4f>);
    _REG("ND_contrast_vector2FA", &_EvalContrastFA<Vec2f>);
    _REG("ND_contrast_vector3FA", &_EvalContrastFA<Vec3f>);
    _REG("ND_contrast_vector4FA", &_EvalContrastFA<Vec4f>);

    // Saturate
    _REG("ND_luminance_color3", &_EvalLuminanceColor3);
    _REG("ND_luminance_color4", &_EvalLuminanceColor4);
    _REG("ND_rgbtohsv_color3", &_EvalRgbToHsv);
    _REG("ND_rgbtohsv_color4", &_EvalRgbToHsvColor4);
    _REG("ND_hsvtorgb_color3", &_EvalHsvToRgb);
    _REG("ND_hsvtorgb_color4", &_EvalHsvToRgbColor4);
    _REG("ND_saturate_color3", &_EvalSaturateColor3);
    _REG("ND_saturate_color4", &_EvalSaturateColor4);

    // HSV Adjust
    _REG("ND_hsvadjust_color3", &_EvalHsvadjustColor3);
    _REG("ND_hsvadjust_color4", &_EvalHsvadjustColor4);

    // Color Correct
    _REG("ND_colorcorrect_color3", &_EvalColorcorrectColor3);
    _REG("ND_colorcorrect_color4", &_EvalColorcorrectColor4);

    // Range
    _REG("ND_range_float",      &_EvalRange<float>);
    _REG("ND_range_color3",     &_EvalRange<Vec3f>);
    _REG("ND_range_color4",     &_EvalRange<Vec4f>);
    _REG("ND_range_vector2",    &_EvalRange<Vec2f>);
    _REG("ND_range_vector3",    &_EvalRange<Vec3f>);
    _REG("ND_range_vector4",    &_EvalRange<Vec4f>);
    _REG("ND_range_color3FA",   &_EvalRangeFA<Vec3f>);
    _REG("ND_range_color4FA",   &_EvalRangeFA<Vec4f>);
    _REG("ND_range_vector2FA",  &_EvalRangeFA<Vec2f>);
    _REG("ND_range_vector3FA",  &_EvalRangeFA<Vec3f>);
    _REG("ND_range_vector4FA",  &_EvalRangeFA<Vec4f>);
}

#undef _REG

}  // namespace mxcpp
