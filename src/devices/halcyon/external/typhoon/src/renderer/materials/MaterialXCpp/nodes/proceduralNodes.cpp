//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "proceduralNodes.h"

#include <renderer/materials/MaterialXCpp/nodeRegistry.h>
#include <renderer/materials/MaterialXCpp/nodes/helpers/proceduralHelpers.h>

#include <algorithm>
#include <cmath>

namespace mxcpp {

static const SlotName _kEdgeColor("edge_color");
static const SlotName _kExtinction("extinction");
static const SlotName _kIn("in");
static const SlotName _kIor("ior");
static const SlotName _kMax("max");
static const SlotName _kMin("min");
static const SlotName _kOut("out");
static const SlotName _kReflectivity("reflectivity");
static const SlotName _kSeed("seed");
static const SlotName _kTemperature("temperature");
static const SlotName _kValue("value");

template<typename T>
static void
_EvalConstant(const ParamMap& inputs, const ShadingContext&,
              NodeOutputMap* outputs)
{
    const T value = Get<T>(inputs, _kValue, Zero<T>());
    (*outputs)[_kOut] = Value(value);
}

template<typename T>
static void
_EvalRandomFloatTyped(const ParamMap& inputs,
                      const ShadingContext&,
                      NodeOutputMap* outputs)
{
    float inputValue = 0.0f;
    if constexpr (std::is_same<T, int>::value) {
        inputValue = static_cast<float>(Get<int>(inputs, _kIn, 0));
    } else {
        inputValue = Get<float>(inputs, _kIn, 0.0f) * 4096.0f;
    }

    StoreTypedOutput(
        outputs,
        _kOut,
        RandomFloatValue(
            inputValue,
            Get<float>(inputs, _kMin, 0.0f),
            Get<float>(inputs, _kMax, 1.0f),
            Get<int>(inputs, _kSeed, 0)));
}

static void
_EvalArtisticIor(const ParamMap& inputs,
                 const ShadingContext&,
                 NodeOutputMap* outputs)
{
    const Vec3f reflectivity =
        Get<Vec3f>(inputs, _kReflectivity, Vec3f(0.944f, 0.776f, 0.373f));
    const Vec3f edgeColor =
        Get<Vec3f>(inputs, _kEdgeColor, Vec3f(0.998f, 0.981f, 0.751f));

    Vec3f ior;
    Vec3f extinction;
    for (int i = 0; i < 3; ++i) {
        const float r = std::clamp(reflectivity[i], 0.0f, 0.99f);
        const float rSqrt = std::sqrt(r);
        const float nMin = (1.0f - r) / (1.0f + r);
        const float nMax = (1.0f + rSqrt) / (1.0f - rSqrt);
        ior[i] = Mix(nMax, nMin, edgeColor[i]);

        const float np1 = ior[i] + 1.0f;
        const float nm1 = ior[i] - 1.0f;
        const float k2 =
            (np1 * np1 * r - nm1 * nm1) / (1.0f - r);
        extinction[i] = std::sqrt(std::max(k2, 0.0f));
    }

    (*outputs)[_kIor] = Value(ior);
    (*outputs)[_kExtinction] = Value(extinction);
}

static void
_EvalBlackbody(const ParamMap& inputs,
               const ShadingContext&,
               NodeOutputMap* outputs)
{
    const float temperature = std::clamp(
        Get<float>(inputs, _kTemperature, 5000.0f), 800.0f, 25000.0f);
    const float t = 1000.0f / temperature;
    const float t2 = t * t;
    const float t3 = t2 * t;

    float x;
    if (temperature < 4000.0f) {
        x = -0.2661239f * t3 - 0.2343580f * t2 +
            0.8776956f * t + 0.179910f;
    } else {
        x = -3.0258469f * t3 + 2.1070379f * t2 +
            0.2226347f * t + 0.240390f;
    }

    const float x2 = x * x;
    const float x3 = x2 * x;
    float y;
    if (temperature < 2222.0f) {
        y = -1.1063814f * x3 - 1.34811020f * x2 +
            2.18555832f * x - 0.20219683f;
    } else if (temperature < 4000.0f) {
        y = -0.9549476f * x3 - 1.37418593f * x2 +
            2.09137015f * x - 0.16748867f;
    } else {
        y = 3.0817580f * x3 - 5.87338670f * x2 +
            3.75112997f * x - 0.37001483f;
    }

    if (y <= 0.0f) {
        (*outputs)[_kOut] = Value(Vec3f(1.0f));
        return;
    }

    const float X = x / y;
    const float Z = (1.0f - x - y) / y;
    const Vec3f rgb(
        3.2406f * X - 1.5372f - 0.4986f * Z,
        -0.9689f * X + 1.8758f + 0.0415f * Z,
        0.0557f * X - 0.2040f + 1.0570f * Z);
    (*outputs)[_kOut] = Value(Vec3f(
        std::max(rgb[0], 0.0f),
        std::max(rgb[1], 0.0f),
        std::max(rgb[2], 0.0f)));
}

#define _REG(name, fn) reg.Register(name, fn)

void
RegisterProceduralNodes(NodeRegistry& reg)
{
    _REG("ND_constant_float", &_EvalConstant<float>);
    _REG("ND_constant_integer", &_EvalConstant<int>);
    _REG("ND_constant_boolean", &_EvalConstant<bool>);
    _REG("ND_constant_string", &_EvalConstant<std::string>);
    _REG("ND_constant_filename", &_EvalConstant<std::string>);
    _REG("ND_constant_color3", &_EvalConstant<Vec3f>);
    _REG("ND_constant_color4", &_EvalConstant<Vec4f>);
    _REG("ND_constant_vector2", &_EvalConstant<Vec2f>);
    _REG("ND_constant_vector3", &_EvalConstant<Vec3f>);
    _REG("ND_constant_vector4", &_EvalConstant<Vec4f>);
    _REG("ND_constant_matrix33", &_EvalConstant<Mat3f>);
    _REG("ND_constant_matrix44", &_EvalConstant<Mat4f>);

    _REG("ND_randomfloat_float", &_EvalRandomFloatTyped<float>);
    _REG("ND_randomfloat_integer", &_EvalRandomFloatTyped<int>);

    _REG("ND_artistic_ior", &_EvalArtisticIor);
    _REG("ND_blackbody", &_EvalBlackbody);
}

#undef _REG

}  // namespace mxcpp
