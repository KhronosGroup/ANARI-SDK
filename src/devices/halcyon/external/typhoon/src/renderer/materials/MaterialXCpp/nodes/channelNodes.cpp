//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "channelNodes.h"

#include <renderer/materials/MaterialXCpp/nodeRegistry.h>
#include <renderer/materials/MaterialXCpp/surfaceShaderUtils.h>

#include <string>

namespace mxcpp {

static const SlotName _kIn("in");
static const SlotName _kIn1("in1");
static const SlotName _kIn2("in2");
static const SlotName _kIn3("in3");
static const SlotName _kIn4("in4");
static const SlotName _kOut("out");
static const SlotName _kOutx("outx");
static const SlotName _kOuty("outy");
static const SlotName _kOutz("outz");
static const SlotName _kOutw("outw");
static const SlotName _kOutr("outr");
static const SlotName _kOutg("outg");
static const SlotName _kOutb("outb");
static const SlotName _kOuta("outa");
static const SlotName _kIndex("index");

// ---- Combine -------------------------------------------------------------

static void
_EvalCombine2(const ParamMap& inputs, const ShadingContext&,
              NodeOutputMap* outputs)
{
    float x = Get<float>(inputs, _kIn1, 0.0f);
    float y = Get<float>(inputs, _kIn2, 0.0f);
    (*outputs)[_kOut] = Value(Vec2f(x, y));
}

static void
_EvalCombine2Color4CF(const ParamMap& inputs, const ShadingContext&,
                      NodeOutputMap* outputs)
{
    const Vec3f c = Get<Vec3f>(inputs, _kIn1, Vec3f(0.0f));
    const float a = Get<float>(inputs, _kIn2, 0.0f);
    (*outputs)[_kOut] = Value(Vec4f(c[0], c[1], c[2], a));
}

static void
_EvalCombine2Vector4VF(const ParamMap& inputs, const ShadingContext&,
                       NodeOutputMap* outputs)
{
    const Vec3f v = Get<Vec3f>(inputs, _kIn1, Vec3f(0.0f));
    const float w = Get<float>(inputs, _kIn2, 0.0f);
    (*outputs)[_kOut] = Value(Vec4f(v[0], v[1], v[2], w));
}

static void
_EvalCombine2Vector4VV(const ParamMap& inputs, const ShadingContext&,
                       NodeOutputMap* outputs)
{
    const Vec2f a = Get<Vec2f>(inputs, _kIn1, Vec2f(0.0f));
    const Vec2f b = Get<Vec2f>(inputs, _kIn2, Vec2f(0.0f));
    (*outputs)[_kOut] = Value(Vec4f(a[0], a[1], b[0], b[1]));
}

static void
_EvalCombine3_color3(const ParamMap& inputs, const ShadingContext&,
                     NodeOutputMap* outputs)
{
    float x = Get<float>(inputs, _kIn1, 0.0f);
    float y = Get<float>(inputs, _kIn2, 0.0f);
    float z = Get<float>(inputs, _kIn3, 0.0f);
    (*outputs)[_kOut] = Value(Vec3f(x, y, z));
}

static void
_EvalCombine4(const ParamMap& inputs, const ShadingContext&,
              NodeOutputMap* outputs)
{
    float x = Get<float>(inputs, _kIn1, 0.0f);
    float y = Get<float>(inputs, _kIn2, 0.0f);
    float z = Get<float>(inputs, _kIn3, 0.0f);
    float w = Get<float>(inputs, _kIn4, 0.0f);
    (*outputs)[_kOut] = Value(Vec4f(x, y, z, w));
}

// ---- Separate ------------------------------------------------------------

static void
_EvalSeparate2(const ParamMap& inputs, const ShadingContext&,
               NodeOutputMap* outputs)
{
    Vec2f v = Get<Vec2f>(inputs, _kIn, Vec2f(0.0f));
    (*outputs)[_kOutx] = Value(v[0]);
    (*outputs)[_kOuty] = Value(v[1]);
}

static void
_EvalSeparate3Vector(const ParamMap& inputs, const ShadingContext&,
                     NodeOutputMap* outputs)
{
    Vec3f v = Get<Vec3f>(inputs, _kIn, Vec3f(0.0f));
    (*outputs)[_kOutx] = Value(v[0]);
    (*outputs)[_kOuty] = Value(v[1]);
    (*outputs)[_kOutz] = Value(v[2]);
}

static void
_EvalSeparate3Color(const ParamMap& inputs, const ShadingContext&,
                    NodeOutputMap* outputs)
{
    Vec3f v = Get<Vec3f>(inputs, _kIn, Vec3f(0.0f));
    (*outputs)[_kOutr] = Value(v[0]);
    (*outputs)[_kOutg] = Value(v[1]);
    (*outputs)[_kOutb] = Value(v[2]);
}

static void
_EvalSeparate4Vector(const ParamMap& inputs, const ShadingContext&,
                     NodeOutputMap* outputs)
{
    Vec4f v = Get<Vec4f>(inputs, _kIn, Vec4f(0.0f));
    (*outputs)[_kOutx] = Value(v[0]);
    (*outputs)[_kOuty] = Value(v[1]);
    (*outputs)[_kOutz] = Value(v[2]);
    (*outputs)[_kOutw] = Value(v[3]);
}

static void
_EvalSeparate4Color(const ParamMap& inputs, const ShadingContext&,
                    NodeOutputMap* outputs)
{
    Vec4f v = Get<Vec4f>(inputs, _kIn, Vec4f(0.0f));
    (*outputs)[_kOutr] = Value(v[0]);
    (*outputs)[_kOutg] = Value(v[1]);
    (*outputs)[_kOutb] = Value(v[2]);
    (*outputs)[_kOuta] = Value(v[3]);
}

// ---- Extract (single channel by index) -----------------------------------

static void
_EvalExtract_vec2(const ParamMap& inputs, const ShadingContext&,
                  NodeOutputMap* outputs)
{
    Vec2f v = Get<Vec2f>(inputs, _kIn, Vec2f(0.0f));
    int idx   = Get<int>(inputs, _kIndex, 0);
    idx = std::clamp(idx, 0, 1);
    (*outputs)[_kOut] = Value(v[idx]);
}

static void
_EvalExtract_vec3(const ParamMap& inputs, const ShadingContext&,
                  NodeOutputMap* outputs)
{
    Vec3f v = Get<Vec3f>(inputs, _kIn, Vec3f(0.0f));
    int idx   = Get<int>(inputs, _kIndex, 0);
    idx = std::clamp(idx, 0, 2);
    (*outputs)[_kOut] = Value(v[idx]);
}

static void
_EvalExtract_vec4(const ParamMap& inputs, const ShadingContext&,
                  NodeOutputMap* outputs)
{
    Vec4f v = Get<Vec4f>(inputs, _kIn, Vec4f(0.0f));
    int idx   = Get<int>(inputs, _kIndex, 0);
    idx = std::clamp(idx, 0, 3);
    (*outputs)[_kOut] = Value(v[idx]);
}

static void
_EvalExtract_matrix33(const ParamMap& inputs, const ShadingContext&,
                      NodeOutputMap* outputs)
{
    const Mat3f m = Get<Mat3f>(inputs, _kIn, Mat3f(0.0f));
    const int index = std::clamp(Get<int>(inputs, _kIndex, 0), 0, 2);
    (*outputs)[_kOut] = Value(
        Vec3f(m[index][0], m[index][1], m[index][2]));
}

static void
_EvalExtract_matrix44(const ParamMap& inputs, const ShadingContext&,
                      NodeOutputMap* outputs)
{
    const Mat4f m = Get<Mat4f>(inputs, _kIn, Mat4f(0.0f));
    const int index = std::clamp(Get<int>(inputs, _kIndex, 0), 0, 3);
    (*outputs)[_kOut] = Value(Vec4f(
        m[index][0], m[index][1], m[index][2], m[index][3]));
}
// ---- Dot (organization pass-through) -------------------------------------

template<typename T>
static void
_EvalDot(const ParamMap& inputs, const ShadingContext&,
         NodeOutputMap* outputs)
{
    (*outputs)[_kOut] = Value(Get<T>(inputs, _kIn, T{}));
}

static void
_EvalDotSurfaceShader(const ParamMap& inputs, const ShadingContext&,
                      NodeOutputMap* outputs)
{
    (*outputs)[_kOut] = Value(Get<SurfaceClosure>(
        inputs, _kIn, MakeEmptySurfaceClosure()));
}

// ---- Convert (type promotion/demotion) -----------------------------------

template<typename Source, typename Destination>
static void
_EvalConvertScalar(const ParamMap& inputs, const ShadingContext&,
                   NodeOutputMap* outputs)
{
    const Source v = Get<Source>(inputs, _kIn, Source(0));
    (*outputs)[_kOut] = Value(static_cast<Destination>(v));
}

template<typename Source, typename Destination>
static void
_EvalConvertScalarToVector(const ParamMap& inputs, const ShadingContext&,
                           NodeOutputMap* outputs)
{
    const Source v = Get<Source>(inputs, _kIn, Source(0));
    (*outputs)[_kOut] = Value(Destination(static_cast<float>(v)));
}

static void
_EvalConvert_color3_float(const ParamMap& inputs,
                          const ShadingContext&,
                          NodeOutputMap* outputs)
{
    Vec3f v = Get<Vec3f>(inputs, _kIn, Vec3f(0.0f));
    (*outputs)[_kOut] = Value((v[0] + v[1] + v[2]) / 3.0f);
}

static void
_EvalConvert2To3(const ParamMap& inputs, const ShadingContext&,
                 NodeOutputMap* outputs)
{
    const Vec2f v = Get<Vec2f>(inputs, _kIn, Vec2f(0.0f));
    (*outputs)[_kOut] = Value(Vec3f(v[0], v[1], 0.0f));
}

static void
_EvalConvert2To4(const ParamMap& inputs, const ShadingContext&,
                 NodeOutputMap* outputs)
{
    const Vec2f v = Get<Vec2f>(inputs, _kIn, Vec2f(0.0f));
    (*outputs)[_kOut] = Value(Vec4f(v[0], v[1], 0.0f, 1.0f));
}

static void
_EvalConvert3To2(const ParamMap& inputs, const ShadingContext&,
                 NodeOutputMap* outputs)
{
    const Vec3f v = Get<Vec3f>(inputs, _kIn, Vec3f(0.0f));
    (*outputs)[_kOut] = Value(Vec2f(v[0], v[1]));
}

static void
_EvalConvert3To3(const ParamMap& inputs, const ShadingContext&,
                 NodeOutputMap* outputs)
{
    (*outputs)[_kOut] = Value(Get<Vec3f>(inputs, _kIn, Vec3f(0.0f)));
}

static void
_EvalConvert3To4(const ParamMap& inputs, const ShadingContext&,
                 NodeOutputMap* outputs)
{
    const Vec3f v = Get<Vec3f>(inputs, _kIn, Vec3f(0.0f));
    (*outputs)[_kOut] = Value(Vec4f(v[0], v[1], v[2], 1.0f));
}

static void
_EvalConvert4To2(const ParamMap& inputs, const ShadingContext&,
                 NodeOutputMap* outputs)
{
    const Vec4f v = Get<Vec4f>(inputs, _kIn, Vec4f(0.0f));
    (*outputs)[_kOut] = Value(Vec2f(v[0], v[1]));
}

static void
_EvalConvert4To3(const ParamMap& inputs, const ShadingContext&,
                 NodeOutputMap* outputs)
{
    const Vec4f v = Get<Vec4f>(inputs, _kIn, Vec4f(0.0f));
    (*outputs)[_kOut] = Value(Vec3f(v[0], v[1], v[2]));
}

static void
_EvalConvert4To4(const ParamMap& inputs, const ShadingContext&,
                 NodeOutputMap* outputs)
{
    (*outputs)[_kOut] = Value(Get<Vec4f>(inputs, _kIn, Vec4f(0.0f)));
}

static void
_EvalConvertFloatSurfaceShader(const ParamMap& inputs, const ShadingContext&,
                               NodeOutputMap* outputs)
{
    const float v = Get<float>(inputs, _kIn, 0.0f);
    (*outputs)[_kOut] = Value(MakeUnlitSurfaceClosure(Vec3f(v)));
}

static void
_EvalConvertIntegerSurfaceShader(const ParamMap& inputs, const ShadingContext&,
                                 NodeOutputMap* outputs)
{
    const float v = static_cast<float>(Get<int>(inputs, _kIn, 0));
    (*outputs)[_kOut] = Value(MakeUnlitSurfaceClosure(Vec3f(v)));
}

static void
_EvalConvertBooleanSurfaceShader(const ParamMap& inputs, const ShadingContext&,
                                 NodeOutputMap* outputs)
{
    const float v = Get<bool>(inputs, _kIn, false) ? 1.0f : 0.0f;
    (*outputs)[_kOut] = Value(MakeUnlitSurfaceClosure(Vec3f(v)));
}

static void
_EvalConvertVector2SurfaceShader(const ParamMap& inputs, const ShadingContext&,
                                 NodeOutputMap* outputs)
{
    const Vec2f v = Get<Vec2f>(inputs, _kIn, Vec2f(0.0f));
    (*outputs)[_kOut] = Value(MakeUnlitSurfaceClosure(Vec3f(v[0], v[1], 0.0f)));
}

static void
_EvalConvertVector3SurfaceShader(const ParamMap& inputs, const ShadingContext&,
                                 NodeOutputMap* outputs)
{
    (*outputs)[_kOut] = Value(
        MakeUnlitSurfaceClosure(Get<Vec3f>(inputs, _kIn, Vec3f(0.0f))));
}

static void
_EvalConvertVector4SurfaceShader(const ParamMap& inputs, const ShadingContext&,
                                 NodeOutputMap* outputs)
{
    const Vec4f v = Get<Vec4f>(inputs, _kIn, Vec4f(0.0f, 0.0f, 0.0f, 1.0f));
    (*outputs)[_kOut] = Value(
        MakeUnlitSurfaceClosure(Vec3f(v[0], v[1], v[2]), v[3]));
}

// ---- Registration --------------------------------------------------------

#define _REG(name, fn) reg.Register(name, fn)

void
RegisterChannelNodes(NodeRegistry& reg)
{
    _REG("ND_combine2_vector2", &_EvalCombine2);
    _REG("ND_combine2_color4CF", &_EvalCombine2Color4CF);
    _REG("ND_combine2_vector4VF", &_EvalCombine2Vector4VF);
    _REG("ND_combine2_vector4VV", &_EvalCombine2Vector4VV);
    _REG("ND_combine3_color3",  &_EvalCombine3_color3);
    _REG("ND_combine3_vector3", &_EvalCombine3_color3);
    _REG("ND_combine4_color4",  &_EvalCombine4);
    _REG("ND_combine4_vector4", &_EvalCombine4);

    _REG("ND_separate2_vector2", &_EvalSeparate2);
    _REG("ND_separate3_color3",  &_EvalSeparate3Color);
    _REG("ND_separate3_vector3", &_EvalSeparate3Vector);
    _REG("ND_separate4_color4",  &_EvalSeparate4Color);
    _REG("ND_separate4_vector4", &_EvalSeparate4Vector);

    _REG("ND_extract_vector2", &_EvalExtract_vec2);
    _REG("ND_extract_color3",  &_EvalExtract_vec3);
    _REG("ND_extract_vector3", &_EvalExtract_vec3);
    _REG("ND_extract_color4",  &_EvalExtract_vec4);
    _REG("ND_extract_vector4", &_EvalExtract_vec4);
    _REG("ND_extract_matrix33", &_EvalExtract_matrix33);
    _REG("ND_extract_matrix44", &_EvalExtract_matrix44);

    _REG("ND_dot_float", &_EvalDot<float>);
    _REG("ND_dot_color3", &_EvalDot<Vec3f>);
    _REG("ND_dot_color4", &_EvalDot<Vec4f>);
    _REG("ND_dot_vector2", &_EvalDot<Vec2f>);
    _REG("ND_dot_vector3", &_EvalDot<Vec3f>);
    _REG("ND_dot_vector4", &_EvalDot<Vec4f>);
    _REG("ND_dot_boolean", &_EvalDot<bool>);
    _REG("ND_dot_integer", &_EvalDot<int>);
    _REG("ND_dot_matrix33", &_EvalDot<Mat3f>);
    _REG("ND_dot_matrix44", &_EvalDot<Mat4f>);
    _REG("ND_dot_string", &_EvalDot<std::string>);
    _REG("ND_dot_filename", &_EvalDot<std::string>);
    _REG("ND_dot_surfaceshader", &_EvalDotSurfaceShader);

    _REG("ND_convert_float_color3",     (&_EvalConvertScalarToVector<float, Vec3f>));
    _REG("ND_convert_float_color4",     (&_EvalConvertScalarToVector<float, Vec4f>));
    _REG("ND_convert_float_vector2",    (&_EvalConvertScalarToVector<float, Vec2f>));
    _REG("ND_convert_float_vector3",    (&_EvalConvertScalarToVector<float, Vec3f>));
    _REG("ND_convert_float_vector4",    (&_EvalConvertScalarToVector<float, Vec4f>));

    _REG("ND_convert_color3_float",     &_EvalConvert_color3_float);
    _REG("ND_convert_color3_color4",    &_EvalConvert3To4);
    _REG("ND_convert_color3_vector2",   &_EvalConvert3To2);
    _REG("ND_convert_color3_vector3",   &_EvalConvert3To3);
    _REG("ND_convert_color3_vector4",   &_EvalConvert3To4);

    _REG("ND_convert_color4_color3",    &_EvalConvert4To3);
    _REG("ND_convert_color4_vector2",   &_EvalConvert4To2);
    _REG("ND_convert_color4_vector3",   &_EvalConvert4To3);
    _REG("ND_convert_color4_vector4",   &_EvalConvert4To4);

    _REG("ND_convert_vector2_color3",   &_EvalConvert2To3);
    _REG("ND_convert_vector2_color4",   &_EvalConvert2To4);
    _REG("ND_convert_vector2_vector3",  &_EvalConvert2To3);
    _REG("ND_convert_vector2_vector4",  &_EvalConvert2To4);

    _REG("ND_convert_vector3_color3",   &_EvalConvert3To3);
    _REG("ND_convert_vector3_color4",   &_EvalConvert3To4);
    _REG("ND_convert_vector3_vector2",  &_EvalConvert3To2);
    _REG("ND_convert_vector3_vector4",  &_EvalConvert3To4);

    _REG("ND_convert_vector4_color3",   &_EvalConvert4To3);
    _REG("ND_convert_vector4_color4",   &_EvalConvert4To4);
    _REG("ND_convert_vector4_vector2",  &_EvalConvert4To2);
    _REG("ND_convert_vector4_vector3",  &_EvalConvert4To3);

    _REG("ND_convert_boolean_float",    (&_EvalConvertScalar<bool, float>));
    _REG("ND_convert_boolean_color3",   (&_EvalConvertScalarToVector<bool, Vec3f>));
    _REG("ND_convert_boolean_color4",   (&_EvalConvertScalarToVector<bool, Vec4f>));
    _REG("ND_convert_boolean_vector2",  (&_EvalConvertScalarToVector<bool, Vec2f>));
    _REG("ND_convert_boolean_vector3",  (&_EvalConvertScalarToVector<bool, Vec3f>));
    _REG("ND_convert_boolean_vector4",  (&_EvalConvertScalarToVector<bool, Vec4f>));
    _REG("ND_convert_boolean_integer",  (&_EvalConvertScalar<bool, int>));

    _REG("ND_convert_integer_float",    (&_EvalConvertScalar<int, float>));
    _REG("ND_convert_integer_color3",   (&_EvalConvertScalarToVector<int, Vec3f>));
    _REG("ND_convert_integer_color4",   (&_EvalConvertScalarToVector<int, Vec4f>));
    _REG("ND_convert_integer_vector2",  (&_EvalConvertScalarToVector<int, Vec2f>));
    _REG("ND_convert_integer_vector3",  (&_EvalConvertScalarToVector<int, Vec3f>));
    _REG("ND_convert_integer_vector4",  (&_EvalConvertScalarToVector<int, Vec4f>));
    _REG("ND_convert_integer_boolean",  (&_EvalConvertScalar<int, bool>));

    _REG("ND_convert_float_surfaceshader", &_EvalConvertFloatSurfaceShader);
    _REG("ND_convert_integer_surfaceshader", &_EvalConvertIntegerSurfaceShader);
    _REG("ND_convert_boolean_surfaceshader", &_EvalConvertBooleanSurfaceShader);
    _REG("ND_convert_color3_surfaceshader", &_EvalConvertVector3SurfaceShader);
    _REG("ND_convert_color4_surfaceshader", &_EvalConvertVector4SurfaceShader);
    _REG("ND_convert_vector2_surfaceshader", &_EvalConvertVector2SurfaceShader);
    _REG("ND_convert_vector3_surfaceshader", &_EvalConvertVector3SurfaceShader);
    _REG("ND_convert_vector4_surfaceshader", &_EvalConvertVector4SurfaceShader);
}

#undef _REG

}  // namespace mxcpp
