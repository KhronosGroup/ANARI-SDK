//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "mathNodes.h"

#include <renderer/materials/MaterialXCpp/nodeRegistry.h>
#include <renderer/materials/MaterialXCpp/nodes/helpers/mathHelpers.h>
#include <renderer/materials/MaterialXCpp/nodes/helpers/spaceHelpers.h>
#include <renderer/materials/MaterialXCpp/surfaceShaderUtils.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace mxcpp {

static const SlotName _kIn("in");
static const SlotName _kIn1("in1");
static const SlotName _kIn2("in2");
static const SlotName _kInx("inx");
static const SlotName _kIny("iny");
static const SlotName _kLow("low");
static const SlotName _kHigh("high");
static const SlotName _kOut("out");
static const SlotName _kPower("power");
static const SlotName _kAmount("amount");
static const SlotName _kNormal("normal");
static const SlotName _kIor("ior");
static const SlotName _kIn3("in3");
static const SlotName _kIn4("in4");
static const SlotName _kMat("mat");
static const SlotName _kAxis("axis");
static const SlotName _kTexcoord("texcoord");
static const SlotName _kPivot("pivot");
static const SlotName _kScale("scale");
static const SlotName _kRotate("rotate");
static const SlotName _kOffset("offset");
static const SlotName _kOperationOrder("operationorder");
static const SlotName _kFromspace("fromspace");
static const SlotName _kTospace("tospace");
static const SlotName _kRoughness("roughness");
static const SlotName _kAnisotropy("anisotropy");

// ---- Arithmetic templates ------------------------------------------------

template<typename T>
static void
_EvalAdd(const ParamMap& inputs, const ShadingContext&,
         NodeOutputMap* outputs)
{
    T a = Get<T>(inputs, _kIn1, Zero<T>());
    T b = Get<T>(inputs, _kIn2, Zero<T>());
    (*outputs)[_kOut] = Value(a + b);
}

template<typename T>
static void
_EvalSubtract(const ParamMap& inputs, const ShadingContext&,
              NodeOutputMap* outputs)
{
    T a = Get<T>(inputs, _kIn1, Zero<T>());
    T b = Get<T>(inputs, _kIn2, Zero<T>());
    (*outputs)[_kOut] = Value(a - b);
}

template<typename T>
static void
_EvalAddFA(const ParamMap& inputs, const ShadingContext&,
           NodeOutputMap* outputs)
{
    T result = Get<T>(inputs, _kIn1, Zero<T>());
    const float b = Get<float>(inputs, _kIn2, 0.0f);
    for (unsigned int i = 0; i < T::dimensions(); ++i) {
        result[i] += b;
    }
    (*outputs)[_kOut] = Value(result);
}

template<typename T>
static void
_EvalSubtractFA(const ParamMap& inputs, const ShadingContext&,
                NodeOutputMap* outputs)
{
    T result = Get<T>(inputs, _kIn1, Zero<T>());
    const float b = Get<float>(inputs, _kIn2, 0.0f);
    for (unsigned int i = 0; i < T::dimensions(); ++i) {
        result[i] -= b;
    }
    (*outputs)[_kOut] = Value(result);
}

template<typename T>
static void
_EvalMultiply(const ParamMap& inputs, const ShadingContext&,
              NodeOutputMap* outputs)
{
    T a = Get<T>(inputs, _kIn1, One<T>());
    T b = Get<T>(inputs, _kIn2, One<T>());
    (*outputs)[_kOut] = Value(CompMul(a, b));
}

// Multiply vector by scalar (FA = float argument)
template<typename T>
static void
_EvalMultiplyFA(const ParamMap& inputs, const ShadingContext&,
                NodeOutputMap* outputs)
{
    T a = Get<T>(inputs, _kIn1, One<T>());
    float b = Get<float>(inputs, _kIn2, 1.0f);
    (*outputs)[_kOut] = Value(a * b);
}

template<typename T>
static void
_EvalDivide(const ParamMap& inputs, const ShadingContext&,
            NodeOutputMap* outputs)
{
    T a = Get<T>(inputs, _kIn1, Zero<T>());
    T b = Get<T>(inputs, _kIn2, One<T>());
    (*outputs)[_kOut] = Value(CompDiv(a, b));
}


template<typename T>
static void
_EvalDivideFA(const ParamMap& inputs, const ShadingContext&,
              NodeOutputMap* outputs)
{
    T a = Get<T>(inputs, _kIn1, Zero<T>());
    float b = Get<float>(inputs, _kIn2, 1.0f);
    (*outputs)[_kOut] = Value(b != 0.0f ? a / b : Zero<T>());
}

template<typename MatrixT, int matrixDimension>
static MatrixT
_MatrixAddScalar(MatrixT matrix, float value)
{
    for (int row = 0; row < matrixDimension; ++row) {
        for (int col = 0; col < matrixDimension; ++col) {
            matrix[row][col] += value;
        }
    }
    return matrix;
}

template<typename MatrixT, int matrixDimension>
static MatrixT
_MatrixSubtractScalar(MatrixT matrix, float value)
{
    for (int row = 0; row < matrixDimension; ++row) {
        for (int col = 0; col < matrixDimension; ++col) {
            matrix[row][col] -= value;
        }
    }
    return matrix;
}

template<typename MatrixT, int matrixDimension>
static void
_EvalMatrixAddFA(const ParamMap& inputs, const ShadingContext&,
                 NodeOutputMap* outputs)
{
    const MatrixT a = Get<MatrixT>(inputs, _kIn1, One<MatrixT>());
    const float b = Get<float>(inputs, _kIn2, 0.0f);
    (*outputs)[_kOut] = Value(_MatrixAddScalar<MatrixT, matrixDimension>(a, b));
}

template<typename MatrixT, int matrixDimension>
static void
_EvalMatrixSubtractFA(const ParamMap& inputs, const ShadingContext&,
                      NodeOutputMap* outputs)
{
    const MatrixT a = Get<MatrixT>(inputs, _kIn1, One<MatrixT>());
    const float b = Get<float>(inputs, _kIn2, 0.0f);
    (*outputs)[_kOut] =
        Value(_MatrixSubtractScalar<MatrixT, matrixDimension>(a, b));
}

template<typename MatrixT>
static void
_EvalMatrixDivide(const ParamMap& inputs, const ShadingContext&,
                  NodeOutputMap* outputs)
{
    const MatrixT a = Get<MatrixT>(inputs, _kIn1, One<MatrixT>());
    const MatrixT b = Get<MatrixT>(inputs, _kIn2, One<MatrixT>());
    (*outputs)[_kOut] = Value(a * b.inverse());
}

static BsdfClosure
_MakeAddBsdfClosure(
    const BsdfClosure& in1Closure,
    const BsdfClosure& in2Closure)
{
    BsdfClosure closure;
    const Bsdf::NodeId in1 =
        AppendClosureTree(&closure.tree, in1Closure.tree);
    const Bsdf::NodeId in2 =
        AppendClosureTree(&closure.tree, in2Closure.tree);

    if (closure.tree.IsValid(in1) && closure.tree.IsValid(in2)) {
        Bsdf::AddData add;
        add.in1 = in1;
        add.in2 = in2;
        closure.tree.root = closure.tree.Add(add);
    } else if (closure.tree.IsValid(in1)) {
        closure.tree.root = in1;
    } else if (closure.tree.IsValid(in2)) {
        closure.tree.root = in2;
    }

    if (in1Closure.hasInteriorMedium) {
        closure.hasInteriorMedium = true;
        closure.interiorMedium = in1Closure.interiorMedium;
    } else if (in2Closure.hasInteriorMedium) {
        closure.hasInteriorMedium = true;
        closure.interiorMedium = in2Closure.interiorMedium;
    }

    return closure;
}

static BsdfClosure
_MakeMultiplyBsdfClosure(
    const BsdfClosure& inClosure,
    const Vec3f& weight)
{
    BsdfClosure closure = inClosure;
    if (closure.tree.Empty()) {
        return closure;
    }

    const Vec3f clampedWeight(
        std::clamp(weight[0], 0.0f, 1.0f),
        std::clamp(weight[1], 0.0f, 1.0f),
        std::clamp(weight[2], 0.0f, 1.0f));
    if (clampedWeight == Vec3f(1.0f)) {
        return closure;
    }

    Bsdf::MultiplyData multiply;
    multiply.input = closure.tree.root;
    multiply.weight = clampedWeight;
    closure.tree.root = closure.tree.Add(multiply);
    return closure;
}

static float
_MediumScatterWeight(const MediumProperties& medium)
{
    return std::max(0.0f, medium.scattering[0] + medium.scattering[1] +
                              medium.scattering[2]);
}

static VdfClosure
_AddVdfClosures(const VdfClosure& in1Closure, const VdfClosure& in2Closure)
{
    VdfClosure closure;
    closure.medium.absorption =
        in1Closure.medium.absorption + in2Closure.medium.absorption;
    closure.medium.scattering =
        in1Closure.medium.scattering + in2Closure.medium.scattering;

    const float weight1 = _MediumScatterWeight(in1Closure.medium);
    const float weight2 = _MediumScatterWeight(in2Closure.medium);
    const float weightSum = weight1 + weight2;
    if (weightSum > 0.0f) {
        closure.medium.anisotropy =
            (in1Closure.medium.anisotropy * weight1 +
             in2Closure.medium.anisotropy * weight2) / weightSum;
    } else if (!in1Closure.medium.IsVacuum()) {
        closure.medium.anisotropy = in1Closure.medium.anisotropy;
    } else {
        closure.medium.anisotropy = in2Closure.medium.anisotropy;
    }
    return closure;
}

static VdfClosure
_MultiplyVdfClosure(const VdfClosure& inClosure, const Vec3f& weight)
{
    VdfClosure closure = inClosure;
    closure.medium.absorption = CompMul(closure.medium.absorption, weight);
    closure.medium.scattering = CompMul(closure.medium.scattering, weight);
    return closure;
}

static void
_EvalAddBsdf(const ParamMap& inputs, const ShadingContext&,
             NodeOutputMap* outputs)
{
    (*outputs)[_kOut] = Value(_MakeAddBsdfClosure(
        Get<BsdfClosure>(inputs, _kIn1, BsdfClosure{}),
        Get<BsdfClosure>(inputs, _kIn2, BsdfClosure{})));
}

static void
_EvalAddEdf(const ParamMap& inputs, const ShadingContext&,
            NodeOutputMap* outputs)
{
    const UniformEdf in1 = Get<UniformEdf>(
        inputs, _kIn1, UniformEdf{Vec3f(0.0f)});
    const UniformEdf in2 = Get<UniformEdf>(
        inputs, _kIn2, UniformEdf{Vec3f(0.0f)});
    (*outputs)[_kOut] = Value(UniformEdf{in1.emittance + in2.emittance});
}

static void
_EvalAddVdf(const ParamMap& inputs, const ShadingContext&,
            NodeOutputMap* outputs)
{
    (*outputs)[_kOut] = Value(_AddVdfClosures(
        Get<VdfClosure>(inputs, _kIn1, VdfClosure{}),
        Get<VdfClosure>(inputs, _kIn2, VdfClosure{})));
}

static void
_EvalMultiplyBsdfC(const ParamMap& inputs, const ShadingContext&,
                   NodeOutputMap* outputs)
{
    (*outputs)[_kOut] = Value(_MakeMultiplyBsdfClosure(
        Get<BsdfClosure>(inputs, _kIn1, BsdfClosure{}),
        Get<Vec3f>(inputs, _kIn2, Vec3f(1.0f))));
}

static void
_EvalMultiplyBsdfF(const ParamMap& inputs, const ShadingContext&,
                   NodeOutputMap* outputs)
{
    (*outputs)[_kOut] = Value(_MakeMultiplyBsdfClosure(
        Get<BsdfClosure>(inputs, _kIn1, BsdfClosure{}),
        Vec3f(Get<float>(inputs, _kIn2, 1.0f))));
}

static void
_EvalMultiplyEdfC(const ParamMap& inputs, const ShadingContext&,
                  NodeOutputMap* outputs)
{
    const UniformEdf in = Get<UniformEdf>(
        inputs, _kIn1, UniformEdf{Vec3f(0.0f)});
    const Vec3f weight = Get<Vec3f>(inputs, _kIn2, Vec3f(1.0f));
    (*outputs)[_kOut] = Value(UniformEdf{CompMul(in.emittance, weight)});
}

static void
_EvalMultiplyEdfF(const ParamMap& inputs, const ShadingContext&,
                  NodeOutputMap* outputs)
{
    const UniformEdf in = Get<UniformEdf>(
        inputs, _kIn1, UniformEdf{Vec3f(0.0f)});
    const float weight = Get<float>(inputs, _kIn2, 1.0f);
    (*outputs)[_kOut] = Value(UniformEdf{in.emittance * weight});
}

static void
_EvalMultiplyVdfC(const ParamMap& inputs, const ShadingContext&,
                  NodeOutputMap* outputs)
{
    (*outputs)[_kOut] = Value(_MultiplyVdfClosure(
        Get<VdfClosure>(inputs, _kIn1, VdfClosure{}),
        Get<Vec3f>(inputs, _kIn2, Vec3f(1.0f))));
}

static void
_EvalMultiplyVdfF(const ParamMap& inputs, const ShadingContext&,
                  NodeOutputMap* outputs)
{
    (*outputs)[_kOut] = Value(_MultiplyVdfClosure(
        Get<VdfClosure>(inputs, _kIn1, VdfClosure{}),
        Vec3f(Get<float>(inputs, _kIn2, 1.0f))));
}

static void
_EvalModuloFloat(const ParamMap& inputs, const ShadingContext&,
                 NodeOutputMap* outputs)
{
    float a = Get<float>(inputs, _kIn1, 0.0f);
    float b = Get<float>(inputs, _kIn2, 1.0f);
    (*outputs)[_kOut] = Value(b != 0.0f ? a - b * std::floor(a / b) : 0.0f);
}

template<typename T>
static void
_EvalModuloVector(const ParamMap& inputs, const ShadingContext&,
                  NodeOutputMap* outputs)
{
    const T a = Get<T>(inputs, _kIn1, T(0.0f));
    const T b = Get<T>(inputs, _kIn2, T(1.0f));
    T result;
    for (unsigned int i = 0; i < T::dimensions(); ++i) {
        result[i] = b[i] != 0.0f ? a[i] - b[i] * std::floor(a[i] / b[i]) : 0.0f;
    }
    (*outputs)[_kOut] = Value(result);
}

template<typename T>
static void
_EvalModuloVectorFA(const ParamMap& inputs, const ShadingContext&,
                    NodeOutputMap* outputs)
{
    const T a = Get<T>(inputs, _kIn1, T(0.0f));
    const float b = Get<float>(inputs, _kIn2, 1.0f);
    T result;
    for (unsigned int i = 0; i < T::dimensions(); ++i) {
        result[i] = b != 0.0f ? a[i] - b * std::floor(a[i] / b) : 0.0f;
    }
    (*outputs)[_kOut] = Value(result);
}

template<typename T>
static void
_EvalClamp(const ParamMap& inputs, const ShadingContext&,
           NodeOutputMap* outputs)
{
    const T v = Get<T>(inputs, _kIn, Zero<T>());
    const T lo = Get<T>(inputs, _kLow, Zero<T>());
    const T hi = Get<T>(inputs, _kHigh, One<T>());
    (*outputs)[_kOut] = Value(ClampValue(v, lo, hi));
}

template<typename T>
static void
_EvalClampFA(const ParamMap& inputs, const ShadingContext&,
             NodeOutputMap* outputs)
{
    const T v = Get<T>(inputs, _kIn, Zero<T>());
    const float lo = Get<float>(inputs, _kLow, 0.0f);
    const float hi = Get<float>(inputs, _kHigh, 1.0f);
    (*outputs)[_kOut] = Value(ClampValue(v, T(lo), T(hi)));
}

static void
_EvalMinFloat(const ParamMap& inputs, const ShadingContext&,
              NodeOutputMap* outputs)
{
    const float a = Get<float>(inputs, _kIn1, 0.0f);
    const float b = Get<float>(inputs, _kIn2, 0.0f);
    (*outputs)[_kOut] = Value(std::min(a, b));
}

static void
_EvalMaxFloat(const ParamMap& inputs, const ShadingContext&,
              NodeOutputMap* outputs)
{
    const float a = Get<float>(inputs, _kIn1, 0.0f);
    const float b = Get<float>(inputs, _kIn2, 0.0f);
    (*outputs)[_kOut] = Value(std::max(a, b));
}

template<typename T>
static void
_EvalMinVector(const ParamMap& inputs, const ShadingContext&,
               NodeOutputMap* outputs)
{
    const T a = Get<T>(inputs, _kIn1, T(0.0f));
    const T b = Get<T>(inputs, _kIn2, T(0.0f));
    T result;
    for (unsigned int i = 0; i < T::dimensions(); ++i) {
        result[i] = std::min(a[i], b[i]);
    }
    (*outputs)[_kOut] = Value(result);
}

template<typename T>
static void
_EvalMinVectorFA(const ParamMap& inputs, const ShadingContext&,
                 NodeOutputMap* outputs)
{
    const T a = Get<T>(inputs, _kIn1, T(0.0f));
    const float b = Get<float>(inputs, _kIn2, 0.0f);
    T result;
    for (unsigned int i = 0; i < T::dimensions(); ++i) {
        result[i] = std::min(a[i], b);
    }
    (*outputs)[_kOut] = Value(result);
}

template<typename T>
static void
_EvalMaxVector(const ParamMap& inputs, const ShadingContext&,
               NodeOutputMap* outputs)
{
    const T a = Get<T>(inputs, _kIn1, T(0.0f));
    const T b = Get<T>(inputs, _kIn2, T(0.0f));
    T result;
    for (unsigned int i = 0; i < T::dimensions(); ++i) {
        result[i] = std::max(a[i], b[i]);
    }
    (*outputs)[_kOut] = Value(result);
}

template<typename T>
static void
_EvalMaxVectorFA(const ParamMap& inputs, const ShadingContext&,
                 NodeOutputMap* outputs)
{
    const T a = Get<T>(inputs, _kIn1, T(0.0f));
    const float b = Get<float>(inputs, _kIn2, 0.0f);
    T result;
    for (unsigned int i = 0; i < T::dimensions(); ++i) {
        result[i] = std::max(a[i], b);
    }
    (*outputs)[_kOut] = Value(result);
}

template<typename T>
static void
_EvalMinComponent(const ParamMap& inputs, const ShadingContext&,
                  NodeOutputMap* outputs)
{
    const T v = Get<T>(inputs, _kIn, Zero<T>());
    float result = v[0];
    for (unsigned int i = 1; i < T::dimensions(); ++i) {
        result = std::min(result, v[i]);
    }
    (*outputs)[_kOut] = Value(result);
}

template<typename T>
static void
_EvalMaxComponent(const ParamMap& inputs, const ShadingContext&,
                  NodeOutputMap* outputs)
{
    const T v = Get<T>(inputs, _kIn, Zero<T>());
    float result = v[0];
    for (unsigned int i = 1; i < T::dimensions(); ++i) {
        result = std::max(result, v[i]);
    }
    (*outputs)[_kOut] = Value(result);
}

// ---- Unary math ----------------------------------------------------------

static void
_EvalAbsvalFloat(const ParamMap& inputs, const ShadingContext&,
                 NodeOutputMap* outputs)
{
    float v = Get<float>(inputs, _kIn, 0.0f);
    (*outputs)[_kOut] = Value(std::fabs(v));
}

template<typename T>
static void
_EvalAbsvalVector(const ParamMap& inputs, const ShadingContext&,
                  NodeOutputMap* outputs)
{
    const T v = Get<T>(inputs, _kIn, T(0.0f));
    T result;
    for (unsigned int i = 0; i < T::dimensions(); ++i) {
        result[i] = std::fabs(v[i]);
    }
    (*outputs)[_kOut] = Value(result);
}

static void
_EvalSign(const ParamMap& inputs, const ShadingContext&,
          NodeOutputMap* outputs)
{
    float v = Get<float>(inputs, _kIn, 0.0f);
    (*outputs)[_kOut] = Value(v > 0.0f ? 1.0f : (v < 0.0f ? -1.0f : 0.0f));
}

template<typename T>
static void
_EvalSignVector(const ParamMap& inputs, const ShadingContext&,
                NodeOutputMap* outputs)
{
    const T v = Get<T>(inputs, _kIn, T(0.0f));
    T result;
    for (unsigned int i = 0; i < T::dimensions(); ++i) {
        result[i] = v[i] > 0.0f ? 1.0f : (v[i] < 0.0f ? -1.0f : 0.0f);
    }
    (*outputs)[_kOut] = Value(result);
}

#define DEFINE_UNARY_VECTOR_EVALUATOR(name, expression)                     \
template<typename T>                                                        \
static void name(const ParamMap& inputs, const ShadingContext&,              \
                 NodeOutputMap* outputs)                                     \
{                                                                            \
    const T v = Get<T>(inputs, _kIn, T(0.0f));                               \
    T result;                                                                \
    for (unsigned int i = 0; i < T::dimensions(); ++i) {                     \
        result[i] = expression;                                              \
    }                                                                        \
    (*outputs)[_kOut] = Value(result);                                        \
}

DEFINE_UNARY_VECTOR_EVALUATOR(_EvalFloorVector, std::floor(v[i]))
DEFINE_UNARY_VECTOR_EVALUATOR(_EvalCeilVector, std::ceil(v[i]))
DEFINE_UNARY_VECTOR_EVALUATOR(_EvalRoundVector, std::round(v[i]))
DEFINE_UNARY_VECTOR_EVALUATOR(_EvalSqrtVector, std::sqrt(std::max(0.0f, v[i])))
DEFINE_UNARY_VECTOR_EVALUATOR(_EvalLnVector, v[i] > 0.0f ? std::log(v[i]) : 0.0f)
DEFINE_UNARY_VECTOR_EVALUATOR(_EvalExpVector, std::exp(v[i]))
DEFINE_UNARY_VECTOR_EVALUATOR(_EvalSinVector, std::sin(v[i]))
DEFINE_UNARY_VECTOR_EVALUATOR(_EvalCosVector, std::cos(v[i]))
DEFINE_UNARY_VECTOR_EVALUATOR(_EvalTanVector, std::tan(v[i]))
DEFINE_UNARY_VECTOR_EVALUATOR(_EvalAsinVector, std::abs(v[i]) <= 1.0f ? std::asin(v[i]) : 0.0f)
DEFINE_UNARY_VECTOR_EVALUATOR(_EvalAcosVector, std::abs(v[i]) <= 1.0f ? std::acos(v[i]) : 0.0f)

#undef DEFINE_UNARY_VECTOR_EVALUATOR

static void
_EvalFloor(const ParamMap& inputs, const ShadingContext&,
           NodeOutputMap* outputs)
{
    float v = Get<float>(inputs, _kIn, 0.0f);
    (*outputs)[_kOut] = Value(std::floor(v));
}

static void
_EvalCeil(const ParamMap& inputs, const ShadingContext&,
          NodeOutputMap* outputs)
{
    float v = Get<float>(inputs, _kIn, 0.0f);
    (*outputs)[_kOut] = Value(std::ceil(v));
}

static void
_EvalRound(const ParamMap& inputs, const ShadingContext&,
           NodeOutputMap* outputs)
{
    float v = Get<float>(inputs, _kIn, 0.0f);
    (*outputs)[_kOut] = Value(std::round(v));
}

static void
_EvalFloorInteger(const ParamMap& inputs, const ShadingContext&,
                  NodeOutputMap* outputs)
{
    const float v = Get<float>(inputs, _kIn, 0.0f);
    (*outputs)[_kOut] = Value(static_cast<int>(std::floor(v)));
}

static void
_EvalCeilInteger(const ParamMap& inputs, const ShadingContext&,
                 NodeOutputMap* outputs)
{
    const float v = Get<float>(inputs, _kIn, 0.0f);
    (*outputs)[_kOut] = Value(static_cast<int>(std::ceil(v)));
}

static void
_EvalRoundInteger(const ParamMap& inputs, const ShadingContext&,
                  NodeOutputMap* outputs)
{
    const float v = Get<float>(inputs, _kIn, 0.0f);
    (*outputs)[_kOut] = Value(static_cast<int>(std::round(v)));
}

static void
_EvalPower(const ParamMap& inputs, const ShadingContext&,
           NodeOutputMap* outputs)
{
    float base = Get<float>(inputs, _kIn1, 0.0f);
    float exp  = Get<float>(inputs, _kIn2, 1.0f);
    const float result = std::pow(base, exp);
    (*outputs)[_kOut] = Value(std::isfinite(result) ? result : 0.0f);
}

template<typename T>
static void
_EvalPowerVector(const ParamMap& inputs, const ShadingContext&,
                 NodeOutputMap* outputs)
{
    const T base = Get<T>(inputs, _kIn1, T(0.0f));
    const T exponent = Get<T>(inputs, _kIn2, T(1.0f));
    T result;
    for (unsigned int i = 0; i < T::dimensions(); ++i) {
        const float value = std::pow(base[i], exponent[i]);
        result[i] = std::isfinite(value) ? value : 0.0f;
    }
    (*outputs)[_kOut] = Value(result);
}

template<typename T>
static void
_EvalPowerFA(const ParamMap& inputs, const ShadingContext&,
             NodeOutputMap* outputs)
{
    const T base = Get<T>(inputs, _kIn1, T(0.0f));
    const float exponent = Get<float>(inputs, _kIn2, 1.0f);
    T result;
    for (unsigned int i = 0; i < T::dimensions(); ++i) {
        const float value = std::pow(base[i], exponent);
        result[i] = std::isfinite(value) ? value : 0.0f;
    }
    (*outputs)[_kOut] = Value(result);
}

static void
_EvalSqrt(const ParamMap& inputs, const ShadingContext&,
          NodeOutputMap* outputs)
{
    float v = Get<float>(inputs, _kIn, 0.0f);
    (*outputs)[_kOut] = Value(std::sqrt(std::max(0.0f, v)));
}

static void
_EvalLn(const ParamMap& inputs, const ShadingContext&,
        NodeOutputMap* outputs)
{
    float v = Get<float>(inputs, _kIn, 1.0f);
    (*outputs)[_kOut] = Value(v > 0.0f ? std::log(v) : 0.0f);
}

static void
_EvalExp(const ParamMap& inputs, const ShadingContext&,
         NodeOutputMap* outputs)
{
    float v = Get<float>(inputs, _kIn, 0.0f);
    (*outputs)[_kOut] = Value(std::exp(v));
}

template<typename T>
static void
_EvalNegate(const ParamMap& inputs, const ShadingContext&,
            NodeOutputMap* outputs)
{
    T v = Get<T>(inputs, _kIn, Zero<T>());
    (*outputs)[_kOut] = Value(-v);
}

template<typename T>
static void
_EvalInvert(const ParamMap& inputs, const ShadingContext&,
            NodeOutputMap* outputs)
{
    T v = Get<T>(inputs, _kIn, Zero<T>());
    T amount = Get<T>(inputs, _kAmount, One<T>());
    (*outputs)[_kOut] = Value(amount - v);
}

// Scalar specialization for invert
template<>
void
_EvalInvert<float>(const ParamMap& inputs, const ShadingContext&,
                   NodeOutputMap* outputs)
{
    float v = Get<float>(inputs, _kIn, 0.0f);
    float amount = Get<float>(inputs, _kAmount, 1.0f);
    (*outputs)[_kOut] = Value(amount - v);
}

template<typename T>
static void
_EvalInvertFA(const ParamMap& inputs, const ShadingContext&,
              NodeOutputMap* outputs)
{
    T v = Get<T>(inputs, _kIn, Zero<T>());
    float amount = Get<float>(inputs, _kAmount, 1.0f);
    (*outputs)[_kOut] = Value(T(amount) - v);
}

// ---- Trigonometric -------------------------------------------------------

static void _EvalSin(const ParamMap& in, const ShadingContext&,
                     NodeOutputMap* out) {
    (*out)[_kOut] = Value(std::sin(Get<float>(in, _kIn, 0.0f)));
}
static void _EvalCos(const ParamMap& in, const ShadingContext&,
                     NodeOutputMap* out) {
    (*out)[_kOut] = Value(std::cos(Get<float>(in, _kIn, 0.0f)));
}
static void _EvalTan(const ParamMap& in, const ShadingContext&,
                     NodeOutputMap* out) {
    (*out)[_kOut] = Value(std::tan(Get<float>(in, _kIn, 0.0f)));
}
static void _EvalAsin(const ParamMap& in, const ShadingContext&,
                      NodeOutputMap* out) {
    float v = Get<float>(in, _kIn, 0.0f);
    (*out)[_kOut] = Value(std::abs(v) <= 1.0f ? std::asin(v) : 0.0f);
}
static void _EvalAcos(const ParamMap& in, const ShadingContext&,
                      NodeOutputMap* out) {
    float v = Get<float>(in, _kIn, 0.0f);
    (*out)[_kOut] = Value(std::abs(v) <= 1.0f ? std::acos(v) : 0.0f);
}
static void _EvalAtan2(const ParamMap& in, const ShadingContext&,
                       NodeOutputMap* out) {
    float y = Get<float>(in, _kIny, 0.0f);
    float x = Get<float>(in, _kInx, 1.0f);
    (*out)[_kOut] = Value(std::atan2(y, x));
}

template<typename T>
static void
_EvalAtan2Vector(const ParamMap& inputs, const ShadingContext&,
                 NodeOutputMap* outputs)
{
    const T y = Get<T>(inputs, _kIny, T(0.0f));
    const T x = Get<T>(inputs, _kInx, T(1.0f));
    T result;
    for (unsigned int i = 0; i < T::dimensions(); ++i) {
        result[i] = std::atan2(y[i], x[i]);
    }
    (*outputs)[_kOut] = Value(result);
}

// ---- Vector operations ---------------------------------------------------

template<typename T>
static void
_EvalDotProduct(const ParamMap& inputs, const ShadingContext&,
                NodeOutputMap* outputs)
{
    T a = Get<T>(inputs, _kIn1, T(0.0f));
    T b = Get<T>(inputs, _kIn2, T(0.0f));
    (*outputs)[_kOut] = Value(Dot(a, b));
}

static void
_EvalCrossProduct(const ParamMap& inputs, const ShadingContext&,
                  NodeOutputMap* outputs)
{
    Vec3f a = Get<Vec3f>(inputs, _kIn1, Vec3f(0.0f));
    Vec3f b = Get<Vec3f>(inputs, _kIn2, Vec3f(0.0f));
    (*outputs)[_kOut] = Value(Cross(a, b));
}

static void
_EvalNormalize(const ParamMap& inputs, const ShadingContext&,
               NodeOutputMap* outputs)
{
    Vec3f v = Get<Vec3f>(inputs, _kIn, Vec3f(0.0f, 0.0f, 1.0f));
    float len = v.length();
    (*outputs)[_kOut] = Value(len > 0.0f ? v / len : Vec3f(0.0f));
}

template<typename T>
static void
_EvalNormalizeVector(const ParamMap& inputs, const ShadingContext&,
                     NodeOutputMap* outputs)
{
    T v = Get<T>(inputs, _kIn, T(0.0f));
    float len = v.length();
    (*outputs)[_kOut] = Value(len > 0.0f ? v / len : T(0.0f));
}

template<typename T>
static void
_EvalMagnitude(const ParamMap& inputs, const ShadingContext&,
               NodeOutputMap* outputs)
{
    T v = Get<T>(inputs, _kIn, T(0.0f));
    (*outputs)[_kOut] = Value(v.length());
}

// ---- safepower -----------------------------------------------------------
// sign(in1) * pow(abs(in1), in2) — safe for negative base values.

static inline float
_SafePow(float base, float exp)
{
    return (base < 0.0f ? -1.0f : (base > 0.0f ? 1.0f : 0.0f))
         * std::pow(std::fabs(base), exp);
}

static void
_EvalSafePowerFloat(const ParamMap& inputs, const ShadingContext&,
                    NodeOutputMap* outputs)
{
    float a = Get<float>(inputs, _kIn1, 0.0f);
    float b = Get<float>(inputs, _kIn2, 1.0f);
    (*outputs)[_kOut] = Value(_SafePow(a, b));
}

template<typename T>
static void
_EvalSafePower(const ParamMap& inputs, const ShadingContext&,
               NodeOutputMap* outputs)
{
    T a = Get<T>(inputs, _kIn1, Zero<T>());
    T b = Get<T>(inputs, _kIn2, One<T>());
    T result;
    for (int i = 0; i < T::dimensions(); ++i) {
        result[i] = _SafePow(a[i], b[i]);
    }
    (*outputs)[_kOut] = Value(result);
}

template<typename T>
static void
_EvalSafePowerFA(const ParamMap& inputs, const ShadingContext&,
                 NodeOutputMap* outputs)
{
    T a = Get<T>(inputs, _kIn1, Zero<T>());
    float b = Get<float>(inputs, _kIn2, 1.0f);
    T result;
    for (int i = 0; i < T::dimensions(); ++i) {
        result[i] = _SafePow(a[i], b);
    }
    (*outputs)[_kOut] = Value(result);
}

// ---- fract ---------------------------------------------------------------
// in - floor(in)

static void
_EvalFractFloat(const ParamMap& inputs, const ShadingContext&,
                NodeOutputMap* outputs)
{
    float v = Get<float>(inputs, _kIn, 0.0f);
    (*outputs)[_kOut] = Value(v - std::floor(v));
}

template<typename T>
static void
_EvalFract(const ParamMap& inputs, const ShadingContext&,
           NodeOutputMap* outputs)
{
    T v = Get<T>(inputs, _kIn, Zero<T>());
    T result;
    for (int i = 0; i < T::dimensions(); ++i) {
        result[i] = v[i] - std::floor(v[i]);
    }
    (*outputs)[_kOut] = Value(result);
}

// ---- distance ------------------------------------------------------------
// length(in1 - in2)

template<typename T>
static void
_EvalDistance(const ParamMap& inputs, const ShadingContext&,
             NodeOutputMap* outputs)
{
    T a = Get<T>(inputs, _kIn1, Zero<T>());
    T b = Get<T>(inputs, _kIn2, Zero<T>());
    (*outputs)[_kOut] = Value((a - b).length());
}

// ---- reflect -------------------------------------------------------------
// in - 2 * dot(in, normal) * normal

static void
_EvalReflect(const ParamMap& inputs, const ShadingContext& ctx,
             NodeOutputMap* outputs)
{
    Vec3f I = Get<Vec3f>(inputs, _kIn, Vec3f(1.0f, 0.0f, 0.0f));
    const Vec3f normalInput = Get<Vec3f>(inputs, _kNormal, ctx.normal);
    (*outputs)[_kOut] = Value(I - 2.0f * Dot(I, normalInput) * normalInput);
}

// ---- refract -------------------------------------------------------------
// Snell's law refraction

static void
_EvalRefract(const ParamMap& inputs, const ShadingContext& ctx,
             NodeOutputMap* outputs)
{
    Vec3f I = Get<Vec3f>(inputs, _kIn, Vec3f(1.0f, 0.0f, 0.0f));
    const Vec3f normalInput = Get<Vec3f>(inputs, _kNormal, ctx.normal);
    float ior = Get<float>(inputs, _kIor, 1.0f);
    const float normalDotIncident = Dot(normalInput, I);
    float k = 1.0f - ior * ior * (1.0f - normalDotIncident * normalDotIncident);
    if (k < 0.0f) {
        (*outputs)[_kOut] = Value(Vec3f(0.0f));
    } else {
        (*outputs)[_kOut] = Value(
            ior * I - (ior * normalDotIncident + std::sqrt(k)) * normalInput);
    }
}

// ---- creatematrix --------------------------------------------------------

static void
_EvalCreateMatrixVec3M33(const ParamMap& inputs, const ShadingContext&,
                         NodeOutputMap* outputs)
{
    Vec3f r0 = Get<Vec3f>(inputs, _kIn1, Vec3f(1.0f, 0.0f, 0.0f));
    Vec3f r1 = Get<Vec3f>(inputs, _kIn2, Vec3f(0.0f, 1.0f, 0.0f));
    Vec3f r2 = Get<Vec3f>(inputs, _kIn3, Vec3f(0.0f, 0.0f, 1.0f));
    Mat3f m;
    m[0][0] = r0[0]; m[0][1] = r0[1]; m[0][2] = r0[2];
    m[1][0] = r1[0]; m[1][1] = r1[1]; m[1][2] = r1[2];
    m[2][0] = r2[0]; m[2][1] = r2[1]; m[2][2] = r2[2];
    (*outputs)[_kOut] = Value(m);
}

static void
_EvalCreateMatrixVec3M44(const ParamMap& inputs, const ShadingContext&,
                         NodeOutputMap* outputs)
{
    Vec3f r0 = Get<Vec3f>(inputs, _kIn1, Vec3f(1.0f, 0.0f, 0.0f));
    Vec3f r1 = Get<Vec3f>(inputs, _kIn2, Vec3f(0.0f, 1.0f, 0.0f));
    Vec3f r2 = Get<Vec3f>(inputs, _kIn3, Vec3f(0.0f, 0.0f, 1.0f));
    Vec3f r3 = Get<Vec3f>(inputs, _kIn4, Vec3f(0.0f, 0.0f, 0.0f));
    Mat4f m(r0[0], r0[1], r0[2], 0.0f,
            r1[0], r1[1], r1[2], 0.0f,
            r2[0], r2[1], r2[2], 0.0f,
            r3[0], r3[1], r3[2], 1.0f);
    (*outputs)[_kOut] = Value(m);
}

static void
_EvalCreateMatrixVec4M44(const ParamMap& inputs, const ShadingContext&,
                         NodeOutputMap* outputs)
{
    Vec4f r0 = Get<Vec4f>(inputs, _kIn1, Vec4f(1.0f, 0.0f, 0.0f, 0.0f));
    Vec4f r1 = Get<Vec4f>(inputs, _kIn2, Vec4f(0.0f, 1.0f, 0.0f, 0.0f));
    Vec4f r2 = Get<Vec4f>(inputs, _kIn3, Vec4f(0.0f, 0.0f, 1.0f, 0.0f));
    Vec4f r3 = Get<Vec4f>(inputs, _kIn4, Vec4f(0.0f, 0.0f, 0.0f, 1.0f));
    Mat4f m(r0[0], r0[1], r0[2], r0[3],
            r1[0], r1[1], r1[2], r1[3],
            r2[0], r2[1], r2[2], r2[3],
            r3[0], r3[1], r3[2], r3[3]);
    (*outputs)[_kOut] = Value(m);
}

// ---- transformpoint / transformvector / transformnormal -------------------

template<ShadingContext::TransformSpaceType Type>
static void
_EvalTransformNamedSpace(const ParamMap& inputs, const ShadingContext& ctx,
                         NodeOutputMap* outputs)
{
    const Vec3f v = Get<Vec3f>(inputs, _kIn, Vec3f(0.0f));
    const std::string fromSpace =
        Get<std::string>(inputs, _kFromspace, std::string());
    const std::string toSpace =
        Get<std::string>(inputs, _kTospace, std::string());

    Vec3f result = v;
    TransformNamedVec3(ctx, fromSpace, toSpace, Type, v, &result);
    (*outputs)[_kOut] = Value(result);
}

// ---- transformmatrix -----------------------------------------------------

static void
_EvalTransformMatrixVec2M3(const ParamMap& inputs, const ShadingContext&,
                           NodeOutputMap* outputs)
{
    Vec2f v = Get<Vec2f>(inputs, _kIn, Vec2f(0.0f));
    Mat3f m;
    const Value* value = inputs.Find(_kMat);
    if (value && ValueHolds<Mat3f>(*value)) {
        m = ValueGet<Mat3f>(*value);
    }
    // MaterialX uses an implicit homogeneous one, then discards the third
    // component without perspective division.
    (*outputs)[_kOut] = Value(Vec2f(
        v[0] * m[0][0] + v[1] * m[1][0] + m[2][0],
        v[0] * m[0][1] + v[1] * m[1][1] + m[2][1]));
}

static void
_EvalTransformMatrixVec3M3(const ParamMap& inputs, const ShadingContext&,
                           NodeOutputMap* outputs)
{
    Vec3f v = Get<Vec3f>(inputs, _kIn, Vec3f(0.0f));
    Mat3f m;
    const Value* value = inputs.Find(_kMat);
    if (value && ValueHolds<Mat3f>(*value)) {
        m = ValueGet<Mat3f>(*value);
    }
    // Manual 3x3 matrix * vec3 (Imath Matrix33::multVecMatrix is Vec2 only).
    Vec3f r;
    for (int i = 0; i < 3; ++i) {
        r[i] = v[0] * m[0][i] + v[1] * m[1][i] + v[2] * m[2][i];
    }
    (*outputs)[_kOut] = Value(r);
}

static void
_EvalTransformMatrixVec3M4(const ParamMap& inputs, const ShadingContext&,
                           NodeOutputMap* outputs)
{
    Vec3f v = Get<Vec3f>(inputs, _kIn, Vec3f(0.0f));
    Mat4f m;
    const Value* value = inputs.Find(_kMat);
    if (value && ValueHolds<Mat4f>(*value)) {
        m = ValueGet<Mat4f>(*value);
    }
    // Likewise, discard W without applying perspective division.
    (*outputs)[_kOut] = Value(Vec3f(
        v[0] * m[0][0] + v[1] * m[1][0] + v[2] * m[2][0] + m[3][0],
        v[0] * m[0][1] + v[1] * m[1][1] + v[2] * m[2][1] + m[3][1],
        v[0] * m[0][2] + v[1] * m[1][2] + v[2] * m[2][2] + m[3][2]));
}

static void
_EvalTransformMatrixVec4M4(const ParamMap& inputs, const ShadingContext&,
                           NodeOutputMap* outputs)
{
    Vec4f v = Get<Vec4f>(inputs, _kIn, Vec4f(0.0f));
    Mat4f m;
    const Value* value = inputs.Find(_kMat);
    if (value && ValueHolds<Mat4f>(*value)) {
        m = ValueGet<Mat4f>(*value);
    }
    // Manual 4x4 * vec4 multiply
    Vec4f r;
    for (int i = 0; i < 4; ++i) {
        r[i] = v[0]*m[0][i] + v[1]*m[1][i] + v[2]*m[2][i] + v[3]*m[3][i];
    }
    (*outputs)[_kOut] = Value(r);
}

// ---- transpose -----------------------------------------------------------

static void
_EvalTransposeM33(const ParamMap& inputs, const ShadingContext&,
                  NodeOutputMap* outputs)
{
    Mat3f m;
    const Value* value = inputs.Find(_kIn);
    if (value && ValueHolds<Mat3f>(*value)) {
        m = ValueGet<Mat3f>(*value);
    }
    (*outputs)[_kOut] = Value(m.transposed());
}

static void
_EvalTransposeM44(const ParamMap& inputs, const ShadingContext&,
                  NodeOutputMap* outputs)
{
    Mat4f m;
    const Value* value = inputs.Find(_kIn);
    if (value && ValueHolds<Mat4f>(*value)) {
        m = ValueGet<Mat4f>(*value);
    }
    (*outputs)[_kOut] = Value(m.transposed());
}

// ---- determinant ---------------------------------------------------------

static void
_EvalDeterminantM33(const ParamMap& inputs, const ShadingContext&,
                    NodeOutputMap* outputs)
{
    Mat3f m;
    const Value* value = inputs.Find(_kIn);
    if (value && ValueHolds<Mat3f>(*value)) {
        m = ValueGet<Mat3f>(*value);
    }
    // M33f determinant: manually compute
    float det = m[0][0]*(m[1][1]*m[2][2] - m[1][2]*m[2][1])
              - m[0][1]*(m[1][0]*m[2][2] - m[1][2]*m[2][0])
              + m[0][2]*(m[1][0]*m[2][1] - m[1][1]*m[2][0]);
    (*outputs)[_kOut] = Value(det);
}

static void
_EvalDeterminantM44(const ParamMap& inputs, const ShadingContext&,
                    NodeOutputMap* outputs)
{
    Mat4f m;
    const Value* value = inputs.Find(_kIn);
    if (value && ValueHolds<Mat4f>(*value)) {
        m = ValueGet<Mat4f>(*value);
    }
    // Use Imath cofactor expansion
    float det = m[0][0] * (m[1][1]*(m[2][2]*m[3][3] - m[2][3]*m[3][2])
                          - m[1][2]*(m[2][1]*m[3][3] - m[2][3]*m[3][1])
                          + m[1][3]*(m[2][1]*m[3][2] - m[2][2]*m[3][1]))
              - m[0][1] * (m[1][0]*(m[2][2]*m[3][3] - m[2][3]*m[3][2])
                          - m[1][2]*(m[2][0]*m[3][3] - m[2][3]*m[3][0])
                          + m[1][3]*(m[2][0]*m[3][2] - m[2][2]*m[3][0]))
              + m[0][2] * (m[1][0]*(m[2][1]*m[3][3] - m[2][3]*m[3][1])
                          - m[1][1]*(m[2][0]*m[3][3] - m[2][3]*m[3][0])
                          + m[1][3]*(m[2][0]*m[3][1] - m[2][1]*m[3][0]))
              - m[0][3] * (m[1][0]*(m[2][1]*m[3][2] - m[2][2]*m[3][1])
                          - m[1][1]*(m[2][0]*m[3][2] - m[2][2]*m[3][0])
                          + m[1][2]*(m[2][0]*m[3][1] - m[2][1]*m[3][0]));
    (*outputs)[_kOut] = Value(det);
}

// ---- invertmatrix --------------------------------------------------------

static void
_EvalInvertMatrixM33(const ParamMap& inputs, const ShadingContext&,
                     NodeOutputMap* outputs)
{
    Mat3f m;
    const Value* value = inputs.Find(_kIn);
    if (value && ValueHolds<Mat3f>(*value)) {
        m = ValueGet<Mat3f>(*value);
    }
    (*outputs)[_kOut] = Value(m.inverse());
}

static void
_EvalInvertMatrixM44(const ParamMap& inputs, const ShadingContext&,
                     NodeOutputMap* outputs)
{
    Mat4f m;
    const Value* value = inputs.Find(_kIn);
    if (value && ValueHolds<Mat4f>(*value)) {
        m = ValueGet<Mat4f>(*value);
    }
    (*outputs)[_kOut] = Value(m.inverse());
}

// ---- rotate2d ------------------------------------------------------------
// Rotate vector2 by amount (degrees) about the origin.

static void
_EvalRotate2d(const ParamMap& inputs, const ShadingContext&,
              NodeOutputMap* outputs)
{
    const Vec2f v = Get<Vec2f>(inputs, _kIn, Vec2f(0.0f));
    const float amount = Get<float>(inputs, _kAmount, 0.0f);
    (*outputs)[_kOut] = Value(Rotate2d(v, amount));
}

// ---- rotate3d ------------------------------------------------------------
// Rotate vector3 by amount (degrees) about axis.

static void
_EvalRotate3d(const ParamMap& inputs, const ShadingContext&,
              NodeOutputMap* outputs)
{
    const Vec3f v = Get<Vec3f>(inputs, _kIn, Vec3f(0.0f));
    const float amount = Get<float>(inputs, _kAmount, 0.0f);
    const Vec3f axis = Get<Vec3f>(inputs, _kAxis, Vec3f(0.0f, 1.0f, 0.0f));
    (*outputs)[_kOut] = Value(Rotate3d(v, amount, axis));
}

// ---- place2d -------------------------------------------------------------
// Transform UV coordinates: pivot, scale, rotate, offset, operationorder.

static void
_EvalPlace2d(const ParamMap& inputs, const ShadingContext&,
             NodeOutputMap* outputs)
{
    Vec2f uv     = Get<Vec2f>(inputs, _kTexcoord, Vec2f(0.0f));
    Vec2f pivot  = Get<Vec2f>(inputs, _kPivot, Vec2f(0.0f));
    Vec2f scale  = Get<Vec2f>(inputs, _kScale, Vec2f(1.0f));
    float rotate = Get<float>(inputs, _kRotate, 0.0f);
    Vec2f offset = Get<Vec2f>(inputs, _kOffset, Vec2f(0.0f));
    int order    = Get<int>(inputs, _kOperationOrder, 0);

    auto applyScale = [&](Vec2f p) -> Vec2f {
        return Vec2f(p[0] / scale[0], p[1] / scale[1]);
    };
    auto applyRotate = [&](Vec2f p) -> Vec2f {
        return Rotate2d(p, rotate);
    };
    auto applyTranslate = [&](Vec2f p) -> Vec2f {
        return p - offset;
    };

    Vec2f result = uv - pivot;
    if (order == 0) {
        // SRT
        result = applyScale(result);
        result = applyRotate(result);
        result = applyTranslate(result);
    } else {
        // TRS
        result = applyTranslate(result);
        result = applyRotate(result);
        result = applyScale(result);
    }
    result = result + pivot;

    (*outputs)[_kOut] = Value(result);
}

// ---- trianglewave --------------------------------------------------------
// 2 * abs(in - floor(in + 0.5))

static void
_EvalTriangleWave(const ParamMap& inputs, const ShadingContext&,
                  NodeOutputMap* outputs)
{
    float v = Get<float>(inputs, _kIn, 0.0f);
    (*outputs)[_kOut] = Value(2.0f * std::fabs(v - std::floor(v + 0.5f)));
}

static void
_EvalOpenPbrAnisotropy(const ParamMap& inputs, const ShadingContext&,
                       NodeOutputMap* outputs)
{
    const float roughness = Get<float>(inputs, _kRoughness, 0.0f);
    const float anisotropy = Get<float>(inputs, _kAnisotropy, 0.0f);
    const float roughnessSquared = roughness * roughness;
    const float oneMinusAnisotropy = 1.0f - anisotropy;
    const float alphaX = roughnessSquared * std::sqrt(
        2.0f /
        (oneMinusAnisotropy * oneMinusAnisotropy + 1.0f));
    const float alphaY = oneMinusAnisotropy * alphaX;
    (*outputs)[_kOut] = Value(Vec2f(alphaX, alphaY));
}

// ---- Registration --------------------------------------------------------

#define _REG(name, fn) reg.Register(name, fn)

void
RegisterMathNodes(NodeRegistry& reg)
{
    // add
    _REG("ND_add_float",   &_EvalAdd<float>);
    _REG("ND_add_integer", &_EvalAdd<int>);
    _REG("ND_add_color3",  &_EvalAdd<Vec3f>);
    _REG("ND_add_color4",  &_EvalAdd<Vec4f>);
    _REG("ND_add_vector2", &_EvalAdd<Vec2f>);
    _REG("ND_add_vector3", &_EvalAdd<Vec3f>);
    _REG("ND_add_vector4", &_EvalAdd<Vec4f>);
    _REG("ND_add_matrix33", &_EvalAdd<Mat3f>);
    _REG("ND_add_matrix44", &_EvalAdd<Mat4f>);
    _REG("ND_add_color3FA",  &_EvalAddFA<Vec3f>);
    _REG("ND_add_color4FA",  &_EvalAddFA<Vec4f>);
    _REG("ND_add_vector2FA", &_EvalAddFA<Vec2f>);
    _REG("ND_add_vector3FA", &_EvalAddFA<Vec3f>);
    _REG("ND_add_vector4FA", &_EvalAddFA<Vec4f>);
    reg.Register("ND_add_matrix33FA", &_EvalMatrixAddFA<Mat3f, 3>);
    reg.Register("ND_add_matrix44FA", &_EvalMatrixAddFA<Mat4f, 4>);
    _REG("ND_add_bsdf", &_EvalAddBsdf);
    _REG("ND_add_edf", &_EvalAddEdf);
    _REG("ND_add_vdf", &_EvalAddVdf);

    // subtract
    _REG("ND_subtract_float",   &_EvalSubtract<float>);
    _REG("ND_subtract_integer", &_EvalSubtract<int>);
    _REG("ND_subtract_color3",  &_EvalSubtract<Vec3f>);
    _REG("ND_subtract_color4",  &_EvalSubtract<Vec4f>);
    _REG("ND_subtract_vector2", &_EvalSubtract<Vec2f>);
    _REG("ND_subtract_vector3", &_EvalSubtract<Vec3f>);
    _REG("ND_subtract_vector4", &_EvalSubtract<Vec4f>);
    _REG("ND_subtract_matrix33", &_EvalSubtract<Mat3f>);
    _REG("ND_subtract_matrix44", &_EvalSubtract<Mat4f>);
    _REG("ND_subtract_color3FA",  &_EvalSubtractFA<Vec3f>);
    _REG("ND_subtract_color4FA",  &_EvalSubtractFA<Vec4f>);
    _REG("ND_subtract_vector2FA", &_EvalSubtractFA<Vec2f>);
    _REG("ND_subtract_vector3FA", &_EvalSubtractFA<Vec3f>);
    _REG("ND_subtract_vector4FA", &_EvalSubtractFA<Vec4f>);
    reg.Register("ND_subtract_matrix33FA", &_EvalMatrixSubtractFA<Mat3f, 3>);
    reg.Register("ND_subtract_matrix44FA", &_EvalMatrixSubtractFA<Mat4f, 4>);

    // multiply (component-wise)
    _REG("ND_multiply_float",   &_EvalMultiply<float>);
    _REG("ND_multiply_color3",  &_EvalMultiply<Vec3f>);
    _REG("ND_multiply_color4",  &_EvalMultiply<Vec4f>);
    _REG("ND_multiply_vector2", &_EvalMultiply<Vec2f>);
    _REG("ND_multiply_vector3", &_EvalMultiply<Vec3f>);
    _REG("ND_multiply_vector4", &_EvalMultiply<Vec4f>);
    _REG("ND_multiply_matrix33", &_EvalMultiply<Mat3f>);
    _REG("ND_multiply_matrix44", &_EvalMultiply<Mat4f>);

    // multiply (vector * float)
    _REG("ND_multiply_color3FA",  &_EvalMultiplyFA<Vec3f>);
    _REG("ND_multiply_color4FA",  &_EvalMultiplyFA<Vec4f>);
    _REG("ND_multiply_vector2FA", &_EvalMultiplyFA<Vec2f>);
    _REG("ND_multiply_vector3FA", &_EvalMultiplyFA<Vec3f>);
    _REG("ND_multiply_vector4FA", &_EvalMultiplyFA<Vec4f>);
    _REG("ND_multiply_bsdfC", &_EvalMultiplyBsdfC);
    _REG("ND_multiply_bsdfF", &_EvalMultiplyBsdfF);
    _REG("ND_multiply_edfC", &_EvalMultiplyEdfC);
    _REG("ND_multiply_edfF", &_EvalMultiplyEdfF);
    _REG("ND_multiply_vdfC", &_EvalMultiplyVdfC);
    _REG("ND_multiply_vdfF", &_EvalMultiplyVdfF);

    // divide
    _REG("ND_divide_float",   &_EvalDivide<float>);
    _REG("ND_divide_color3",  &_EvalDivide<Vec3f>);
    _REG("ND_divide_color4",  &_EvalDivide<Vec4f>);
    _REG("ND_divide_vector2", &_EvalDivide<Vec2f>);
    _REG("ND_divide_vector3", &_EvalDivide<Vec3f>);
    _REG("ND_divide_vector4", &_EvalDivide<Vec4f>);
    _REG("ND_divide_matrix33", &_EvalMatrixDivide<Mat3f>);
    _REG("ND_divide_matrix44", &_EvalMatrixDivide<Mat4f>);
    _REG("ND_divide_color3FA",  &_EvalDivideFA<Vec3f>);
    _REG("ND_divide_color4FA",  &_EvalDivideFA<Vec4f>);
    _REG("ND_divide_vector2FA", &_EvalDivideFA<Vec2f>);
    _REG("ND_divide_vector3FA", &_EvalDivideFA<Vec3f>);
    _REG("ND_divide_vector4FA", &_EvalDivideFA<Vec4f>);

    // modulo
    _REG("ND_modulo_float", &_EvalModuloFloat);
    _REG("ND_modulo_color3", &_EvalModuloVector<Vec3f>);
    _REG("ND_modulo_color4", &_EvalModuloVector<Vec4f>);
    _REG("ND_modulo_vector2", &_EvalModuloVector<Vec2f>);
    _REG("ND_modulo_vector3", &_EvalModuloVector<Vec3f>);
    _REG("ND_modulo_vector4", &_EvalModuloVector<Vec4f>);
    _REG("ND_modulo_color3FA", &_EvalModuloVectorFA<Vec3f>);
    _REG("ND_modulo_color4FA", &_EvalModuloVectorFA<Vec4f>);
    _REG("ND_modulo_vector2FA", &_EvalModuloVectorFA<Vec2f>);
    _REG("ND_modulo_vector3FA", &_EvalModuloVectorFA<Vec3f>);
    _REG("ND_modulo_vector4FA", &_EvalModuloVectorFA<Vec4f>);
    _REG("ND_clamp_float", &_EvalClamp<float>);
    _REG("ND_clamp_color3", &_EvalClamp<Vec3f>);
    _REG("ND_clamp_color4", &_EvalClamp<Vec4f>);
    _REG("ND_clamp_vector3", &_EvalClamp<Vec3f>);
    _REG("ND_clamp_vector2", &_EvalClamp<Vec2f>);
    _REG("ND_clamp_vector4", &_EvalClamp<Vec4f>);
    _REG("ND_clamp_color3FA", &_EvalClampFA<Vec3f>);
    _REG("ND_clamp_color4FA", &_EvalClampFA<Vec4f>);
    _REG("ND_clamp_vector2FA", &_EvalClampFA<Vec2f>);
    _REG("ND_clamp_vector3FA", &_EvalClampFA<Vec3f>);
    _REG("ND_clamp_vector4FA", &_EvalClampFA<Vec4f>);
    _REG("ND_min_float", &_EvalMinFloat);
    _REG("ND_min_color3", &_EvalMinVector<Vec3f>);
    _REG("ND_min_color4", &_EvalMinVector<Vec4f>);
    _REG("ND_min_vector2", &_EvalMinVector<Vec2f>);
    _REG("ND_min_vector3", &_EvalMinVector<Vec3f>);
    _REG("ND_min_vector4", &_EvalMinVector<Vec4f>);
    _REG("ND_min_color3FA", &_EvalMinVectorFA<Vec3f>);
    _REG("ND_min_color4FA", &_EvalMinVectorFA<Vec4f>);
    _REG("ND_min_vector2FA", &_EvalMinVectorFA<Vec2f>);
    _REG("ND_min_vector3FA", &_EvalMinVectorFA<Vec3f>);
    _REG("ND_min_vector4FA", &_EvalMinVectorFA<Vec4f>);
    _REG("ND_max_float", &_EvalMaxFloat);
    _REG("ND_max_color3", &_EvalMaxVector<Vec3f>);
    _REG("ND_max_color4", &_EvalMaxVector<Vec4f>);
    _REG("ND_max_vector2", &_EvalMaxVector<Vec2f>);
    _REG("ND_max_vector3", &_EvalMaxVector<Vec3f>);
    _REG("ND_max_vector4", &_EvalMaxVector<Vec4f>);
    _REG("ND_max_color3FA", &_EvalMaxVectorFA<Vec3f>);
    _REG("ND_max_color4FA", &_EvalMaxVectorFA<Vec4f>);
    _REG("ND_max_vector2FA", &_EvalMaxVectorFA<Vec2f>);
    _REG("ND_max_vector3FA", &_EvalMaxVectorFA<Vec3f>);
    _REG("ND_max_vector4FA", &_EvalMaxVectorFA<Vec4f>);
    _REG("ND_mincomponent_color3", &_EvalMinComponent<Vec3f>);
    _REG("ND_mincomponent_color4", &_EvalMinComponent<Vec4f>);
    _REG("ND_mincomponent_vector2", &_EvalMinComponent<Vec2f>);
    _REG("ND_mincomponent_vector3", &_EvalMinComponent<Vec3f>);
    _REG("ND_mincomponent_vector4", &_EvalMinComponent<Vec4f>);
    _REG("ND_maxcomponent_color3", &_EvalMaxComponent<Vec3f>);
    _REG("ND_maxcomponent_color4", &_EvalMaxComponent<Vec4f>);
    _REG("ND_maxcomponent_vector2", &_EvalMaxComponent<Vec2f>);
    _REG("ND_maxcomponent_vector3", &_EvalMaxComponent<Vec3f>);
    _REG("ND_maxcomponent_vector4", &_EvalMaxComponent<Vec4f>);

    // unary
    _REG("ND_absval_float",   &_EvalAbsvalFloat);
    _REG("ND_absval_color3",  &_EvalAbsvalVector<Vec3f>);
    _REG("ND_absval_color4",  &_EvalAbsvalVector<Vec4f>);
    _REG("ND_absval_vector2", &_EvalAbsvalVector<Vec2f>);
    _REG("ND_absval_vector3", &_EvalAbsvalVector<Vec3f>);
    _REG("ND_absval_vector4", &_EvalAbsvalVector<Vec4f>);
    _REG("ND_sign_float",  &_EvalSign);
    _REG("ND_sign_color3", &_EvalSignVector<Vec3f>);
    _REG("ND_sign_color4", &_EvalSignVector<Vec4f>);
    _REG("ND_sign_vector2", &_EvalSignVector<Vec2f>);
    _REG("ND_sign_vector3", &_EvalSignVector<Vec3f>);
    _REG("ND_sign_vector4", &_EvalSignVector<Vec4f>);
    _REG("ND_floor_float",  &_EvalFloor);
    _REG("ND_floor_color3", &_EvalFloorVector<Vec3f>);
    _REG("ND_floor_color4", &_EvalFloorVector<Vec4f>);
    _REG("ND_floor_vector2", &_EvalFloorVector<Vec2f>);
    _REG("ND_floor_vector3", &_EvalFloorVector<Vec3f>);
    _REG("ND_floor_vector4", &_EvalFloorVector<Vec4f>);
    _REG("ND_floor_integer", &_EvalFloorInteger);
    _REG("ND_ceil_float",   &_EvalCeil);
    _REG("ND_ceil_color3", &_EvalCeilVector<Vec3f>);
    _REG("ND_ceil_color4", &_EvalCeilVector<Vec4f>);
    _REG("ND_ceil_vector2", &_EvalCeilVector<Vec2f>);
    _REG("ND_ceil_vector3", &_EvalCeilVector<Vec3f>);
    _REG("ND_ceil_vector4", &_EvalCeilVector<Vec4f>);
    _REG("ND_ceil_integer", &_EvalCeilInteger);
    _REG("ND_round_float",  &_EvalRound);
    _REG("ND_round_color3", &_EvalRoundVector<Vec3f>);
    _REG("ND_round_color4", &_EvalRoundVector<Vec4f>);
    _REG("ND_round_vector2", &_EvalRoundVector<Vec2f>);
    _REG("ND_round_vector3", &_EvalRoundVector<Vec3f>);
    _REG("ND_round_vector4", &_EvalRoundVector<Vec4f>);
    _REG("ND_round_integer", &_EvalRoundInteger);
    _REG("ND_power_float",  &_EvalPower);
    _REG("ND_power_color3", &_EvalPowerVector<Vec3f>);
    _REG("ND_power_color4", &_EvalPowerVector<Vec4f>);
    _REG("ND_power_vector2", &_EvalPowerVector<Vec2f>);
    _REG("ND_power_vector3", &_EvalPowerVector<Vec3f>);
    _REG("ND_power_vector4", &_EvalPowerVector<Vec4f>);
    _REG("ND_power_color3FA", &_EvalPowerFA<Vec3f>);
    _REG("ND_power_color4FA", &_EvalPowerFA<Vec4f>);
    _REG("ND_power_vector2FA", &_EvalPowerFA<Vec2f>);
    _REG("ND_power_vector3FA", &_EvalPowerFA<Vec3f>);
    _REG("ND_power_vector4FA", &_EvalPowerFA<Vec4f>);
    _REG("ND_sqrt_float",   &_EvalSqrt);
    _REG("ND_sqrt_vector2", &_EvalSqrtVector<Vec2f>);
    _REG("ND_sqrt_vector3", &_EvalSqrtVector<Vec3f>);
    _REG("ND_sqrt_vector4", &_EvalSqrtVector<Vec4f>);
    _REG("ND_ln_float",     &_EvalLn);
    _REG("ND_ln_color3", &_EvalLnVector<Vec3f>);
    _REG("ND_ln_color4", &_EvalLnVector<Vec4f>);
    _REG("ND_ln_vector2", &_EvalLnVector<Vec2f>);
    _REG("ND_ln_vector3", &_EvalLnVector<Vec3f>);
    _REG("ND_ln_vector4", &_EvalLnVector<Vec4f>);
    _REG("ND_exp_float",    &_EvalExp);
    _REG("ND_exp_vector2", &_EvalExpVector<Vec2f>);
    _REG("ND_exp_vector3", &_EvalExpVector<Vec3f>);
    _REG("ND_exp_vector4", &_EvalExpVector<Vec4f>);

    // negate
    _REG("ND_negate_float",   &_EvalNegate<float>);
    _REG("ND_negate_color3",  &_EvalNegate<Vec3f>);
    _REG("ND_negate_vector3", &_EvalNegate<Vec3f>);

    // invert
    _REG("ND_invert_float",   &_EvalInvert<float>);
    _REG("ND_invert_color3",  &_EvalInvert<Vec3f>);
    _REG("ND_invert_color4",  &_EvalInvert<Vec4f>);
    _REG("ND_invert_color3FA", &_EvalInvertFA<Vec3f>);
    _REG("ND_invert_color4FA", &_EvalInvertFA<Vec4f>);
    _REG("ND_invert_vector2", &_EvalInvert<Vec2f>);
    _REG("ND_invert_vector2FA", &_EvalInvertFA<Vec2f>);
    _REG("ND_invert_vector3", &_EvalInvert<Vec3f>);
    _REG("ND_invert_vector3FA", &_EvalInvertFA<Vec3f>);
    _REG("ND_invert_vector4", &_EvalInvert<Vec4f>);
    _REG("ND_invert_vector4FA", &_EvalInvertFA<Vec4f>);

    // trigonometric
    _REG("ND_sin_float",   &_EvalSin);
    _REG("ND_sin_vector2", &_EvalSinVector<Vec2f>);
    _REG("ND_sin_vector3", &_EvalSinVector<Vec3f>);
    _REG("ND_sin_vector4", &_EvalSinVector<Vec4f>);
    _REG("ND_cos_float",   &_EvalCos);
    _REG("ND_cos_vector2", &_EvalCosVector<Vec2f>);
    _REG("ND_cos_vector3", &_EvalCosVector<Vec3f>);
    _REG("ND_cos_vector4", &_EvalCosVector<Vec4f>);
    _REG("ND_tan_float",   &_EvalTan);
    _REG("ND_tan_vector2", &_EvalTanVector<Vec2f>);
    _REG("ND_tan_vector3", &_EvalTanVector<Vec3f>);
    _REG("ND_tan_vector4", &_EvalTanVector<Vec4f>);
    _REG("ND_asin_float",  &_EvalAsin);
    _REG("ND_acos_float",  &_EvalAcos);
    _REG("ND_asin_color3", &_EvalAsinVector<Vec3f>);
    _REG("ND_asin_color4", &_EvalAsinVector<Vec4f>);
    _REG("ND_asin_vector2", &_EvalAsinVector<Vec2f>);
    _REG("ND_asin_vector3", &_EvalAsinVector<Vec3f>);
    _REG("ND_asin_vector4", &_EvalAsinVector<Vec4f>);
    _REG("ND_acos_color3", &_EvalAcosVector<Vec3f>);
    _REG("ND_acos_color4", &_EvalAcosVector<Vec4f>);
    _REG("ND_acos_vector2", &_EvalAcosVector<Vec2f>);
    _REG("ND_acos_vector3", &_EvalAcosVector<Vec3f>);
    _REG("ND_acos_vector4", &_EvalAcosVector<Vec4f>);
    _REG("ND_atan2_float", &_EvalAtan2);
    _REG("ND_atan2_vector2", &_EvalAtan2Vector<Vec2f>);
    _REG("ND_atan2_vector3", &_EvalAtan2Vector<Vec3f>);
    _REG("ND_atan2_vector4", &_EvalAtan2Vector<Vec4f>);

    // vector
    _REG("ND_dotproduct_vector2",  &_EvalDotProduct<Vec2f>);
    _REG("ND_dotproduct_vector3",  &_EvalDotProduct<Vec3f>);
    _REG("ND_dotproduct_vector4",  &_EvalDotProduct<Vec4f>);
    _REG("ND_crossproduct_vector3", &_EvalCrossProduct);
    _REG("ND_normalize_vector2",   &_EvalNormalizeVector<Vec2f>);
    _REG("ND_normalize_vector3",   &_EvalNormalize);
    _REG("ND_normalize_vector4",   &_EvalNormalizeVector<Vec4f>);
    _REG("ND_magnitude_vector2",   &_EvalMagnitude<Vec2f>);
    _REG("ND_magnitude_vector3",   &_EvalMagnitude<Vec3f>);
    _REG("ND_magnitude_vector4",   &_EvalMagnitude<Vec4f>);

    // safepower
    _REG("ND_safepower_float",      &_EvalSafePowerFloat);
    _REG("ND_safepower_color3",     &_EvalSafePower<Vec3f>);
    _REG("ND_safepower_color4",     &_EvalSafePower<Vec4f>);
    _REG("ND_safepower_vector2",    &_EvalSafePower<Vec2f>);
    _REG("ND_safepower_vector3",    &_EvalSafePower<Vec3f>);
    _REG("ND_safepower_vector4",    &_EvalSafePower<Vec4f>);
    _REG("ND_safepower_color3FA",   &_EvalSafePowerFA<Vec3f>);
    _REG("ND_safepower_color4FA",   &_EvalSafePowerFA<Vec4f>);
    _REG("ND_safepower_vector2FA",  &_EvalSafePowerFA<Vec2f>);
    _REG("ND_safepower_vector3FA",  &_EvalSafePowerFA<Vec3f>);
    _REG("ND_safepower_vector4FA",  &_EvalSafePowerFA<Vec4f>);

    // fract
    _REG("ND_fract_float",   &_EvalFractFloat);
    _REG("ND_fract_color3",  &_EvalFract<Vec3f>);
    _REG("ND_fract_color4",  &_EvalFract<Vec4f>);
    _REG("ND_fract_vector2", &_EvalFract<Vec2f>);
    _REG("ND_fract_vector3", &_EvalFract<Vec3f>);
    _REG("ND_fract_vector4", &_EvalFract<Vec4f>);

    // distance
    _REG("ND_distance_vector2", &_EvalDistance<Vec2f>);
    _REG("ND_distance_vector3", &_EvalDistance<Vec3f>);
    _REG("ND_distance_vector4", &_EvalDistance<Vec4f>);

    // reflect / refract
    _REG("ND_reflect_vector3", &_EvalReflect);
    _REG("ND_refract_vector3", &_EvalRefract);

    // creatematrix
    _REG("ND_creatematrix_vector3_matrix33", &_EvalCreateMatrixVec3M33);
    _REG("ND_creatematrix_vector3_matrix44", &_EvalCreateMatrixVec3M44);
    _REG("ND_creatematrix_vector4_matrix44", &_EvalCreateMatrixVec4M44);

    // transform (named space)
    _REG("ND_transformpoint_vector3",
         &_EvalTransformNamedSpace<ShadingContext::TransformSpaceType::Point>);
    _REG("ND_transformvector_vector3",
         &_EvalTransformNamedSpace<ShadingContext::TransformSpaceType::Vector>);
    _REG("ND_transformnormal_vector3",
         &_EvalTransformNamedSpace<ShadingContext::TransformSpaceType::Normal>);

    // transformmatrix
    _REG("ND_transformmatrix_vector2M3",  &_EvalTransformMatrixVec2M3);
    _REG("ND_transformmatrix_vector3",    &_EvalTransformMatrixVec3M3);
    _REG("ND_transformmatrix_vector3M4",  &_EvalTransformMatrixVec3M4);
    _REG("ND_transformmatrix_vector4",    &_EvalTransformMatrixVec4M4);

    // transpose
    _REG("ND_transpose_matrix33", &_EvalTransposeM33);
    _REG("ND_transpose_matrix44", &_EvalTransposeM44);

    // determinant
    _REG("ND_determinant_matrix33", &_EvalDeterminantM33);
    _REG("ND_determinant_matrix44", &_EvalDeterminantM44);

    // invertmatrix
    _REG("ND_invertmatrix_matrix33", &_EvalInvertMatrixM33);
    _REG("ND_invertmatrix_matrix44", &_EvalInvertMatrixM44);

    // rotate
    _REG("ND_rotate2d_vector2", &_EvalRotate2d);
    _REG("ND_rotate3d_vector3", &_EvalRotate3d);

    // place2d
    _REG("ND_place2d_vector2", &_EvalPlace2d);

    // trianglewave
    _REG("ND_trianglewave_float", &_EvalTriangleWave);

    // OpenPBR
    _REG("ND_open_pbr_anisotropy", &_EvalOpenPbrAnisotropy);

}

#undef _REG

}  // namespace mxcpp
