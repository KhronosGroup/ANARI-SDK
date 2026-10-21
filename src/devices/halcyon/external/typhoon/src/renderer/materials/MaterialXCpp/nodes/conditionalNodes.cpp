//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "conditionalNodes.h"

#include <renderer/materials/MaterialXCpp/nodeRegistry.h>

namespace mxcpp {

static const SlotName _kValue1("value1");
static const SlotName _kValue2("value2");
static const SlotName _kIn1("in1");
static const SlotName _kIn2("in2");
static const SlotName _kOut("out");
static const SlotName _kWhich("which");
static const SlotName _kIn("in");

// ---- Conditional nodes ---------------------------------------------------

template<typename T>
static void
_EvalIfGreater(const ParamMap& inputs, const ShadingContext&,
               NodeOutputMap* outputs)
{
    float a = Get<float>(inputs, _kValue1, 0.0f);
    float b = Get<float>(inputs, _kValue2, 0.0f);
    T in1 = Get<T>(inputs, _kIn1, Zero<T>());
    T in2 = Get<T>(inputs, _kIn2, Zero<T>());
    (*outputs)[_kOut] = Value(a > b ? in1 : in2);
}

template<typename T>
static void
_EvalIfGreaterEq(const ParamMap& inputs, const ShadingContext&,
                 NodeOutputMap* outputs)
{
    float a = Get<float>(inputs, _kValue1, 0.0f);
    float b = Get<float>(inputs, _kValue2, 0.0f);
    T in1 = Get<T>(inputs, _kIn1, Zero<T>());
    T in2 = Get<T>(inputs, _kIn2, Zero<T>());
    (*outputs)[_kOut] = Value(a >= b ? in1 : in2);
}

template<typename T>
static void
_EvalIfEqual(const ParamMap& inputs, const ShadingContext&,
             NodeOutputMap* outputs)
{
    float a = Get<float>(inputs, _kValue1, 0.0f);
    float b = Get<float>(inputs, _kValue2, 0.0f);
    T in1 = Get<T>(inputs, _kIn1, Zero<T>());
    T in2 = Get<T>(inputs, _kIn2, Zero<T>());
    (*outputs)[_kOut] = Value(a == b ? in1 : in2);
}

// Switch selects from up to 10 inputs based on a float index (truncated to int).
template<typename T>
static void
_EvalSwitch(const ParamMap& inputs, const ShadingContext&,
            NodeOutputMap* outputs)
{
    float whichF = Get<float>(inputs, _kWhich, 0.0f);
    int which = static_cast<int>(whichF);
    static const SlotName inNames[] = {
        SlotName("in1"), SlotName("in2"), SlotName("in3"),
        SlotName("in4"), SlotName("in5"), SlotName("in6"),
        SlotName("in7"), SlotName("in8"), SlotName("in9"),
        SlotName("in10")
    };
    int idx = std::clamp(which, 0, 9);
    T result = Get<T>(inputs, inNames[idx], Zero<T>());
    (*outputs)[_kOut] = Value(result);
}

// Switch selects from up to 10 inputs based on an integer index.
template<typename T>
static void
_EvalSwitchI(const ParamMap& inputs, const ShadingContext&,
             NodeOutputMap* outputs)
{
    int which = Get<int>(inputs, _kWhich, 0);
    static const SlotName inNames[] = {
        SlotName("in1"), SlotName("in2"), SlotName("in3"),
        SlotName("in4"), SlotName("in5"), SlotName("in6"),
        SlotName("in7"), SlotName("in8"), SlotName("in9"),
        SlotName("in10")
    };
    int idx = std::clamp(which, 0, 9);
    T result = Get<T>(inputs, inNames[idx], Zero<T>());
    (*outputs)[_kOut] = Value(result);
}

// ---- Integer-comparison conditional (I suffix variants) ------------------

template<typename T>
static void
_EvalIfGreaterI(const ParamMap& inputs, const ShadingContext&,
                NodeOutputMap* outputs)
{
    int a = Get<int>(inputs, _kValue1, 0);
    int b = Get<int>(inputs, _kValue2, 0);
    T in1 = Get<T>(inputs, _kIn1, Zero<T>());
    T in2 = Get<T>(inputs, _kIn2, Zero<T>());
    (*outputs)[_kOut] = Value(a > b ? in1 : in2);
}

template<typename T>
static void
_EvalIfGreaterEqI(const ParamMap& inputs, const ShadingContext&,
                  NodeOutputMap* outputs)
{
    int a = Get<int>(inputs, _kValue1, 0);
    int b = Get<int>(inputs, _kValue2, 0);
    T in1 = Get<T>(inputs, _kIn1, Zero<T>());
    T in2 = Get<T>(inputs, _kIn2, Zero<T>());
    (*outputs)[_kOut] = Value(a >= b ? in1 : in2);
}

template<typename T>
static void
_EvalIfEqualI(const ParamMap& inputs, const ShadingContext&,
              NodeOutputMap* outputs)
{
    int a = Get<int>(inputs, _kValue1, 0);
    int b = Get<int>(inputs, _kValue2, 0);
    T in1 = Get<T>(inputs, _kIn1, Zero<T>());
    T in2 = Get<T>(inputs, _kIn2, Zero<T>());
    (*outputs)[_kOut] = Value(a == b ? in1 : in2);
}

// ---- Boolean-comparison conditional (B suffix, ifequal only) -------------

template<typename T>
static void
_EvalIfEqualB(const ParamMap& inputs, const ShadingContext&,
              NodeOutputMap* outputs)
{
    bool a = Get<bool>(inputs, _kValue1, false);
    bool b = Get<bool>(inputs, _kValue2, false);
    T in1 = Get<T>(inputs, _kIn1, Zero<T>());
    T in2 = Get<T>(inputs, _kIn2, Zero<T>());
    (*outputs)[_kOut] = Value(a == b ? in1 : in2);
}

// ---- Boolean output functions (output the comparison result as bool) -----

static void
_EvalIfGreaterBool(const ParamMap& inputs, const ShadingContext&,
                   NodeOutputMap* outputs)
{
    float a = Get<float>(inputs, _kValue1, 0.0f);
    float b = Get<float>(inputs, _kValue2, 0.0f);
    (*outputs)[_kOut] = Value(a > b);
}

static void
_EvalIfGreaterBoolI(const ParamMap& inputs, const ShadingContext&,
                    NodeOutputMap* outputs)
{
    int a = Get<int>(inputs, _kValue1, 0);
    int b = Get<int>(inputs, _kValue2, 0);
    (*outputs)[_kOut] = Value(a > b);
}

static void
_EvalIfGreaterEqBool(const ParamMap& inputs, const ShadingContext&,
                     NodeOutputMap* outputs)
{
    float a = Get<float>(inputs, _kValue1, 0.0f);
    float b = Get<float>(inputs, _kValue2, 0.0f);
    (*outputs)[_kOut] = Value(a >= b);
}

static void
_EvalIfGreaterEqBoolI(const ParamMap& inputs, const ShadingContext&,
                      NodeOutputMap* outputs)
{
    int a = Get<int>(inputs, _kValue1, 0);
    int b = Get<int>(inputs, _kValue2, 0);
    (*outputs)[_kOut] = Value(a >= b);
}

static void
_EvalIfEqualBool(const ParamMap& inputs, const ShadingContext&,
                 NodeOutputMap* outputs)
{
    float a = Get<float>(inputs, _kValue1, 0.0f);
    float b = Get<float>(inputs, _kValue2, 0.0f);
    (*outputs)[_kOut] = Value(a == b);
}

static void
_EvalIfEqualBoolI(const ParamMap& inputs, const ShadingContext&,
                  NodeOutputMap* outputs)
{
    int a = Get<int>(inputs, _kValue1, 0);
    int b = Get<int>(inputs, _kValue2, 0);
    (*outputs)[_kOut] = Value(a == b);
}

static void
_EvalIfEqualBoolB(const ParamMap& inputs, const ShadingContext&,
                  NodeOutputMap* outputs)
{
    bool a = Get<bool>(inputs, _kValue1, false);
    bool b = Get<bool>(inputs, _kValue2, false);
    (*outputs)[_kOut] = Value(a == b);
}

// ---- Logical operations --------------------------------------------------

static void
_EvalLogicalAnd(const ParamMap& inputs, const ShadingContext&,
                NodeOutputMap* outputs)
{
    bool a = Get<bool>(inputs, _kIn1, false);
    bool b = Get<bool>(inputs, _kIn2, false);
    (*outputs)[_kOut] = Value(a && b);
}

static void
_EvalLogicalOr(const ParamMap& inputs, const ShadingContext&,
               NodeOutputMap* outputs)
{
    bool a = Get<bool>(inputs, _kIn1, false);
    bool b = Get<bool>(inputs, _kIn2, false);
    (*outputs)[_kOut] = Value(a || b);
}

static void
_EvalLogicalXor(const ParamMap& inputs, const ShadingContext&,
                NodeOutputMap* outputs)
{
    bool a = Get<bool>(inputs, _kIn1, false);
    bool b = Get<bool>(inputs, _kIn2, false);
    (*outputs)[_kOut] = Value(a != b);
}

static void
_EvalLogicalNot(const ParamMap& inputs, const ShadingContext&,
                NodeOutputMap* outputs)
{
    bool a = Get<bool>(inputs, _kIn, false);
    (*outputs)[_kOut] = Value(!a);
}

// ---- Registration --------------------------------------------------------

#define _REG(name, fn) reg.Register(name, fn)

void
RegisterConditionalNodes(NodeRegistry& reg)
{
    // --- ifgreater (float comparison) ---
    _REG("ND_ifgreater_float",     &_EvalIfGreater<float>);
    _REG("ND_ifgreater_integer",   &_EvalIfGreater<int>);
    _REG("ND_ifgreater_color3",    &_EvalIfGreater<Vec3f>);
    _REG("ND_ifgreater_color4",    &_EvalIfGreater<Vec4f>);
    _REG("ND_ifgreater_vector2",   &_EvalIfGreater<Vec2f>);
    _REG("ND_ifgreater_vector3",   &_EvalIfGreater<Vec3f>);
    _REG("ND_ifgreater_vector4",   &_EvalIfGreater<Vec4f>);
    _REG("ND_ifgreater_matrix33",  &_EvalIfGreater<Mat3f>);
    _REG("ND_ifgreater_matrix44",  &_EvalIfGreater<Mat4f>);
    _REG("ND_ifgreater_boolean",   &_EvalIfGreaterBool);

    // --- ifgreater (integer comparison, I suffix) ---
    _REG("ND_ifgreater_floatI",    &_EvalIfGreaterI<float>);
    _REG("ND_ifgreater_integerI",  &_EvalIfGreaterI<int>);
    _REG("ND_ifgreater_color3I",   &_EvalIfGreaterI<Vec3f>);
    _REG("ND_ifgreater_color4I",   &_EvalIfGreaterI<Vec4f>);
    _REG("ND_ifgreater_vector2I",  &_EvalIfGreaterI<Vec2f>);
    _REG("ND_ifgreater_vector3I",  &_EvalIfGreaterI<Vec3f>);
    _REG("ND_ifgreater_vector4I",  &_EvalIfGreaterI<Vec4f>);
    _REG("ND_ifgreater_matrix33I", &_EvalIfGreaterI<Mat3f>);
    _REG("ND_ifgreater_matrix44I", &_EvalIfGreaterI<Mat4f>);
    _REG("ND_ifgreater_booleanI",  &_EvalIfGreaterBoolI);

    // --- ifgreatereq (float comparison) ---
    _REG("ND_ifgreatereq_float",     &_EvalIfGreaterEq<float>);
    _REG("ND_ifgreatereq_integer",   &_EvalIfGreaterEq<int>);
    _REG("ND_ifgreatereq_color3",    &_EvalIfGreaterEq<Vec3f>);
    _REG("ND_ifgreatereq_color4",    &_EvalIfGreaterEq<Vec4f>);
    _REG("ND_ifgreatereq_vector2",   &_EvalIfGreaterEq<Vec2f>);
    _REG("ND_ifgreatereq_vector3",   &_EvalIfGreaterEq<Vec3f>);
    _REG("ND_ifgreatereq_vector4",   &_EvalIfGreaterEq<Vec4f>);
    _REG("ND_ifgreatereq_matrix33",  &_EvalIfGreaterEq<Mat3f>);
    _REG("ND_ifgreatereq_matrix44",  &_EvalIfGreaterEq<Mat4f>);
    _REG("ND_ifgreatereq_boolean",   &_EvalIfGreaterEqBool);

    // --- ifgreatereq (integer comparison, I suffix) ---
    _REG("ND_ifgreatereq_floatI",    &_EvalIfGreaterEqI<float>);
    _REG("ND_ifgreatereq_integerI",  &_EvalIfGreaterEqI<int>);
    _REG("ND_ifgreatereq_color3I",   &_EvalIfGreaterEqI<Vec3f>);
    _REG("ND_ifgreatereq_color4I",   &_EvalIfGreaterEqI<Vec4f>);
    _REG("ND_ifgreatereq_vector2I",  &_EvalIfGreaterEqI<Vec2f>);
    _REG("ND_ifgreatereq_vector3I",  &_EvalIfGreaterEqI<Vec3f>);
    _REG("ND_ifgreatereq_vector4I",  &_EvalIfGreaterEqI<Vec4f>);
    _REG("ND_ifgreatereq_matrix33I", &_EvalIfGreaterEqI<Mat3f>);
    _REG("ND_ifgreatereq_matrix44I", &_EvalIfGreaterEqI<Mat4f>);
    _REG("ND_ifgreatereq_booleanI",  &_EvalIfGreaterEqBoolI);

    // --- ifequal (float comparison) ---
    _REG("ND_ifequal_float",     &_EvalIfEqual<float>);
    _REG("ND_ifequal_integer",   &_EvalIfEqual<int>);
    _REG("ND_ifequal_color3",    &_EvalIfEqual<Vec3f>);
    _REG("ND_ifequal_color4",    &_EvalIfEqual<Vec4f>);
    _REG("ND_ifequal_vector2",   &_EvalIfEqual<Vec2f>);
    _REG("ND_ifequal_vector3",   &_EvalIfEqual<Vec3f>);
    _REG("ND_ifequal_vector4",   &_EvalIfEqual<Vec4f>);
    _REG("ND_ifequal_matrix33",  &_EvalIfEqual<Mat3f>);
    _REG("ND_ifequal_matrix44",  &_EvalIfEqual<Mat4f>);
    _REG("ND_ifequal_boolean",   &_EvalIfEqualBool);

    // --- ifequal (integer comparison, I suffix) ---
    _REG("ND_ifequal_floatI",    &_EvalIfEqualI<float>);
    _REG("ND_ifequal_integerI",  &_EvalIfEqualI<int>);
    _REG("ND_ifequal_color3I",   &_EvalIfEqualI<Vec3f>);
    _REG("ND_ifequal_color4I",   &_EvalIfEqualI<Vec4f>);
    _REG("ND_ifequal_vector2I",  &_EvalIfEqualI<Vec2f>);
    _REG("ND_ifequal_vector3I",  &_EvalIfEqualI<Vec3f>);
    _REG("ND_ifequal_vector4I",  &_EvalIfEqualI<Vec4f>);
    _REG("ND_ifequal_matrix33I", &_EvalIfEqualI<Mat3f>);
    _REG("ND_ifequal_matrix44I", &_EvalIfEqualI<Mat4f>);
    _REG("ND_ifequal_booleanI",  &_EvalIfEqualBoolI);

    // --- ifequal (boolean comparison, B suffix) ---
    _REG("ND_ifequal_floatB",    &_EvalIfEqualB<float>);
    _REG("ND_ifequal_integerB",  &_EvalIfEqualB<int>);
    _REG("ND_ifequal_color3B",   &_EvalIfEqualB<Vec3f>);
    _REG("ND_ifequal_color4B",   &_EvalIfEqualB<Vec4f>);
    _REG("ND_ifequal_vector2B",  &_EvalIfEqualB<Vec2f>);
    _REG("ND_ifequal_vector3B",  &_EvalIfEqualB<Vec3f>);
    _REG("ND_ifequal_vector4B",  &_EvalIfEqualB<Vec4f>);
    _REG("ND_ifequal_matrix33B", &_EvalIfEqualB<Mat3f>);
    _REG("ND_ifequal_matrix44B", &_EvalIfEqualB<Mat4f>);
    _REG("ND_ifequal_booleanB",  &_EvalIfEqualBoolB);

    // --- switch (float which) ---
    _REG("ND_switch_float",    &_EvalSwitch<float>);
    _REG("ND_switch_color3",   &_EvalSwitch<Vec3f>);
    _REG("ND_switch_color4",   &_EvalSwitch<Vec4f>);
    _REG("ND_switch_vector2",  &_EvalSwitch<Vec2f>);
    _REG("ND_switch_vector3",  &_EvalSwitch<Vec3f>);
    _REG("ND_switch_vector4",  &_EvalSwitch<Vec4f>);
    _REG("ND_switch_matrix33", &_EvalSwitch<Mat3f>);
    _REG("ND_switch_matrix44", &_EvalSwitch<Mat4f>);

    // --- switch (integer which, I suffix) ---
    _REG("ND_switch_floatI",    &_EvalSwitchI<float>);
    _REG("ND_switch_color3I",   &_EvalSwitchI<Vec3f>);
    _REG("ND_switch_color4I",   &_EvalSwitchI<Vec4f>);
    _REG("ND_switch_vector2I",  &_EvalSwitchI<Vec2f>);
    _REG("ND_switch_vector3I",  &_EvalSwitchI<Vec3f>);
    _REG("ND_switch_vector4I",  &_EvalSwitchI<Vec4f>);
    _REG("ND_switch_matrix33I", &_EvalSwitchI<Mat3f>);
    _REG("ND_switch_matrix44I", &_EvalSwitchI<Mat4f>);

    // --- logical operations ---
    _REG("ND_logical_and", &_EvalLogicalAnd);
    _REG("ND_logical_or",  &_EvalLogicalOr);
    _REG("ND_logical_xor", &_EvalLogicalXor);
    _REG("ND_logical_not", &_EvalLogicalNot);
}

#undef _REG

}  // namespace mxcpp
