//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include <renderer/materials/MaterialXCpp/nodeRegistry.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <functional>
#include <iostream>
#include <sstream>
#include <unordered_map>
#include <vector>

using namespace mxcpp;

void Test_Register(const char* name, std::function<bool()> fn);
bool Test_IsClose(float a, float b, float eps = 1e-5f);
bool Test_IsClose(const Vec3f& a, const Vec3f& b, float eps = 1e-5f);

#define _REG(name) Test_Register("Nodes." #name, &name)

// Helper: evaluate a registered node with given inputs.
static NodeOutputMap
_Eval(const char* nodeTypeId, const ParamMap& inputs)
{
    NodeRegistry::RegisterBuiltinNodes();
    auto fn = NodeRegistry::GetInstance().Find(std::string(nodeTypeId));
    NodeOutputMap outputs;
    ShadingContext ctx;
    if (fn) {
        fn(inputs, ctx, &outputs);
    }
    return outputs;
}

static NodeOutputMap
_EvalWithCtx(const char* nodeTypeId,
             const ParamMap& inputs,
             const ShadingContext& ctx)
{
    NodeRegistry::RegisterBuiltinNodes();
    auto fn = NodeRegistry::GetInstance().Find(std::string(nodeTypeId));
    NodeOutputMap outputs;
    if (fn) {
        fn(inputs, ctx, &outputs);
    }
    return outputs;
}

static float _GetFloat(const NodeOutputMap& o, const char* name = "out") {
    const Value* value = o.Find(name);
    if (value && ValueHolds<float>(*value))
        return ValueGet<float>(*value);
    return -9999.0f;
}

static Vec3f _GetVec3(const NodeOutputMap& o, const char* name = "out") {
    const Value* value = o.Find(name);
    if (value && ValueHolds<Vec3f>(*value))
        return ValueGet<Vec3f>(*value);
    return Vec3f(-9999.0f);
}

static Vec2f _GetVec2(const NodeOutputMap& o, const char* name = "out") {
    const Value* value = o.Find(name);
    if (value && ValueHolds<Vec2f>(*value))
        return ValueGet<Vec2f>(*value);
    return Vec2f(-9999.0f);
}

static Vec4f _GetVec4(const NodeOutputMap& o, const char* name = "out") {
    const Value* value = o.Find(name);
    if (value && ValueHolds<Vec4f>(*value))
        return ValueGet<Vec4f>(*value);
    return Vec4f(-9999.0f);
}

static bool _GetBool(const NodeOutputMap& o, const char* name = "out") {
    const Value* value = o.Find(name);
    if (value && ValueHolds<bool>(*value))
        return ValueGet<bool>(*value);
    return false;
}

static int _GetInt(const NodeOutputMap& o, const char* name = "out") {
    const Value* value = o.Find(name);
    if (value && ValueHolds<int>(*value))
        return ValueGet<int>(*value);
    return -9999;
}

static bool Test_IsClose(const Vec2f& a, const Vec2f& b, float eps = 1e-5f) {
    return Test_IsClose(a[0], b[0], eps) && Test_IsClose(a[1], b[1], eps);
}

static bool Test_IsClose(const Vec4f& a, const Vec4f& b, float eps = 1e-5f) {
    return Test_IsClose(a[0], b[0], eps) && Test_IsClose(a[1], b[1], eps) &&
           Test_IsClose(a[2], b[2], eps) && Test_IsClose(a[3], b[3], eps);
}

static Mat4f _MakeTranslationMatrix(const Vec3f& t) {
    Mat4f m;
    m.setTranslation(t);
    return m;
}

static Mat4f _MakeIdentityMatrix() {
    Mat4f m;
    m.makeIdentity();
    return m;
}

static void _SetObjectWorldTransform(ShadingContext* ctx, const Mat4f& objectToWorld) {
    if (!ctx) {
        return;
    }
    ctx->objectToWorldMatrix = objectToWorld;
    ctx->worldToObjectMatrix = objectToWorld.inverse();
    ctx->hasObjectToWorldTransform = true;
    ctx->hasWorldToObjectTransform = true;
}

struct _TestTextureSystem final : public TextureSystem {
    mutable bool called = false;
    mutable Texture2DRequest lastRequest;
    mutable std::vector<Texture2DRequest> requests;
    Texture2DResult nextResult;
    std::vector<Texture2DResult> nextResults;

    Texture2DResult Sample2D(const Texture2DRequest& request) const override {
        called = true;
        lastRequest = request;
        requests.push_back(request);
        const size_t index = requests.size() - 1;
        if (index < nextResults.size()) {
            return nextResults[index];
        }
        return nextResult;
    }
};

static bool
_EvalHeightFromTexcoordX(const void*,
                         int,
                         SlotId,
                         const ShadingContext& ctx,
                         Value* out)
{
    if (out) {
        *out = Value(ctx.texcoord[0]);
    }
    return true;
}

static bool
_EvalHeightFromTexcoordXSquared(const void*,
                                int,
                                SlotId,
                                const ShadingContext& ctx,
                                Value* out)
{
    if (out) {
        const float u = ctx.texcoord[0];
        *out = Value(u * u);
    }
    return true;
}

static bool
_EvalHeightFromTextureBlurS(const void*,
                            int,
                            SlotId,
                            const ShadingContext& ctx,
                            Value* out)
{
    if (out) {
        *out = Value(ctx.textureBlur[0]);
    }
    return true;
}

static bool
_EvalRotationFromTexcoordX(const void*,
                           int,
                           SlotId,
                           const ShadingContext& ctx,
                           Value* out)
{
    if (out) {
        *out = Value(ctx.texcoord[0] * 90.0f);
    }
    return true;
}

static bool
_EvalViewdirFromTexcoord(const void*,
                         int,
                         SlotId,
                         const ShadingContext& ctx,
                         Value* out)
{
    if (out) {
        *out = Value(Vec3f(ctx.texcoord[0], ctx.texcoord[1], 1.0f));
    }
    return true;
}

static bool
_EvalPositionFromTexcoordAndZ(const void*,
                              int,
                              SlotId,
                              const ShadingContext& ctx,
                              Value* out)
{
    if (out) {
        *out = Value(Vec3f(ctx.texcoord[0], ctx.texcoord[1], ctx.position[2]));
    }
    return true;
}

static bool
_TransformWorldNormalToObjectX(const void*,
                               const std::string& fromSpace,
                               const std::string& toSpace,
                               ShadingContext::TransformSpaceType type,
                               const Vec3f& in,
                               Vec3f* out)
{
    if (type != ShadingContext::TransformSpaceType::Normal ||
        fromSpace != "world" || toSpace != "object") {
        return false;
    }

    if (out) {
        *out = Vec3f(in[2], in[1], -in[0]);
    }
    return true;
}

struct _TestColorTransformState {
    bool handled = true;
    int callCount = 0;
    std::string lastSourceColorSpace;
    std::string lastTargetColorSpace;
    Vec3f lastInput = Vec3f(0.0f);
    Vec3f outputValue = Vec3f(0.0f);
};

static bool
_TestColorTransformCallback(const void* userData,
                            const std::string& sourceColorSpace,
                            const std::string& targetColorSpace,
                            const Vec3f& in,
                            Vec3f* out)
{
    auto* const state = const_cast<_TestColorTransformState*>(
        static_cast<const _TestColorTransformState*>(userData));
    if (!state) {
        return false;
    }

    state->callCount += 1;
    state->lastSourceColorSpace = sourceColorSpace;
    state->lastTargetColorSpace = targetColorSpace;
    state->lastInput = in;
    if (!state->handled) {
        return false;
    }

    if (out) {
        *out = state->outputValue;
    }
    return true;
}

static float
_ApplySrgbTextureToLinearRec709Test(const float in)
{
    if (in > 0.04045f) {
        return std::pow(std::max(in + 0.055f, 0.0f) / 1.055f, 2.4f);
    }
    return in / 12.92f;
}

// ---------------------------------------------------------------------------
// Math node tests
// ---------------------------------------------------------------------------

static bool TestAddFloat() {
    ParamMap in;
    in["in1"] = Value(2.0f);
    in["in2"] = Value(3.0f);
    auto out = _Eval("ND_add_float", in);
    return Test_IsClose(_GetFloat(out), 5.0f);
}

static bool TestAddColor3() {
    ParamMap in;
    in["in1"] = Value(Vec3f(1, 2, 3));
    in["in2"] = Value(Vec3f(4, 5, 6));
    auto out = _Eval("ND_add_color3", in);
    return Test_IsClose(_GetVec3(out), Vec3f(5, 7, 9));
}

static bool TestMultiplyFloat() {
    ParamMap in;
    in["in1"] = Value(3.0f);
    in["in2"] = Value(4.0f);
    auto out = _Eval("ND_multiply_float", in);
    return Test_IsClose(_GetFloat(out), 12.0f);
}

static bool TestSubtractFloat() {
    ParamMap in;
    in["in1"] = Value(10.0f);
    in["in2"] = Value(3.0f);
    auto out = _Eval("ND_subtract_float", in);
    return Test_IsClose(_GetFloat(out), 7.0f);
}

static bool TestDivideFloat() {
    ParamMap in;
    in["in1"] = Value(10.0f);
    in["in2"] = Value(4.0f);
    auto out = _Eval("ND_divide_float", in);
    if (!Test_IsClose(_GetFloat(out), 2.5f)) return false;

    // OSL maps degenerate division to zero.
    in["in2"] = Value(0.0f);
    out = _Eval("ND_divide_float", in);
    return Test_IsClose(_GetFloat(out), 0.0f);
}
static bool TestRun0052MathSemantics() {
    ParamMap in;
    in["in"] = Value(Vec4f(-1.2f, -0.2f, 0.5f, 1.2f));
    auto out = _Eval("ND_asin_vector4", in);
    if (!Test_IsClose(
            _GetVec4(out),
            Vec4f(0.0f, std::asin(-0.2f), std::asin(0.5f), 0.0f))) {
        return false;
    }

    in = ParamMap();
    in["in"] = Value(true);
    if (!Test_IsClose(_GetFloat(_Eval("ND_dot_float", in)), 1.0f)) {
        return false;
    }

    in = ParamMap();
    in["in"] = Value(Vec2f(1.0f, 0.0f));
    in["amount"] = Value(90.0f);
    if (!Test_IsClose(
            _GetVec2(_Eval("ND_rotate2d_vector2", in)),
            Vec2f(0.0f, -1.0f),
            1.0e-5f)) {
        return false;
    }

    in = ParamMap();
    in["texcoord"] = Value(Vec2f(0.8f, 0.6f));
    in["scale"] = Value(Vec2f(2.0f, 4.0f));
    if (!Test_IsClose(
            _GetVec2(_Eval("ND_place2d_vector2", in)),
            Vec2f(0.4f, 0.15f))) {
        return false;
    }

    Mat3f matrix(0.0f);
    matrix[0][0] = 1.0f;
    matrix[0][1] = 2.0f;
    matrix[0][2] = 3.0f;
    in = ParamMap();
    in["in"] = Value(matrix);
    in["index"] = Value(0);
    return Test_IsClose(
        _GetVec3(_Eval("ND_extract_matrix33", in)),
        Vec3f(1.0f, 2.0f, 3.0f));
}
static bool TestTransformMatrixDropsHomogeneousOutput() {
    const Vec2f input2(0.25f, 0.75f);
    ParamMap in;

    Mat3f matrix3;
    matrix3[0][2] = 0.2f;
    matrix3[1][2] = -0.1f;
    in["in"] = Value(input2);
    in["mat"] = Value(matrix3);
    if (!Test_IsClose(
            _GetVec2(_Eval("ND_transformmatrix_vector2M3", in)),
            input2)) {
        return false;
    }

    matrix3 = Mat3f();
    matrix3[2][0] = 0.2f;
    matrix3[2][1] = -0.1f;
    in["mat"] = Value(matrix3);
    if (!Test_IsClose(
            _GetVec2(_Eval("ND_transformmatrix_vector2M3", in)),
            input2 + Vec2f(0.2f, -0.1f))) {
        return false;
    }

    const Vec3f input3(0.25f, 0.75f, 0.0f);
    Mat4f matrix4;
    matrix4[0][3] = 0.2f;
    matrix4[1][3] = -0.1f;
    in["in"] = Value(input3);
    in["mat"] = Value(matrix4);
    if (!Test_IsClose(
            _GetVec3(_Eval("ND_transformmatrix_vector3M4", in)),
            input3)) {
        return false;
    }

    matrix4 = Mat4f();
    matrix4[3][0] = 0.2f;
    matrix4[3][1] = -0.1f;
    in["mat"] = Value(matrix4);
    return Test_IsClose(
        _GetVec3(_Eval("ND_transformmatrix_vector3M4", in)),
        input3 + Vec3f(0.2f, -0.1f, 0.0f));
}

static bool TestAtan2UsesCanonicalInputs() {
    ParamMap in;
    in["iny"] = Value(1.0f);
    in["inx"] = Value(0.0f);
    auto out = _Eval("ND_atan2_float", in);
    if (!Test_IsClose(_GetFloat(out), 0.5f * 3.14159265358979323846f)) {
        return false;
    }

    return Test_IsClose(_GetFloat(_Eval("ND_atan2_float", ParamMap())), 0.0f);
}

static bool TestClamp() {
    ParamMap in;
    in["in"] = Value(1.5f);
    in["low"] = Value(0.0f);
    in["high"] = Value(1.0f);
    auto out = _Eval("ND_clamp_float", in);
    if (!Test_IsClose(_GetFloat(out), 1.0f)) return false;

    in["in"] = Value(-0.5f);
    out = _Eval("ND_clamp_float", in);
    return Test_IsClose(_GetFloat(out), 0.0f);
}

static bool TestMix() {
    ParamMap in;
    in["fg"] = Value(10.0f);
    in["bg"] = Value(0.0f);

    in["mix"] = Value(0.0f);
    auto out = _Eval("ND_mix_float", in);
    if (!Test_IsClose(_GetFloat(out), 0.0f)) return false;

    in["mix"] = Value(1.0f);
    out = _Eval("ND_mix_float", in);
    if (!Test_IsClose(_GetFloat(out), 10.0f)) return false;

    in["mix"] = Value(0.5f);
    out = _Eval("ND_mix_float", in);
    return Test_IsClose(_GetFloat(out), 5.0f);
}

static bool TestSmoothstep() {
    ParamMap in;
    in["low"] = Value(0.0f);
    in["high"] = Value(1.0f);

    in["in"] = Value(0.0f);
    auto out = _Eval("ND_smoothstep_float", in);
    if (!Test_IsClose(_GetFloat(out), 0.0f)) return false;

    in["in"] = Value(1.0f);
    out = _Eval("ND_smoothstep_float", in);
    if (!Test_IsClose(_GetFloat(out), 1.0f)) return false;

    in["in"] = Value(0.5f);
    out = _Eval("ND_smoothstep_float", in);
    if (!Test_IsClose(_GetFloat(out), 0.5f)) return false;

    // Equal and inverted bounds use the deterministic hard-step behavior
    // emitted by MaterialX, with the upper comparison taking precedence.
    in["low"] = Value(0.5f);
    in["high"] = Value(0.5f);
    in["in"] = Value(0.49f);
    out = _Eval("ND_smoothstep_float", in);
    if (!Test_IsClose(_GetFloat(out), 0.0f)) return false;
    in["in"] = Value(0.5f);
    out = _Eval("ND_smoothstep_float", in);
    if (!Test_IsClose(_GetFloat(out), 1.0f)) return false;

    in["low"] = Value(0.75f);
    in["high"] = Value(0.25f);
    in["in"] = Value(0.2f);
    out = _Eval("ND_smoothstep_float", in);
    if (!Test_IsClose(_GetFloat(out), 0.0f)) return false;
    in["in"] = Value(0.5f);
    out = _Eval("ND_smoothstep_float", in);
    return Test_IsClose(_GetFloat(out), 1.0f);
}

static bool TestSmoothstepOverloads() {
    ParamMap full;
    full["in"] = Value(Vec4f(0.5f, 0.5f, 0.2f, 0.4f));
    full["low"] = Value(Vec4f(0.0f, 0.5f, 0.75f, 0.5f));
    full["high"] = Value(Vec4f(1.0f, 0.5f, 0.25f, 0.5f));
    auto out = _Eval("ND_smoothstep_vector4", full);
    if (!Test_IsClose(_GetVec4(out), Vec4f(0.5f, 1.0f, 0.0f, 0.0f))) {
        return false;
    }

    ParamMap color;
    color["in"] = Value(Vec3f(0.25f, 0.5f, 0.75f));
    color["low"] = Value(Vec3f(0.0f));
    color["high"] = Value(Vec3f(1.0f));
    out = _Eval("ND_smoothstep_color3", color);
    if (!Test_IsClose(_GetVec3(out), Vec3f(0.15625f, 0.5f, 0.84375f))) {
        return false;
    }

    ParamMap scalarBounds;
    scalarBounds["in"] = Value(Vec2f(0.25f, 0.5f));
    scalarBounds["low"] = Value(0.25f);
    scalarBounds["high"] = Value(0.75f);
    out = _Eval("ND_smoothstep_vector2FA", scalarBounds);
    return Test_IsClose(_GetVec2(out), Vec2f(0.0f, 0.5f));
}

static bool TestRemap() {
    ParamMap in;
    in["in"] = Value(0.5f);
    in["inlow"] = Value(0.0f);
    in["inhigh"] = Value(1.0f);
    in["outlow"] = Value(10.0f);
    in["outhigh"] = Value(20.0f);
    auto out = _Eval("ND_remap_float", in);
    return Test_IsClose(_GetFloat(out), 15.0f);
}

static bool TestRemapOverloads() {
    ParamMap full;
    full["in"] = Value(Vec4f(0.5f, 0.5f, 0.25f, 0.75f));
    full["inlow"] = Value(Vec4f(0.0f, 0.0f, 0.75f, 0.75f));
    full["inhigh"] = Value(Vec4f(1.0f, 0.0f, 0.25f, 0.25f));
    full["outlow"] = Value(Vec4f(0.1f, 0.2f, 0.0f, 0.0f));
    full["outhigh"] = Value(Vec4f(0.9f, 0.8f, 1.0f, 1.0f));
    auto out = _Eval("ND_remap_vector4", full);
    if (!Test_IsClose(_GetVec4(out), Vec4f(0.5f, 0.2f, 1.0f, 0.0f))) {
        return false;
    }

    ParamMap color;
    color["in"] = Value(Vec3f(-2.0f, 0.0f, 2.0f));
    color["inlow"] = Value(Vec3f(-2.0f));
    color["inhigh"] = Value(Vec3f(2.0f));
    color["outlow"] = Value(Vec3f(0.0f, 0.1f, 0.2f));
    color["outhigh"] = Value(Vec3f(1.0f, 0.9f, 0.8f));
    out = _Eval("ND_remap_color3", color);
    if (!Test_IsClose(_GetVec3(out), Vec3f(0.0f, 0.5f, 0.8f))) {
        return false;
    }

    ParamMap scalarBounds;
    scalarBounds["in"] = Value(Vec2f(0.25f, 0.75f));
    scalarBounds["inlow"] = Value(0.0f);
    scalarBounds["inhigh"] = Value(1.0f);
    scalarBounds["outlow"] = Value(2.0f);
    scalarBounds["outhigh"] = Value(4.0f);
    out = _Eval("ND_remap_vector2FA", scalarBounds);
    return Test_IsClose(_GetVec2(out), Vec2f(2.5f, 3.5f));
}

static bool TestOpenPbrAnisotropy() {
    ParamMap in;
    auto out = _Eval("ND_open_pbr_anisotropy", in);
    if (!Test_IsClose(_GetVec2(out), Vec2f(0.0f))) return false;

    in["roughness"] = Value(0.5f);
    in["anisotropy"] = Value(0.0f);
    out = _Eval("ND_open_pbr_anisotropy", in);
    if (!Test_IsClose(_GetVec2(out), Vec2f(0.25f, 0.25f))) return false;

    in["anisotropy"] = Value(0.5f);
    out = _Eval("ND_open_pbr_anisotropy", in);
    return Test_IsClose(
        _GetVec2(out), Vec2f(0.31622777f, 0.15811388f));
}

// ---------------------------------------------------------------------------
// Channel node tests
// ---------------------------------------------------------------------------

static bool TestCombineSeparateRoundtrip() {
    ParamMap in;
    in["in1"] = Value(0.2f);
    in["in2"] = Value(0.4f);
    in["in3"] = Value(0.6f);
    auto combOut = _Eval("ND_combine3_color3", in);
    Vec3f combined = _GetVec3(combOut);

    ParamMap in2;
    in2["in"] = Value(combined);
    auto sepOut = _Eval("ND_separate3_color3", in2);
    float x = _GetFloat(sepOut, "outr");
    float y = _GetFloat(sepOut, "outg");
    float z = _GetFloat(sepOut, "outb");
    return Test_IsClose(x, 0.2f) &&
           Test_IsClose(y, 0.4f) &&
           Test_IsClose(z, 0.6f);
}

static bool TestSeparateColorOutputs() {
    ParamMap in;
    in["in"] = Value(Vec4f(0.1f, 0.2f, 0.3f, 0.4f));
    const auto out = _Eval("ND_separate4_color4", in);
    return Test_IsClose(_GetFloat(out, "outr"), 0.1f) &&
           Test_IsClose(_GetFloat(out, "outg"), 0.2f) &&
           Test_IsClose(_GetFloat(out, "outb"), 0.3f) &&
           Test_IsClose(_GetFloat(out, "outa"), 0.4f);
}

static bool TestConvertColor4ToColor3() {
    ParamMap in;
    in["in"] = Value(Vec4f(0.2f, 0.4f, 0.6f, 0.8f));

    auto colorOut = _Eval("ND_convert_color4_color3", in);
    if (!Test_IsClose(_GetVec3(colorOut), Vec3f(0.2f, 0.4f, 0.6f), 1e-6f)) {
        return false;
    }

    auto vec2Out = _Eval("ND_convert_vector4_vector2", in);
    return Test_IsClose(_GetVec2(vec2Out), Vec2f(0.2f, 0.4f), 1e-6f);
}

static bool TestChannelConvertValues() {
    ParamMap in;
    in["in"] = Value(0.25f);
    auto out = _Eval("ND_convert_float_color4", in);
    if (!Test_IsClose(_GetVec4(out), Vec4f(0.25f))) return false;

    in["in"] = Value(Vec2f(0.2f, 0.4f));
    out = _Eval("ND_convert_vector2_color4", in);
    if (!Test_IsClose(_GetVec4(out), Vec4f(0.2f, 0.4f, 0.0f, 1.0f))) {
        return false;
    }

    in["in"] = Value(Vec3f(0.2f, 0.4f, 0.6f));
    out = _Eval("ND_convert_color3_vector2", in);
    if (!Test_IsClose(_GetVec2(out), Vec2f(0.2f, 0.4f))) return false;
    out = _Eval("ND_convert_vector3_color4", in);
    if (!Test_IsClose(_GetVec4(out), Vec4f(0.2f, 0.4f, 0.6f, 1.0f))) {
        return false;
    }

    in["in"] = Value(true);
    out = _Eval("ND_convert_boolean_vector3", in);
    if (!Test_IsClose(_GetVec3(out), Vec3f(1.0f))) return false;
    out = _Eval("ND_convert_boolean_integer", in);
    if (_GetInt(out) != 1) return false;

    in["in"] = Value(-2);
    out = _Eval("ND_convert_integer_color3", in);
    if (!Test_IsClose(_GetVec3(out), Vec3f(-2.0f))) return false;
    out = _Eval("ND_convert_integer_boolean", in);
    if (!_GetBool(out)) return false;

    in["in"] = Value(0);
    out = _Eval("ND_convert_integer_boolean", in);
    return !_GetBool(out);
}

static bool TestChannelConversionRegistrations() {
    NodeRegistry::RegisterBuiltinNodes();
    static const std::array<const char*, 39> nodeTypeIds = {{
        "ND_convert_float_color3", "ND_convert_float_color4",
        "ND_convert_float_vector2", "ND_convert_float_vector3",
        "ND_convert_float_vector4", "ND_convert_color3_color4",
        "ND_convert_color3_vector2", "ND_convert_color3_vector3",
        "ND_convert_color3_vector4", "ND_convert_color4_color3",
        "ND_convert_color4_vector2", "ND_convert_color4_vector3",
        "ND_convert_color4_vector4", "ND_convert_vector2_color3",
        "ND_convert_vector2_color4", "ND_convert_vector2_vector3",
        "ND_convert_vector2_vector4", "ND_convert_vector3_color3",
        "ND_convert_vector3_color4", "ND_convert_vector3_vector2",
        "ND_convert_vector3_vector4", "ND_convert_vector4_color3",
        "ND_convert_vector4_color4", "ND_convert_vector4_vector2",
        "ND_convert_vector4_vector3", "ND_convert_boolean_float",
        "ND_convert_boolean_color3", "ND_convert_boolean_color4",
        "ND_convert_boolean_vector2", "ND_convert_boolean_vector3",
        "ND_convert_boolean_vector4", "ND_convert_boolean_integer",
        "ND_convert_integer_float", "ND_convert_integer_color3",
        "ND_convert_integer_color4", "ND_convert_integer_vector2",
        "ND_convert_integer_vector3", "ND_convert_integer_vector4",
        "ND_convert_integer_boolean"
    }};
    for (const char* nodeTypeId : nodeTypeIds) {
        if (!NodeRegistry::GetInstance().Find(std::string(nodeTypeId))) {
            return false;
        }
    }
    return true;
}

static bool TestDotSurfaceShaderPassThrough() {
    SurfaceClosure input;
    input.emissiveColor = Vec3f(0.2f, 0.4f, 0.6f);
    input.opacity = 0.75f;
    input.presence = 0.5f;
    input.thinWalled = true;
    input.isVolumeBoundary = true;

    ParamMap in;
    in["in"] = Value(input);
    const NodeOutputMap out = _Eval("ND_dot_surfaceshader", in);
    const Value* value = out.Find("out");
    if (!value || !ValueHolds<SurfaceClosure>(*value)) {
        return false;
    }

    const SurfaceClosure& result = ValueGet<SurfaceClosure>(*value);
    return Test_IsClose(result.emissiveColor, input.emissiveColor) &&
           Test_IsClose(result.opacity, input.opacity) &&
           Test_IsClose(result.presence, input.presence) &&
           result.thinWalled == input.thinWalled &&
           result.isVolumeBoundary == input.isVolumeBoundary;
}

static bool TestMagnitudeVectorVariants() {
    ParamMap in;
    in["in"] = Value(Vec2f(3.0f, 4.0f));
    auto out = _Eval("ND_magnitude_vector2", in);
    if (!Test_IsClose(_GetFloat(out), 5.0f)) return false;

    in["in"] = Value(Vec4f(1.0f, 2.0f, 2.0f, 4.0f));
    out = _Eval("ND_magnitude_vector4", in);
    return Test_IsClose(_GetFloat(out), 5.0f);
}

// ---------------------------------------------------------------------------
// Conditional node tests
// ---------------------------------------------------------------------------

static bool TestIfgreater() {
    NodeRegistry::RegisterBuiltinNodes();
    auto fn = NodeRegistry::GetInstance().Find(std::string("ND_ifgreater_float"));
    if (!fn) {
        printf("    ND_ifgreater_float not registered\n");
        return false;
    }
    // Just verify the node exists and is callable.
    ParamMap in;
    in["value1"] = Value(2.0f);
    in["value2"] = Value(1.0f);
    in["in1"] = Value(10.0f);
    in["in2"] = Value(20.0f);
    NodeOutputMap out;
    ShadingContext ctx;
    fn(in, ctx, &out);
    return Test_IsClose(_GetFloat(out), 10.0f);
}

static bool TestIfgreaterInteger() {
    ParamMap in;
    in["value1"] = Value(2.0f);
    in["value2"] = Value(1.0f);
    in["in1"] = Value(10);
    in["in2"] = Value(20);
    auto out = _Eval("ND_ifgreater_integer", in);
    return _GetInt(out) == 10;
}

static bool TestIfgreaterVector2() {
    ParamMap in;
    in["value1"] = Value(0.0f);
    in["value2"] = Value(1.0f);
    in["in1"] = Value(Vec2f(1.0f, 2.0f));
    in["in2"] = Value(Vec2f(3.0f, 4.0f));
    auto out = _Eval("ND_ifgreater_vector2", in);
    return Test_IsClose(_GetVec2(out), Vec2f(3.0f, 4.0f));
}

static bool TestIfgreaterBoolOutput() {
    ParamMap in;
    in["value1"] = Value(2.0f);
    in["value2"] = Value(1.0f);
    auto out = _Eval("ND_ifgreater_boolean", in);
    if (!_GetBool(out)) return false;

    in["value1"] = Value(0.0f);
    out = _Eval("ND_ifgreater_boolean", in);
    return !_GetBool(out);
}

static bool TestIfgreaterIntComparison() {
    ParamMap in;
    in["value1"] = Value(5);
    in["value2"] = Value(3);
    in["in1"] = Value(Vec3f(1.0f));
    in["in2"] = Value(Vec3f(0.0f));
    auto out = _Eval("ND_ifgreater_color3I", in);
    return Test_IsClose(_GetVec3(out), Vec3f(1.0f));
}

static bool TestIfEqualBoolComparison() {
    ParamMap in;
    in["value1"] = Value(true);
    in["value2"] = Value(true);
    in["in1"] = Value(10.0f);
    in["in2"] = Value(20.0f);
    auto out = _Eval("ND_ifequal_floatB", in);
    if (!Test_IsClose(_GetFloat(out), 10.0f)) return false;

    in["value2"] = Value(false);
    out = _Eval("ND_ifequal_floatB", in);
    return Test_IsClose(_GetFloat(out), 20.0f);
}

static bool TestSwitchFloat() {
    ParamMap in;
    in["in1"] = Value(10.0f);
    in["in2"] = Value(20.0f);
    in["in3"] = Value(30.0f);
    in["which"] = Value(1.0f);
    auto out = _Eval("ND_switch_float", in);
    return Test_IsClose(_GetFloat(out), 20.0f);
}

static bool TestSwitchIntegerWhich() {
    ParamMap in;
    in["in1"] = Value(Vec3f(1.0f));
    in["in2"] = Value(Vec3f(2.0f));
    in["in3"] = Value(Vec3f(3.0f));
    in["which"] = Value(2);
    auto out = _Eval("ND_switch_color3I", in);
    return Test_IsClose(_GetVec3(out), Vec3f(3.0f));
}

static bool TestLogicalOps() {
    {
        ParamMap in;
        in["in1"] = Value(true);
        in["in2"] = Value(false);
        auto out = _Eval("ND_logical_and", in);
        if (_GetBool(out)) return false;
    }
    {
        ParamMap in;
        in["in1"] = Value(true);
        in["in2"] = Value(false);
        auto out = _Eval("ND_logical_or", in);
        if (!_GetBool(out)) return false;
    }
    {
        ParamMap in;
        in["in1"] = Value(true);
        in["in2"] = Value(true);
        auto out = _Eval("ND_logical_xor", in);
        if (_GetBool(out)) return false;
    }
    {
        ParamMap in;
        in["in"] = Value(false);
        auto out = _Eval("ND_logical_not", in);
        if (!_GetBool(out)) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Compositing node tests
// ---------------------------------------------------------------------------

static bool TestCompositingPlus() {
    ParamMap in;
    in["fg"] = Value(0.3f);
    in["bg"] = Value(0.4f);
    in["mix"] = Value(1.0f);
    auto out = _Eval("ND_plus_float", in);
    return Test_IsClose(_GetFloat(out), 0.7f);
}

static bool TestCompositingMinus() {
    ParamMap in;
    in["fg"] = Value(0.3f);
    in["bg"] = Value(0.5f);
    in["mix"] = Value(1.0f);
    auto out = _Eval("ND_minus_float", in);
    return Test_IsClose(_GetFloat(out), 0.2f);
}

static bool TestCompositingBurn() {
    ParamMap in;
    in["fg"] = Value(0.5f);
    in["bg"] = Value(0.8f);
    in["mix"] = Value(1.0f);
    auto out = _Eval("ND_burn_float", in);
    if (!Test_IsClose(_GetFloat(out), 0.6f)) return false;

    in["fg"] = Value(0.0f);
    out = _Eval("ND_burn_float", in);
    return Test_IsClose(_GetFloat(out), 0.0f);
}

static bool TestCompositingDodge() {
    ParamMap in;
    in["fg"] = Value(0.5f);
    in["bg"] = Value(0.4f);
    in["mix"] = Value(1.0f);
    auto out = _Eval("ND_dodge_float", in);
    if (!Test_IsClose(_GetFloat(out), 0.8f)) return false;

    in["fg"] = Value(1.0f);
    out = _Eval("ND_dodge_float", in);
    return Test_IsClose(_GetFloat(out), 0.0f);
}

static bool TestCompositingScreen() {
    ParamMap in;
    in["fg"] = Value(0.5f);
    in["bg"] = Value(0.3f);
    in["mix"] = Value(1.0f);
    auto out = _Eval("ND_screen_float", in);
    return Test_IsClose(_GetFloat(out), 0.65f);
}

static bool TestCompositingOverlay() {
    ParamMap in;
    in["fg"] = Value(0.6f);
    in["bg"] = Value(0.3f);
    in["mix"] = Value(1.0f);
    auto out = _Eval("ND_overlay_float", in);
    if (!Test_IsClose(_GetFloat(out), 0.36f)) return false;

    in["bg"] = Value(0.7f);
    out = _Eval("ND_overlay_float", in);
    return Test_IsClose(_GetFloat(out), 0.76f);
}

static bool TestCompositingMixParam() {
    ParamMap in;
    in["fg"] = Value(0.8f);
    in["bg"] = Value(0.2f);
    in["mix"] = Value(0.0f);
    auto out = _Eval("ND_plus_float", in);
    return Test_IsClose(_GetFloat(out), 0.2f);
}

static bool TestPorterDuffOver() {
    ParamMap in;
    in["fg"] = Value(Vec4f(1.0f, 0.0f, 0.0f, 0.5f));
    in["bg"] = Value(Vec4f(0.0f, 1.0f, 0.0f, 0.8f));
    in["mix"] = Value(1.0f);
    auto out = _Eval("ND_over_color4", in);
    Vec4f result = _GetVec4(out);
    return Test_IsClose(result, Vec4f(1.0f, 0.5f, 0.0f, 0.9f));
}

static bool TestPorterDuffIn() {
    ParamMap in;
    in["fg"] = Value(Vec4f(1.0f, 0.5f, 0.0f, 0.8f));
    in["bg"] = Value(Vec4f(0.0f, 1.0f, 0.0f, 0.6f));
    in["mix"] = Value(1.0f);
    auto out = _Eval("ND_in_color4", in);
    Vec4f result = _GetVec4(out);
    return Test_IsClose(result, Vec4f(0.6f, 0.3f, 0.0f, 0.48f));
}

static bool TestPorterDuffDisjointover() {
    ParamMap in;
    in["fg"] = Value(Vec4f(0.5f, 0.0f, 0.0f, 0.3f));
    in["bg"] = Value(Vec4f(0.0f, 0.5f, 0.0f, 0.4f));
    in["mix"] = Value(1.0f);
    auto out = _Eval("ND_disjointover_color4", in);
    Vec4f result = _GetVec4(out);
    if (!Test_IsClose(result, Vec4f(0.5f, 0.5f, 0.0f, 0.7f)))
        return false;

    in["fg"] = Value(Vec4f(0.8f, 0.0f, 0.0f, 0.7f));
    in["bg"] = Value(Vec4f(0.0f, 0.6f, 0.0f, 0.5f));
    out = _Eval("ND_disjointover_color4", in);
    result = _GetVec4(out);
    return Test_IsClose(result, Vec4f(0.8f, 0.36f, 0.0f, 1.0f));
}

static bool TestPorterDuffMatte() {
    ParamMap in;
    in["fg"] = Value(Vec4f(1.0f, 0.0f, 0.0f, 0.6f));
    in["bg"] = Value(Vec4f(0.0f, 1.0f, 0.0f, 0.8f));
    in["mix"] = Value(1.0f);
    auto out = _Eval("ND_matte_color4", in);
    Vec4f result = _GetVec4(out);
    return Test_IsClose(result, Vec4f(0.6f, 0.4f, 0.0f, 0.92f));
}

static bool TestPorterDuffOut() {
    ParamMap in;
    in["fg"] = Value(Vec4f(1.0f, 0.5f, 0.0f, 0.8f));
    in["bg"] = Value(Vec4f(0.0f, 1.0f, 0.0f, 0.6f));
    in["mix"] = Value(1.0f);
    auto out = _Eval("ND_out_color4", in);
    Vec4f result = _GetVec4(out);
    return Test_IsClose(result, Vec4f(0.4f, 0.2f, 0.0f, 0.32f));
}

static bool TestPremultUnpremult() {
    ParamMap in;
    in["in"] = Value(Vec4f(1.0f, 0.5f, 0.0f, 0.5f));
    auto out = _Eval("ND_premult_color4", in);
    Vec4f result = _GetVec4(out);
    if (!Test_IsClose(result, Vec4f(0.5f, 0.25f, 0.0f, 0.5f)))
        return false;

    in["in"] = Value(Vec4f(0.5f, 0.25f, 0.0f, 0.5f));
    out = _Eval("ND_unpremult_color4", in);
    result = _GetVec4(out);
    return Test_IsClose(result, Vec4f(1.0f, 0.5f, 0.0f, 0.5f));
}

static bool TestInsideOutside() {
    ParamMap in;
    in["in"] = Value(Vec3f(0.5f, 0.8f, 1.0f));
    in["mask"] = Value(0.5f);
    auto out = _Eval("ND_inside_color3", in);
    if (!Test_IsClose(_GetVec3(out), Vec3f(0.25f, 0.4f, 0.5f)))
        return false;

    out = _Eval("ND_outside_color3", in);
    return Test_IsClose(_GetVec3(out), Vec3f(0.25f, 0.4f, 0.5f));
}

static bool TestMixVecVariant() {
    ParamMap in;
    in["fg"] = Value(Vec3f(1.0f, 0.0f, 0.0f));
    in["bg"] = Value(Vec3f(0.0f, 1.0f, 0.0f));
    in["mix"] = Value(Vec3f(1.0f, 0.0f, 0.5f));
    auto out = _Eval("ND_mix_color3_color3", in);
    return Test_IsClose(_GetVec3(out), Vec3f(1.0f, 1.0f, 0.0f));
}

// ---------------------------------------------------------------------------
// Procedural node tests
// ---------------------------------------------------------------------------

static bool TestProceduralWorleyNoise2d() {
    ParamMap in;
    in["texcoord"] = Value(Vec2f(0.25f, 0.75f));
    in["jitter"] = Value(0.0f);

    auto outFloat = _Eval("ND_worleynoise2d_float", in);
    if (!Test_IsClose(_GetFloat(outFloat), 0.35355339f, 1e-5f)) {
        return false;
    }

    auto outVec2 = _Eval("ND_worleynoise2d_vector2", in);
    return Test_IsClose(_GetVec2(outVec2),
                        Vec2f(0.35355339f, 0.79056942f),
                        1e-5f);
}

static bool TestProceduralWorleyNoise3d() {
    ParamMap in;
    in["position"] = Value(Vec3f(0.25f, 0.75f, 0.5f));
    in["jitter"] = Value(0.0f);

    auto outFloat = _Eval("ND_worleynoise3d_float", in);
    if (!Test_IsClose(_GetFloat(outFloat), 0.35355339f, 1e-5f)) {
        return false;
    }

    auto outVec2 = _Eval("ND_worleynoise3d_vector2", in);
    if (!Test_IsClose(_GetVec2(outVec2),
                      Vec2f(0.35355339f, 0.79056942f),
                      1e-5f)) {
        return false;
    }

    auto outVec3 = _Eval("ND_worleynoise3d_vector3", in);
    return Test_IsClose(_GetVec3(outVec3),
                        Vec3f(0.35355339f, 0.79056942f, 0.79056942f),
                        1e-5f);
}

static bool TestProceduralFractal2dSingleOctave() {
    ParamMap in;
    in["texcoord"] = Value(Vec2f(0.37f, 0.81f));
    in["amplitude"] = Value(1.0f);
    in["octaves"] = Value(1);
    in["lacunarity"] = Value(2.0f);
    in["diminish"] = Value(0.5f);

    auto fractal = _Eval("ND_fractal2d_float", in);

    ParamMap noiseIn;
    noiseIn["texcoord"] = Value(Vec2f(0.37f, 0.81f));
    noiseIn["amplitude"] = Value(1.0f);
    noiseIn["pivot"] = Value(0.0f);
    auto noise = _Eval("ND_noise2d_float", noiseIn);
    return Test_IsClose(_GetFloat(fractal), _GetFloat(noise), 1e-5f);
}

static bool TestProceduralFractal3dColor() {
    const Vec3f position(0.37f, 0.81f, 1.23f);
    const Vec3f amplitude(0.9f, 0.75f, 0.6f);
    constexpr float lacunarity = 2.25f;
    constexpr float diminish = 0.55f;

    ParamMap in;
    in["position"] = Value(position);
    in["amplitude"] = Value(amplitude);
    in["octaves"] = Value(2);
    in["lacunarity"] = Value(lacunarity);
    in["diminish"] = Value(diminish);
    const auto fractal = _Eval("ND_fractal3d_color3", in);

    ParamMap noiseIn;
    noiseIn["amplitude"] = Value(amplitude);
    noiseIn["pivot"] = Value(0.0f);
    noiseIn["position"] = Value(position);
    const Vec3f first = _GetVec3(_Eval("ND_noise3d_color3", noiseIn));
    noiseIn["amplitude"] = Value(amplitude * diminish);
    noiseIn["position"] = Value(position * lacunarity);
    const Vec3f second = _GetVec3(_Eval("ND_noise3d_color3", noiseIn));

    return Test_IsClose(_GetVec3(fractal), first + second, 1e-5f);
}

static bool TestArtisticIor() {
    ParamMap in;
    in["reflectivity"] = Value(Vec3f(0.25f));
    in["edge_color"] = Value(Vec3f(0.0f, 1.0f, 0.5f));
    const auto out = _Eval("ND_artistic_ior", in);

    return Test_IsClose(
               _GetVec3(out, "ior"),
               Vec3f(3.0f, 0.6f, 1.8f),
               1e-5f) &&
           Test_IsClose(
               _GetVec3(out, "extinction"),
               Vec3f(0.0f, 0.8f, std::sqrt(1.76f)),
               1e-5f);
}

static bool TestBlackbody() {
    ParamMap in;
    in["temperature"] = Value(1900.0f);
    const Vec3f warm = _GetVec3(_Eval("ND_blackbody", in));
    if (!Test_IsClose(
            warm, Vec3f(2.64310986f, 0.61265858f, 0.0f), 1e-5f)) {
        return false;
    }

    in["temperature"] = Value(4500.0f);
    const Vec3f neutral = _GetVec3(_Eval("ND_blackbody", in));
    if (!Test_IsClose(
            neutral, Vec3f(1.30027675f, 0.94580768f, 0.65325726f), 1e-5f)) {
        return false;
    }

    in["temperature"] = Value(9000.0f);
    return Test_IsClose(
        _GetVec3(_Eval("ND_blackbody", in)),
        Vec3f(0.90484315f, 0.99376682f, 1.34251550f),
        1e-5f);
}

static bool TestProceduralUnifiedNoise2dCellRemap() {
    ParamMap in;
    in["texcoord"] = Value(Vec2f(1.2f, 2.7f));
    in["freq"] = Value(Vec2f(1.0f, 1.0f));
    in["offset"] = Value(Vec2f(0.0f, 0.0f));
    in["jitter"] = Value(1.0f);
    in["type"] = Value(1);
    in["outmin"] = Value(2.0f);
    in["outmax"] = Value(4.0f);
    in["clampoutput"] = Value(true);

    auto unified = _Eval("ND_unifiednoise2d_float", in);

    ParamMap cellIn;
    cellIn["texcoord"] = Value(Vec2f(1.2f, 2.7f));
    auto cell = _Eval("ND_cellnoise2d_float", cellIn);
    const float expected = 2.0f + _GetFloat(cell) * 2.0f;
    return Test_IsClose(_GetFloat(unified), expected, 1e-5f);
}

static bool TestProceduralUnifiedNoise3dCellRemap() {
    ParamMap in;
    in["position"] = Value(Vec3f(1.2f, 2.7f, 0.4f));
    in["freq"] = Value(Vec3f(1.0f, 1.0f, 1.0f));
    in["offset"] = Value(Vec3f(0.0f, 0.0f, 0.0f));
    in["jitter"] = Value(1.0f);
    in["type"] = Value(1);
    in["outmin"] = Value(2.0f);
    in["outmax"] = Value(4.0f);
    in["clampoutput"] = Value(true);

    auto unified = _Eval("ND_unifiednoise3d_float", in);

    ParamMap cellIn;
    cellIn["position"] = Value(Vec3f(1.2f, 2.7f, 0.4f));
    auto cell = _Eval("ND_cellnoise3d_float", cellIn);
    const float expected = 2.0f + _GetFloat(cell) * 2.0f;
    return Test_IsClose(_GetFloat(unified), expected, 1e-5f);
}

static bool TestProceduralRampLrAndSplitLr() {
    {
        ParamMap in;
        in["valuel"] = Value(1.0f);
        in["valuer"] = Value(5.0f);
        in["texcoord"] = Value(Vec2f(0.25f, 0.0f));
        auto out = _Eval("ND_ramplr_float", in);
        if (!Test_IsClose(_GetFloat(out), 2.0f, 1e-5f)) {
            return false;
        }
    }

    {
        ParamMap in;
        in["valuel"] = Value(0.0f);
        in["valuer"] = Value(1.0f);
        in["center"] = Value(0.5f);
        in["texcoord"] = Value(Vec2f(0.5f, 0.0f));
        ShadingContext ctx;
        ctx.texcoord = Vec2f(0.5f, 0.0f);
        ctx.dudx = 1.0f;
        auto out = _EvalWithCtx("ND_splitlr_float", in, ctx);
        if (!Test_IsClose(_GetFloat(out), 0.5f, 1e-5f)) {
            return false;
        }
    }

    return true;
}

static bool TestProceduralRampTbAndSplitTb() {
    {
        ParamMap in;
        in["valuet"] = Value(5.0f);
        in["valueb"] = Value(1.0f);
        in["texcoord"] = Value(Vec2f(0.0f, 0.25f));
        if (!Test_IsClose(
                _GetFloat(_Eval("ND_ramptb_float", in)), 2.0f, 1e-5f)) {
            return false;
        }
    }

    ParamMap in;
    in["valuet"] = Value(5.0f);
    in["valueb"] = Value(1.0f);
    in["center"] = Value(0.5f);
    in["texcoord"] = Value(Vec2f(0.0f, 0.25f));
    if (!Test_IsClose(
            _GetFloat(_Eval("ND_splittb_float", in)), 1.0f, 1e-5f)) {
        return false;
    }

    in["texcoord"] = Value(Vec2f(0.0f, 0.75f));
    return Test_IsClose(
        _GetFloat(_Eval("ND_splittb_float", in)), 5.0f, 1e-5f);
}

static bool TestProceduralRamp4AndRamp() {
    {
        ParamMap in;
        in["valuetl"] = Value(0.0f);
        in["valuetr"] = Value(1.0f);
        in["valuebl"] = Value(2.0f);
        in["valuebr"] = Value(3.0f);
        in["texcoord"] = Value(Vec2f(0.25f, 0.25f));
        auto out = _Eval("ND_ramp4_float", in);
        if (!Test_IsClose(_GetFloat(out), 1.75f, 1e-5f)) {
            return false;
        }
    }

    {
        ParamMap in;
        in["texcoord"] = Value(Vec2f(0.25f, 0.0f));
        in["type"] = Value(0);
        in["interpolation"] = Value(0);
        in["num_intervals"] = Value(2);
        in["interval1"] = Value(0.0f);
        in["interval2"] = Value(1.0f);
        in["color1"] = Value(Vec4f(0.0f, 0.0f, 0.0f, 1.0f));
        in["color2"] = Value(Vec4f(1.0f, 1.0f, 1.0f, 1.0f));
        auto out = _Eval("ND_ramp", in);
        return Test_IsClose(_GetVec4(out), Vec4f(0.25f, 0.25f, 0.25f, 1.0f));
    }
}

static bool TestProceduralPatterns() {
    {
        ParamMap in;
        in["texcoord"] = Value(Vec2f(0.2f, 0.1f));
        auto out = _Eval("ND_checkerboard_color3", in);
        if (!Test_IsClose(_GetVec3(out), Vec3f(1.0f), 1e-5f)) {
            return false;
        }
    }
    {
        ParamMap in;
        in["texcoord"] = Value(Vec2f(0.5f, 0.05f));
        in["center"] = Value(Vec2f(0.0f));
        in["radius"] = Value(0.1f);
        in["point1"] = Value(Vec2f(0.0f, 0.0f));
        in["point2"] = Value(Vec2f(1.0f, 0.0f));
        auto out = _Eval("ND_line_float", in);
        if (!Test_IsClose(_GetFloat(out), 1.0f, 1e-5f)) {
            return false;
        }
    }
    {
        ParamMap in;
        in["texcoord"] = Value(Vec2f(0.2f, 0.2f));
        in["center"] = Value(Vec2f(0.0f));
        in["radius"] = Value(0.5f);
        auto out = _Eval("ND_circle_float", in);
        if (!Test_IsClose(_GetFloat(out), 1.0f, 1e-5f)) {
            return false;
        }
    }
    {
        ParamMap in;
        in["texcoord"] = Value(Vec2f(0.0f, 0.0f));
        in["center"] = Value(Vec2f(0.0f));
        in["radius"] = Value(0.5f);
        auto out = _Eval("ND_hexagon_float", in);
        if (!Test_IsClose(_GetFloat(out), 1.0f, 1e-5f)) {
            return false;
        }
    }
    return true;
}

static bool TestProceduralGridAndCrosshatch() {
    {
        ParamMap in;
        in["texcoord"] = Value(Vec2f(0.0f, 0.0f));
        auto out = _Eval("ND_grid_color3", in);
        if (!Test_IsClose(_GetVec3(out), Vec3f(1.0f), 1e-5f)) {
            return false;
        }
    }
    {
        ParamMap in;
        in["texcoord"] = Value(Vec2f(0.5f, 0.5f));
        auto out = _Eval("ND_crosshatch_color3", in);
        if (!Test_IsClose(_GetVec3(out), Vec3f(1.0f), 1e-5f)) {
            return false;
        }
    }
    return true;
}

static bool TestProceduralRandomNodes() {
    ParamMap rf;
    rf["in"] = Value(0.25f);
    rf["min"] = Value(2.0f);
    rf["max"] = Value(4.0f);
    rf["seed"] = Value(7);
    auto out1 = _Eval("ND_randomfloat_float", rf);
    auto out2 = _Eval("ND_randomfloat_float", rf);
    const float randomFloat = _GetFloat(out1);
    if (randomFloat < 2.0f || randomFloat > 4.0f) {
        return false;
    }
    if (!Test_IsClose(randomFloat, _GetFloat(out2), 1e-6f)) {
        return false;
    }

    ParamMap rc;
    rc["in"] = Value(0.25f);
    rc["seed"] = Value(11);
    auto color1 = _Eval("ND_randomcolor_float", rc);
    auto color2 = _Eval("ND_randomcolor_float", rc);
    return Test_IsClose(_GetVec3(color1), _GetVec3(color2), 1e-6f);
}

static bool TestProceduralFlake2dCoverageZero() {
    ParamMap in;
    in["coverage"] = Value(0.0f);
    in["texcoord"] = Value(Vec2f(0.3f, 0.7f));
    in["normal"] = Value(Vec3f(0.0f, 0.0f, 1.0f));
    in["tangent"] = Value(Vec3f(1.0f, 0.0f, 0.0f));
    in["bitangent"] = Value(Vec3f(0.0f, 1.0f, 0.0f));

    auto out = _Eval("ND_flake2d", in);
    return _GetInt(out, "id") == 0 &&
           Test_IsClose(_GetFloat(out, "rand"), 0.0f, 1e-6f) &&
           Test_IsClose(_GetFloat(out, "presence"), 0.0f, 1e-6f) &&
           Test_IsClose(_GetVec3(out, "flakenormal"), Vec3f(0.0f, 0.0f, 1.0f));
}

static bool TestProceduralFlake3dCoverageZero() {
    ParamMap in;
    in["coverage"] = Value(0.0f);
    in["position"] = Value(Vec3f(0.3f, 0.7f, 0.2f));
    in["normal"] = Value(Vec3f(0.0f, 0.0f, 1.0f));
    in["tangent"] = Value(Vec3f(1.0f, 0.0f, 0.0f));
    in["bitangent"] = Value(Vec3f(0.0f, 1.0f, 0.0f));

    auto out = _Eval("ND_flake3d", in);
    return _GetInt(out, "id") == 0 &&
           Test_IsClose(_GetFloat(out, "rand"), 0.0f, 1e-6f) &&
           Test_IsClose(_GetFloat(out, "presence"), 0.0f, 1e-6f) &&
           Test_IsClose(_GetVec3(out, "flakenormal"), Vec3f(0.0f, 0.0f, 1.0f));
}

// ---------------------------------------------------------------------------
// Test helpers for geompropvalue
// ---------------------------------------------------------------------------

struct _TestGeomPropData {
    std::vector<Value> props;
};

static Value
_TestGeomPropLookup(const void* userData, int geomPropHandle)
{
    auto* data = static_cast<const _TestGeomPropData*>(userData);
    if (!data || geomPropHandle < 0 ||
        static_cast<size_t>(geomPropHandle) >= data->props.size()) {
        return Value();
    }
    return data->props[geomPropHandle];
}

// ---------------------------------------------------------------------------
// Geompropvalue node tests
// ---------------------------------------------------------------------------

static bool TestGeomPropValueFloat() {
    _TestGeomPropData gpData;
    gpData.props.push_back(Value(3.14f));

    ShadingContext ctx;
    ctx.geomPropLookup = &_TestGeomPropLookup;
    ctx.geomPropUserData = &gpData;

    ParamMap in;
    in["geomprop"] = Value(0);
    in["default"] = Value(0.0f);

    NodeRegistry::RegisterBuiltinNodes();
    auto fn = NodeRegistry::GetInstance().Find(
        std::string("ND_geompropvalue_float"));
    if (!fn) return false;

    NodeOutputMap out;
    fn(in, ctx, &out);
    return Test_IsClose(_GetFloat(out), 3.14f);
}

static bool TestGeomPropValueColor3() {
    _TestGeomPropData gpData;
    gpData.props.push_back(Value(Vec3f(1.0f, 0.0f, 0.5f)));

    ShadingContext ctx;
    ctx.geomPropLookup = &_TestGeomPropLookup;
    ctx.geomPropUserData = &gpData;

    ParamMap in;
    in["geomprop"] = Value(0);
    in["default"] = Value(Vec3f(0.0f));

    NodeRegistry::RegisterBuiltinNodes();
    auto fn = NodeRegistry::GetInstance().Find(
        std::string("ND_geompropvalue_color3"));
    if (!fn) return false;

    NodeOutputMap out;
    fn(in, ctx, &out);
    return Test_IsClose(_GetVec3(out), Vec3f(1.0f, 0.0f, 0.5f));
}

static bool TestGeomPropValueDefault() {
    // No callback set — should return default value.
    ShadingContext ctx;

    ParamMap in;
    in["geomprop"] = Value(0);
    in["default"] = Value(42.0f);

    NodeRegistry::RegisterBuiltinNodes();
    auto fn = NodeRegistry::GetInstance().Find(
        std::string("ND_geompropvalue_float"));
    if (!fn) return false;

    NodeOutputMap out;
    fn(in, ctx, &out);
    return Test_IsClose(_GetFloat(out), 42.0f);
}

static bool TestGeomPropValueTypeMismatch() {
    // Callback returns float but node expects Vec3f — should return default.
    _TestGeomPropData gpData;
    gpData.props.push_back(Value(1.0f));

    ShadingContext ctx;
    ctx.geomPropLookup = &_TestGeomPropLookup;
    ctx.geomPropUserData = &gpData;

    ParamMap in;
    in["geomprop"] = Value(0);
    in["default"] = Value(Vec3f(0.5f));

    NodeRegistry::RegisterBuiltinNodes();
    auto fn = NodeRegistry::GetInstance().Find(
        std::string("ND_geompropvalue_color3"));
    if (!fn) return false;

    NodeOutputMap out;
    fn(in, ctx, &out);
    return Test_IsClose(_GetVec3(out), Vec3f(0.5f));
}

static bool TestGeomPropValueEmptyName() {
    // Empty property name should return default.
    _TestGeomPropData gpData;
    gpData.props.push_back(Value(99.0f));

    ShadingContext ctx;
    ctx.geomPropLookup = &_TestGeomPropLookup;
    ctx.geomPropUserData = &gpData;

    ParamMap in;
    in["geomprop"] = Value(-1);
    in["default"] = Value(7.0f);

    NodeRegistry::RegisterBuiltinNodes();
    auto fn = NodeRegistry::GetInstance().Find(
        std::string("ND_geompropvalue_float"));
    if (!fn) return false;

    NodeOutputMap out;
    fn(in, ctx, &out);
    return Test_IsClose(_GetFloat(out), 7.0f);
}

static bool TestGeomPropValueNotFound() {
    // Callback set but property name not in data — should return default.
    _TestGeomPropData gpData;
    gpData.props.push_back(Value(1.0f));

    ShadingContext ctx;
    ctx.geomPropLookup = &_TestGeomPropLookup;
    ctx.geomPropUserData = &gpData;

    ParamMap in;
    in["geomprop"] = Value(1);
    in["default"] = Value(99.0f);

    NodeRegistry::RegisterBuiltinNodes();
    auto fn = NodeRegistry::GetInstance().Find(
        std::string("ND_geompropvalue_float"));
    if (!fn) return false;

    NodeOutputMap out;
    fn(in, ctx, &out);
    return Test_IsClose(_GetFloat(out), 99.0f);
}

static bool TestGeomPropValueUniformString() {
    std::vector<Value> uniformValues{
        Value(std::string("/path/to/texture.png"))};

    ShadingContext ctx;
    ctx.uniformProps = &uniformValues;

    ParamMap in;
    in["geomprop"] = Value(0);
    in["default"] = Value(std::string(""));

    NodeRegistry::RegisterBuiltinNodes();
    auto fn = NodeRegistry::GetInstance().Find(
        std::string("ND_geompropvalueuniform_string"));
    if (!fn) return false;

    NodeOutputMap out;
    fn(in, ctx, &out);
    const Value* val = out.Find("out");
    if (!val || !ValueHolds<std::string>(*val)) return false;
    return ValueGet<std::string>(*val) == "/path/to/texture.png";
}

static bool TestGeomPropValueUniformDefault() {
    // No uniformProps set — should return default.
    ShadingContext ctx;

    ParamMap in;
    in["geomprop"] = Value(0);
    in["default"] = Value(std::string("fallback.png"));

    NodeRegistry::RegisterBuiltinNodes();
    auto fn = NodeRegistry::GetInstance().Find(
        std::string("ND_geompropvalueuniform_filename"));
    if (!fn) return false;

    NodeOutputMap out;
    fn(in, ctx, &out);
    const Value* val = out.Find("out");
    if (!val || !ValueHolds<std::string>(*val)) return false;
    return ValueGet<std::string>(*val) == "fallback.png";
}

static bool TestGeomPropValueUniformInvalidHandles() {
    const std::vector<Value> uniformValues{
        Value(std::string("authored.png"))};
    ShadingContext ctx;
    ctx.uniformProps = &uniformValues;

    NodeRegistry::RegisterBuiltinNodes();
    NodeEvalFn fn = NodeRegistry::GetInstance().Find(
        std::string("ND_geompropvalueuniform_filename"));
    if (!fn) return false;

    for (int geomPropHandle : {-1, 1}) {
        ParamMap in;
        in["geomprop"] = Value(geomPropHandle);
        in["default"] = Value(std::string("fallback.png"));
        NodeOutputMap out;
        fn(in, ctx, &out);
        const Value* const value = out.Find("out");
        if (!value || !ValueHolds<std::string>(*value) ||
            ValueGet<std::string>(*value) != "fallback.png") {
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Geometric node tests
// ---------------------------------------------------------------------------

static bool TestGeometricPosition() {
    NodeRegistry::RegisterBuiltinNodes();
    auto fn = NodeRegistry::GetInstance().Find(std::string("ND_position_vector3"));
    if (!fn) return false;

    ParamMap in;
    ShadingContext ctx;
    ctx.position = Vec3f(1.0f, 2.0f, 3.0f);
    NodeOutputMap out;
    fn(in, ctx, &out);
    return Test_IsClose(_GetVec3(out), Vec3f(1.0f, 2.0f, 3.0f));
}

static bool TestGeometricPositionWorldSpace() {
    NodeRegistry::RegisterBuiltinNodes();
    auto fn = NodeRegistry::GetInstance().Find(std::string("ND_position_vector3"));
    if (!fn) return false;

    ParamMap in;
    in["space"] = Value(std::string("world"));
    ShadingContext ctx;
    ctx.position = Vec3f(1.0f, 2.0f, 3.0f);
    _SetObjectWorldTransform(&ctx, _MakeTranslationMatrix(Vec3f(5.0f, 0.0f, -2.0f)));
    NodeOutputMap out;
    fn(in, ctx, &out);
    return Test_IsClose(_GetVec3(out), Vec3f(6.0f, 2.0f, 1.0f));
}

static bool TestGeometricNormal() {
    NodeRegistry::RegisterBuiltinNodes();
    auto fn = NodeRegistry::GetInstance().Find(std::string("ND_normal_vector3"));
    if (!fn) return false;

    ParamMap in;
    ShadingContext ctx;
    ctx.normal = Vec3f(0.0f, 1.0f, 0.0f);
    _SetObjectWorldTransform(&ctx, _MakeIdentityMatrix());
    NodeOutputMap out;
    fn(in, ctx, &out);
    return Test_IsClose(_GetVec3(out), Vec3f(0.0f, 1.0f, 0.0f));
}

static bool TestGeometricTangentUsesInterpolatedFrame() {
    NodeRegistry::RegisterBuiltinNodes();
    auto fn = NodeRegistry::GetInstance().Find(
        std::string("ND_tangent_vector3"));
    if (!fn) return false;

    ShadingContext ctx;
    ctx.dPdu = Vec3f(1.0f, 0.0f, 0.0f);
    ctx.tangent = Vec3f(0.0f, 3.0f, 4.0f);
    _SetObjectWorldTransform(&ctx, _MakeIdentityMatrix());

    NodeOutputMap out;
    fn(ParamMap(), ctx, &out);
    if (!Test_IsClose(_GetVec3(out), Vec3f(0.0f, 0.6f, 0.8f))) {
        return false;
    }

    ctx.tangent = Vec3f(0.0f);
    ctx.dPdu = Vec3f(3.0f, 0.0f, 4.0f);
    NodeOutputMap fallbackOut;
    fn(ParamMap(), ctx, &fallbackOut);
    return Test_IsClose(
        _GetVec3(fallbackOut), Vec3f(0.6f, 0.0f, 0.8f));
}

static bool TestGeometricBitangentUsesInterpolatedFrame() {
    NodeRegistry::RegisterBuiltinNodes();
    auto fn = NodeRegistry::GetInstance().Find(
        std::string("ND_bitangent_vector3"));
    if (!fn) return false;

    ShadingContext ctx;
    ctx.normal = Vec3f(0.0f, 0.0f, 1.0f);
    ctx.dPdu = Vec3f(0.0f, 2.0f, 0.0f);
    ctx.dPdv = Vec3f(-1.0f, 0.0f, 0.0f);
    ctx.bitangent = Vec3f(3.0f, 4.0f, 0.0f);
    _SetObjectWorldTransform(&ctx, _MakeIdentityMatrix());

    NodeOutputMap out;
    fn(ParamMap(), ctx, &out);
    if (!Test_IsClose(_GetVec3(out), Vec3f(0.6f, 0.8f, 0.0f))) {
        return false;
    }

    ctx.bitangent = Vec3f(0.0f);
    ctx.dPdv = Vec3f(0.0f, 0.0f, -2.0f);
    NodeOutputMap fallbackOut;
    fn(ParamMap(), ctx, &fallbackOut);
    return Test_IsClose(_GetVec3(fallbackOut), Vec3f(0.0f, 0.0f, -1.0f));
}

static bool TestGeometricTexcoordVector3() {
    NodeRegistry::RegisterBuiltinNodes();
    auto fn = NodeRegistry::GetInstance().Find(
        std::string("ND_texcoord_vector3"));
    if (!fn) return false;

    ShadingContext ctx;
    ctx.texcoord = Vec2f(0.25f, 0.75f);

    NodeOutputMap out;
    fn(ParamMap(), ctx, &out);
    return Test_IsClose(_GetVec3(out), Vec3f(0.25f, 0.75f, 0.0f));
}

static bool TestGeometricViewDirectionWorldSpace() {
    NodeRegistry::RegisterBuiltinNodes();
    auto fn = NodeRegistry::GetInstance().Find(
        std::string("ND_viewdirection_vector3"));
    if (!fn) return false;

    ParamMap in;
    in["space"] = Value(std::string("world"));

    ShadingContext ctx;
    ctx.position = Vec3f(0.0f, 1.0f, 0.0f);
    ctx.viewPosition = Vec3f(1.0f, 2.0f, 8.0f);
    _SetObjectWorldTransform(&ctx, _MakeTranslationMatrix(Vec3f(1.0f, 2.0f, 3.0f)));

    NodeOutputMap out;
    fn(in, ctx, &out);

    return Test_IsClose(
        _GetVec3(out),
        Vec3f(0.0f, 1.0f, -5.0f).normalized());
}

static bool TestGeometricViewDirectionRemainsNormalizedAcrossSpaces() {
    NodeRegistry::RegisterBuiltinNodes();
    auto fn = NodeRegistry::GetInstance().Find(
        std::string("ND_viewdirection_vector3"));
    if (!fn) return false;

    ParamMap in;
    in["space"] = Value(std::string("object"));

    ShadingContext ctx;
    ctx.position = Vec3f(0.0f);
    ctx.viewPosition = Vec3f(0.0f, 0.0f, 5.0f);
    Mat4f objectToWorld;
    objectToWorld.setScale(Vec3f(2.0f));
    _SetObjectWorldTransform(&ctx, objectToWorld);

    NodeOutputMap out;
    fn(in, ctx, &out);
    return Test_IsClose(_GetVec3(out), Vec3f(0.0f, 0.0f, -1.0f));
}

static bool TestFacingRatioDefaultsToViewDirectionAndNormal() {
    NodeRegistry::RegisterBuiltinNodes();
    auto fn = NodeRegistry::GetInstance().Find(
        std::string("ND_facingratio_float"));
    if (!fn) return false;

    ParamMap in;

    ShadingContext ctx;
    ctx.position = Vec3f(0.0f, 0.0f, 0.0f);
    ctx.viewPosition = Vec3f(0.0f, 0.0f, 5.0f);
    ctx.normal = Vec3f(0.0f, 0.0f, 1.0f);

    NodeOutputMap out;
    fn(in, ctx, &out);
    return Test_IsClose(_GetFloat(out), 1.0f);
}

static bool TestFacingRatioSupportsFlagsAndAuthoredInputs() {
    NodeRegistry::RegisterBuiltinNodes();
    auto fn = NodeRegistry::GetInstance().Find(
        std::string("ND_facingratio_float"));
    if (!fn) return false;

    ParamMap in;
    in["viewdirection"] = Value(Vec3f(0.0f, 0.0f, 1.0f));
    in["normal"] = Value(Vec3f(0.0f, 0.0f, 1.0f));
    in["faceforward"] = Value(false);
    in["invert"] = Value(true);

    ShadingContext ctx;
    NodeOutputMap out;
    fn(in, ctx, &out);
    return Test_IsClose(_GetFloat(out), 2.0f);
}

static bool TestGoochShadeAddsSpecularHighlight() {
    NodeRegistry::RegisterBuiltinNodes();
    auto fn = NodeRegistry::GetInstance().Find(
        std::string("ND_gooch_shade"));
    if (!fn) return false;

    ParamMap in;
    in["warm_color"] = Value(Vec3f(0.8f, 0.7f, 0.6f));
    in["cool_color"] = Value(Vec3f(0.1f, 0.2f, 0.3f));
    in["specular_intensity"] = Value(0.25f);
    in["shininess"] = Value(4.0f);
    in["light_direction"] = Value(Vec3f(0.0f, 0.0f, -2.0f));

    ShadingContext ctx;
    ctx.position = Vec3f(0.0f, 0.0f, 0.0f);
    ctx.viewPosition = Vec3f(0.0f, 0.0f, 5.0f);
    ctx.normal = Vec3f(0.0f, 0.0f, 1.0f);

    NodeOutputMap out;
    fn(in, ctx, &out);
    return Test_IsClose(_GetVec3(out), Vec3f(1.05f, 0.95f, 0.85f));
}

static bool TestApplicationFrame() {
    NodeRegistry::RegisterBuiltinNodes();
    auto fn = NodeRegistry::GetInstance().Find(std::string("ND_frame_float"));
    if (!fn) return false;

    ParamMap in;
    ShadingContext ctx;
    ctx.frame = 42.5f;
    NodeOutputMap out;
    fn(in, ctx, &out);
    return Test_IsClose(_GetFloat(out), 42.5f);
}

static bool TestApplicationTime() {
    NodeRegistry::RegisterBuiltinNodes();
    auto fn = NodeRegistry::GetInstance().Find(std::string("ND_time_float"));
    if (!fn) return false;

    ParamMap in;
    ShadingContext ctx;
    ctx.time = 1.75f;
    NodeOutputMap out;
    fn(in, ctx, &out);
    return Test_IsClose(_GetFloat(out), 1.75f);
}

static bool TestTransformPointObjectToWorld() {
    ParamMap in;
    in["in"] = Value(Vec3f(1.0f, 2.0f, 3.0f));
    in["fromspace"] = Value(std::string("object"));
    in["tospace"] = Value(std::string("world"));

    ShadingContext ctx;
    _SetObjectWorldTransform(&ctx, _MakeTranslationMatrix(Vec3f(3.0f, -1.0f, 2.0f)));

    auto out = _EvalWithCtx("ND_transformpoint_vector3", in, ctx);
    return Test_IsClose(_GetVec3(out), Vec3f(4.0f, 1.0f, 5.0f));
}

static bool TestImageNodeUsesTextureSystem() {
    ParamMap in;
    in["file"] = Value(std::string("/tmp/test_albedo.tx"));
    in["colorSpace:file"] = Value(std::string("srgb_texture"));
    in["layer"] = Value(std::string("albedo"));
    in["default"] = Value(Vec4f(0.1f, 0.2f, 0.3f, 0.4f));
    in["uaddressmode"] = Value(std::string("mirror"));
    in["vaddressmode"] = Value(std::string("clamp"));
    in["filtertype"] = Value(std::string("cubic"));
    in["framerange"] = Value(std::string("1001-1010"));
    in["frameoffset"] = Value(3);
    in["frameendaction"] = Value(std::string("periodic"));

    _TestTextureSystem textureSystem;
    textureSystem.nextResult.value = Vec4f(0.7f, 0.6f, 0.5f, 0.4f);
    textureSystem.nextResult.status = TextureSampleStatus::Ok;

    ShadingContext ctx;
    ctx.textureSystem = &textureSystem;
    ctx.texcoord = Vec2f(0.25f, 0.75f);
    ctx.dudx = 0.125f;
    ctx.dvdx = -0.25f;
    ctx.dudy = 0.5f;
    ctx.dvdy = 0.25f;
    ctx.frame = 12.0f;

    const NodeOutputMap out = _EvalWithCtx("ND_image_color4", in, ctx);
    if (!textureSystem.called) {
        printf("    texture system was not called\n");
        return false;
    }

    if (!Test_IsClose(_GetVec4(out), textureSystem.nextResult.value)) {
        printf("    unexpected sampled output\n");
        return false;
    }

    const Texture2DRequest& request = textureSystem.lastRequest;
    return request.filePath == "/tmp/test_albedo.tx" &&
           request.layerName == "albedo" &&
           Test_IsClose(request.st, Vec2f(0.25f, 0.75f)) &&
           Test_IsClose(request.dstdx, Vec2f(0.125f, -0.25f)) &&
           Test_IsClose(request.dstdy, Vec2f(0.5f, 0.25f)) &&
           request.uAddressMode == TextureAddressMode::Mirror &&
           request.vAddressMode == TextureAddressMode::Clamp &&
           request.filterType == TextureFilterType::Cubic &&
           request.frameRange == "1001-1010" &&
           request.frameOffset == 3 &&
           request.frameEndAction == TextureAddressMode::Periodic &&
           Test_IsClose(request.frame, 12.0f) &&
           request.dataRole == TextureDataRole::Color &&
           request.sourceColorSpace == "srgb_texture" &&
           request.channelCount == 4 &&
           Test_IsClose(request.channelFillValue, 1.0f) &&
           Test_IsClose(request.defaultValue, Vec4f(0.1f, 0.2f, 0.3f, 0.4f));
}

static bool TestImageNodeConstantWrapReturnsDefault() {
    ParamMap in;
    in["file"] = Value(std::string("/tmp/test_mask.tx"));
    in["default"] = Value(0.25f);
    in["uaddressmode"] = Value(std::string("constant"));

    _TestTextureSystem textureSystem;
    textureSystem.nextResult.value = Vec4f(1.0f);
    textureSystem.nextResult.status = TextureSampleStatus::Ok;

    ShadingContext ctx;
    ctx.textureSystem = &textureSystem;
    ctx.texcoord = Vec2f(1.2f, 0.5f);

    const NodeOutputMap out = _EvalWithCtx("ND_image_float", in, ctx);
    if (textureSystem.called) {
        printf("    constant-wrap out-of-range lookup should not sample\n");
        return false;
    }

    return Test_IsClose(_GetFloat(out), 0.25f);
}

static bool TestUsdUvTextureUsesNativeWrapAndColorSpaceSemantics() {
    ParamMap in;
    in["file"] = Value(std::string("/tmp/test_usd_uv.tx"));
    in["fallback"] = Value(Vec4f(0.1f, 0.2f, 0.3f, 0.4f));
    in["scale"] = Value(Vec4f(2.0f, 3.0f, 4.0f, 5.0f));
    in["bias"] = Value(Vec4f(0.01f, 0.02f, 0.03f, 0.04f));
    in["wrapS"] = Value(std::string("useMetadata"));
    in["wrapT"] = Value(std::string("repeat"));
    in["sourceColorSpace"] = Value(std::string("sRGB"));

    _TestTextureSystem textureSystem;
    textureSystem.nextResult.value = Vec4f(0.2f, 0.3f, 0.4f, 0.5f);
    textureSystem.nextResult.status = TextureSampleStatus::Ok;

    ShadingContext ctx;
    ctx.textureSystem = &textureSystem;
    ctx.texcoord = Vec2f(0.25f, 0.75f);
    ctx.dudx = 0.125f;
    ctx.dvdx = -0.25f;
    ctx.dudy = 0.5f;
    ctx.dvdy = 0.25f;
    ctx.frame = 18.0f;

    const NodeOutputMap out = _EvalWithCtx("UsdUVTexture", in, ctx);
    if (!textureSystem.called) {
        printf("    UsdUVTexture did not invoke texture system\n");
        return false;
    }

    const Texture2DRequest& request = textureSystem.lastRequest;
    return Test_IsClose(_GetFloat(out, "r"), 0.41f) &&
           Test_IsClose(_GetFloat(out, "g"), 0.92f) &&
           Test_IsClose(_GetFloat(out, "b"), 1.63f) &&
           Test_IsClose(_GetFloat(out, "a"), 2.54f) &&
           Test_IsClose(_GetVec3(out, "rgb"), Vec3f(0.41f, 0.92f, 1.63f)) &&
           request.filePath == "/tmp/test_usd_uv.tx" &&
           Test_IsClose(request.st, Vec2f(0.25f, 0.75f)) &&
           Test_IsClose(request.dstdx, Vec2f(0.125f, -0.25f)) &&
           Test_IsClose(request.dstdy, Vec2f(0.5f, 0.25f)) &&
           request.uAddressMode == TextureAddressMode::UseMetadata &&
           request.vAddressMode == TextureAddressMode::Periodic &&
           request.filterType == TextureFilterType::Linear &&
           Test_IsClose(request.frame, 18.0f) &&
           request.dataRole == TextureDataRole::Color &&
           request.sourceColorSpace == "srgb_rec709_scene" &&
           !request.inferSrgbFromFile &&
           request.channelCount == 4 &&
           Test_IsClose(request.channelFillValue, 1.0f) &&
           Test_IsClose(request.defaultValue, Vec4f(0.1f, 0.2f, 0.3f, 0.4f));
}

static bool TestOnlyNativeUsdUvTextureRequestsAutomaticColorSpace() {
    ParamMap in;
    in["file"] = Value(std::string("/tmp/test_usd_uv_auto.tx"));
    in["sourceColorSpace"] = Value(std::string("auto"));

    _TestTextureSystem textureSystem;
    textureSystem.nextResult.value = Vec4f(0.2f, 0.3f, 0.4f, 1.0f);
    textureSystem.nextResult.status = TextureSampleStatus::Ok;

    ShadingContext ctx;
    ctx.textureSystem = &textureSystem;

    _EvalWithCtx("UsdUVTexture", in, ctx);
    if (!textureSystem.called ||
        !textureSystem.lastRequest.inferSrgbFromFile ||
        !textureSystem.lastRequest.sourceColorSpace.empty()) {
        printf("    native UsdUVTexture did not request auto detection\n");
        return false;
    }

    textureSystem.called = false;
    _EvalWithCtx("ND_UsdUVTexture", in, ctx);
    if (!textureSystem.called ||
        textureSystem.lastRequest.inferSrgbFromFile ||
        !textureSystem.lastRequest.sourceColorSpace.empty()) {
        printf("    MaterialX UsdUVTexture unexpectedly requested auto detection\n");
        return false;
    }

    ParamMap imageIn;
    imageIn["file"] = Value(std::string("/tmp/test_image_auto.tx"));
    imageIn["colorSpace:file"] = Value(std::string("auto"));
    textureSystem.called = false;
    _EvalWithCtx("ND_image_color3", imageIn, ctx);
    return textureSystem.called &&
           !textureSystem.lastRequest.inferSrgbFromFile &&
           textureSystem.lastRequest.sourceColorSpace == "auto";
}

static bool TestNativeUsdUvTextureDefaultsToAutomaticColorSpace() {
    ParamMap in;
    in["file"] = Value(std::string("/tmp/test_usd_uv_default_auto.tx"));

    _TestTextureSystem textureSystem;
    textureSystem.nextResult.value = Vec4f(0.2f, 0.3f, 0.4f, 1.0f);
    textureSystem.nextResult.status = TextureSampleStatus::Ok;

    ShadingContext ctx;
    ctx.textureSystem = &textureSystem;

    _EvalWithCtx("UsdUVTexture", in, ctx);
    return textureSystem.called &&
           textureSystem.lastRequest.inferSrgbFromFile &&
           textureSystem.lastRequest.sourceColorSpace.empty();
}

static bool TestUsdUvTextureFallsBackToFileColorSpaceMetadata() {
    ParamMap in;
    in["file"] = Value(std::string("/tmp/test_usd_uv_metadata.tx"));
    in["colorSpace:file"] = Value(std::string("sRGB"));

    _TestTextureSystem textureSystem;
    textureSystem.nextResult.value = Vec4f(0.2f, 0.3f, 0.4f, 0.5f);
    textureSystem.nextResult.status = TextureSampleStatus::Ok;

    ShadingContext ctx;
    ctx.textureSystem = &textureSystem;
    ctx.texcoord = Vec2f(0.1f, 0.2f);

    const NodeOutputMap out = _EvalWithCtx("UsdUVTexture", in, ctx);
    if (!textureSystem.called) {
        printf("    UsdUVTexture did not invoke texture system\n");
        return false;
    }

    return Test_IsClose(_GetVec3(out, "rgb"), Vec3f(0.2f, 0.3f, 0.4f)) &&
           textureSystem.lastRequest.sourceColorSpace ==
               "srgb_rec709_scene";
}

static bool TestMaterialXUsdUvTextureOutputsRgbaAndScaledFallback() {
    ParamMap in;
    in["fallback"] = Value(Vec4f(0.1f, 0.2f, 0.3f, 0.4f));
    in["scale"] = Value(Vec4f(2.0f, 3.0f, 4.0f, 5.0f));
    in["bias"] = Value(Vec4f(0.01f, 0.02f, 0.03f, 0.04f));
    in["wrapS"] = Value(std::string("periodic"));
    in["wrapT"] = Value(std::string("mirror"));

    ShadingContext ctx;
    ctx.texcoord = Vec2f(0.33f, 0.66f);

    const NodeOutputMap out = _EvalWithCtx("ND_UsdUVTexture", in, ctx);
    return Test_IsClose(_GetFloat(out, "r"), 0.21f) &&
           Test_IsClose(_GetFloat(out, "g"), 0.62f) &&
           Test_IsClose(_GetFloat(out, "b"), 1.23f) &&
           Test_IsClose(_GetFloat(out, "a"), 2.04f) &&
           Test_IsClose(_GetVec3(out, "rgb"), Vec3f(0.21f, 0.62f, 1.23f)) &&
           Test_IsClose(_GetVec4(out, "rgba"), Vec4f(0.21f, 0.62f, 1.23f, 2.04f));
}

static bool TestTiledImageTransformsTexcoords() {
    ParamMap in;
    in["file"] = Value(std::string("/tmp/test_normal.tx"));
    in["default"] = Value(Vec3f(0.0f));
    in["uvtiling"] = Value(Vec2f(4.0f, 2.0f));
    in["uvoffset"] = Value(Vec2f(1.0f, 0.5f));
    in["realworldimagesize"] = Value(Vec2f(2.0f, 4.0f));
    in["realworldtilesize"] = Value(Vec2f(10.0f, 8.0f));

    _TestTextureSystem textureSystem;
    textureSystem.nextResult.value = Vec4f(0.2f, 0.4f, 0.6f, 0.0f);
    textureSystem.nextResult.status = TextureSampleStatus::Ok;

    ShadingContext ctx;
    ctx.textureSystem = &textureSystem;
    ctx.texcoord = Vec2f(0.25f, 0.5f);
    ctx.dudx = 0.10f;
    ctx.dvdx = 0.20f;
    ctx.dudy = -0.05f;
    ctx.dvdy = 0.10f;

    const NodeOutputMap out = _EvalWithCtx("ND_tiledimage_vector3", in, ctx);
    if (!textureSystem.called) {
        printf("    tiledimage did not invoke texture system\n");
        return false;
    }

    const Texture2DRequest& request = textureSystem.lastRequest;
    return Test_IsClose(_GetVec3(out), Vec3f(0.2f, 0.4f, 0.6f)) &&
           Test_IsClose(request.st, Vec2f(0.0f, 1.0f)) &&
           Test_IsClose(request.dstdx, Vec2f(2.0f, 0.8f)) &&
           Test_IsClose(request.dstdy, Vec2f(-1.0f, 0.4f)) &&
           request.dataRole == TextureDataRole::NonColor &&
           request.channelCount == 3;
}

static bool TestLatLongImageMapsViewdirToLatLongUv() {
    ParamMap in;
    in["file"] = Value(std::string("/tmp/test_env.tx"));
    in["colorSpace:file"] = Value(std::string("ACEScg"));
    in["default"] = Value(Vec3f(0.1f, 0.2f, 0.3f));
    in["viewdir"] = Value(Vec3f(1.0f, 0.0f, 0.0f));
    in["rotation"] = Value(90.0f);

    _TestTextureSystem textureSystem;
    textureSystem.nextResult.value = Vec4f(0.9f, 0.8f, 0.7f, 1.0f);
    textureSystem.nextResult.status = TextureSampleStatus::Ok;

    ShadingContext ctx;
    ctx.textureSystem = &textureSystem;
    ctx.frame = 7.0f;

    const NodeOutputMap out = _EvalWithCtx("ND_latlongimage", in, ctx);
    if (!textureSystem.called) {
        printf("    latlongimage did not invoke texture system\n");
        return false;
    }

    const Texture2DRequest& request = textureSystem.lastRequest;
    return Test_IsClose(_GetVec3(out), Vec3f(0.9f, 0.8f, 0.7f)) &&
           request.filePath == "/tmp/test_env.tx" &&
           Test_IsClose(request.st, Vec2f(0.5f, 0.5f)) &&
           Test_IsClose(request.dstdx, Vec2f(0.0f, 0.0f)) &&
           Test_IsClose(request.dstdy, Vec2f(0.0f, 0.0f)) &&
           request.uAddressMode == TextureAddressMode::Periodic &&
           request.vAddressMode == TextureAddressMode::Mirror &&
           request.filterType == TextureFilterType::Linear &&
           Test_IsClose(request.frame, 7.0f) &&
           request.dataRole == TextureDataRole::Color &&
           request.sourceColorSpace == "ACEScg" &&
           request.channelCount == 3 &&
           Test_IsClose(request.channelFillValue, 0.0f) &&
           Test_IsClose(request.defaultValue, Vec4f(0.1f, 0.2f, 0.3f, 0.0f));
}

static bool TestLatLongImageReevaluatesConnectedViewdir() {
    ParamMap in;
    in["file"] = Value(std::string("/tmp/test_env.tx"));
    in["default"] = Value(Vec3f(0.0f));
    in.Add(
        AsSlotId("viewdir"),
        nullptr,
        &_EvalViewdirFromTexcoord,
        nullptr,
        -1,
        InvalidSlotId);

    _TestTextureSystem textureSystem;
    textureSystem.nextResult.value = Vec4f(0.3f, 0.4f, 0.5f, 1.0f);
    textureSystem.nextResult.status = TextureSampleStatus::Ok;

    ShadingContext ctx;
    ctx.textureSystem = &textureSystem;
    ctx.texcoord = Vec2f(0.0f, 0.0f);
    ctx.dudx = 1.0f;
    ctx.dvdx = 0.0f;
    ctx.dudy = 0.0f;
    ctx.dvdy = 1.0f;

    const NodeOutputMap out = _EvalWithCtx("ND_latlongimage", in, ctx);
    if (!textureSystem.called) {
        printf("    latlongimage reevaluation did not invoke texture system\n");
        return false;
    }

    const Texture2DRequest& request = textureSystem.lastRequest;
    return Test_IsClose(_GetVec3(out), Vec3f(0.3f, 0.4f, 0.5f)) &&
           Test_IsClose(request.st, Vec2f(0.5f, 0.5f)) &&
           Test_IsClose(request.dstdx, Vec2f(-0.125f, 0.0f)) &&
           Test_IsClose(request.dstdy, Vec2f(0.0f, 0.5f));
}

static bool TestHexTiledImageBlendsThreeColorSamples() {
    ParamMap in;
    in["file"] = Value(std::string("/tmp/test_hex.tx"));
    in["colorSpace:file"] = Value(std::string("srgb_texture"));
    in["tiling"] = Value(Vec2f(2.0f, 3.0f));
    in["rotation"] = Value(0.0f);
    in["scale"] = Value(0.0f);
    in["offset"] = Value(0.0f);
    in["falloff"] = Value(0.5f);
    in["falloffcontrast"] = Value(0.0f);

    _TestTextureSystem textureSystem;
    textureSystem.nextResults = {
        {Vec4f(1.0f, 0.0f, 0.0f, 1.0f), TextureSampleStatus::Ok},
        {Vec4f(0.0f, 1.0f, 0.0f, 1.0f), TextureSampleStatus::Ok},
        {Vec4f(0.0f, 0.0f, 1.0f, 1.0f), TextureSampleStatus::Ok},
    };

    ShadingContext ctx;
    ctx.textureSystem = &textureSystem;
    ctx.texcoord = Vec2f(0.07216878f, 0.02777778f);
    ctx.dudx = 0.125f;
    ctx.dvdx = -0.25f;
    ctx.dudy = 0.5f;
    ctx.dvdy = 0.25f;
    ctx.frame = 3.0f;

    const NodeOutputMap out = _EvalWithCtx("ND_hextiledimage_color3", in, ctx);
    if (textureSystem.requests.size() != 3) {
        printf("    hextiledimage color3 should sample three tiles\n");
        return false;
    }

    const Vec2f expectedSt(0.14433757f, 0.08333333f);
    const Vec2f expectedDstdx(0.25f, -0.75f);
    const Vec2f expectedDstdy(1.0f, 0.75f);
    for (const Texture2DRequest& request : textureSystem.requests) {
        if (request.filePath != "/tmp/test_hex.tx" ||
            !Test_IsClose(request.st, expectedSt) ||
            !Test_IsClose(request.dstdx, expectedDstdx) ||
            !Test_IsClose(request.dstdy, expectedDstdy) ||
            request.uAddressMode != TextureAddressMode::Periodic ||
            request.vAddressMode != TextureAddressMode::Periodic ||
            request.filterType != TextureFilterType::SmartBicubic ||
            request.dataRole != TextureDataRole::Color ||
            request.sourceColorSpace != "srgb_texture" ||
            request.channelCount != 3) {
            printf("    hextiledimage color3 request mismatch\n");
            return false;
        }
    }

    return Test_IsClose(_GetVec3(out), Vec3f(1.0f / 3.0f), 1e-5f);
}

static bool TestHexTiledImageColor4BlendsAlphaSeparately() {
    ParamMap in;
    in["file"] = Value(std::string("/tmp/test_hex_rgba.tx"));
    in["rotation"] = Value(0.0f);
    in["scale"] = Value(0.0f);
    in["offset"] = Value(0.0f);
    in["falloff"] = Value(0.5f);
    in["falloffcontrast"] = Value(1.0f);
    in["lumacoeffs"] = Value(Vec3f(0.2f, 0.3f, 0.5f));

    _TestTextureSystem textureSystem;
    textureSystem.nextResults = {
        {Vec4f(1.0f, 0.0f, 0.0f, 0.2f), TextureSampleStatus::Ok},
        {Vec4f(0.0f, 1.0f, 0.0f, 0.4f), TextureSampleStatus::Ok},
        {Vec4f(0.0f, 0.0f, 1.0f, 0.8f), TextureSampleStatus::Ok},
    };

    ShadingContext ctx;
    ctx.textureSystem = &textureSystem;
    ctx.texcoord = Vec2f(0.14433757f, 0.08333333f);

    const NodeOutputMap out = _EvalWithCtx("ND_hextiledimage_color4", in, ctx);
    if (textureSystem.requests.size() != 3) {
        printf("    hextiledimage color4 should sample three tiles\n");
        return false;
    }

    const Vec4f expected(0.2f, 0.3f, 0.5f, (0.2f + 0.4f + 0.8f) / 3.0f);
    return Test_IsClose(_GetVec4(out), expected, 1e-5f);
}

static bool TestGltfNormalMapSamplesAndTransformsToWorld() {
    ParamMap in;
    in["file"] = Value(std::string("/tmp/gltf_normal.jpg"));
    in["colorSpace:file"] = Value(std::string("none"));
    in["texcoord"] = Value(Vec2f(0.25f, 0.75f));

    _TestTextureSystem textureSystem;
    textureSystem.nextResult = {
        Vec4f(0.75f, 0.25f, 1.0f, 1.0f),
        TextureSampleStatus::Ok};

    ShadingContext ctx;
    ctx.textureSystem = &textureSystem;
    ctx.normal = Vec3f(0.0f, 0.0f, 1.0f);
    ctx.tangent = Vec3f(1.0f, 0.0f, 0.0f);
    // Deliberately disagree with dPdv so the test verifies the generated
    // OSL normalmap frame is derived from dPdu/dPdv.
    ctx.bitangent = Vec3f(0.0f, -1.0f, 0.0f);
    ctx.dPdu = ctx.tangent;
    ctx.dPdv = Vec3f(0.0f, 1.0f, 0.0f);

    const NodeOutputMap out =
        _EvalWithCtx("ND_gltf_normalmap_vector3_1_0", in, ctx);
    if (textureSystem.requests.size() != 1) {
        printf("    glTF normalmap should sample one texture\n");
        return false;
    }

    const Texture2DRequest& request = textureSystem.lastRequest;
    if (request.filePath != "/tmp/gltf_normal.jpg" ||
        !Test_IsClose(request.st, Vec2f(0.25f, 0.75f)) ||
        request.dataRole != TextureDataRole::NonColor ||
        request.sourceColorSpace != "none" ||
        request.channelCount != 3) {
        printf("    glTF normalmap texture request mismatch\n");
        return false;
    }

    const Vec3f expected =
        Vec3f(0.5f, -0.5f, 1.0f).normalized();
    if (!Test_IsClose(_GetVec3(out), expected, 1e-5f)) {
        printf("    glTF normalmap tangent-frame conversion mismatch\n");
        return false;
    }

    ParamMap missing;
    const NodeOutputMap missingOut =
        _EvalWithCtx("ND_gltf_normalmap_vector3_1_0", missing, ctx);
    return Test_IsClose(_GetVec3(missingOut), ctx.normal, 1e-5f);
}

static bool TestGltfNormalMapReevaluatesConnectedTransformInputs() {
    ParamMap in;
    in["file"] = Value(std::string("/tmp/gltf_normal.jpg"));
    in["pivot"] = Value(Vec2f(0.0f));
    in.Add(
        AsSlotId("rotate"),
        nullptr,
        &_EvalRotationFromTexcoordX,
        nullptr,
        -1,
        InvalidSlotId);

    _TestTextureSystem textureSystem;
    textureSystem.nextResult = {
        Vec4f(0.5f, 0.5f, 1.0f, 1.0f),
        TextureSampleStatus::Ok};

    ShadingContext ctx;
    ctx.textureSystem = &textureSystem;
    ctx.texcoord = Vec2f(0.0f);
    ctx.dudx = 0.1f;
    ctx.dvdy = 0.1f;

    _EvalWithCtx("ND_gltf_normalmap_vector3_1_0", in, ctx);
    const Texture2DRequest& request = textureSystem.lastRequest;
    return Test_IsClose(request.st, Vec2f(0.0f)) &&
           Test_IsClose(
               request.dstdx,
               Vec2f(0.0987688f, 0.0156434f),
               1e-5f) &&
           Test_IsClose(request.dstdy, Vec2f(0.0f, 0.1f), 1e-5f);
}

static bool TestGltfImageColor3SamplesAndAppliesFactor() {
    ParamMap in;
    in["file"] = Value(std::string("/tmp/gltf_color.png"));
    in["colorSpace:file"] = Value(std::string("srgb_texture"));
    in["factor"] = Value(Vec3f(0.5f, 0.25f, 0.75f));

    _TestTextureSystem textureSystem;
    textureSystem.nextResult = {
        Vec4f(0.8f, 0.6f, 0.4f, 1.0f),
        TextureSampleStatus::Ok};

    ShadingContext ctx;
    ctx.textureSystem = &textureSystem;
    const NodeOutputMap out = _EvalWithCtx(
        "ND_gltf_image_color3_color3_1_0", in, ctx);

    const Texture2DRequest& request = textureSystem.lastRequest;
    return Test_IsClose(_GetVec3(out), Vec3f(0.4f, 0.15f, 0.3f)) &&
           request.dataRole == TextureDataRole::Color &&
           request.sourceColorSpace == "srgb_texture" &&
           request.channelCount == 3;
}

static bool TestGltfColorImageModulatesRgbAndAlpha() {
    ParamMap in;
    in["file"] = Value(std::string("/tmp/gltf_color.png"));
    in["color"] = Value(Vec4f(0.5f, 1.0f, 0.25f, 0.5f));
    in["geomcolor"] = Value(Vec4f(0.5f, 0.25f, 1.0f, 0.5f));

    _TestTextureSystem textureSystem;
    textureSystem.nextResult = {
        Vec4f(0.8f, 0.6f, 0.4f, 0.2f),
        TextureSampleStatus::Ok};

    ShadingContext ctx;
    ctx.textureSystem = &textureSystem;
    const NodeOutputMap out =
        _EvalWithCtx("ND_gltf_colorimage", in, ctx);

    const Texture2DRequest& request = textureSystem.lastRequest;
    return Test_IsClose(
               _GetVec3(out, "outcolor"),
               Vec3f(0.2f, 0.15f, 0.1f)) &&
           Test_IsClose(_GetFloat(out, "outa"), 0.05f) &&
           request.dataRole == TextureDataRole::Color &&
           request.channelCount == 4;
}

static bool TestGltfIridescenceThicknessUsesGreenChannel() {
    ParamMap in;
    in["file"] = Value(std::string("/tmp/gltf_thickness.png"));
    in["thicknessMin"] = Value(120.0f);
    in["thicknessMax"] = Value(900.0f);

    _TestTextureSystem textureSystem;
    textureSystem.nextResult = {
        Vec4f(0.1f, 0.25f, 0.9f, 1.0f),
        TextureSampleStatus::Ok};

    ShadingContext ctx;
    ctx.textureSystem = &textureSystem;
    const NodeOutputMap out = _EvalWithCtx(
        "ND_gltf_iridescence_thickness_float_1_0", in, ctx);

    const Texture2DRequest& request = textureSystem.lastRequest;
    return Test_IsClose(_GetFloat(out), 705.0f) &&
           request.dataRole == TextureDataRole::NonColor &&
           request.channelCount == 3;
}

static bool TestGltfAnisotropyImageDecodesDirectionAndStrength() {
    ParamMap in;
    in["file"] = Value(std::string("/tmp/gltf_anisotropy.png"));
    in["anisotropy_strength"] = Value(0.8f);
    in["anisotropy_rotation"] = Value(0.25f);

    _TestTextureSystem textureSystem;
    textureSystem.nextResult = {
        Vec4f(0.5f, 1.0f, 0.75f, 1.0f),
        TextureSampleStatus::Ok};

    ShadingContext ctx;
    ctx.textureSystem = &textureSystem;
    const NodeOutputMap out =
        _EvalWithCtx("ND_gltf_anisotropy_image", in, ctx);

    const Texture2DRequest& request = textureSystem.lastRequest;
    return Test_IsClose(
               _GetFloat(out, "anisotropy_strength_out"), 0.6f) &&
           Test_IsClose(
               _GetFloat(out, "anisotropy_rotation_out"),
               0.25f + 1.57079632679f) &&
           request.dataRole == TextureDataRole::NonColor &&
           request.channelCount == 3;
}

static bool TestGltfAnisotropyImageUsesSpecificationDefault() {
    const NodeOutputMap out =
        _EvalWithCtx("ND_gltf_anisotropy_image", ParamMap(), ShadingContext());
    return Test_IsClose(
               _GetFloat(out, "anisotropy_strength_out"), 1.0f) &&
           Test_IsClose(
               _GetFloat(out, "anisotropy_rotation_out"), 0.0f);
}

static bool TestTriplanarProjectionColor3SamplesAxesAndBlends() {
    ParamMap in;
    in["filex"] = Value(std::string("/tmp/triplanar_x.tx"));
    in["filey"] = Value(std::string("/tmp/triplanar_y.tx"));
    in["filez"] = Value(std::string("/tmp/triplanar_z.tx"));
    in["colorSpace:filex"] = Value(std::string("ACEScg"));
    in["colorSpace:filey"] = Value(std::string("srgb_texture"));
    in["colorSpace:filez"] = Value(std::string("raw"));
    in["layerx"] = Value(std::string("layerX"));
    in["layery"] = Value(std::string("layerY"));
    in["layerz"] = Value(std::string("layerZ"));
    in["default"] = Value(Vec3f(0.1f, 0.2f, 0.3f));
    in["position"] = Value(Vec3f(1.0f, 2.0f, 3.0f));
    in["normal"] = Value(Vec3f(1.0f, 2.0f, 1.0f));
    in["blend"] = Value(1.0f);
    in["filtertype"] = Value(std::string("cubic"));
    in["framerange"] = Value(std::string("1001-1010"));
    in["frameoffset"] = Value(4);
    in["frameendaction"] = Value(std::string("mirror"));

    _TestTextureSystem textureSystem;
    textureSystem.nextResults = {
        {Vec4f(1.0f, 0.0f, 0.0f, 1.0f), TextureSampleStatus::Ok},
        {Vec4f(0.0f, 1.0f, 0.0f, 1.0f), TextureSampleStatus::Ok},
        {Vec4f(0.0f, 0.0f, 1.0f, 1.0f), TextureSampleStatus::Ok},
    };

    ShadingContext ctx;
    ctx.textureSystem = &textureSystem;
    ctx.frame = 12.0f;
    ctx.dPositiondx = Vec3f(0.1f, 0.2f, 0.3f);
    ctx.dPositiondy = Vec3f(-0.5f, 0.25f, 1.0f);

    const NodeOutputMap out = _EvalWithCtx("ND_triplanarprojection_color3", in, ctx);
    if (textureSystem.requests.size() != 3) {
        printf("    triplanarprojection color3 should sample three projections\n");
        return false;
    }

    const std::array<std::string, 3> expectedFiles = {
        "/tmp/triplanar_x.tx",
        "/tmp/triplanar_y.tx",
        "/tmp/triplanar_z.tx"};
    const std::array<std::string, 3> expectedLayers = {
        "layerX", "layerY", "layerZ"};
    const std::array<std::string, 3> expectedColorSpaces = {
        "ACEScg", "srgb_texture", "raw"};
    const std::array<Vec2f, 3> expectedSt = {
        Vec2f(2.0f, 3.0f),
        Vec2f(1.0f, 3.0f),
        Vec2f(1.0f, 2.0f)};
    const std::array<Vec2f, 3> expectedDstdx = {
        Vec2f(0.0f),
        Vec2f(0.0f),
        Vec2f(0.0f)};
    const std::array<Vec2f, 3> expectedDstdy = {
        Vec2f(0.0f),
        Vec2f(0.0f),
        Vec2f(0.0f)};

    for (size_t i = 0; i < textureSystem.requests.size(); ++i) {
        const Texture2DRequest& request = textureSystem.requests[i];
        if (request.filePath != expectedFiles[i] ||
            request.layerName != expectedLayers[i] ||
            request.sourceColorSpace != expectedColorSpaces[i] ||
            !Test_IsClose(request.st, expectedSt[i]) ||
            !Test_IsClose(request.dstdx, expectedDstdx[i]) ||
            !Test_IsClose(request.dstdy, expectedDstdy[i]) ||
            request.uAddressMode != TextureAddressMode::Periodic ||
            request.vAddressMode != TextureAddressMode::Periodic ||
            request.filterType != TextureFilterType::Cubic ||
            request.frameRange != "1001-1010" ||
            request.frameOffset != 4 ||
            request.frameEndAction != TextureAddressMode::Mirror ||
            !Test_IsClose(request.frame, 12.0f) ||
            request.dataRole != TextureDataRole::Color ||
            request.channelCount != 3 ||
            !Test_IsClose(request.channelFillValue, 0.0f) ||
            !Test_IsClose(request.defaultValue, Vec4f(0.1f, 0.2f, 0.3f, 0.0f))) {
            printf("    triplanarprojection color3 request mismatch at sample %zu\n", i);
            return false;
        }
    }

    return Test_IsClose(_GetVec3(out), Vec3f(0.25f, 0.5f, 0.25f), 1e-5f);
}

static bool TestTriplanarProjectionReevaluatesConnectedPosition() {
    ParamMap in;
    in["filex"] = Value(std::string("/tmp/triplanar_x.tx"));
    in["filey"] = Value(std::string("/tmp/triplanar_y.tx"));
    in["filez"] = Value(std::string("/tmp/triplanar_z.tx"));
    in["default"] = Value(0.0f);
    in["normal"] = Value(Vec3f(0.0f, 0.0f, 1.0f));
    in["upaxis"] = Value(0);
    in.Add(
        AsSlotId("position"),
        nullptr,
        &_EvalPositionFromTexcoordAndZ,
        nullptr,
        -1,
        InvalidSlotId);

    _TestTextureSystem textureSystem;
    textureSystem.nextResults = {
        {Vec4f(0.1f, 0.0f, 0.0f, 0.0f), TextureSampleStatus::Ok},
        {Vec4f(0.2f, 0.0f, 0.0f, 0.0f), TextureSampleStatus::Ok},
        {Vec4f(0.75f, 0.0f, 0.0f, 0.0f), TextureSampleStatus::Ok},
    };

    ShadingContext ctx;
    ctx.textureSystem = &textureSystem;
    ctx.position = Vec3f(9.0f, 9.0f, 0.5f);
    ctx.texcoord = Vec2f(0.2f, 0.4f);
    ctx.dudx = 0.1f;
    ctx.dvdx = -0.2f;
    ctx.dudy = 0.3f;
    ctx.dvdy = 0.4f;
    ctx.dPositiondx = Vec3f(0.0f, 0.0f, 0.05f);
    ctx.dPositiondy = Vec3f(0.0f, 0.0f, -0.1f);

    const NodeOutputMap out = _EvalWithCtx("ND_triplanarprojection_float", in, ctx);
    if (textureSystem.requests.size() != 3) {
        printf("    triplanarprojection float should sample three projections\n");
        return false;
    }

    const std::array<Vec2f, 3> expectedSt = {
        Vec2f(0.5f, 0.4f),
        Vec2f(0.5f, 0.2f),
        Vec2f(-0.4f, 0.2f)};
    const std::array<Vec2f, 3> expectedDstdx = {
        Vec2f(0.05f, -0.2f),
        Vec2f(0.05f, 0.1f),
        Vec2f(0.2f, 0.1f)};
    const std::array<Vec2f, 3> expectedDstdy = {
        Vec2f(-0.1f, 0.4f),
        Vec2f(-0.1f, 0.3f),
        Vec2f(-0.4f, 0.3f)};

    for (size_t i = 0; i < textureSystem.requests.size(); ++i) {
        const Texture2DRequest& request = textureSystem.requests[i];
        if (!Test_IsClose(request.st, expectedSt[i]) ||
            !Test_IsClose(request.dstdx, expectedDstdx[i]) ||
            !Test_IsClose(request.dstdy, expectedDstdy[i])) {
            printf("    triplanarprojection connected position mismatch at sample %zu\n", i);
            return false;
        }
    }

    return Test_IsClose(_GetFloat(out), 0.75f, 1e-5f);
}

static bool TestTriplanarProjectionDefaultsNormalToObjectSpace() {
    ParamMap in;
    in["filex"] = Value(std::string("/tmp/triplanar_x.tx"));
    in["filey"] = Value(std::string("/tmp/triplanar_y.tx"));
    in["filez"] = Value(std::string("/tmp/triplanar_z.tx"));
    in["default"] = Value(0.0f);

    _TestTextureSystem textureSystem;
    textureSystem.nextResults = {
        {Vec4f(0.8f, 0.0f, 0.0f, 0.0f), TextureSampleStatus::Ok},
        {Vec4f(0.5f, 0.0f, 0.0f, 0.0f), TextureSampleStatus::Ok},
        {Vec4f(0.2f, 0.0f, 0.0f, 0.0f), TextureSampleStatus::Ok},
    };

    ShadingContext ctx;
    ctx.textureSystem = &textureSystem;
    ctx.position = Vec3f(1.0f, 2.0f, 3.0f);
    ctx.normal = Vec3f(0.0f, 0.0f, 1.0f);
    ctx.transformSpace = &_TransformWorldNormalToObjectX;

    const NodeOutputMap out = _EvalWithCtx("ND_triplanarprojection_float", in, ctx);
    return Test_IsClose(_GetFloat(out), 0.8f, 1e-5f);
}

static bool TestHeightToNormalDefaultTexcoord() {
    NodeRegistry::RegisterBuiltinNodes();
    auto fn = NodeRegistry::GetInstance().Find(
        std::string("ND_heighttonormal_vector3"));
    if (!fn) return false;

    ParamMap in;
    in.Add(
        AsSlotId("in"),
        nullptr,
        &_EvalHeightFromTexcoordX,
        nullptr,
        -1,
        InvalidSlotId);

    ShadingContext ctx;
    ctx.texcoord = Vec2f(0.25f, 0.5f);
    ctx.dudx = 1.0f;
    ctx.dvdy = 1.0f;

    NodeOutputMap out;
    fn(in, ctx, &out);

    Vec3f expected = Vec3f(-1.0f / 16.0f, 0.0f, 1.0f).normalized();
    expected = expected * 0.5f + Vec3f(0.5f, 0.5f, 0.5f);
    if (!Test_IsClose(_GetVec3(out), expected, 1e-5f)) return false;

    ParamMap nonlinearIn;
    nonlinearIn.Add(
        AsSlotId("in"),
        nullptr,
        &_EvalHeightFromTexcoordXSquared,
        nullptr,
        -1,
        InvalidSlotId);
    ctx.dudx = 0.1f;
    ctx.dvdy = 0.1f;
    out.Clear();
    fn(nonlinearIn, ctx, &out);

    expected = Vec3f(-0.5f / 16.0f, 0.0f, 1.0f).normalized();
    expected = expected * 0.5f + Vec3f(0.5f, 0.5f, 0.5f);
    return Test_IsClose(_GetVec3(out), expected, 1e-5f);
}

static bool TestHeightToNormalPreservesSmallDerivatives() {
    ParamMap in;
    in.Add(
        AsSlotId("in"),
        nullptr,
        &_EvalHeightFromTexcoordX,
        nullptr,
        -1,
        InvalidSlotId);

    ShadingContext ctx;
    ctx.texcoord = Vec2f(0.25f, 0.5f);
    ctx.dudx = 1e-5f;
    ctx.dvdy = 1e-5f;

    const NodeOutputMap out =
        _EvalWithCtx("ND_heighttonormal_vector3", in, ctx);
    Vec3f expected = Vec3f(-1.0f / 16.0f, 0.0f, 1.0f).normalized();
    expected = expected * 0.5f + Vec3f(0.5f);
    return Test_IsClose(_GetVec3(out), expected, 1e-5f);
}

static bool TestBlurPropagatesTextureBlur() {
    ParamMap floatInputs;
    floatInputs["in"] = Value(0.375f);
    floatInputs["size"] = Value(0.2f);
    const NodeOutputMap constantOut = _Eval("ND_blur_float", floatInputs);
    if (!Test_IsClose(_GetFloat(constantOut), 0.375f)) {
        printf("    blur changed a constant input\n");
        return false;
    }

    NodeRegistry::RegisterBuiltinNodes();
    auto fn = NodeRegistry::GetInstance().Find(std::string("ND_blur_float"));
    if (!fn) return false;

    ParamMap connectedInputs;
    connectedInputs["size"] = Value(0.2f);
    connectedInputs["filtertype"] = Value(std::string("box"));
    connectedInputs.Add(
        AsSlotId("in"),
        nullptr,
        &_EvalHeightFromTextureBlurS,
        nullptr,
        -1,
        InvalidSlotId);

    ShadingContext ctx;

    NodeOutputMap out;
    fn(connectedInputs, ctx, &out);
    if (!Test_IsClose(_GetFloat(out), 0.2f, 1e-5f)) {
        printf("    blur did not propagate texture blur context: %f\n", _GetFloat(out));
        return false;
    }

    ParamMap imageInputs;
    imageInputs["file"] = Value(std::string("/tmp/blur.tx"));
    imageInputs["default"] = Value(Vec3f(0.0f));

    _TestTextureSystem textureSystem;
    textureSystem.nextResult = {
        Vec4f(0.25f, 0.5f, 0.75f, 1.0f),
        TextureSampleStatus::Ok};

    ShadingContext imageCtx;
    imageCtx.textureSystem = &textureSystem;
    imageCtx.textureBlur = Vec2f(0.12f, 0.34f);
    _EvalWithCtx("ND_image_color3", imageInputs, imageCtx);
    if (!Test_IsClose(textureSystem.lastRequest.blur, Vec2f(0.12f, 0.34f))) {
        printf("    image request did not receive texture blur\n");
        return false;
    }

    return true;
}

static bool TestBumpDefaultBasis() {
    NodeRegistry::RegisterBuiltinNodes();
    auto fn = NodeRegistry::GetInstance().Find(std::string("ND_bump_vector3"));
    if (!fn) return false;

    ParamMap in;
    in.Add(
        AsSlotId("height"),
        nullptr,
        &_EvalHeightFromTexcoordX,
        nullptr,
        -1,
        InvalidSlotId);

    ShadingContext ctx;
    ctx.normal = Vec3f(0.0f, 0.0f, 1.0f);
    ctx.tangent = Vec3f(1.0f, 0.0f, 0.0f);
    ctx.bitangent = Vec3f(0.0f, 1.0f, 0.0f);
    ctx.dPdu = Vec3f(1.0f, 0.0f, 0.0f);
    ctx.dPdv = Vec3f(0.0f, 1.0f, 0.0f);
    ctx.texcoord = Vec2f(0.25f, 0.5f);
    ctx.dudx = 1.0f;
    ctx.dvdy = 1.0f;

    NodeOutputMap out;
    fn(in, ctx, &out);

    Vec3f expected = Vec3f(-1.0f, 0.0f, 1.0f).normalized();
    if (!Test_IsClose(_GetVec3(out), expected, 1e-5f)) return false;

    ctx.normal = Vec3f(0.0f, 1.0f, 0.0f);
    ctx.tangent = Vec3f(1.0f, 0.0f, 0.0f);
    ctx.bitangent = Vec3f(0.0f, 0.0f, -1.0f);
    ctx.dPdu = ctx.tangent;
    ctx.dPdv = ctx.bitangent;

    out.Clear();
    fn(in, ctx, &out);

    expected = Vec3f(-1.0f, 1.0f, 0.0f).normalized();
    return Test_IsClose(_GetVec3(out), expected, 1e-5f);
}

static bool TestNormalMapVariants() {
    ShadingContext ctx;
    ctx.normal = Vec3f(0.0f, 1.0f, 0.0f);
    ctx.tangent = Vec3f(1.0f, 0.0f, 0.0f);
    ctx.bitangent = Vec3f(0.0f, 0.0f, -1.0f);

    ParamMap scalarInputs;
    scalarInputs["in"] = Value(Vec3f(0.25f, 0.75f, 1.0f));
    scalarInputs["scale"] = Value(0.5f);
    const Vec3f authoredNormal(0.0f, 0.0f, 1.0f);
    const Vec3f authoredTangent(0.0f, 1.0f, 0.0f);
    const Vec3f authoredBitangent(-1.0f, 0.0f, 0.0f);
    scalarInputs["normal"] = Value(authoredNormal);
    scalarInputs["tangent"] = Value(authoredTangent);
    scalarInputs["bitangent"] = Value(authoredBitangent);
    NodeOutputMap out =
        _EvalWithCtx("ND_normalmap_float", scalarInputs, ctx);
    Vec3f expected =
        (authoredTangent * -0.25f +
         authoredBitangent * 0.25f +
         authoredNormal).normalized();
    if (!Test_IsClose(_GetVec3(out), expected, 1e-5f)) return false;

    ParamMap vectorInputs;
    vectorInputs["in"] = Value(Vec3f(1.0f));
    vectorInputs["scale"] = Value(Vec2f(0.25f, 0.75f));
    out = _EvalWithCtx("ND_normalmap_vector2", vectorInputs, ctx);
    expected =
        (ctx.tangent * 0.25f +
         ctx.bitangent * 0.75f +
         ctx.normal).normalized();
    if (!Test_IsClose(_GetVec3(out), expected, 1e-5f)) return false;

    ParamMap zeroInputs;
    zeroInputs["in"] = Value(Vec3f(0.0f));
    out = _EvalWithCtx("ND_normalmap_float", zeroInputs, ctx);
    if (!Test_IsClose(_GetVec3(out), ctx.normal, 1e-5f)) return false;

    ParamMap defaultInputs;
    out = _EvalWithCtx("ND_normalmap_float", defaultInputs, ctx);
    return Test_IsClose(_GetVec3(out), ctx.normal, 1e-5f);
}

// ---------------------------------------------------------------------------
// Color node tests
// ---------------------------------------------------------------------------

static bool TestLuminance() {
    NodeRegistry::RegisterBuiltinNodes();
    auto fn = NodeRegistry::GetInstance().Find(std::string("ND_luminance_color3"));
    if (!fn) {
        printf("    ND_luminance_color3 not registered\n");
        return false;
    }
    ParamMap in;
    in["in"] = Value(Vec3f(1.0f, 1.0f, 1.0f));
    in["lumacoeffs"] = Value(Vec3f(0.2126f, 0.7152f, 0.0722f));
    NodeOutputMap out;
    ShadingContext ctx;
    fn(in, ctx, &out);
    const Vec3f whiteResult = _GetVec3(out);
    // Rec.709: 0.2126 + 0.7152 + 0.0722 = 1.0
    if (!Test_IsClose(whiteResult, Vec3f(1.0f), 0.01f)) {
        printf("    luminance of white: (%f, %f, %f) "
               "(expected ~(1.0, 1.0, 1.0))\n",
               whiteResult[0], whiteResult[1], whiteResult[2]);
        return false;
    }

    // Non-trivial color
    in["in"] = Value(Vec3f(0.5f, 0.0f, 0.0f));
    fn(in, ctx, &out);
    const Vec3f redResult = _GetVec3(out);
    constexpr float expected = 0.2126f * 0.5f;
    if (!Test_IsClose(redResult, Vec3f(expected), 0.001f)) {
        printf("    luminance of (0.5,0,0): (%f, %f, %f) "
               "(expected (%f, %f, %f))\n",
               redResult[0], redResult[1], redResult[2],
               expected, expected, expected);
        return false;
    }
    return true;
}

static bool TestColorCorrectUsesCanonicalNodeOrder() {
    ParamMap in;
    in["in"] = Value(Vec3f(1.0215262f, 0.12157982f, 0.3721071f));
    in["hue"] = Value(0.08f);
    in["saturation"] = Value(1.25f);
    in["gamma"] = Value(0.95f);
    in["lift"] = Value(0.02f);
    in["gain"] = Value(1.1f);
    in["contrast"] = Value(1.2f);
    in["contrastpivot"] = Value(0.5f);
    in["exposure"] = Value(0.15f);

    const NodeOutputMap out = _Eval("ND_colorcorrect_color3", in);
    const Vec3f expected(1.5882975f, 0.2611460f, -0.0461693f);
    return Test_IsClose(_GetVec3(out), expected, 1e-5f);
}

static bool TestColorTransformG22Rec709FallbackClamp() {
    ParamMap in;
    in["in"] = Value(Vec3f(-1.0f, 0.5f, 2.0f));
    const NodeOutputMap out = _Eval("ND_g22_rec709_to_lin_rec709_color3", in);
    const Vec3f expected(
        0.0f,
        std::pow(0.5f, 2.2f),
        std::pow(2.0f, 2.2f));
    return Test_IsClose(_GetVec3(out), expected, 1e-5f);
}

static bool TestColorTransformSrgbTextureFallbackPiecewise() {
    ParamMap in;
    in["in"] = Value(Vec3f(0.04045f, 0.5f, -0.1f));
    const NodeOutputMap out =
        _Eval("ND_srgb_texture_to_lin_rec709_color3", in);
    const Vec3f expected(
        _ApplySrgbTextureToLinearRec709Test(0.04045f),
        _ApplySrgbTextureToLinearRec709Test(0.5f),
        _ApplySrgbTextureToLinearRec709Test(-0.1f));
    return Test_IsClose(_GetVec3(out), expected, 1e-5f);
}

static bool TestColorTransformAcescgMatrix() {
    ParamMap in;
    in["in"] = Value(Vec3f(1.0f, 0.0f, 0.0f));
    const NodeOutputMap out = _Eval("ND_acescg_to_lin_rec709_color3", in);
    const Vec3f expected(
        1.705050992658f,
        -0.130256417507f,
        -0.024003356805f);
    return Test_IsClose(_GetVec3(out), expected, 1e-5f);
}

static bool TestColorTransformColor4PreservesAlpha() {
    ParamMap in;
    in["in"] = Value(Vec4f(1.0f, 0.0f, 0.0f, 0.25f));
    const NodeOutputMap out = _Eval("ND_acescg_to_lin_rec709_color4", in);
    const Vec4f expected(
        1.705050992658f,
        -0.130256417507f,
        -0.024003356805f,
        0.25f);
    return Test_IsClose(_GetVec4(out), expected, 1e-5f);
}

static bool TestColorTransformCallbackOverridesFallback() {
    ParamMap in;
    in["in"] = Value(Vec3f(0.1f, 0.2f, 0.3f));

    _TestColorTransformState state;
    state.outputValue = Vec3f(9.0f, 8.0f, 7.0f);

    ShadingContext ctx;
    ctx.colorTransform = &_TestColorTransformCallback;
    ctx.colorTransformUserData = &state;

    const NodeOutputMap out =
        _EvalWithCtx("ND_lin_displayp3_to_lin_rec709_color3", in, ctx);
    return Test_IsClose(_GetVec3(out), state.outputValue) &&
           state.callCount == 1 &&
           state.lastSourceColorSpace == "lin_displayp3" &&
           state.lastTargetColorSpace == "lin_rec709" &&
           Test_IsClose(state.lastInput, Vec3f(0.1f, 0.2f, 0.3f));
}

static bool TestColorTransformCallbackFallbackWhenUnhandled() {
    ParamMap in;
    in["in"] = Value(Vec3f(0.0f, 1.0f, 0.0f));

    _TestColorTransformState state;
    state.handled = false;

    ShadingContext ctx;
    ctx.colorTransform = &_TestColorTransformCallback;
    ctx.colorTransformUserData = &state;

    const NodeOutputMap out =
        _EvalWithCtx("ND_lin_displayp3_to_lin_rec709_color3", in, ctx);
    const Vec3f expected(
        -0.22492968f,
        1.04205894f,
        -0.07864794f);
    return state.callCount == 1 &&
           state.lastSourceColorSpace == "lin_displayp3" &&
           state.lastTargetColorSpace == "lin_rec709" &&
           Test_IsClose(_GetVec3(out), expected, 1e-5f);
}

static bool TestColorTransformCallbackReceivesClampedInput() {
    ParamMap in;
    in["in"] = Value(Vec3f(-0.5f, 0.25f, 1.0f));

    _TestColorTransformState state;
    state.outputValue = Vec3f(0.0f);

    ShadingContext ctx;
    ctx.colorTransform = &_TestColorTransformCallback;
    ctx.colorTransformUserData = &state;

    const NodeOutputMap out =
        _EvalWithCtx("ND_g22_ap1_to_lin_rec709_color3", in, ctx);
    return state.callCount == 1 &&
           state.lastSourceColorSpace == "g22_ap1" &&
           state.lastTargetColorSpace == "lin_rec709" &&
           Test_IsClose(state.lastInput, Vec3f(0.0f, 0.25f, 1.0f)) &&
           Test_IsClose(_GetVec3(out), Vec3f(0.0f));
}

static bool TestColorTransformDataModeBypassesCallbackAndFallback() {
    ParamMap in;
    const Vec3f authoredValue(-0.25f, 0.5f, 1.5f);
    in["in"] = Value(authoredValue);

    _TestColorTransformState state;
    state.outputValue = Vec3f(9.0f);

    ShadingContext ctx;
    ctx.colorTransform = &_TestColorTransformCallback;
    ctx.colorTransformUserData = &state;
    ctx.bypassColorTransforms = true;

    const NodeOutputMap out =
        _EvalWithCtx("ND_g22_ap1_to_lin_rec709_color3", in, ctx);
    return state.callCount == 0 &&
           Test_IsClose(_GetVec3(out), authoredValue);
}

// ---------------------------------------------------------------------------
// Param map tests
// ---------------------------------------------------------------------------

static bool TestParamMapCopyOnWriteForBorrowedValue() {
    ParamMap params;
    Value borrowed(1.0f);
    params.Add(AsSlotId("in"), &borrowed);

    params["in"] = Value(2.0f);

    if (!ValueHolds<float>(borrowed) ||
        !Test_IsClose(ValueGet<float>(borrowed), 1.0f)) {
        printf("    borrowed value was mutated\n");
        return false;
    }

    const Value* value = params.Find("in");
    if (!value || !ValueHolds<float>(*value)) {
        printf("    params['in'] missing after overwrite\n");
        return false;
    }

    return Test_IsClose(ValueGet<float>(*value), 2.0f);
}

// ---------------------------------------------------------------------------
// Registry tests
// ---------------------------------------------------------------------------

static bool TestNodeRegistryLookup() {
    NodeRegistry::RegisterBuiltinNodes();
    auto fn = NodeRegistry::GetInstance().Find(std::string("ND_add_float"));
    return fn != nullptr;
}

static bool TestNodeRegistryMissing() {
    NodeRegistry::RegisterBuiltinNodes();
    auto fn = NodeRegistry::GetInstance().Find(
        std::string("ND_nonexistent_node_xyz"));
    return fn == nullptr;
}

static bool TestNodeRegistryRepresentativeNodegroups() {
    NodeRegistry::RegisterBuiltinNodes();

    static const std::array<const char*, 10> nodeTypeIds = {{
        "ND_clamp_float",
        "ND_luminance_color3",
        "ND_g22_rec709_to_lin_rec709_color3",
        "ND_heighttonormal_vector3",
        "ND_gooch_shade",
        "ND_latlongimage",
        "ND_triplanarprojection_color3",
        "ND_constant_float",
        "ND_noise2d_float",
        "ND_noise3d_float",
    }};

    for (const char* nodeTypeId : nodeTypeIds) {
        auto fn = NodeRegistry::GetInstance().Find(std::string(nodeTypeId));
        if (!fn) {
            return false;
        }
    }

    return true;
}

// ---------------------------------------------------------------------------

void
Test_RegisterNodeTests()
{
    _REG(TestAddFloat);
    _REG(TestAddColor3);
    _REG(TestMultiplyFloat);
    _REG(TestSubtractFloat);
    _REG(TestDivideFloat);
    _REG(TestRun0052MathSemantics);
    _REG(TestDotSurfaceShaderPassThrough);
    _REG(TestTransformMatrixDropsHomogeneousOutput);
    _REG(TestAtan2UsesCanonicalInputs);
    _REG(TestClamp);
    _REG(TestMix);
    _REG(TestSmoothstep);
    _REG(TestSmoothstepOverloads);
    _REG(TestRemap);
    _REG(TestRemapOverloads);
    _REG(TestOpenPbrAnisotropy);
    _REG(TestCombineSeparateRoundtrip);
    _REG(TestSeparateColorOutputs);
    _REG(TestConvertColor4ToColor3);
    _REG(TestChannelConvertValues);
    _REG(TestChannelConversionRegistrations);
    _REG(TestMagnitudeVectorVariants);
    _REG(TestIfgreater);
    // Conditional (expanded)
    _REG(TestIfgreaterInteger);
    _REG(TestIfgreaterVector2);
    _REG(TestIfgreaterBoolOutput);
    _REG(TestIfgreaterIntComparison);
    _REG(TestIfEqualBoolComparison);
    _REG(TestSwitchFloat);
    _REG(TestSwitchIntegerWhich);
    _REG(TestLogicalOps);

    // Compositing
    _REG(TestCompositingPlus);
    _REG(TestCompositingMinus);
    _REG(TestCompositingBurn);
    _REG(TestCompositingDodge);
    _REG(TestCompositingScreen);
    _REG(TestCompositingOverlay);
    _REG(TestCompositingMixParam);
    _REG(TestPorterDuffOver);
    _REG(TestPorterDuffIn);
    _REG(TestPorterDuffDisjointover);
    _REG(TestPorterDuffMatte);
    _REG(TestPorterDuffOut);
    _REG(TestPremultUnpremult);
    _REG(TestInsideOutside);
    _REG(TestMixVecVariant);
    _REG(TestProceduralWorleyNoise2d);
    _REG(TestProceduralWorleyNoise3d);
    _REG(TestProceduralFractal2dSingleOctave);
    _REG(TestProceduralFractal3dColor);
    _REG(TestArtisticIor);
    _REG(TestBlackbody);
    _REG(TestProceduralUnifiedNoise2dCellRemap);
    _REG(TestProceduralUnifiedNoise3dCellRemap);
    _REG(TestProceduralRampLrAndSplitLr);
    _REG(TestProceduralRampTbAndSplitTb);
    _REG(TestProceduralRamp4AndRamp);
    _REG(TestProceduralPatterns);
    _REG(TestProceduralGridAndCrosshatch);
    _REG(TestProceduralRandomNodes);
    _REG(TestProceduralFlake2dCoverageZero);
    _REG(TestProceduralFlake3dCoverageZero);
    _REG(TestGeometricPosition);
    _REG(TestGeometricPositionWorldSpace);
    _REG(TestGeometricNormal);
    _REG(TestGeometricTangentUsesInterpolatedFrame);
    _REG(TestGeometricBitangentUsesInterpolatedFrame);
    _REG(TestGeometricTexcoordVector3);
    _REG(TestGeometricViewDirectionWorldSpace);
    _REG(TestGeometricViewDirectionRemainsNormalizedAcrossSpaces);
    _REG(TestFacingRatioDefaultsToViewDirectionAndNormal);
    _REG(TestFacingRatioSupportsFlagsAndAuthoredInputs);
    _REG(TestGoochShadeAddsSpecularHighlight);
    _REG(TestApplicationFrame);
    _REG(TestApplicationTime);
    _REG(TestTransformPointObjectToWorld);
    _REG(TestImageNodeUsesTextureSystem);
    _REG(TestImageNodeConstantWrapReturnsDefault);
    _REG(TestUsdUvTextureUsesNativeWrapAndColorSpaceSemantics);
    _REG(TestOnlyNativeUsdUvTextureRequestsAutomaticColorSpace);
    _REG(TestNativeUsdUvTextureDefaultsToAutomaticColorSpace);
    _REG(TestUsdUvTextureFallsBackToFileColorSpaceMetadata);
    _REG(TestMaterialXUsdUvTextureOutputsRgbaAndScaledFallback);
    _REG(TestTiledImageTransformsTexcoords);
    _REG(TestLatLongImageMapsViewdirToLatLongUv);
    _REG(TestLatLongImageReevaluatesConnectedViewdir);
    _REG(TestHexTiledImageBlendsThreeColorSamples);
    _REG(TestHexTiledImageColor4BlendsAlphaSeparately);
    _REG(TestGltfNormalMapSamplesAndTransformsToWorld);
    _REG(TestGltfNormalMapReevaluatesConnectedTransformInputs);
    _REG(TestGltfImageColor3SamplesAndAppliesFactor);
    _REG(TestGltfColorImageModulatesRgbAndAlpha);
    _REG(TestGltfIridescenceThicknessUsesGreenChannel);
    _REG(TestGltfAnisotropyImageDecodesDirectionAndStrength);
    _REG(TestGltfAnisotropyImageUsesSpecificationDefault);
    _REG(TestTriplanarProjectionColor3SamplesAxesAndBlends);
    _REG(TestTriplanarProjectionReevaluatesConnectedPosition);
    _REG(TestTriplanarProjectionDefaultsNormalToObjectSpace);
    _REG(TestBlurPropagatesTextureBlur);
    _REG(TestHeightToNormalDefaultTexcoord);
    _REG(TestHeightToNormalPreservesSmallDerivatives);
    _REG(TestBumpDefaultBasis);
    _REG(TestNormalMapVariants);
    _REG(TestLuminance);
    _REG(TestColorCorrectUsesCanonicalNodeOrder);
    _REG(TestColorTransformG22Rec709FallbackClamp);
    _REG(TestColorTransformSrgbTextureFallbackPiecewise);
    _REG(TestColorTransformAcescgMatrix);
    _REG(TestColorTransformColor4PreservesAlpha);
    _REG(TestColorTransformCallbackOverridesFallback);
    _REG(TestColorTransformCallbackFallbackWhenUnhandled);
    _REG(TestColorTransformCallbackReceivesClampedInput);
    _REG(TestColorTransformDataModeBypassesCallbackAndFallback);
    _REG(TestParamMapCopyOnWriteForBorrowedValue);
    _REG(TestNodeRegistryLookup);
    _REG(TestNodeRegistryMissing);
    _REG(TestNodeRegistryRepresentativeNodegroups);

    // Geompropvalue
    _REG(TestGeomPropValueFloat);
    _REG(TestGeomPropValueColor3);
    _REG(TestGeomPropValueDefault);
    _REG(TestGeomPropValueTypeMismatch);
    _REG(TestGeomPropValueEmptyName);
    _REG(TestGeomPropValueNotFound);
    _REG(TestGeomPropValueUniformString);
    _REG(TestGeomPropValueUniformDefault);
    _REG(TestGeomPropValueUniformInvalidHandles);
}

#undef _REG
