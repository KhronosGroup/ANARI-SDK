//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "compositingNodes.h"

#include <renderer/materials/MaterialXCpp/nodeRegistry.h>
#include <renderer/materials/MaterialXCpp/surfaceShaderUtils.h>

#include <algorithm>
#include <cmath>

namespace mxcpp {

static const SlotName _kIn("in");
static const SlotName _kFg("fg");
static const SlotName _kBg("bg");
static const SlotName _kMix("mix");
static const SlotName _kMask("mask");
static const SlotName _kOut("out");

static constexpr float _kEps = 1e-7f;

// ---- Mix -----------------------------------------------------------------

template<typename T>
static void
_EvalMix(const ParamMap& inputs, const ShadingContext&,
         NodeOutputMap* outputs)
{
    T fg  = Get<T>(inputs, _kFg, Zero<T>());
    T bg  = Get<T>(inputs, _kBg, Zero<T>());
    float m = Get<float>(inputs, _kMix, 0.0f);
    (*outputs)[_kOut] = Value(bg + (fg - bg) * m);
}

template<typename T>
static void
_EvalMixVec(const ParamMap& inputs, const ShadingContext&,
            NodeOutputMap* outputs)
{
    T fg  = Get<T>(inputs, _kFg, Zero<T>());
    T bg  = Get<T>(inputs, _kBg, Zero<T>());
    T m   = Get<T>(inputs, _kMix, Zero<T>());
    (*outputs)[_kOut] = Value(bg + CompMul(fg - bg, m));
}

static void
_EvalMixBsdf(const ParamMap& inputs, const ShadingContext&,
             NodeOutputMap* outputs)
{
    const BsdfClosure fg = Get<BsdfClosure>(inputs, _kFg, BsdfClosure{});
    const BsdfClosure bg = Get<BsdfClosure>(inputs, _kBg, BsdfClosure{});
    const float mix = Get<float>(inputs, _kMix, 0.0f);

    BsdfClosure result;
    const Bsdf::NodeId bgRoot = AppendClosureTree(&result.tree, bg.tree);
    const Bsdf::NodeId fgRoot = AppendClosureTree(&result.tree, fg.tree);

    if (result.tree.IsValid(bgRoot) && result.tree.IsValid(fgRoot)) {
        Bsdf::MixData mixData;
        mixData.bg = bgRoot;
        mixData.fg = fgRoot;
        mixData.mix = mix;
        result.tree.root = result.tree.Add(mixData);
    } else if (result.tree.IsValid(fgRoot)) {
        result.tree.root = WeightClosureTreeRoot(&result.tree, fgRoot, mix);
    } else if (result.tree.IsValid(bgRoot)) {
        result.tree.root = WeightClosureTreeRoot(
            &result.tree, bgRoot, 1.0f - mix);
    }

    const bool chooseFg = mix >= 0.5f;
    if (fg.hasInteriorMedium && (!bg.hasInteriorMedium || chooseFg)) {
        result.hasInteriorMedium = true;
        result.interiorMedium = fg.interiorMedium;
    } else if (bg.hasInteriorMedium) {
        result.hasInteriorMedium = true;
        result.interiorMedium = bg.interiorMedium;
    }

    (*outputs)[_kOut] = Value(result);
}

static void
_EvalMixEdf(const ParamMap& inputs, const ShadingContext&,
            NodeOutputMap* outputs)
{
    const UniformEdf fg = Get<UniformEdf>(
        inputs, _kFg, UniformEdf{Vec3f(0.0f)});
    const UniformEdf bg = Get<UniformEdf>(
        inputs, _kBg, UniformEdf{Vec3f(0.0f)});
    const float mix = Get<float>(inputs, _kMix, 0.0f);
    (*outputs)[_kOut] = Value(UniformEdf{
        bg.emittance + (fg.emittance - bg.emittance) * mix});
}

static float
_MediumScatterWeight(const MediumProperties& medium)
{
    return std::max(0.0f, medium.scattering[0] + medium.scattering[1] +
                              medium.scattering[2]);
}

static void
_EvalMixVdf(const ParamMap& inputs, const ShadingContext&,
            NodeOutputMap* outputs)
{
    const VdfClosure fg = Get<VdfClosure>(inputs, _kFg, VdfClosure{});
    const VdfClosure bg = Get<VdfClosure>(inputs, _kBg, VdfClosure{});
    const float mix = Get<float>(inputs, _kMix, 0.0f);
    const float bgScale = 1.0f - mix;

    VdfClosure result;
    result.medium.absorption =
        bg.medium.absorption * bgScale + fg.medium.absorption * mix;
    result.medium.scattering =
        bg.medium.scattering * bgScale + fg.medium.scattering * mix;

    const float bgScatterWeight = std::max(
        0.0f, _MediumScatterWeight(bg.medium) * bgScale);
    const float fgScatterWeight = std::max(
        0.0f, _MediumScatterWeight(fg.medium) * mix);
    const float scatterWeightSum = bgScatterWeight + fgScatterWeight;
    if (scatterWeightSum > 0.0f) {
        result.medium.anisotropy =
            (bg.medium.anisotropy * bgScatterWeight +
             fg.medium.anisotropy * fgScatterWeight) /
            scatterWeightSum;
    } else if (!bg.medium.IsVacuum() && bgScale > 0.0f) {
        result.medium.anisotropy = bg.medium.anisotropy;
    } else {
        result.medium.anisotropy = fg.medium.anisotropy;
    }

    (*outputs)[_kOut] = Value(result);
}

static void
_EvalMixVolumeShader(const ParamMap& inputs, const ShadingContext&,
                     NodeOutputMap* outputs)
{
    (*outputs)[_kOut] = Value(EvalMixVolumeShader(inputs));
}

// ---- Premult / Unpremult -------------------------------------------------

static void
_EvalPremult(const ParamMap& inputs, const ShadingContext&,
             NodeOutputMap* outputs)
{
    Vec4f v = Get<Vec4f>(inputs, _kIn, Vec4f(0.0f));
    (*outputs)[_kOut] = Value(
        Vec4f(v[0]*v[3], v[1]*v[3], v[2]*v[3], v[3]));
}

static void
_EvalUnpremult(const ParamMap& inputs, const ShadingContext&,
               NodeOutputMap* outputs)
{
    Vec4f v = Get<Vec4f>(inputs, _kIn, Vec4f(0.0f));
    float a = v[3] > 0.0f ? 1.0f / v[3] : 0.0f;
    (*outputs)[_kOut] = Value(
        Vec4f(v[0]*a, v[1]*a, v[2]*a, v[3]));
}

// ---- Blend mode helpers --------------------------------------------------

static float _BurnF(float fg, float bg) {
    return (std::abs(fg) < _kEps) ? 0.0f : 1.0f - (1.0f - bg) / fg;
}

static float _DodgeF(float fg, float bg) {
    float denom = 1.0f - fg;
    return (std::abs(denom) < _kEps) ? 0.0f : bg / denom;
}

static float _ScreenF(float fg, float bg) {
    return 1.0f - (1.0f - fg) * (1.0f - bg);
}

static float _OverlayF(float fg, float bg) {
    return (bg < 0.5f)
        ? 2.0f * fg * bg
        : 1.0f - 2.0f * (1.0f - fg) * (1.0f - bg);
}

// ---- Blend mode nodes ----------------------------------------------------

template<typename T>
static T _ApplyMix(const T& result, const T& bg, float m) {
    return result * m + bg * (1.0f - m);
}

template<typename T>
static void
_EvalPlus(const ParamMap& inputs, const ShadingContext&,
          NodeOutputMap* outputs)
{
    T fg = Get<T>(inputs, _kFg, Zero<T>());
    T bg = Get<T>(inputs, _kBg, Zero<T>());
    float m = Get<float>(inputs, _kMix, 1.0f);
    (*outputs)[_kOut] = Value(_ApplyMix(fg + bg, bg, m));
}

template<typename T>
static void
_EvalMinus(const ParamMap& inputs, const ShadingContext&,
           NodeOutputMap* outputs)
{
    T fg = Get<T>(inputs, _kFg, Zero<T>());
    T bg = Get<T>(inputs, _kBg, Zero<T>());
    float m = Get<float>(inputs, _kMix, 1.0f);
    (*outputs)[_kOut] = Value(_ApplyMix(bg - fg, bg, m));
}

// Difference: abs(fg - bg) per component
static void
_EvalDifferenceFloat(const ParamMap& inputs, const ShadingContext&,
                     NodeOutputMap* outputs)
{
    float fg = Get<float>(inputs, _kFg, 0.0f);
    float bg = Get<float>(inputs, _kBg, 0.0f);
    float m  = Get<float>(inputs, _kMix, 1.0f);
    float result = std::abs(fg - bg);
    (*outputs)[_kOut] = Value(result * m + bg * (1.0f - m));
}

static void
_EvalDifferenceColor3(const ParamMap& inputs, const ShadingContext&,
                      NodeOutputMap* outputs)
{
    Vec3f fg = Get<Vec3f>(inputs, _kFg, Vec3f(0.0f));
    Vec3f bg = Get<Vec3f>(inputs, _kBg, Vec3f(0.0f));
    float m  = Get<float>(inputs, _kMix, 1.0f);
    Vec3f result(std::abs(fg[0]-bg[0]), std::abs(fg[1]-bg[1]),
                 std::abs(fg[2]-bg[2]));
    (*outputs)[_kOut] = Value(result * m + bg * (1.0f - m));
}

static void
_EvalDifferenceColor4(const ParamMap& inputs, const ShadingContext&,
                      NodeOutputMap* outputs)
{
    Vec4f fg = Get<Vec4f>(inputs, _kFg, Vec4f(0.0f));
    Vec4f bg = Get<Vec4f>(inputs, _kBg, Vec4f(0.0f));
    float m  = Get<float>(inputs, _kMix, 1.0f);
    Vec4f result(std::abs(fg[0]-bg[0]), std::abs(fg[1]-bg[1]),
                 std::abs(fg[2]-bg[2]), std::abs(fg[3]-bg[3]));
    (*outputs)[_kOut] = Value(result * m + bg * (1.0f - m));
}

// Burn
static void
_EvalBurnFloat(const ParamMap& inputs, const ShadingContext&,
               NodeOutputMap* outputs)
{
    float fg = Get<float>(inputs, _kFg, 0.0f);
    float bg = Get<float>(inputs, _kBg, 0.0f);
    float m  = Get<float>(inputs, _kMix, 1.0f);
    (*outputs)[_kOut] = Value(_BurnF(fg, bg) * m + bg * (1.0f - m));
}

static void
_EvalBurnColor3(const ParamMap& inputs, const ShadingContext&,
                NodeOutputMap* outputs)
{
    Vec3f fg = Get<Vec3f>(inputs, _kFg, Vec3f(0.0f));
    Vec3f bg = Get<Vec3f>(inputs, _kBg, Vec3f(0.0f));
    float m  = Get<float>(inputs, _kMix, 1.0f);
    Vec3f result(_BurnF(fg[0],bg[0]), _BurnF(fg[1],bg[1]),
                 _BurnF(fg[2],bg[2]));
    (*outputs)[_kOut] = Value(result * m + bg * (1.0f - m));
}

static void
_EvalBurnColor4(const ParamMap& inputs, const ShadingContext&,
                NodeOutputMap* outputs)
{
    Vec4f fg = Get<Vec4f>(inputs, _kFg, Vec4f(0.0f));
    Vec4f bg = Get<Vec4f>(inputs, _kBg, Vec4f(0.0f));
    float m  = Get<float>(inputs, _kMix, 1.0f);
    Vec4f result(_BurnF(fg[0],bg[0]), _BurnF(fg[1],bg[1]),
                 _BurnF(fg[2],bg[2]), _BurnF(fg[3],bg[3]));
    (*outputs)[_kOut] = Value(result * m + bg * (1.0f - m));
}

// Dodge
static void
_EvalDodgeFloat(const ParamMap& inputs, const ShadingContext&,
                NodeOutputMap* outputs)
{
    float fg = Get<float>(inputs, _kFg, 0.0f);
    float bg = Get<float>(inputs, _kBg, 0.0f);
    float m  = Get<float>(inputs, _kMix, 1.0f);
    (*outputs)[_kOut] = Value(_DodgeF(fg, bg) * m + bg * (1.0f - m));
}

static void
_EvalDodgeColor3(const ParamMap& inputs, const ShadingContext&,
                 NodeOutputMap* outputs)
{
    Vec3f fg = Get<Vec3f>(inputs, _kFg, Vec3f(0.0f));
    Vec3f bg = Get<Vec3f>(inputs, _kBg, Vec3f(0.0f));
    float m  = Get<float>(inputs, _kMix, 1.0f);
    Vec3f result(_DodgeF(fg[0],bg[0]), _DodgeF(fg[1],bg[1]),
                 _DodgeF(fg[2],bg[2]));
    (*outputs)[_kOut] = Value(result * m + bg * (1.0f - m));
}

static void
_EvalDodgeColor4(const ParamMap& inputs, const ShadingContext&,
                 NodeOutputMap* outputs)
{
    Vec4f fg = Get<Vec4f>(inputs, _kFg, Vec4f(0.0f));
    Vec4f bg = Get<Vec4f>(inputs, _kBg, Vec4f(0.0f));
    float m  = Get<float>(inputs, _kMix, 1.0f);
    Vec4f result(_DodgeF(fg[0],bg[0]), _DodgeF(fg[1],bg[1]),
                 _DodgeF(fg[2],bg[2]), _DodgeF(fg[3],bg[3]));
    (*outputs)[_kOut] = Value(result * m + bg * (1.0f - m));
}

// Screen
static void
_EvalScreenFloat(const ParamMap& inputs, const ShadingContext&,
                 NodeOutputMap* outputs)
{
    float fg = Get<float>(inputs, _kFg, 0.0f);
    float bg = Get<float>(inputs, _kBg, 0.0f);
    float m  = Get<float>(inputs, _kMix, 1.0f);
    (*outputs)[_kOut] = Value(_ScreenF(fg, bg) * m + bg * (1.0f - m));
}

static void
_EvalScreenColor3(const ParamMap& inputs, const ShadingContext&,
                  NodeOutputMap* outputs)
{
    Vec3f fg = Get<Vec3f>(inputs, _kFg, Vec3f(0.0f));
    Vec3f bg = Get<Vec3f>(inputs, _kBg, Vec3f(0.0f));
    float m  = Get<float>(inputs, _kMix, 1.0f);
    Vec3f result(_ScreenF(fg[0],bg[0]), _ScreenF(fg[1],bg[1]),
                 _ScreenF(fg[2],bg[2]));
    (*outputs)[_kOut] = Value(result * m + bg * (1.0f - m));
}

static void
_EvalScreenColor4(const ParamMap& inputs, const ShadingContext&,
                  NodeOutputMap* outputs)
{
    Vec4f fg = Get<Vec4f>(inputs, _kFg, Vec4f(0.0f));
    Vec4f bg = Get<Vec4f>(inputs, _kBg, Vec4f(0.0f));
    float m  = Get<float>(inputs, _kMix, 1.0f);
    Vec4f result(_ScreenF(fg[0],bg[0]), _ScreenF(fg[1],bg[1]),
                 _ScreenF(fg[2],bg[2]), _ScreenF(fg[3],bg[3]));
    (*outputs)[_kOut] = Value(result * m + bg * (1.0f - m));
}

// Overlay
static void
_EvalOverlayFloat(const ParamMap& inputs, const ShadingContext&,
                  NodeOutputMap* outputs)
{
    float fg = Get<float>(inputs, _kFg, 0.0f);
    float bg = Get<float>(inputs, _kBg, 0.0f);
    float m  = Get<float>(inputs, _kMix, 1.0f);
    (*outputs)[_kOut] = Value(_OverlayF(fg, bg) * m + bg * (1.0f - m));
}

static void
_EvalOverlayColor3(const ParamMap& inputs, const ShadingContext&,
                   NodeOutputMap* outputs)
{
    Vec3f fg = Get<Vec3f>(inputs, _kFg, Vec3f(0.0f));
    Vec3f bg = Get<Vec3f>(inputs, _kBg, Vec3f(0.0f));
    float m  = Get<float>(inputs, _kMix, 1.0f);
    Vec3f result(_OverlayF(fg[0],bg[0]), _OverlayF(fg[1],bg[1]),
                 _OverlayF(fg[2],bg[2]));
    (*outputs)[_kOut] = Value(result * m + bg * (1.0f - m));
}

static void
_EvalOverlayColor4(const ParamMap& inputs, const ShadingContext&,
                   NodeOutputMap* outputs)
{
    Vec4f fg = Get<Vec4f>(inputs, _kFg, Vec4f(0.0f));
    Vec4f bg = Get<Vec4f>(inputs, _kBg, Vec4f(0.0f));
    float m  = Get<float>(inputs, _kMix, 1.0f);
    Vec4f result(_OverlayF(fg[0],bg[0]), _OverlayF(fg[1],bg[1]),
                 _OverlayF(fg[2],bg[2]), _OverlayF(fg[3],bg[3]));
    (*outputs)[_kOut] = Value(result * m + bg * (1.0f - m));
}

// ---- Porter-Duff alpha compositing (color4 only) -------------------------

static void
_EvalOver(const ParamMap& inputs, const ShadingContext&,
          NodeOutputMap* outputs)
{
    Vec4f fg = Get<Vec4f>(inputs, _kFg, Vec4f(0.0f));
    Vec4f bg = Get<Vec4f>(inputs, _kBg, Vec4f(0.0f));
    float m  = Get<float>(inputs, _kMix, 1.0f);
    float f = fg[3], b = bg[3];
    Vec4f result(
        fg[0] + bg[0]*(1.0f-f), fg[1] + bg[1]*(1.0f-f),
        fg[2] + bg[2]*(1.0f-f), f + b*(1.0f-f));
    (*outputs)[_kOut] = Value(result * m + bg * (1.0f - m));
}

static void
_EvalDisjointover(const ParamMap& inputs, const ShadingContext&,
                  NodeOutputMap* outputs)
{
    Vec4f fg = Get<Vec4f>(inputs, _kFg, Vec4f(0.0f));
    Vec4f bg = Get<Vec4f>(inputs, _kBg, Vec4f(0.0f));
    float m  = Get<float>(inputs, _kMix, 1.0f);
    float f = fg[3], b = bg[3];
    float sumAlpha = f + b;
    Vec4f result;
    if (sumAlpha <= 1.0f) {
        result = Vec4f(fg[0]+bg[0], fg[1]+bg[1], fg[2]+bg[2],
                       std::min(sumAlpha, 1.0f));
    } else {
        float x = (std::abs(b) < _kEps) ? 0.0f : (1.0f - f) / b;
        result = Vec4f(fg[0]+bg[0]*x, fg[1]+bg[1]*x, fg[2]+bg[2]*x,
                       std::min(sumAlpha, 1.0f));
    }
    (*outputs)[_kOut] = Value(result * m + bg * (1.0f - m));
}

static void
_EvalIn(const ParamMap& inputs, const ShadingContext&,
        NodeOutputMap* outputs)
{
    Vec4f fg = Get<Vec4f>(inputs, _kFg, Vec4f(0.0f));
    Vec4f bg = Get<Vec4f>(inputs, _kBg, Vec4f(0.0f));
    float m  = Get<float>(inputs, _kMix, 1.0f);
    float b = bg[3];
    Vec4f result(fg[0]*b, fg[1]*b, fg[2]*b, fg[3]*b);
    (*outputs)[_kOut] = Value(result * m + bg * (1.0f - m));
}

static void
_EvalMask(const ParamMap& inputs, const ShadingContext&,
          NodeOutputMap* outputs)
{
    Vec4f fg = Get<Vec4f>(inputs, _kFg, Vec4f(0.0f));
    Vec4f bg = Get<Vec4f>(inputs, _kBg, Vec4f(0.0f));
    float m  = Get<float>(inputs, _kMix, 1.0f);
    float f = fg[3];
    Vec4f result(bg[0]*f, bg[1]*f, bg[2]*f, bg[3]*f);
    (*outputs)[_kOut] = Value(result * m + bg * (1.0f - m));
}

static void
_EvalMatte(const ParamMap& inputs, const ShadingContext&,
           NodeOutputMap* outputs)
{
    Vec4f fg = Get<Vec4f>(inputs, _kFg, Vec4f(0.0f));
    Vec4f bg = Get<Vec4f>(inputs, _kBg, Vec4f(0.0f));
    float m  = Get<float>(inputs, _kMix, 1.0f);
    float f = fg[3];
    Vec4f result(
        fg[0]*f + bg[0]*(1.0f-f), fg[1]*f + bg[1]*(1.0f-f),
        fg[2]*f + bg[2]*(1.0f-f), f + bg[3]*(1.0f-f));
    (*outputs)[_kOut] = Value(result * m + bg * (1.0f - m));
}

static void
_EvalOut(const ParamMap& inputs, const ShadingContext&,
         NodeOutputMap* outputs)
{
    Vec4f fg = Get<Vec4f>(inputs, _kFg, Vec4f(0.0f));
    Vec4f bg = Get<Vec4f>(inputs, _kBg, Vec4f(0.0f));
    float m  = Get<float>(inputs, _kMix, 1.0f);
    float b = bg[3];
    Vec4f result(
        fg[0]*(1.0f-b), fg[1]*(1.0f-b),
        fg[2]*(1.0f-b), fg[3]*(1.0f-b));
    (*outputs)[_kOut] = Value(result * m + bg * (1.0f - m));
}

// ---- Mask operations (in, mask inputs) -----------------------------------

template<typename T>
static void
_EvalInside(const ParamMap& inputs, const ShadingContext&,
            NodeOutputMap* outputs)
{
    T v = Get<T>(inputs, _kIn, Zero<T>());
    float mask = Get<float>(inputs, _kMask, 1.0f);
    (*outputs)[_kOut] = Value(v * mask);
}

template<typename T>
static void
_EvalOutside(const ParamMap& inputs, const ShadingContext&,
             NodeOutputMap* outputs)
{
    T v = Get<T>(inputs, _kIn, Zero<T>());
    float mask = Get<float>(inputs, _kMask, 1.0f);
    (*outputs)[_kOut] = Value(v * (1.0f - mask));
}

// ---- Registration --------------------------------------------------------

#define _REG(name, fn) reg.Register(name, fn)

void
RegisterCompositingNodes(NodeRegistry& reg)
{
    // premult / unpremult
    _REG("ND_premult_color4",   &_EvalPremult);
    _REG("ND_unpremult_color4", &_EvalUnpremult);

    // mix (scalar mix param)
    _REG("ND_mix_float",   &_EvalMix<float>);
    _REG("ND_mix_color3",  &_EvalMix<Vec3f>);
    _REG("ND_mix_color4",  &_EvalMix<Vec4f>);
    _REG("ND_mix_vector2", &_EvalMix<Vec2f>);
    _REG("ND_mix_vector3", &_EvalMix<Vec3f>);
    _REG("ND_mix_vector4", &_EvalMix<Vec4f>);
    _REG("ND_mix_bsdf", &_EvalMixBsdf);
    _REG("ND_mix_edf", &_EvalMixEdf);
    _REG("ND_mix_vdf", &_EvalMixVdf);
    _REG("ND_mix_volumeshader", &_EvalMixVolumeShader);

    // mix (vector mix param)
    _REG("ND_mix_color3_color3",   &_EvalMixVec<Vec3f>);
    _REG("ND_mix_color4_color4",   &_EvalMixVec<Vec4f>);
    _REG("ND_mix_vector2_vector2", &_EvalMixVec<Vec2f>);
    _REG("ND_mix_vector3_vector3", &_EvalMixVec<Vec3f>);
    _REG("ND_mix_vector4_vector4", &_EvalMixVec<Vec4f>);

    // plus / minus
    _REG("ND_plus_float",  &_EvalPlus<float>);
    _REG("ND_plus_color3", &_EvalPlus<Vec3f>);
    _REG("ND_plus_color4", &_EvalPlus<Vec4f>);

    _REG("ND_minus_float",  &_EvalMinus<float>);
    _REG("ND_minus_color3", &_EvalMinus<Vec3f>);
    _REG("ND_minus_color4", &_EvalMinus<Vec4f>);

    // difference
    _REG("ND_difference_float",  &_EvalDifferenceFloat);
    _REG("ND_difference_color3", &_EvalDifferenceColor3);
    _REG("ND_difference_color4", &_EvalDifferenceColor4);

    // burn / dodge / screen / overlay
    _REG("ND_burn_float",  &_EvalBurnFloat);
    _REG("ND_burn_color3", &_EvalBurnColor3);
    _REG("ND_burn_color4", &_EvalBurnColor4);

    _REG("ND_dodge_float",  &_EvalDodgeFloat);
    _REG("ND_dodge_color3", &_EvalDodgeColor3);
    _REG("ND_dodge_color4", &_EvalDodgeColor4);

    _REG("ND_screen_float",  &_EvalScreenFloat);
    _REG("ND_screen_color3", &_EvalScreenColor3);
    _REG("ND_screen_color4", &_EvalScreenColor4);

    _REG("ND_overlay_float",  &_EvalOverlayFloat);
    _REG("ND_overlay_color3", &_EvalOverlayColor3);
    _REG("ND_overlay_color4", &_EvalOverlayColor4);

    // Porter-Duff alpha compositing
    _REG("ND_over_color4",        &_EvalOver);
    _REG("ND_disjointover_color4",&_EvalDisjointover);
    _REG("ND_in_color4",          &_EvalIn);
    _REG("ND_mask_color4",        &_EvalMask);
    _REG("ND_matte_color4",       &_EvalMatte);
    _REG("ND_out_color4",         &_EvalOut);

    // inside / outside
    _REG("ND_inside_float",  &_EvalInside<float>);
    _REG("ND_inside_color3", &_EvalInside<Vec3f>);
    _REG("ND_inside_color4", &_EvalInside<Vec4f>);

    _REG("ND_outside_float",  &_EvalOutside<float>);
    _REG("ND_outside_color3", &_EvalOutside<Vec3f>);
    _REG("ND_outside_color4", &_EvalOutside<Vec4f>);
}

#undef _REG

}  // namespace mxcpp
