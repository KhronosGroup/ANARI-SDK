//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "procedural2dNodes.h"

#include <renderer/materials/MaterialXCpp/nodeRegistry.h>
#include <renderer/materials/MaterialXCpp/nodes/helpers/proceduralHelpers.h>

namespace mxcpp {

static const SlotName _kAmplitude("amplitude");
static const SlotName _kBitangent("bitangent");
static const SlotName _kCenter("center");
static const SlotName _kClampoutput("clampoutput");
static const SlotName _kColor1("color1");
static const SlotName _kColor10("color10");
static const SlotName _kColor2("color2");
static const SlotName _kColor3("color3");
static const SlotName _kColor4("color4");
static const SlotName _kColor5("color5");
static const SlotName _kColor6("color6");
static const SlotName _kColor7("color7");
static const SlotName _kColor8("color8");
static const SlotName _kColor9("color9");
static const SlotName _kCoverage("coverage");
static const SlotName _kDiminish("diminish");
static const SlotName _kFlakenormal("flakenormal");
static const SlotName _kFreq("freq");
static const SlotName _kId("id");
static const SlotName _kInterpolation("interpolation");
static const SlotName _kInterval1("interval1");
static const SlotName _kInterval10("interval10");
static const SlotName _kInterval2("interval2");
static const SlotName _kInterval3("interval3");
static const SlotName _kInterval4("interval4");
static const SlotName _kInterval5("interval5");
static const SlotName _kInterval6("interval6");
static const SlotName _kInterval7("interval7");
static const SlotName _kInterval8("interval8");
static const SlotName _kInterval9("interval9");
static const SlotName _kIntervalNum("interval_num");
static const SlotName _kJitter("jitter");
static const SlotName _kLacunarity("lacunarity");
static const SlotName _kNormal("normal");
static const SlotName _kNumIntervals("num_intervals");
static const SlotName _kOctaves("octaves");
static const SlotName _kOffset("offset");
static const SlotName _kOut("out");
static const SlotName _kOutmax("outmax");
static const SlotName _kOutmin("outmin");
static const SlotName _kPivot("pivot");
static const SlotName _kPoint1("point1");
static const SlotName _kPoint2("point2");
static const SlotName _kPresence("presence");
static const SlotName _kPrevColor("prev_color");
static const SlotName _kRadius("radius");
static const SlotName _kRand("rand");
static const SlotName _kRoughness("roughness");
static const SlotName _kSize("size");
static const SlotName _kStaggered("staggered");
static const SlotName _kStyle("style");
static const SlotName _kTangent("tangent");
static const SlotName _kTexcoord("texcoord");
static const SlotName _kThickness("thickness");
static const SlotName _kType("type");
static const SlotName _kUvoffset("uvoffset");
static const SlotName _kUvtiling("uvtiling");
static const SlotName _kValueb("valueb");
static const SlotName _kValuebl("valuebl");
static const SlotName _kValuebr("valuebr");
static const SlotName _kValuel("valuel");
static const SlotName _kValuer("valuer");
static const SlotName _kValuet("valuet");
static const SlotName _kValuetl("valuetl");
static const SlotName _kValuetr("valuetr");
static const SlotName _kX("x");

template<typename T>
static T
_RampLr(const ParamMap& inputs, const ShadingContext& ctx)
{
    const T left = Get<T>(inputs, _kValuel, Zero<T>());
    const T right = Get<T>(inputs, _kValuer, Zero<T>());
    const Vec2f tc = Get<Vec2f>(inputs, _kTexcoord, ctx.texcoord);
    return Mix(left, right, Clamp01(tc[0]));
}

template<typename T>
static T
_RampTb(const ParamMap& inputs, const ShadingContext& ctx)
{
    const T top = Get<T>(inputs, _kValuet, Zero<T>());
    const T bottom = Get<T>(inputs, _kValueb, Zero<T>());
    const Vec2f tc = Get<Vec2f>(inputs, _kTexcoord, ctx.texcoord);
    return Mix(bottom, top, Clamp01(tc[1]));
}

template<typename T>
static T
_Ramp4(const ParamMap& inputs, const ShadingContext& ctx)
{
    const T topLeft = Get<T>(inputs, _kValuetl, Zero<T>());
    const T topRight = Get<T>(inputs, _kValuetr, Zero<T>());
    const T bottomLeft = Get<T>(inputs, _kValuebl, Zero<T>());
    const T bottomRight = Get<T>(inputs, _kValuebr, Zero<T>());
    const Vec2f uv = Clamp01(Get<Vec2f>(inputs, _kTexcoord, ctx.texcoord));
    const T bottom = Mix(bottomLeft, bottomRight, uv[0]);
    const T top = Mix(topLeft, topRight, uv[0]);
    return Mix(bottom, top, uv[1]);
}

static Vec4f
_EvalRampGradientColor4(const ParamMap& inputs)
{
    const float x = Get<float>(inputs, _kX, 0.0f);
    const float interval1 = Get<float>(inputs, _kInterval1, 0.0f);
    const float interval2 = Get<float>(inputs, _kInterval2, 1.0f);
    const Vec4f color1 = Get<Vec4f>(inputs, _kColor1, Vec4f(0.0f));
    const Vec4f color2 = Get<Vec4f>(inputs, _kColor2, Vec4f(1.0f));
    const Vec4f prevColor = Get<Vec4f>(inputs, _kPrevColor, color1);
    const int interpolation = Get<int>(inputs, _kInterpolation, 1);
    const int intervalNum = Get<int>(inputs, _kIntervalNum, 1);
    const int numIntervals = Get<int>(inputs, _kNumIntervals, 2);

    const float linear = Remap(
        ClampValue(x, interval1, interval2), interval1, interval2, 0.0f, 1.0f);
    const float smooth = Smoothstep(interval1, interval2, x);
    const float t = interpolation == 1 ? smooth : linear;
    const Vec4f interpColor = interpolation == 2
        ? (interval2 > x ? color1 : color2)
        : Mix(color1, color2, t);
    const Vec4f intervalColor = x > interval1 ? interpColor : prevColor;
    return intervalNum >= numIntervals ? prevColor : intervalColor;
}

static float
_RampCoordinate(const ParamMap& inputs, const ShadingContext& ctx)
{
    const int type = Get<int>(inputs, _kType, 0);
    const Vec2f texcoord = Get<Vec2f>(inputs, _kTexcoord, ctx.texcoord);
    if (type == 0) {
        return texcoord[0];
    }

    const Vec2f delta = texcoord - Vec2f(0.5f);
    if (type == 1) {
        return std::atan2(delta[0], delta[1]) / 6.28319f + 0.5f;
    }
    if (type == 2) {
        return (delta * 1.414f).length();
    }

    const Vec2f deltaAbs(std::fabs(delta[0]), std::fabs(delta[1]));
    return (deltaAbs[0] > deltaAbs[1])
        ? deltaAbs[0] * 2.0f
        : deltaAbs[1] * 2.0f;
}

template<typename T>
static T
_Noise2dValue(const ParamMap& inputs, const ShadingContext& ctx);

template<>
float
_Noise2dValue<float>(const ParamMap& inputs, const ShadingContext& ctx)
{
    const Vec2f tc = Get<Vec2f>(inputs, _kTexcoord, ctx.texcoord);
    const float amplitude = Get<float>(inputs, _kAmplitude, 1.0f);
    const float pivot = Get<float>(inputs, _kPivot, 0.0f);
    return PerlinNoise2d(tc[0], tc[1]) * amplitude + pivot;
}

template<>
Vec2f
_Noise2dValue<Vec2f>(const ParamMap& inputs, const ShadingContext& ctx)
{
    const Vec2f tc = Get<Vec2f>(inputs, _kTexcoord, ctx.texcoord);
    const Vec2f amplitude = ReadAmplitude<Vec2f>(inputs, _kAmplitude);
    const float pivot = Get<float>(inputs, _kPivot, 0.0f);
    const Vec3f value = PerlinNoise2dVec3(tc[0], tc[1]);
    return CompMul(Vec2f(value[0], value[1]), amplitude) + Vec2f(pivot);
}

template<>
Vec3f
_Noise2dValue<Vec3f>(const ParamMap& inputs, const ShadingContext& ctx)
{
    const Vec2f tc = Get<Vec2f>(inputs, _kTexcoord, ctx.texcoord);
    const Vec3f amplitude = ReadAmplitude<Vec3f>(inputs, _kAmplitude);
    const float pivot = Get<float>(inputs, _kPivot, 0.0f);
    return CompMul(PerlinNoise2dVec3(tc[0], tc[1]), amplitude) + Vec3f(pivot);
}

template<>
Vec4f
_Noise2dValue<Vec4f>(const ParamMap& inputs, const ShadingContext& ctx)
{
    const Vec2f tc = Get<Vec2f>(inputs, _kTexcoord, ctx.texcoord);
    const Vec4f amplitude = ReadAmplitude<Vec4f>(inputs, _kAmplitude);
    const float pivot = Get<float>(inputs, _kPivot, 0.0f);
    const Vec3f xyz = PerlinNoise2dVec3(tc[0], tc[1]);
    const float w = PerlinNoise2d(tc[0] + 19.0f, tc[1] + 73.0f);
    return CompMul(Vec4f(xyz[0], xyz[1], xyz[2], w), amplitude) +
           Vec4f(pivot);
}

template<typename T>
static T
_Fractal2dValue(const ParamMap& inputs, const ShadingContext& ctx);

template<>
float
_Fractal2dValue<float>(const ParamMap& inputs, const ShadingContext& ctx)
{
    const Vec2f tc = Get<Vec2f>(inputs, _kTexcoord, ctx.texcoord);
    const float amplitude = Get<float>(inputs, _kAmplitude, 1.0f);
    const int octaves = Get<int>(inputs, _kOctaves, 3);
    const float lacunarity = Get<float>(inputs, _kLacunarity, 2.0f);
    const float diminish = Get<float>(inputs, _kDiminish, 0.5f);
    return FractalNoise2dFloat(tc, octaves, lacunarity, diminish) * amplitude;
}

template<>
Vec2f
_Fractal2dValue<Vec2f>(const ParamMap& inputs, const ShadingContext& ctx)
{
    const Vec2f tc = Get<Vec2f>(inputs, _kTexcoord, ctx.texcoord);
    const Vec2f amplitude = ReadAmplitude<Vec2f>(inputs, _kAmplitude);
    const int octaves = Get<int>(inputs, _kOctaves, 3);
    const float lacunarity = Get<float>(inputs, _kLacunarity, 2.0f);
    const float diminish = Get<float>(inputs, _kDiminish, 0.5f);
    return CompMul(FractalNoise2dVec2(tc, octaves, lacunarity, diminish),
                    amplitude);
}

template<>
Vec3f
_Fractal2dValue<Vec3f>(const ParamMap& inputs, const ShadingContext& ctx)
{
    const Vec2f tc = Get<Vec2f>(inputs, _kTexcoord, ctx.texcoord);
    const Vec3f amplitude = ReadAmplitude<Vec3f>(inputs, _kAmplitude);
    const int octaves = Get<int>(inputs, _kOctaves, 3);
    const float lacunarity = Get<float>(inputs, _kLacunarity, 2.0f);
    const float diminish = Get<float>(inputs, _kDiminish, 0.5f);
    return CompMul(FractalNoise2dVec3(tc, octaves, lacunarity, diminish),
                    amplitude);
}

template<>
Vec4f
_Fractal2dValue<Vec4f>(const ParamMap& inputs, const ShadingContext& ctx)
{
    const Vec2f tc = Get<Vec2f>(inputs, _kTexcoord, ctx.texcoord);
    const Vec4f amplitude = ReadAmplitude<Vec4f>(inputs, _kAmplitude);
    const int octaves = Get<int>(inputs, _kOctaves, 3);
    const float lacunarity = Get<float>(inputs, _kLacunarity, 2.0f);
    const float diminish = Get<float>(inputs, _kDiminish, 0.5f);
    return CompMul(FractalNoise2dVec4(tc, octaves, lacunarity, diminish),
                    amplitude);
}

template<typename T>
static void
_EvalNoise2dTyped(const ParamMap& inputs,
                  const ShadingContext& ctx,
                  NodeOutputMap* outputs)
{
    StoreTypedOutput(outputs, _kOut, _Noise2dValue<T>(inputs, ctx));
}

template<typename T>
static void
_EvalFractal2dTyped(const ParamMap& inputs,
                    const ShadingContext& ctx,
                    NodeOutputMap* outputs)
{
    StoreTypedOutput(outputs, _kOut, _Fractal2dValue<T>(inputs, ctx));
}

template<typename T>
static void
_EvalRampLrTyped(const ParamMap& inputs,
                 const ShadingContext& ctx,
                 NodeOutputMap* outputs)
{
    StoreTypedOutput(outputs, _kOut, _RampLr<T>(inputs, ctx));
}

template<typename T>
static void
_EvalRampTbTyped(const ParamMap& inputs,
                 const ShadingContext& ctx,
                 NodeOutputMap* outputs)
{
    StoreTypedOutput(outputs, _kOut, _RampTb<T>(inputs, ctx));
}

template<typename T>
static void
_EvalRamp4Typed(const ParamMap& inputs,
                const ShadingContext& ctx,
                NodeOutputMap* outputs)
{
    StoreTypedOutput(outputs, _kOut, _Ramp4<T>(inputs, ctx));
}

template<typename T>
static void
_EvalSplitLrTyped(const ParamMap& inputs,
                  const ShadingContext& ctx,
                  NodeOutputMap* outputs)
{
    const T left = Get<T>(inputs, _kValuel, Zero<T>());
    const T right = Get<T>(inputs, _kValuer, Zero<T>());
    const float center = Get<float>(inputs, _kCenter, 0.5f);
    const Vec2f tc = Get<Vec2f>(inputs, _kTexcoord, ctx.texcoord);
    const float t = AAStep(center, tc[0], ctx.dudx, ctx.dudy);
    StoreTypedOutput(outputs, _kOut, Mix(left, right, t));
}

template<typename T>
static void
_EvalSplitTbTyped(const ParamMap& inputs,
                  const ShadingContext& ctx,
                  NodeOutputMap* outputs)
{
    const T top = Get<T>(inputs, _kValuet, Zero<T>());
    const T bottom = Get<T>(inputs, _kValueb, Zero<T>());
    const float center = Get<float>(inputs, _kCenter, 0.5f);
    const Vec2f tc = Get<Vec2f>(inputs, _kTexcoord, ctx.texcoord);
    const float t = AAStep(center, tc[1], ctx.dvdx, ctx.dvdy);
    StoreTypedOutput(outputs, _kOut, Mix(bottom, top, t));
}

static void
_EvalCellnoise2d(const ParamMap& inputs,
                 const ShadingContext& ctx,
                 NodeOutputMap* outputs)
{
    const Vec2f tc = Get<Vec2f>(inputs, _kTexcoord, ctx.texcoord);
    StoreTypedOutput(outputs, _kOut, CellNoise2d(tc[0], tc[1]));
}

static void
_EvalWorleyNoise2dFloat(const ParamMap& inputs,
                        const ShadingContext& ctx,
                        NodeOutputMap* outputs)
{
    const Vec2f tc = Get<Vec2f>(inputs, _kTexcoord, ctx.texcoord);
    const float jitter = Get<float>(inputs, _kJitter, 1.0f);
    const int style = Get<int>(inputs, _kStyle, 0);
    StoreTypedOutput(outputs, _kOut,
                     WorleyNoise2dFloat(tc, jitter, style));
}

static void
_EvalWorleyNoise2dVec2(const ParamMap& inputs,
                       const ShadingContext& ctx,
                       NodeOutputMap* outputs)
{
    const Vec2f tc = Get<Vec2f>(inputs, _kTexcoord, ctx.texcoord);
    const float jitter = Get<float>(inputs, _kJitter, 1.0f);
    const int style = Get<int>(inputs, _kStyle, 0);
    StoreTypedOutput(outputs, _kOut,
                     WorleyNoise2dVec2(tc, jitter, style));
}

static void
_EvalWorleyNoise2dVec3(const ParamMap& inputs,
                       const ShadingContext& ctx,
                       NodeOutputMap* outputs)
{
    const Vec2f tc = Get<Vec2f>(inputs, _kTexcoord, ctx.texcoord);
    const float jitter = Get<float>(inputs, _kJitter, 1.0f);
    const int style = Get<int>(inputs, _kStyle, 0);
    StoreTypedOutput(outputs, _kOut,
                     WorleyNoise2dVec3(tc, jitter, style));
}

static void
_EvalUnifiedNoise2dFloat(const ParamMap& inputs,
                         const ShadingContext& ctx,
                         NodeOutputMap* outputs)
{
    const Vec2f texcoord = Get<Vec2f>(inputs, _kTexcoord, ctx.texcoord);
    const Vec2f freq = Get<Vec2f>(inputs, _kFreq, Vec2f(1.0f));
    const Vec2f offset = Get<Vec2f>(inputs, _kOffset, Vec2f(0.0f));
    const float jitter = Get<float>(inputs, _kJitter, 1.0f);
    const float outMin = Get<float>(inputs, _kOutmin, 0.0f);
    const float outMax = Get<float>(inputs, _kOutmax, 1.0f);
    const bool clampOutput = Get<bool>(inputs, _kClampoutput, true);
    const int octaves = Get<int>(inputs, _kOctaves, 3);
    const float lacunarity = Get<float>(inputs, _kLacunarity, 2.0f);
    const float diminish = Get<float>(inputs, _kDiminish, 0.5f);
    const int type = Get<int>(inputs, _kType, 0);
    const int style = Get<int>(inputs, _kStyle, 0);

    const Vec2f applyFreq = CompMul(texcoord, freq);
    const Vec2f applyOffset = applyFreq + offset;
    const float cellJitterMult = (jitter - 1.0f) * 90000.0f;
    const Vec2f applyCellJitter = Rotate2d(applyOffset, cellJitterMult);
    const Vec3f fractalPos(applyOffset[0], applyOffset[1], cellJitterMult);

    float value = 0.0f;
    switch (type) {
    case 1:
        value = CellNoise2d(applyCellJitter[0], applyCellJitter[1]);
        break;
    case 2:
        value = WorleyNoise2dFloat(applyOffset, jitter, style);
        break;
    case 3:
        {
            Vec3f p = fractalPos;
            float weight = 1.0f;
            for (int i = 0; i < octaves; ++i) {
                value += weight * PerlinNoise3d(p[0], p[1], p[2]);
                p *= lacunarity;
                weight *= diminish;
            }
        }
        break;
    case 0:
    default:
        value = PerlinNoise2d(applyCellJitter[0], applyCellJitter[1]) * 0.5f +
                0.5f;
        break;
    }

    value = Remap(value, 0.0f, 1.0f, outMin, outMax);
    if (clampOutput) {
        const float lo = std::min(outMin, outMax);
        const float hi = std::max(outMin, outMax);
        value = ClampValue(value, lo, hi);
    }
    StoreTypedOutput(outputs, _kOut, value);
}

static void
_EvalRampGradient(const ParamMap& inputs,
                  const ShadingContext&,
                  NodeOutputMap* outputs)
{
    StoreTypedOutput(outputs, _kOut, _EvalRampGradientColor4(inputs));
}

static void
_EvalRamp(const ParamMap& inputs,
          const ShadingContext& ctx,
          NodeOutputMap* outputs)
{
    const float x = _RampCoordinate(inputs, ctx);

    std::array<float, 10> intervals = {
        Get<float>(inputs, _kInterval1, 0.0f),
        Get<float>(inputs, _kInterval2, 1.0f),
        Get<float>(inputs, _kInterval3, 1.0f),
        Get<float>(inputs, _kInterval4, 1.0f),
        Get<float>(inputs, _kInterval5, 1.0f),
        Get<float>(inputs, _kInterval6, 1.0f),
        Get<float>(inputs, _kInterval7, 1.0f),
        Get<float>(inputs, _kInterval8, 1.0f),
        Get<float>(inputs, _kInterval9, 1.0f),
        Get<float>(inputs, _kInterval10, 1.0f)
    };

    std::array<Vec4f, 10> colors = {
        Get<Vec4f>(inputs, _kColor1, Vec4f(0.0f, 0.0f, 0.0f, 1.0f)),
        Get<Vec4f>(inputs, _kColor2, Vec4f(1.0f, 1.0f, 1.0f, 1.0f)),
        Get<Vec4f>(inputs, _kColor3, Vec4f(1.0f)),
        Get<Vec4f>(inputs, _kColor4, Vec4f(1.0f)),
        Get<Vec4f>(inputs, _kColor5, Vec4f(1.0f)),
        Get<Vec4f>(inputs, _kColor6, Vec4f(1.0f)),
        Get<Vec4f>(inputs, _kColor7, Vec4f(1.0f)),
        Get<Vec4f>(inputs, _kColor8, Vec4f(1.0f)),
        Get<Vec4f>(inputs, _kColor9, Vec4f(1.0f)),
        Get<Vec4f>(inputs, _kColor10, Vec4f(1.0f))
    };

    const int interpolation = Get<int>(inputs, _kInterpolation, 1);
    const int numIntervals = Get<int>(inputs, _kNumIntervals, 2);

    Vec4f result = colors[0];
    for (int i = 0; i < 9; ++i) {
        ParamMap gradientInputs;
        gradientInputs[_kX] = Value(x);
        gradientInputs[_kInterval1] = Value(intervals[i]);
        gradientInputs[_kInterval2] = Value(intervals[i + 1]);
        gradientInputs[_kColor1] = Value(colors[i]);
        gradientInputs[_kColor2] = Value(colors[i + 1]);
        gradientInputs[_kPrevColor] = Value(result);
        gradientInputs[_kInterpolation] = Value(interpolation);
        gradientInputs[_kIntervalNum] = Value(i + 1);
        gradientInputs[_kNumIntervals] = Value(numIntervals);
        result = _EvalRampGradientColor4(gradientInputs);
    }

    StoreTypedOutput(outputs, _kOut, result);
}

static void
_EvalCheckerboardColor3(const ParamMap& inputs,
                        const ShadingContext& ctx,
                        NodeOutputMap* outputs)
{
    const Vec3f color1 = Get<Vec3f>(inputs, _kColor1, Vec3f(1.0f));
    const Vec3f color2 = Get<Vec3f>(inputs, _kColor2, Vec3f(0.0f));
    const Vec2f uvtiling = Get<Vec2f>(inputs, _kUvtiling, Vec2f(8.0f));
    const Vec2f uvoffset = Get<Vec2f>(inputs, _kUvoffset, Vec2f(0.0f));
    const Vec2f texcoord = Get<Vec2f>(inputs, _kTexcoord, ctx.texcoord);
    const Vec2f tiled = CompMul(texcoord, uvtiling) - uvoffset;
    const Vec2f floored(std::floor(tiled[0]), std::floor(tiled[1]));
    const float selector = PositiveMod(floored[0] + floored[1], 2.0f);
    StoreTypedOutput(outputs, _kOut, Mix(color2, color1, selector));
}

static float
_LineMask(const Vec2f& texcoord, const Vec2f& center, float radius,
          const Vec2f& point1, const Vec2f& point2)
{
    const Vec2f delta = texcoord - center;
    const Vec2f pa = delta - point1;
    const Vec2f ba = point2 - point1;
    const float dotBaBa = Dot(ba, ba);
    const float h = dotBaBa > 0.0f
        ? Clamp01(Dot(pa, ba) / dotBaBa)
        : 0.0f;
    const float dist = (pa - ba * h).length();
    return dist > radius ? 0.0f : 1.0f;
}

static float
_CircleMask(const Vec2f& texcoord, const Vec2f& center, float radius)
{
    const Vec2f delta = texcoord - center;
    return Dot(delta, delta) > radius * radius ? 0.0f : 1.0f;
}

static float
_CloverleafMask(const Vec2f& texcoord, const Vec2f& center, float radius)
{
    const Vec2f sampleDouble = texcoord + texcoord;
    const Vec2f centerDouble = center + center;
    const Vec2f sampleAdd = sampleDouble + Vec2f(radius);
    const Vec2f sampleSubtract = sampleDouble - Vec2f(radius);

    const std::array<Vec2f, 4> coords = {
        Vec2f(sampleAdd[0], sampleDouble[1]),
        Vec2f(sampleSubtract[0], sampleDouble[1]),
        Vec2f(sampleDouble[0], sampleSubtract[1]),
        Vec2f(sampleDouble[0], sampleAdd[1])
    };

    float result = 0.0f;
    for (const Vec2f& coord : coords) {
        result = std::max(
            result, _CircleMask(coord, centerDouble, radius));
    }
    return result;
}

static float
_HexagonMask(const Vec2f& texcoord, const Vec2f& center, float radius)
{
    const Vec3f k(-0.866025f, 0.5f, 0.57735f);
    const Vec2f delta = texcoord - center;
    Vec2f p(std::fabs(delta[1]), std::fabs(delta[0]));

    const Vec2f kxy(k[0], k[1]);
    const float m1 = std::min(Dot(kxy, p), 0.0f);
    p -= kxy * (2.0f * m1);

    const Vec2f minusKxKy(-k[0], k[1]);
    const float m2 = std::min(Dot(minusKxKy, p), 0.0f);
    p -= minusKxKy * (2.0f * m2);

    p -= Vec2f(ClampValue(p[0], -k[2] * radius, k[2] * radius), radius);

    return p[0] + p[1] > 0.0f ? 0.0f : 1.0f;
}

static void
_EvalLineFloat(const ParamMap& inputs,
               const ShadingContext& ctx,
               NodeOutputMap* outputs)
{
    const Vec2f texcoord = Get<Vec2f>(inputs, _kTexcoord, ctx.texcoord);
    const Vec2f center = Get<Vec2f>(inputs, _kCenter, Vec2f(0.0f));
    const float radius = Get<float>(inputs, _kRadius, 0.1f);
    const Vec2f point1 = Get<Vec2f>(inputs, _kPoint1, Vec2f(0.25f, 0.25f));
    const Vec2f point2 = Get<Vec2f>(inputs, _kPoint2, Vec2f(0.75f, 0.75f));
    StoreTypedOutput(outputs, _kOut,
                      _LineMask(texcoord, center, radius, point1, point2));
}

static void
_EvalCircleFloat(const ParamMap& inputs,
                 const ShadingContext& ctx,
                 NodeOutputMap* outputs)
{
    const Vec2f texcoord = Get<Vec2f>(inputs, _kTexcoord, ctx.texcoord);
    const Vec2f center = Get<Vec2f>(inputs, _kCenter, Vec2f(0.0f));
    const float radius = Get<float>(inputs, _kRadius, 0.5f);
    StoreTypedOutput(outputs, _kOut, _CircleMask(texcoord, center, radius));
}

static void
_EvalCloverleafFloat(const ParamMap& inputs,
                     const ShadingContext& ctx,
                     NodeOutputMap* outputs)
{
    const Vec2f texcoord = Get<Vec2f>(inputs, _kTexcoord, ctx.texcoord);
    const Vec2f center = Get<Vec2f>(inputs, _kCenter, Vec2f(0.0f));
    const float radius = Get<float>(inputs, _kRadius, 0.5f);
    StoreTypedOutput(outputs, _kOut, _CloverleafMask(texcoord, center, radius));
}

static void
_EvalHexagonFloat(const ParamMap& inputs,
                  const ShadingContext& ctx,
                  NodeOutputMap* outputs)
{
    const Vec2f texcoord = Get<Vec2f>(inputs, _kTexcoord, ctx.texcoord);
    const Vec2f center = Get<Vec2f>(inputs, _kCenter, Vec2f(0.0f));
    const float radius = Get<float>(inputs, _kRadius, 0.5f);
    StoreTypedOutput(outputs, _kOut, _HexagonMask(texcoord, center, radius));
}

static Vec2f
_TiledShapeTexcoordBias(const ParamMap& inputs, const ShadingContext& ctx)
{
    const Vec2f texcoord = Get<Vec2f>(inputs, _kTexcoord, ctx.texcoord);
    const Vec2f uvtiling = Get<Vec2f>(inputs, _kUvtiling, Vec2f(1.0f));
    const Vec2f uvoffset = Get<Vec2f>(inputs, _kUvoffset, Vec2f(0.0f));
    return CompMul(texcoord, uvtiling) - uvoffset;
}

static Vec2f
_TiledShapeRecenter(const Vec2f& texcoordBias)
{
    const Vec2f modTexcoord(
        PositiveMod(texcoordBias[0]),
        PositiveMod(texcoordBias[1]));
    return modTexcoord * 2.0f - Vec2f(1.0f);
}

static float
_TiledTriangleStaggerMask(
    const Vec2f& texcoordBias,
    float size,
    float (*shapeMask)(const Vec2f&, const Vec2f&, float))
{
    const float staggY = PositiveMod(texcoordBias[1], 1.73205f);
    const float deltaX = staggY > 0.866025f ? 0.5f : 0.0f;
    const float shiftX = texcoordBias[0] + deltaX;
    const float modX = PositiveMod(shiftX);
    const float modY = PositiveMod(texcoordBias[1], 0.866025f);
    const float scaleHalf = size * 0.5f;

    const Vec2f coord1(modX, modY);
    const Vec2f coord2(1.0f - modX, modY);
    const Vec2f coord3(modX - 0.5f, 0.866025f - modY);

    return std::max(
        std::max(
            shapeMask(coord1, Vec2f(0.0f), scaleHalf),
            shapeMask(coord2, Vec2f(0.0f), scaleHalf)),
        shapeMask(coord3, Vec2f(0.0f), scaleHalf));
}

static float
_TiledCloverleafStaggerMask(const Vec2f& texcoordBias, float size)
{
    const float staggY = PositiveMod(texcoordBias[1]);
    const float deltaX = staggY > 0.5f ? 0.5f : 0.0f;
    const float shiftX = texcoordBias[0] + deltaX;
    const float modX = PositiveMod(shiftX);
    const float modY = PositiveMod(texcoordBias[1], 0.5f);
    const float scaleHalf = size * 0.5f;

    const Vec2f coord1(modX, modY);
    const Vec2f coord2(1.0f - modX, modY);
    const Vec2f coord3(modX - 0.5f, 0.5f - modY);

    return std::max(
        std::max(
            _CloverleafMask(coord1, Vec2f(0.0f), scaleHalf),
            _CloverleafMask(coord2, Vec2f(0.0f), scaleHalf)),
        _CloverleafMask(coord3, Vec2f(0.0f), scaleHalf));
}

static void
_EvalTiledCirclesColor3(const ParamMap& inputs,
                        const ShadingContext& ctx,
                        NodeOutputMap* outputs)
{
    const Vec2f texcoordBias = _TiledShapeTexcoordBias(inputs, ctx);
    const float size = Get<float>(inputs, _kSize, 0.5f);
    const bool staggered = Get<bool>(inputs, _kStaggered, false);
    const float regular =
        _CircleMask(_TiledShapeRecenter(texcoordBias), Vec2f(0.0f), size);
    const float value = staggered
        ? _TiledTriangleStaggerMask(texcoordBias, size, &_CircleMask)
        : regular;
    StoreTypedOutput(outputs, _kOut, Vec3f(value));
}

static void
_EvalTiledCloverleafsColor3(const ParamMap& inputs,
                            const ShadingContext& ctx,
                            NodeOutputMap* outputs)
{
    const Vec2f texcoordBias = _TiledShapeTexcoordBias(inputs, ctx);
    const float size = Get<float>(inputs, _kSize, 0.5f);
    const bool staggered = Get<bool>(inputs, _kStaggered, false);
    const float regular =
        _CloverleafMask(_TiledShapeRecenter(texcoordBias), Vec2f(0.0f), size);
    const float value = staggered
        ? _TiledCloverleafStaggerMask(texcoordBias, size)
        : regular;
    StoreTypedOutput(outputs, _kOut, Vec3f(value));
}

static void
_EvalTiledHexagonsColor3(const ParamMap& inputs,
                         const ShadingContext& ctx,
                         NodeOutputMap* outputs)
{
    const Vec2f texcoordBias = _TiledShapeTexcoordBias(inputs, ctx);
    const float size = Get<float>(inputs, _kSize, 0.5f);
    const bool staggered = Get<bool>(inputs, _kStaggered, false);
    const float regular =
        _HexagonMask(_TiledShapeRecenter(texcoordBias), Vec2f(0.0f), size);
    const float value = staggered
        ? _TiledTriangleStaggerMask(texcoordBias, size, &_HexagonMask)
        : regular;
    StoreTypedOutput(outputs, _kOut, Vec3f(value));
}

static void
_EvalGridColor3(const ParamMap& inputs,
                const ShadingContext& ctx,
                NodeOutputMap* outputs)
{
    const Vec2f texcoord = Get<Vec2f>(inputs, _kTexcoord, ctx.texcoord);
    const Vec2f uvtiling = Get<Vec2f>(inputs, _kUvtiling, Vec2f(1.0f));
    const Vec2f uvoffset = Get<Vec2f>(inputs, _kUvoffset, Vec2f(0.0f));
    const float thickness = Get<float>(inputs, _kThickness, 0.05f);
    const bool staggered = Get<bool>(inputs, _kStaggered, false);

    const Vec2f texcoordBias = CompMul(texcoord, uvtiling) - uvoffset;
    const float thickToSize = 1.0f - thickness;
    const float modY = PositiveMod(texcoordBias[1]);
    const float modYRow = PositiveMod(texcoordBias[1], 2.0f);
    const float altRowsShift = modYRow > 1.0f ? 0.5f : 0.0f;
    const float x = staggered ? texcoordBias[0] + altRowsShift : texcoordBias[0];
    const float modX = PositiveMod(x);
    const float absX = std::fabs(modX * 2.0f - 1.0f);
    const float absY = std::fabs(modY * 2.0f - 1.0f);
    const float xDetect = absX > thickToSize ? 0.0f : 1.0f;
    const float yDetect = absY > thickToSize ? 0.0f : 1.0f;
    const float value = 1.0f - std::min(xDetect, yDetect);
    StoreTypedOutput(outputs, _kOut, Vec3f(value));
}

static void
_EvalCrosshatchColor3(const ParamMap& inputs,
                      const ShadingContext& ctx,
                      NodeOutputMap* outputs)
{
    const Vec2f texcoord = Get<Vec2f>(inputs, _kTexcoord, ctx.texcoord);
    const Vec2f uvtiling = Get<Vec2f>(inputs, _kUvtiling, Vec2f(1.0f));
    const Vec2f uvoffset = Get<Vec2f>(inputs, _kUvoffset, Vec2f(0.0f));
    const float thickness = Get<float>(inputs, _kThickness, 0.05f);
    const bool staggered = Get<bool>(inputs, _kStaggered, false);

    const Vec2f texcoordBias = CompMul(texcoord, uvtiling) - uvoffset;
    const float modY = PositiveMod(texcoordBias[1]);
    const float modYRow = PositiveMod(texcoordBias[1], 2.0f);
    const float altRowsShift = modYRow > 1.0f ? 0.5f : 0.0f;
    const float x = staggered ? texcoordBias[0] + altRowsShift : texcoordBias[0];
    const float modX = PositiveMod(x);
    const Vec2f sampleVec(modX * 2.0f - 1.0f, modY * 2.0f - 1.0f);

    const float diag1 = _LineMask(sampleVec, Vec2f(0.0f), thickness,
                                  Vec2f(1.0f, 1.0f), Vec2f(-1.0f, -1.0f));
    const float diag2 = _LineMask(sampleVec, Vec2f(0.0f), thickness,
                                  Vec2f(-1.0f, 1.0f), Vec2f(1.0f, -1.0f));
    const float value = std::max(diag1, diag2);
    StoreTypedOutput(outputs, _kOut, Vec3f(value));
}

static void
_EvalFlake2d(const ParamMap& inputs,
             const ShadingContext& ctx,
             NodeOutputMap* outputs)
{
    const float size = Get<float>(inputs, _kSize, 0.01f);
    const float roughness = Get<float>(inputs, _kRoughness, 0.1f);
    const float coverage = Get<float>(inputs, _kCoverage, 0.5f);
    const Vec2f texcoord = Get<Vec2f>(inputs, _kTexcoord, ctx.texcoord);
    const Vec3f normal = Get<Vec3f>(inputs, _kNormal, ctx.normal);
    const Vec3f tangent = Get<Vec3f>(inputs, _kTangent, ctx.tangent);
    const Vec3f bitangent = Get<Vec3f>(inputs, _kBitangent, ctx.bitangent);

    int id = 0;
    float rand = 0.0f;
    float presence = 0.0f;
    Vec3f flakeNormal = normal;
    EvalFlake(Vec3f(texcoord[0], texcoord[1], 0.0f),
              size,
              roughness,
              coverage,
              normal,
              tangent,
              bitangent,
              &id,
              &rand,
              &presence,
              &flakeNormal);

    StoreTypedOutput(outputs, _kId, id);
    StoreTypedOutput(outputs, _kRand, rand);
    StoreTypedOutput(outputs, _kPresence, presence);
    StoreTypedOutput(outputs, _kFlakenormal, flakeNormal);
}

#define _REG(name, fn) reg.Register(name, fn)

void
RegisterProcedural2dNodes(NodeRegistry& reg)
{
    _REG("ND_noise2d_float", &_EvalNoise2dTyped<float>);
    _REG("ND_noise2d_color3", &_EvalNoise2dTyped<Vec3f>);
    _REG("ND_noise2d_color4", &_EvalNoise2dTyped<Vec4f>);
    _REG("ND_noise2d_vector2", &_EvalNoise2dTyped<Vec2f>);
    _REG("ND_noise2d_vector3", &_EvalNoise2dTyped<Vec3f>);
    _REG("ND_noise2d_vector4", &_EvalNoise2dTyped<Vec4f>);
    _REG("ND_noise2d_color3FA", &_EvalNoise2dTyped<Vec3f>);
    _REG("ND_noise2d_color4FA", &_EvalNoise2dTyped<Vec4f>);
    _REG("ND_noise2d_vector2FA", &_EvalNoise2dTyped<Vec2f>);
    _REG("ND_noise2d_vector3FA", &_EvalNoise2dTyped<Vec3f>);
    _REG("ND_noise2d_vector4FA", &_EvalNoise2dTyped<Vec4f>);

    _REG("ND_fractal2d_float", &_EvalFractal2dTyped<float>);
    _REG("ND_fractal2d_color3", &_EvalFractal2dTyped<Vec3f>);
    _REG("ND_fractal2d_color4", &_EvalFractal2dTyped<Vec4f>);
    _REG("ND_fractal2d_vector2", &_EvalFractal2dTyped<Vec2f>);
    _REG("ND_fractal2d_vector3", &_EvalFractal2dTyped<Vec3f>);
    _REG("ND_fractal2d_vector4", &_EvalFractal2dTyped<Vec4f>);
    _REG("ND_fractal2d_color3FA", &_EvalFractal2dTyped<Vec3f>);
    _REG("ND_fractal2d_color4FA", &_EvalFractal2dTyped<Vec4f>);
    _REG("ND_fractal2d_vector2FA", &_EvalFractal2dTyped<Vec2f>);
    _REG("ND_fractal2d_vector3FA", &_EvalFractal2dTyped<Vec3f>);
    _REG("ND_fractal2d_vector4FA", &_EvalFractal2dTyped<Vec4f>);

    _REG("ND_cellnoise2d_float", &_EvalCellnoise2d);
    _REG("ND_worleynoise2d_float", &_EvalWorleyNoise2dFloat);
    _REG("ND_worleynoise2d_vector2", &_EvalWorleyNoise2dVec2);
    _REG("ND_worleynoise2d_vector3", &_EvalWorleyNoise2dVec3);
    _REG("ND_unifiednoise2d_float", &_EvalUnifiedNoise2dFloat);

    _REG("ND_ramplr_float", &_EvalRampLrTyped<float>);
    _REG("ND_ramplr_color3", &_EvalRampLrTyped<Vec3f>);
    _REG("ND_ramplr_color4", &_EvalRampLrTyped<Vec4f>);
    _REG("ND_ramplr_vector2", &_EvalRampLrTyped<Vec2f>);
    _REG("ND_ramplr_vector3", &_EvalRampLrTyped<Vec3f>);
    _REG("ND_ramplr_vector4", &_EvalRampLrTyped<Vec4f>);

    _REG("ND_ramptb_float", &_EvalRampTbTyped<float>);
    _REG("ND_ramptb_color3", &_EvalRampTbTyped<Vec3f>);
    _REG("ND_ramptb_color4", &_EvalRampTbTyped<Vec4f>);
    _REG("ND_ramptb_vector2", &_EvalRampTbTyped<Vec2f>);
    _REG("ND_ramptb_vector3", &_EvalRampTbTyped<Vec3f>);
    _REG("ND_ramptb_vector4", &_EvalRampTbTyped<Vec4f>);

    _REG("ND_ramp4_float", &_EvalRamp4Typed<float>);
    _REG("ND_ramp4_color3", &_EvalRamp4Typed<Vec3f>);
    _REG("ND_ramp4_color4", &_EvalRamp4Typed<Vec4f>);
    _REG("ND_ramp4_vector2", &_EvalRamp4Typed<Vec2f>);
    _REG("ND_ramp4_vector3", &_EvalRamp4Typed<Vec3f>);
    _REG("ND_ramp4_vector4", &_EvalRamp4Typed<Vec4f>);

    _REG("ND_splitlr_float", &_EvalSplitLrTyped<float>);
    _REG("ND_splitlr_color3", &_EvalSplitLrTyped<Vec3f>);
    _REG("ND_splitlr_color4", &_EvalSplitLrTyped<Vec4f>);
    _REG("ND_splitlr_vector2", &_EvalSplitLrTyped<Vec2f>);
    _REG("ND_splitlr_vector3", &_EvalSplitLrTyped<Vec3f>);
    _REG("ND_splitlr_vector4", &_EvalSplitLrTyped<Vec4f>);

    _REG("ND_splittb_float", &_EvalSplitTbTyped<float>);
    _REG("ND_splittb_color3", &_EvalSplitTbTyped<Vec3f>);
    _REG("ND_splittb_color4", &_EvalSplitTbTyped<Vec4f>);
    _REG("ND_splittb_vector2", &_EvalSplitTbTyped<Vec2f>);
    _REG("ND_splittb_vector3", &_EvalSplitTbTyped<Vec3f>);
    _REG("ND_splittb_vector4", &_EvalSplitTbTyped<Vec4f>);

    _REG("ND_ramp_gradient", &_EvalRampGradient);
    _REG("ND_ramp", &_EvalRamp);
    _REG("ND_checkerboard_color3", &_EvalCheckerboardColor3);
    _REG("ND_line_float", &_EvalLineFloat);
    _REG("ND_circle_float", &_EvalCircleFloat);
    _REG("ND_cloverleaf_float", &_EvalCloverleafFloat);
    _REG("ND_hexagon_float", &_EvalHexagonFloat);
    _REG("ND_grid_color3", &_EvalGridColor3);
    _REG("ND_crosshatch_color3", &_EvalCrosshatchColor3);
    _REG("ND_tiledcircles_color3", &_EvalTiledCirclesColor3);
    _REG("ND_tiledcloverleafs_color3", &_EvalTiledCloverleafsColor3);
    _REG("ND_tiledhexagons_color3", &_EvalTiledHexagonsColor3);
    _REG("ND_flake2d", &_EvalFlake2d);
}

#undef _REG

}  // namespace mxcpp
