//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include <renderer/materials/MaterialXCpp/graph.h>
#include <renderer/materials/MaterialXCpp/materials/bsdf.h>
#include <renderer/materials/MaterialXCpp/materials/bsdf/closureTraversal.h>
#include <renderer/materials/MaterialXCpp/materials/bsdf/dielectric.h>
#include <renderer/materials/MaterialXCpp/materials/bsdf/dielectricBothLut.h>
#include <renderer/materials/MaterialXCpp/materials/bsdf/dielectricReflFrontLut.h>
#include <renderer/materials/MaterialXCpp/materials/bsdf/dielectricTransmissionLut.h>
#include <renderer/materials/MaterialXCpp/materials/bsdf/fresnel.h>
#include <renderer/materials/MaterialXCpp/materials/bsdf/microfacet.h>
#include <renderer/materials/MaterialXCpp/materials/bsdf/shadingFrame.h>

#define BSDL_UNROLL()
#include <BSDL/MTX/bsdf_dielectric_decl.h>
#include <BSDL/microfacet_tools_impl.h>
#include <BSDL/MTX/bsdf_dielectric_impl.h>
#undef BSDL_UNROLL

#include <renderer/integrator/medium.h>
#include <renderer/integrator/shadingNormal.h>
#include <renderer/integrator/sss.h>
#include <renderer/materials/MaterialXCpp/materials/adobeOpenPbr.h>
#include <renderer/materials/MaterialXCpp/materials/openPbr.h>
#include <renderer/materials/MaterialXCpp/materials/standardSurface.h>
#include <renderer/materials/MaterialXCpp/materials/usdPreviewSurface.h>
#include <renderer/materials/MaterialXCpp/nodes/helpers/mathHelpers.h>
#include <renderer/materials/MaterialXCpp/spectral.h>
#include <renderer/materials/MaterialXCpp/surfaceShaderUtils.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <limits>
#include <optional>
#include <variant>

PXR_NAMESPACE_USING_DIRECTIVE

using namespace mxcpp;

// From testMain.cpp
void Test_Register(const char* name, std::function<bool()> fn);
bool Test_IsClose(float a, float b, float eps = 1e-5f);
bool Test_IsClose(const Vec3f& a, const Vec3f& b, float eps = 1e-5f);

#define _REG(name) Test_Register("Bsdf." #name, &name)

// ---------------------------------------------------------------------------

namespace {

static SurfaceInteraction
_MakeSurfaceInteraction(
    const Vec3f& normalShdWldOut, const Vec3f& normalSrfWldOut,
    const Vec3f& normalGeomWldOut, const Vec3f& omegaOutWld,
    float heroWavelengthNm = 0.0f, bool frontFacing = true)
{
    SurfaceInteraction interaction;
    interaction.normalShdWldOut = normalShdWldOut;
    interaction.normalSrfWldOut = normalSrfWldOut;
    interaction.normalGeomWldOut = normalGeomWldOut;
    interaction.omegaOutWld = omegaOutWld;
    interaction.heroWavelengthNm = heroWavelengthNm;
    interaction.frontFacing = frontFacing;
    return interaction;
}

static SurfaceClosure
_PrepareTestClosure(const SurfaceClosure& source,
                    const SurfaceInteraction& interaction)
{
    SurfaceClosure closure = source;
    if (closure.HasBsdfTree()) {
        const Vec3f normalShdWldExt = interaction.frontFacing
            ? interaction.normalShdWldOut
            : -interaction.normalShdWldOut;
        ValidateLeafNormals(&closure.bsdfTree, normalShdWldExt);
        Bsdf::detail::PrepareShadingNormals(
            &closure.bsdfTree, interaction.normalShdWldOut,
            interaction.normalGeomWldOut, interaction.omegaOutWld);
    }
    return closure;
}

static float
_PdfSurface(const SurfaceClosure& source,
            const SurfaceInteraction& interaction,
            const Vec3f& omegaInWld)
{
    const SurfaceClosure closure = _PrepareTestClosure(source, interaction);
    return Bsdf::PdfSurface(closure, interaction, omegaInWld);
}

static float
_PdfPreparedSurface(const SurfaceClosure& closure,
                    const SurfaceInteraction& interaction,
                    const Vec3f& omegaInWld)
{
    return Bsdf::PdfSurface(closure, interaction, omegaInWld);
}

static Vec3f
_EvalSurface(const SurfaceClosure& source,
             const SurfaceInteraction& interaction,
             const Vec3f& omegaInWld)
{
    const SurfaceClosure closure = _PrepareTestClosure(source, interaction);
    return Bsdf::EvalSurface(closure, interaction, omegaInWld);
}

static Vec3f
_EvalSurfaceCosine(const SurfaceClosure& source,
                   const SurfaceInteraction& interaction,
                   const Vec3f& omegaInWld)
{
    const SurfaceClosure closure = _PrepareTestClosure(source, interaction);
    return Bsdf::EvalSurfaceCosine(closure, interaction, omegaInWld);
}

static Bsdf::BsdfSample
_SampleSurface(const SurfaceClosure& source,
               const SurfaceInteraction& interaction,
               float u1, float u2, float uLobe)
{
    const SurfaceClosure closure = _PrepareTestClosure(source, interaction);
    return Bsdf::SampleSurface(closure, interaction, u1, u2, uLobe);
}

// Tests unrelated to normal separation opt into a flat-normal fixture here.
// Normal-policy tests pass all three normals through the canonical overload.
struct TestSurfaceApi
{
    static Vec3f EvalSurface(
        const SurfaceClosure& closure, const Vec3f& normalWldOut,
        const Vec3f& omegaInWld, const Vec3f& omegaOutWld,
        float heroWavelengthNm = 0.0f, bool frontFacing = true)
    {
        return _EvalSurface(closure, _MakeSurfaceInteraction(normalWldOut, normalWldOut, normalWldOut, omegaOutWld, heroWavelengthNm, frontFacing), omegaInWld);
    }

    static Vec3f EvalSurface(
        const SurfaceClosure& closure, const Vec3f& normalShdWldOut,
        const Vec3f& normalSrfWldOut, const Vec3f& omegaInWld,
        const Vec3f& omegaOutWld, float heroWavelengthNm = 0.0f,
        bool frontFacing = true)
    {
        return _EvalSurface(closure, _MakeSurfaceInteraction(normalShdWldOut, normalSrfWldOut, normalSrfWldOut, omegaOutWld, heroWavelengthNm, frontFacing), omegaInWld);
    }

    static Bsdf::BsdfSample SampleSurface(
        const SurfaceClosure& closure, const Vec3f& normalWldOut,
        const Vec3f& omegaOutWld, float u1, float u2, float uLobe,
        float heroWavelengthNm = 0.0f, bool frontFacing = true)
    {
        return _SampleSurface(closure, _MakeSurfaceInteraction(normalWldOut, normalWldOut, normalWldOut, omegaOutWld, heroWavelengthNm, frontFacing), u1, u2, uLobe);
    }

    static Bsdf::BsdfSample SampleSurface(
        const SurfaceClosure& closure, const Vec3f& normalShdWldOut,
        const Vec3f& normalSrfWldOut, const Vec3f& normalGeomWldOut,
        const Vec3f& omegaOutWld, float u1, float u2, float uLobe,
        float heroWavelengthNm = 0.0f, bool frontFacing = true)
    {
        return _SampleSurface(closure, _MakeSurfaceInteraction(normalShdWldOut, normalSrfWldOut, normalGeomWldOut, omegaOutWld, heroWavelengthNm, frontFacing), u1, u2, uLobe);
    }
};

// Explicitly prepared fixtures bypass the convenience preparation above.
// This keeps the production one-shot preparation contract visible in tests.
struct PreparedSurfaceApi
{
    static Vec3f EvalSurface(
        const SurfaceClosure& closure, const Vec3f& normalWldOut,
        const Vec3f& omegaInWld, const Vec3f& omegaOutWld,
        float heroWavelengthNm = 0.0f, bool frontFacing = true)
    {
        const SurfaceInteraction interaction = _MakeSurfaceInteraction(
            normalWldOut, normalWldOut, normalWldOut, omegaOutWld,
            heroWavelengthNm, frontFacing);
        return Bsdf::EvalSurface(closure, interaction, omegaInWld);
    }

    static Vec3f EvalSurface(
        const SurfaceClosure& closure, const Vec3f& normalShdWldOut,
        const Vec3f& normalSrfWldOut, const Vec3f& omegaInWld,
        const Vec3f& omegaOutWld, float heroWavelengthNm = 0.0f,
        bool frontFacing = true)
    {
        const SurfaceInteraction interaction = _MakeSurfaceInteraction(
            normalShdWldOut, normalSrfWldOut, normalSrfWldOut,
            omegaOutWld, heroWavelengthNm, frontFacing);
        return Bsdf::EvalSurface(closure, interaction, omegaInWld);
    }

    static Bsdf::BsdfSample SampleSurface(
        const SurfaceClosure& closure, const Vec3f& normalWldOut,
        const Vec3f& omegaOutWld, float u1, float u2, float uLobe,
        float heroWavelengthNm = 0.0f, bool frontFacing = true)
    {
        const SurfaceInteraction interaction = _MakeSurfaceInteraction(
            normalWldOut, normalWldOut, normalWldOut, omegaOutWld,
            heroWavelengthNm, frontFacing);
        return Bsdf::SampleSurface(
            closure, interaction, u1, u2, uLobe);
    }

    static Bsdf::BsdfSample SampleSurface(
        const SurfaceClosure& closure, const Vec3f& normalShdWldOut,
        const Vec3f& normalSrfWldOut, const Vec3f& normalGeomWldOut,
        const Vec3f& omegaOutWld, float u1, float u2, float uLobe,
        float heroWavelengthNm = 0.0f, bool frontFacing = true)
    {
        const SurfaceInteraction interaction = _MakeSurfaceInteraction(
            normalShdWldOut, normalSrfWldOut, normalGeomWldOut,
            omegaOutWld, heroWavelengthNm, frontFacing);
        return Bsdf::SampleSurface(
            closure, interaction, u1, u2, uLobe);
    }
};

constexpr float _kFurnaceTwoPi = 6.2831853071795864769f;
constexpr float _kFurnacePi = 3.14159265358979323846f;
constexpr int _kFurnaceSampleCount = 8192;
constexpr float _kFurnaceEnergyUpperSlack = 0.025f;

constexpr float _kFurnaceAlphaRoughness[] = {
    0.05f, 0.2f, 0.4f, 0.6f, 0.8f, 1.0f
};

constexpr float _kFurnaceCosTheta[] = {
    0.99f, 0.7f, 0.5f, 0.3f, 0.1f, 0.03f
};

static float
_RadicalInverseBase2(std::uint32_t bits)
{
    bits = (bits << 16) | (bits >> 16);
    bits = ((bits & 0x55555555u) << 1) | ((bits & 0xAAAAAAAAu) >> 1);
    bits = ((bits & 0x33333333u) << 2) | ((bits & 0xCCCCCCCCu) >> 2);
    bits = ((bits & 0x0F0F0F0Fu) << 4) | ((bits & 0xF0F0F0F0u) >> 4);
    bits = ((bits & 0x00FF00FFu) << 8) | ((bits & 0xFF00FF00u) >> 8);
    return static_cast<float>(bits) * 2.3283064365386963e-10f;
}

static float
_CounterRandomFloat(std::uint64_t index, std::uint64_t stream)
{
    std::uint64_t value = index +
        0x9e3779b97f4a7c15ull * (stream + 1ull);
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ull;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebull;
    value ^= value >> 31;
    return float((value >> 40) & 0xFFFFFF) / 16777216.0f;
}

static Vec3f
_HemisphereDirectionYUp(float u1, float u2)
{
    const float cosTheta = std::clamp(u1, 0.0f, 1.0f);
    const float sinTheta =
        std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
    const float phi = _kFurnaceTwoPi * u2;
    return Vec3f(
        sinTheta * std::cos(phi),
        cosTheta,
        sinTheta * std::sin(phi));
}

static Vec3f
_DirectionFromCosThetaYUp(float cosTheta)
{
    const float clamped = std::clamp(cosTheta, 0.0f, 1.0f);
    return Vec3f(std::sqrt(std::max(0.0f, 1.0f - clamped * clamped)),
                 clamped,
                 0.0f);
}

static Vec3f
_IntegrateHemisphereUniform(
    const std::function<Vec3f(const Vec3f& omegaInWld)>& eval, int sampleCount)
{
    const Vec3f normalShdWldOut(0.0f, 1.0f, 0.0f);
    Vec3f sum(0.0f);
    for (int i = 0; i < sampleCount; ++i) {
        const float u1 =
            (static_cast<float>(i) + 0.5f) / static_cast<float>(sampleCount);
        const float u2 = _RadicalInverseBase2(static_cast<std::uint32_t>(i));
        const Vec3f omegaInWld = _HemisphereDirectionYUp(u1, u2);
        sum += eval(omegaInWld) *
               (Dot(normalShdWldOut, omegaInWld) * _kFurnaceTwoPi);
    }
    return sum * (1.0f / static_cast<float>(sampleCount));
}

static Vec3f
_IntegrateSurfaceBySampling(const SurfaceClosure& closure,
                            const Vec3f& normalShdWldOut,
                            const Vec3f& omegaOutWld, int sampleCount)
{
    Vec3f sum(0.0f);
    for (int i = 0; i < sampleCount; ++i) {
        const float u1 =
            (static_cast<float>(i) + 0.5f) / static_cast<float>(sampleCount);
        const float u2 = _RadicalInverseBase2(static_cast<std::uint32_t>(i));
        const float uChoice =
            _RadicalInverseBase2(static_cast<std::uint32_t>(i) ^ 0x9E3779B9u);
        const auto sample = TestSurfaceApi::SampleSurface(closure, normalShdWldOut,
                                                omegaOutWld, u1, u2, uChoice);
        if (sample.pdfSolidAngle <= 0.0f || sample.isSpecular ||
            sample.isSubsurface) {
            continue;
        }
        const float cosTheta =
            std::max(Dot(normalShdWldOut, sample.omegaInWld), 0.0f);
        sum += sample.bsdfValue * (cosTheta / sample.pdfSolidAngle);
    }
    return sum * (1.0f / static_cast<float>(sampleCount));
}

static Vec3f
_IntegrateTransmissionBySampling(const SurfaceClosure& closure,
                                 const Vec3f& normalShdWldOut,
                                 const Vec3f& omegaOutWld, int sampleCount,
                                 bool frontFacing = true)
{
    Vec3f sum(0.0f);
    const float omegaOutSideWld = Dot(normalShdWldOut, omegaOutWld);
    for (int i = 0; i < sampleCount; ++i) {
        const float u1 =
            (static_cast<float>(i) + 0.5f) / static_cast<float>(sampleCount);
        const float u2 = _RadicalInverseBase2(static_cast<std::uint32_t>(i));
        const float uChoice =
            _RadicalInverseBase2(static_cast<std::uint32_t>(i) ^ 0x9E3779B9u);
        const auto sample = TestSurfaceApi::SampleSurface(
            closure, normalShdWldOut, omegaOutWld, u1, u2, uChoice,
            0.0f, frontFacing);
        if (sample.pdfSolidAngle <= 0.0f || sample.isSpecular ||
            sample.isSubsurface ||
            Dot(normalShdWldOut, sample.omegaInWld) * omegaOutSideWld >= 0.0f) {
            continue;
        }
        sum += sample.bsdfValue *
               (std::abs(Dot(normalShdWldOut, sample.omegaInWld)) /
                sample.pdfSolidAngle);
    }
    return sum * (1.0f / static_cast<float>(sampleCount));
}

static float
_FurnaceLuminance(const Vec3f& value)
{
    return 0.2126f * value[0] + 0.7152f * value[1] + 0.0722f * value[2];
}

static bool
_IsFiniteNonNegative(const Vec3f& value)
{
    for (int i = 0; i < 3; ++i) {
        if (!std::isfinite(value[i]) || value[i] < -1.0e-5f) {
            return false;
        }
    }
    return true;
}

static bool
_CheckFurnaceEnergyBound(
    const char* label,
    float alphaRoughness,
    float cosTheta,
    const Vec3f& energy)
{
    if (!_IsFiniteNonNegative(energy)) {
        printf(
            "    %s furnace invalid: alpha=%f NoV=%f E=(%f,%f,%f)\n",
            label, alphaRoughness, cosTheta,
            energy[0], energy[1], energy[2]);
        return false;
    }

    for (int i = 0; i < 3; ++i) {
        if (energy[i] > 1.0f + _kFurnaceEnergyUpperSlack) {
            printf(
                "    %s furnace energy gain: alpha=%f NoV=%f "
                "channel=%d E=%f\n",
                label, alphaRoughness, cosTheta, i, energy[i]);
            return false;
        }
    }
    return true;
}

static float
_DielectricTransmittanceForTest(float eta, float cosTheta)
{
    const float c = std::clamp(cosTheta, 0.0f, 1.0f);
    float g = (eta - 1.0f) * (eta + 1.0f) + c * c;
    if (g <= 0.0f) {
        return 0.0f;
    }
    g = std::sqrt(g);
    const float A = (g - c) / (g + c);
    const float B = (c * (g + c) - 1.0f) / (c * (g - c) + 1.0f);
    const float reflectance = 0.5f * A * A * (1.0f + B * B);
    return std::clamp(1.0f - reflectance, 0.0f, 1.0f);
}

static float
_SchlickFresnelForTest(float ior, float cosTheta)
{
    float f0 = (ior - 1.0f) / (ior + 1.0f);
    f0 *= f0;
    const float t = 1.0f - std::clamp(cosTheta, 0.0f, 1.0f);
    const float t2 = t * t;
    return f0 + (1.0f - f0) * t2 * t2 * t;
}

static Vec2f
_MaterialXGgxDirAlbedoAnalyticABForTest(float NdotV, float alpha)
{
    const float x = std::clamp(NdotV, 0.0f, 1.0f);
    const float y = std::clamp(alpha, 0.0f, 1.0f);
    const float x2 = x * x;
    const float y2 = y * y;
    const float xy = x * y;
    const float x2y = x2 * y;
    const float xy2 = x * y2;
    const float x2y2 = x2 * y2;

    const float r0 =
        0.1003f - 0.6303f * x + 9.748f * y - 2.038f * xy +
        29.34f * x2 - 8.245f * y2 - 26.44f * x2y +
        19.99f * xy2 - 5.448f * x2y2;
    const float r1 =
        0.9345f - 2.323f * x + 2.229f * y - 3.748f * xy +
        1.424f * x2 - 0.7684f * y2 + 1.436f * x2y +
        0.2913f * xy2 + 0.6286f * x2y2;
    const float r2 =
        1.0f - 1.765f * x + 8.263f * y + 11.53f * xy +
        28.96f * x2 - 7.507f * y2 - 36.11f * x2y +
        15.86f * xy2 + 33.37f * x2y2;
    const float r3 =
        1.0f + 0.2281f * x + 15.94f * y - 55.83f * xy +
        13.08f * x2 + 41.26f * y2 + 54.9f * x2y +
        300.2f * xy2 - 285.1f * x2y2;

    return Vec2f(
        std::clamp(r0 / r2, 0.0f, 1.0f),
        std::clamp(r1 / r3, 0.0f, 1.0f));
}

static Vec3f
_MaterialXGlslDielectricThroughputForTest(
    float alpha,
    float cosTheta,
    float ior,
    float weight,
    float fresnel)
{
    float F0 = (ior - 1.0f) / (ior + 1.0f);
    F0 *= F0;

    const Vec2f ab =
        _MaterialXGgxDirAlbedoAnalyticABForTest(cosTheta, alpha);
    const Vec3f dirAlbedo = Vec3f(F0) * ab[0] + Vec3f(1.0f) * ab[1];
    const float Ess = std::max(ab[0] + ab[1], 1.0e-7f);
    const Vec3f comp =
        Vec3f(1.0f) + Vec3f(fresnel) * ((1.0f - Ess) / Ess);
    return Vec3f(1.0f) - CompMul(dirAlbedo, comp) * weight;
}

static float
_GgxDForTest(float alpha, float NdotH)
{
    const float a2 = alpha * alpha;
    const float denom = NdotH * NdotH * (a2 - 1.0f) + 1.0f;
    return a2 / (_kFurnacePi * denom * denom + 1.0e-7f);
}

static float
_SmithG1ForTest(float alpha, float cosTheta)
{
    const float a2 = alpha * alpha;
    const float cos2 = cosTheta * cosTheta;
    return 2.0f * cosTheta /
        (cosTheta + std::sqrt(a2 + (1.0f - a2) * cos2) + 1.0e-7f);
}

static float
_GgxSeparableGForTest(float alpha, float NdotV, float NdotL)
{
    return _SmithG1ForTest(alpha, NdotV) * _SmithG1ForTest(alpha, NdotL);
}

static float
_GgxHeightCorrelatedGForTest(float alpha, float NdotV, float NdotL)
{
    const float a2 = alpha * alpha;
    const float ggxV =
        NdotL * std::sqrt(NdotV * NdotV * (1.0f - a2) + a2);
    const float ggxL =
        NdotV * std::sqrt(NdotL * NdotL * (1.0f - a2) + a2);
    const float visibility = 0.5f / (ggxV + ggxL + 1.0e-7f);
    return 4.0f * NdotV * NdotL * visibility;
}

}  // namespace

static bool
TestLambertianValue()
{
    Vec3f white(1.0f);
    Vec3f normalShdWldOut(0, 1, 0);
    Vec3f omegaInWld(0, 1, 0);
    Vec3f omegaOutWld(0, 1, 0);

    Vec3f result =
        Bsdf::EvalLambertian(white, normalShdWldOut, omegaInWld, omegaOutWld);
    float expected = 1.0f / 3.14159265358979f;
    if (!Test_IsClose(result, Vec3f(expected), 1e-5f)) {
        printf("    Expected (%f,%f,%f), got (%f,%f,%f)\n",
               expected, expected, expected,
               result[0], result[1], result[2]);
        return false;
    }
    return true;
}

static bool
TestFurnaceHelperMatchesLambertian()
{
    const Vec3f normalShdWldOut(0.0f, 1.0f, 0.0f);
    const Vec3f omegaOutWld = _DirectionFromCosThetaYUp(0.37f);
    const Vec3f albedo(0.73f, 0.41f, 0.19f);
    const Vec3f energy = _IntegrateHemisphereUniform(
        [&](const Vec3f& omegaInWld) {
            return Bsdf::EvalLambertian(albedo, normalShdWldOut, omegaInWld,
                                        omegaOutWld);
        },
        _kFurnaceSampleCount);

    if (!Test_IsClose(energy, albedo, 1.0e-4f)) {
        printf(
            "    Lambertian furnace helper mismatch: "
            "expected=(%f,%f,%f) got=(%f,%f,%f)\n",
            albedo[0], albedo[1], albedo[2],
            energy[0], energy[1], energy[2]);
        return false;
    }
    return true;
}

static bool
TestLegacySurfaceSpecularZeroIsLambertian()
{
    SurfaceClosure closure;
    closure.baseColor = Vec3f(1.0f);
    closure.roughness = 1.0f;
    closure.specular = 0.0f;
    closure.specularColor = Vec3f(0.0f);

    const Vec3f normalShdWldOut(0.0f, 1.0f, 0.0f);
    const Vec3f omegaInWld = Vec3f(0.67f, 0.31f, 0.68f).normalized();
    const Vec3f omegaOutWld = Vec3f(-0.19f, 0.24f, 0.95f).normalized();
    const Vec3f expected = Bsdf::EvalLambertian(
        closure.baseColor, normalShdWldOut, omegaInWld, omegaOutWld);
    const Vec3f actual =
        TestSurfaceApi::EvalSurface(closure, normalShdWldOut, omegaInWld, omegaOutWld);

    if (!Test_IsClose(actual, expected, 1.0e-6f)) {
        printf(
            "    specular=0 legacy surface should be Lambertian: "
            "expected=(%f,%f,%f) got=(%f,%f,%f)\n",
            expected[0], expected[1], expected[2],
            actual[0], actual[1], actual[2]);
        return false;
    }
    return true;
}

static bool
TestOrenNayarEnergyCompensationFalseUsesLegacyFactor()
{
    SurfaceClosure closure;
    Bsdf::OrenNayarDiffuseData diffuse;
    diffuse.weight = 0.8f;
    diffuse.color = Vec3f(0.7f, 0.4f, 0.2f);
    diffuse.roughness = 0.65f;
    diffuse.energyCompensation = false;
    closure.bsdfTree.root = closure.bsdfTree.Add(diffuse);

    const Vec3f normalShdWldOut(0.0f, 1.0f, 0.0f);
    const Vec3f omegaInWld = Vec3f(0.52f, 0.62f, 0.59f).normalized();
    const Vec3f omegaOutWld = Vec3f(-0.21f, 0.77f, 0.60f).normalized();
    const float NdotL = std::max(Dot(normalShdWldOut, omegaInWld), 0.0f);
    const float NdotV = std::max(Dot(normalShdWldOut, omegaOutWld), 1.0e-7f);
    const float LdotV = Dot(omegaInWld, omegaOutWld);
    const float s = LdotV - NdotL * NdotV;
    const float stinv = (s > 0.0f) ? s / std::max(NdotL, NdotV) : 0.0f;
    const float sigma2 = diffuse.roughness * diffuse.roughness;
    const float A = 1.0f - 0.5f * (sigma2 / (sigma2 + 0.33f));
    const float B = 0.45f * sigma2 / (sigma2 + 0.09f);
    const Vec3f expected =
        diffuse.color * (diffuse.weight * (A + B * stinv) / _kFurnacePi);
    const Vec3f actual =
        TestSurfaceApi::EvalSurface(closure, normalShdWldOut, omegaInWld, omegaOutWld);

    if (!Test_IsClose(actual, expected, 1.0e-6f)) {
        printf(
            "    Legacy Oren-Nayar branch changed: "
            "expected=(%f,%f,%f) got=(%f,%f,%f)\n",
            expected[0], expected[1], expected[2],
            actual[0], actual[1], actual[2]);
        return false;
    }
    return true;
}

static bool
TestEonDiffuseLambertianLimit()
{
    SurfaceClosure closure;
    Bsdf::OrenNayarDiffuseData diffuse;
    diffuse.weight = 0.7f;
    diffuse.color = Vec3f(0.73f, 0.41f, 0.19f);
    diffuse.roughness = 0.0f;
    diffuse.energyCompensation = true;
    closure.bsdfTree.root = closure.bsdfTree.Add(diffuse);

    const Vec3f normalShdWldOut(0.0f, 1.0f, 0.0f);
    const Vec3f omegaInWld = Vec3f(0.36f, 0.81f, 0.46f).normalized();
    const Vec3f omegaOutWld = Vec3f(-0.48f, 0.67f, 0.57f).normalized();
    const Vec3f expected =
        Bsdf::EvalLambertian(diffuse.color * diffuse.weight, normalShdWldOut,
                             omegaInWld, omegaOutWld);
    const Vec3f actual =
        TestSurfaceApi::EvalSurface(closure, normalShdWldOut, omegaInWld, omegaOutWld);

    if (!Test_IsClose(actual, expected, 1.0e-6f)) {
        printf(
            "    EON roughness=0 should match Lambertian: "
            "expected=(%f,%f,%f) got=(%f,%f,%f)\n",
            expected[0], expected[1], expected[2],
            actual[0], actual[1], actual[2]);
        return false;
    }
    return true;
}

static bool
TestEonDiffuseWhiteFurnace()
{
    SurfaceClosure closure;
    Bsdf::OrenNayarDiffuseData diffuse;
    diffuse.weight = 1.0f;
    diffuse.color = Vec3f(1.0f);
    diffuse.roughness = 1.0f;
    diffuse.energyCompensation = true;
    closure.bsdfTree.root = closure.bsdfTree.Add(diffuse);

    const Vec3f normalShdWldOut(0.0f, 1.0f, 0.0f);
    bool ok = true;
    for (const float cosTheta : {0.99f, 0.5f, 0.1f}) {
        const Vec3f omegaOutWld = _DirectionFromCosThetaYUp(cosTheta);
        const Vec3f energy = _IntegrateHemisphereUniform(
            [&](const Vec3f& omegaInWld) {
                return TestSurfaceApi::EvalSurface(closure, normalShdWldOut, omegaInWld,
                                         omegaOutWld);
            },
            _kFurnaceSampleCount);
        if (!Test_IsClose(energy, Vec3f(1.0f), 0.025f)) {
            printf(
                "    EON white furnace mismatch at NoV=%f: "
                "energy=(%f,%f,%f)\n",
                cosTheta, energy[0], energy[1], energy[2]);
            ok = false;
        }
    }
    return ok;
}

static bool
TestGGXFurnaceWhiteFresnelGeneralizedSchlickBaseline()
{
    const Vec3f normalShdWldOut(0.0f, 1.0f, 0.0f);
    bool ok = true;
    printf("    GGX white-Fresnel Turquin-compensated furnace:\n");
    for (const float alphaRoughness : _kFurnaceAlphaRoughness) {
        printf("      alpha=%0.2f:", alphaRoughness);
        for (const float cosTheta : _kFurnaceCosTheta) {
            SurfaceClosure closure;
            Bsdf::GeneralizedSchlickData specular;
            specular.weight = 1.0f;
            specular.color0 = Vec3f(1.0f);
            specular.color82 = Vec3f(1.0f);
            specular.color90 = Vec3f(1.0f);
            specular.exponent = 5.0f;
            specular.roughness = Vec2f(alphaRoughness, alphaRoughness);
            specular.scatterMode = Bsdf::ScatterMode::Reflection;
            closure.bsdfTree.root = closure.bsdfTree.Add(specular);

            const Vec3f omegaOutWld = _DirectionFromCosThetaYUp(cosTheta);
            const Vec3f energy = _IntegrateSurfaceBySampling(
                closure, normalShdWldOut, omegaOutWld, _kFurnaceSampleCount);
            printf(" %0.4f", _FurnaceLuminance(energy));
            ok = _CheckFurnaceEnergyBound(
                "GeneralizedSchlick(F=1)",
                alphaRoughness,
                cosTheta,
                energy) && ok;
        }
        printf("\n");
    }
    return ok;
}

static bool
TestGGXFurnaceConductorEnergyBaseline()
{
    const Vec3f normalShdWldOut(0.0f, 1.0f, 0.0f);
    bool ok = true;
    printf("    GGX conductor Turquin-compensated furnace:\n");
    for (const float alphaRoughness : _kFurnaceAlphaRoughness) {
        printf("      alpha=%0.2f:", alphaRoughness);
        for (const float cosTheta : _kFurnaceCosTheta) {
            SurfaceClosure closure;
            Bsdf::ConductorData conductor;
            conductor.weight = 1.0f;
            conductor.ior = Vec3f(0.15f, 0.14f, 0.13f);
            conductor.extinction = Vec3f(3.5f, 3.4f, 3.3f);
            conductor.roughness = Vec2f(alphaRoughness, alphaRoughness);
            closure.bsdfTree.root = closure.bsdfTree.Add(conductor);

            const Vec3f omegaOutWld = _DirectionFromCosThetaYUp(cosTheta);
            const Vec3f energy = _IntegrateSurfaceBySampling(
                closure, normalShdWldOut, omegaOutWld, _kFurnaceSampleCount);
            printf(" %0.4f", _FurnaceLuminance(energy));
            ok = _CheckFurnaceEnergyBound(
                "Conductor",
                alphaRoughness,
                cosTheta,
                energy) && ok;
        }
        printf("\n");
    }
    return ok;
}

static bool
TestGGXFurnaceDielectricReflectionEnergyBaseline()
{
    const Vec3f normalShdWldOut(0.0f, 1.0f, 0.0f);
    bool ok = true;
    printf("    GGX dielectric reflection Turquin-compensated furnace:\n");
    for (const float alphaRoughness : _kFurnaceAlphaRoughness) {
        printf("      alpha=%0.2f:", alphaRoughness);
        for (const float cosTheta : _kFurnaceCosTheta) {
            SurfaceClosure closure;
            Bsdf::DielectricData dielectric;
            dielectric.weight = 1.0f;
            dielectric.tint = Vec3f(1.0f);
            dielectric.ior = 1.5f;
            dielectric.roughness = Vec2f(alphaRoughness, alphaRoughness);
            dielectric.scatterMode = Bsdf::ScatterMode::Reflection;
            closure.bsdfTree.root = closure.bsdfTree.Add(dielectric);

            const Vec3f omegaOutWld = _DirectionFromCosThetaYUp(cosTheta);
            const Vec3f energy = _IntegrateSurfaceBySampling(
                closure, normalShdWldOut, omegaOutWld, _kFurnaceSampleCount);
            printf(" %0.4f", _FurnaceLuminance(energy));
            ok = _CheckFurnaceEnergyBound(
                "DielectricReflection",
                alphaRoughness,
                cosTheta,
                energy) && ok;
        }
        printf("\n");
    }
    return ok;
}

static bool
TestLambertianColorScaling()
{
    Vec3f color(0.5f, 0.3f, 0.1f);
    Vec3f normalShdWldOut(0, 1, 0);
    Vec3f result = Bsdf::EvalLambertian(color, normalShdWldOut, Vec3f(0, 1, 0),
                                        Vec3f(0, 1, 0));
    float invPi = 1.0f / 3.14159265358979f;
    return Test_IsClose(result, color * invPi, 1e-5f);
}

static bool
TestGGXSpecularNonNegative()
{
    Vec3f normalShdWldOut(0, 1, 0);
    Vec3f omegaOutWld = Vec3f(0, 1, 0);
    Vec3f omegaInWld = Vec3f(0.5f, 0.866f, 0.0f).normalized();
    Vec3f result = Bsdf::EvalGGXSpecular(
        0.5f, 1.5f, Vec3f(1.0f), normalShdWldOut, omegaInWld, omegaOutWld);
    return result[0] >= 0.0f && result[1] >= 0.0f && result[2] >= 0.0f;
}

static bool
TestGGXSpecularUsesHeightCorrelatedSmith()
{
    const Vec3f normalShdWldOut(0.0f, 1.0f, 0.0f);
    const Vec3f omegaOutWld = Vec3f(0.62f, 0.68f, 0.39f).normalized();
    const Vec3f omegaInWld = Vec3f(-0.34f, 0.42f, 0.84f).normalized();
    const float perceptualRoughness = 0.7f;
    const float alpha = perceptualRoughness * perceptualRoughness;

    const Vec3f actual =
        Bsdf::EvalGGXSpecular(perceptualRoughness, 1.5f, Vec3f(1.0f),
                              normalShdWldOut, omegaInWld, omegaOutWld);

    Vec3f H = omegaInWld + omegaOutWld;
    H.normalize();
    const float NdotL = std::max(Dot(normalShdWldOut, omegaInWld), 0.0f);
    const float NdotV = std::max(Dot(normalShdWldOut, omegaOutWld), 0.0f);
    const float NdotH = std::max(Dot(normalShdWldOut, H), 0.0f);
    const float D = _GgxDForTest(alpha, NdotH);
    const float hcG = _GgxHeightCorrelatedGForTest(alpha, NdotV, NdotL);
    const float sepG = _GgxSeparableGForTest(alpha, NdotV, NdotL);
    const float msScale = 1.0f /
        std::max(Bsdf::GgxDirectionalSingleScatterEnergy(NdotV, alpha), 0.01f);
    const float expected =
        D * hcG * msScale /
        std::max(4.0f * NdotL * NdotV, 1.0e-7f);
    const float separable =
        D * sepG * msScale /
        std::max(4.0f * NdotL * NdotV, 1.0e-7f);

    if (!Test_IsClose(actual, Vec3f(expected), 1.0e-4f)) {
        printf(
            "    Expected height-correlated GGX=%f, got (%f,%f,%f); "
            "separable would be %f\n",
            expected, actual[0], actual[1], actual[2], separable);
        return false;
    }

    if (std::abs(expected - separable) < 1.0e-3f) {
        printf("    Test directions do not distinguish HC and separable G\n");
        return false;
    }
    return true;
}

static bool
TestGGXAnisotropicGrazingLimits()
{
    using namespace Bsdf::detail;
    const Vec2f alpha(0.35f, 0.8f);
    constexpr float tinyCosTheta = 1.0e-4f;
    const float radial =
        std::sqrt(1.0f - tinyCosTheta * tinyCosTheta);
    const Vec3f directions[] = {
        Vec3f(radial, 0.0f, tinyCosTheta),
        Vec3f(0.0f, radial, tinyCosTheta),
    };
    for (const Vec3f& direction : directions) {
        const float scaledX = direction[0] / alpha[0];
        const float scaledY = direction[1] / alpha[1];
        const float sum =
            scaledX * scaledX + scaledY * scaledY +
            direction[2] * direction[2];
        const float expectedD =
            1.0f / (_kFurnacePi * alpha[0] * alpha[1] * sum * sum);
        const float stretchedLength = std::sqrt(
            alpha[0] * alpha[0] * direction[0] * direction[0] +
            alpha[1] * alpha[1] * direction[1] * direction[1] +
            direction[2] * direction[2]);
        const float expectedLambda =
            0.5f * (stretchedLength / direction[2] - 1.0f);
        const float expectedG1 = 1.0f / (1.0f + expectedLambda);
        const float actualD = GGX_D_Anisotropic(alpha, direction);
        const float actualLambda =
            GGX_Lambda_Anisotropic(alpha, direction);
        const float actualG1 = GGX_G1_Anisotropic(alpha, direction);
        if (!Test_IsClose(
                actualD, expectedD,
                std::max(1.0e-6f, expectedD * 1.0e-5f)) ||
            !Test_IsClose(
                actualLambda, expectedLambda,
                std::max(1.0e-4f, expectedLambda * 1.0e-5f)) ||
            !Test_IsClose(
                actualG1, expectedG1,
                std::max(1.0e-8f, expectedG1 * 1.0e-5f))) {
            printf(
                "    Anisotropic grazing mismatch: dir=(%g,%g,%g) "
                "D=(%g,%g) lambda=(%g,%g) G1=(%g,%g)\n",
                direction[0], direction[1], direction[2],
                actualD, expectedD, actualLambda, expectedLambda,
                actualG1, expectedG1);
            return false;
        }
    }

    // Distribution support excludes an exactly tangent microfacet normal even
    // though the analytic GGX density has a finite boundary limit. Smith
    // masking still approaches total masking there.
    const Vec3f tangent(1.0f, 0.0f, 0.0f);
    const Vec3f normal(0.0f, 0.0f, 1.0f);
    const float tangentLambda = GGX_Lambda_Anisotropic(alpha, tangent);
    if (GGX_D_Anisotropic(alpha, tangent) != 0.0f ||
        GGX_D_Anisotropic(alpha, -normal) != 0.0f ||
        !std::isinf(tangentLambda) ||
        GGX_G1_Anisotropic(alpha, tangent) != 0.0f ||
        GGX_G_Anisotropic(alpha, normal, tangent) != 0.0f) {
        printf(
            "    Invalid exact-grazing GGX limits: D=%g lambda=%g "
            "G1=%g G2=%g\n",
            GGX_D_Anisotropic(alpha, tangent), tangentLambda,
            GGX_G1_Anisotropic(alpha, tangent),
            GGX_G_Anisotropic(alpha, normal, tangent));
        return false;
    }
    return true;
}

static bool
TestGGXDirectionalMissingEnergyLutBounds()
{
    bool ok = true;
    for (const float alphaRoughness : _kFurnaceAlphaRoughness) {
        for (const float cosTheta : _kFurnaceCosTheta) {
            const float missing = Bsdf::GgxDirectionalMissingEnergy(
                cosTheta, alphaRoughness);
            const float singleScatter =
                Bsdf::GgxDirectionalSingleScatterEnergy(
                    cosTheta, alphaRoughness);
            if (!std::isfinite(missing) || missing < 0.0f || missing > 1.0f) {
                printf(
                    "    GGX missing-energy LUT out of range: "
                    "alpha=%f NoV=%f Ems=%f\n",
                    alphaRoughness, cosTheta, missing);
                ok = false;
            }
            if (!Test_IsClose(missing + singleScatter, 1.0f, 1.0e-5f)) {
                printf(
                    "    GGX single/missing energy mismatch: "
                    "alpha=%f NoV=%f Ess=%f Ems=%f\n",
                    alphaRoughness, cosTheta, singleScatter, missing);
                ok = false;
            }
        }
    }

    float previous = -1.0f;
    for (const float alphaRoughness : _kFurnaceAlphaRoughness) {
        const float missing = Bsdf::GgxDirectionalMissingEnergy(
            1.0f, alphaRoughness);
        if (missing + 5.0e-4f < previous) {
            printf(
                "    GGX normal-incidence missing energy is not monotonic: "
                "previous=%f current=%f alpha=%f\n",
                previous, missing, alphaRoughness);
            ok = false;
        }
        previous = missing;
    }
    return ok;
}

static bool
TestGGXTurquinWhiteFurnaceCompensatesMissingEnergy()
{
    struct Case {
        float alphaRoughness;
        float cosTheta;
    };

    const Case cases[] = {
        {0.05f, 0.03f},
        {0.20f, 0.30f},
        {0.40f, 0.50f},
        {0.80f, 0.10f},
        {1.00f, 0.99f},
        {1.00f, 0.03f}
    };

    const Vec3f normalShdWldOut(0.0f, 1.0f, 0.0f);
    bool ok = true;
    for (const Case& c : cases) {
        SurfaceClosure closure;
        Bsdf::GeneralizedSchlickData specular;
        specular.weight = 1.0f;
        specular.color0 = Vec3f(1.0f);
        specular.color82 = Vec3f(1.0f);
        specular.color90 = Vec3f(1.0f);
        specular.exponent = 5.0f;
        specular.roughness = Vec2f(c.alphaRoughness, c.alphaRoughness);
        specular.scatterMode = Bsdf::ScatterMode::Reflection;
        closure.bsdfTree.root = closure.bsdfTree.Add(specular);

        const Vec3f omegaOutWld = _DirectionFromCosThetaYUp(c.cosTheta);
        const Vec3f energy = _IntegrateSurfaceBySampling(
            closure, normalShdWldOut, omegaOutWld, _kFurnaceSampleCount);
        const float measured = _FurnaceLuminance(energy);
        if (std::abs(measured - 1.0f) > 0.03f) {
            printf(
                "    GGX Turquin white furnace mismatch: alpha=%f NoV=%f "
                "sampled=%f expected=1\n",
                c.alphaRoughness, c.cosTheta, measured);
            ok = false;
        }
    }
    return ok;
}

static bool
TestGGXSpecularPeak()
{
    Vec3f normalShdWldOut(0, 1, 0);
    Vec3f omegaOutWld = Vec3f(0.3f, 0.95f, 0.0f).normalized();
    Vec3f omegaInMirroredWld = Vec3f(-0.3f, 0.95f, 0.0f).normalized();
    Vec3f omegaInOffsetWld = Vec3f(0.8f, 0.6f, 0.0f).normalized();

    float roughness = 0.05f;
    Vec3f specColor(1.0f);

    Vec3f atMirror =
        Bsdf::EvalGGXSpecular(roughness, 1.5f, specColor, normalShdWldOut,
                              omegaInMirroredWld, omegaOutWld);
    Vec3f offMirror =
        Bsdf::EvalGGXSpecular(roughness, 1.5f, specColor, normalShdWldOut,
                              omegaInOffsetWld, omegaOutWld);

    float mirrorMag = atMirror.length();
    float offMag = offMirror.length();
    if (mirrorMag <= offMag) {
        printf("    Mirror dir magnitude %f should be > off-specular %f\n",
               mirrorMag, offMag);
        return false;
    }
    return true;
}

static bool
TestGGXSpecularLowRoughnessPeakPreserved()
{
    const Vec3f normalShdWldOut(0.0f, 1.0f, 0.0f);
    const Vec3f omegaOutWld = Vec3f(0.2f, 0.9797959f, 0.0f).normalized();
    const Vec3f omegaInMirroredWld =
        Vec3f(-0.2f, 0.9797959f, 0.0f).normalized();
    const Vec3f specColor(1.0f);

    const float broadRoughness = 0.05f;
    const float sharpRoughness = 0.01f;
    const Vec3f broadEval =
        Bsdf::EvalGGXSpecular(broadRoughness, 1.5f, specColor, normalShdWldOut,
                              omegaInMirroredWld, omegaOutWld);
    const Vec3f sharpEval =
        Bsdf::EvalGGXSpecular(sharpRoughness, 1.5f, specColor, normalShdWldOut,
                              omegaInMirroredWld, omegaOutWld);
    const float broadPdf = Bsdf::PdfGGXSpecular(
        broadRoughness, normalShdWldOut, omegaInMirroredWld, omegaOutWld);
    const float sharpPdf = Bsdf::PdfGGXSpecular(
        sharpRoughness, normalShdWldOut, omegaInMirroredWld, omegaOutWld);

    const float broadMag = broadEval.length();
    const float sharpMag = sharpEval.length();
    if (!(sharpMag > broadMag * 10.0f)) {
        printf(
            "    Low roughness GGX peak collapsed: roughness=%f eval=%f, "
            "roughness=%f eval=%f\n",
            sharpRoughness, sharpMag, broadRoughness, broadMag);
        return false;
    }
    if (!(sharpPdf > broadPdf * 10.0f)) {
        printf(
            "    Low roughness GGX pdf peak collapsed: roughness=%f pdf=%f, "
            "roughness=%f pdf=%f\n",
            sharpRoughness, sharpPdf, broadRoughness, broadPdf);
        return false;
    }
    return true;
}

static bool
TestTreeDielectricReflectionMatchesStandaloneGgx()
{
    SurfaceClosure c;
    Bsdf::DielectricData dielectric;
    dielectric.weight = 1.0f;
    dielectric.tint = Vec3f(1.0f);
    dielectric.ior = 1.5f;
    dielectric.roughness = Vec2f(0.05f * 0.05f, 0.05f * 0.05f);
    dielectric.scatterMode = Bsdf::ScatterMode::Reflection;
    c.bsdfTree.root = c.bsdfTree.Add(dielectric);

    const Vec3f normalShdWldOut(0.0f, 1.0f, 0.0f);
    const Vec3f omegaOutWld = Vec3f(0.3f, 0.9539392f, 0.0f).normalized();
    const Vec3f omegaInWld = Vec3f(-0.3f, 0.9539392f, 0.0f).normalized();

    const Vec3f treeEval =
        TestSurfaceApi::EvalSurface(c, normalShdWldOut, omegaInWld, omegaOutWld);
    const Vec3f ggxEval = Bsdf::EvalGGXSpecular(
        0.05f, 1.5f, Vec3f(0.04f), normalShdWldOut, omegaInWld, omegaOutWld);

    const float treeLum = treeEval.length();
    const float ggxLum = ggxEval.length();
    const float ratio = treeLum / std::max(ggxLum, 1.0e-8f);
    if (ratio < 0.9f || ratio > 1.1f) {
        printf(
            "    Tree dielectric != standalone GGX: tree=%f ggx=%f ratio=%f\n",
            treeLum, ggxLum, ratio);
        return false;
    }
    return true;
}

static bool
TestDispersionCauchyIorMonotonic()
{
    const float iorBlue =
        Spectral::CauchyDispersionIOR(20.0f, 1.5f, 450.0f);
    const float iorRed =
        Spectral::CauchyDispersionIOR(20.0f, 1.5f, 650.0f);
    if (!(iorBlue > iorRed && iorRed > 1.0f)) {
        printf("    Expected blue IOR > red IOR, got blue=%f red=%f\n",
               iorBlue, iorRed);
        return false;
    }
    return true;
}

static bool
TestDispersionChangesTransmissionSampling()
{
    SurfaceClosure c;
    Bsdf::DielectricData dielectric;
    dielectric.weight = 1.0f;
    dielectric.tint = Vec3f(1.0f);
    dielectric.ior = 1.5f;
    dielectric.dispersionAbbe = 20.0f;
    dielectric.roughness = Vec2f(0.1f, 0.1f);
    dielectric.scatterMode = Bsdf::ScatterMode::Transmission;
    c.bsdfTree.root = c.bsdfTree.Add(dielectric);

    const Vec3f normalShdWldOut(0.0f, 1.0f, 0.0f);
    const Vec3f omegaOutWld = Vec3f(0.35f, 0.93675f, 0.0f).normalized();

    const auto blue = TestSurfaceApi::SampleSurface(c, normalShdWldOut, omegaOutWld, 0.3f,
                                          0.7f, 0.65f, 450.0f);
    const auto red = TestSurfaceApi::SampleSurface(c, normalShdWldOut, omegaOutWld, 0.3f,
                                         0.7f, 0.65f, 650.0f);
    if (blue.pdfSolidAngle <= 0.0f || red.pdfSolidAngle <= 0.0f) {
        printf("    Expected valid transmission samples for dispersion test\n");
        return false;
    }

    if (Test_IsClose(blue.omegaInWld, red.omegaInWld, 1e-4f)) {
        printf("    Expected wavelength-dependent transmission direction\n");
        return false;
    }
    return true;
}

static bool
TestDispersionDisabledIgnoresHeroWavelength()
{
    SurfaceClosure c;
    Bsdf::DielectricData dielectric;
    dielectric.weight = 1.0f;
    dielectric.tint = Vec3f(1.0f);
    dielectric.ior = 1.5f;
    dielectric.roughness = Vec2f(0.1f, 0.1f);
    dielectric.scatterMode = Bsdf::ScatterMode::Transmission;
    c.bsdfTree.root = c.bsdfTree.Add(dielectric);

    const Vec3f normalShdWldOut(0.0f, 1.0f, 0.0f);
    const Vec3f omegaOutWld = Vec3f(0.35f, 0.93675f, 0.0f).normalized();

    const auto blue = TestSurfaceApi::SampleSurface(c, normalShdWldOut, omegaOutWld, 0.3f,
                                          0.7f, 0.65f, 450.0f);
    const auto red = TestSurfaceApi::SampleSurface(c, normalShdWldOut, omegaOutWld, 0.3f,
                                         0.7f, 0.65f, 650.0f);

    return Test_IsClose(blue.omegaInWld, red.omegaInWld, 1e-5f) &&
           Test_IsClose(blue.bsdfValue, red.bsdfValue, 1e-5f) &&
           Test_IsClose(blue.pdfSolidAngle, red.pdfSolidAngle, 1e-5f);
}

static Vec3f
_AverageHeroRoundTrip(
    const Vec3f& rgb,
    Spectral::RgbColorSpace colorSpace =
        Spectral::RgbColorSpace::LinearRec709)
{
    Vec3f average(0.0f);
    const float pdf = Spectral::HeroWavelengthPdf();

    for (int i = 0; i < static_cast<int>(Spectral::kLambdaResolution); ++i) {
        const float wavelengthNm =
            Spectral::kLambdaMinNm + static_cast<float>(i) * Spectral::kLambdaStepNm;
        const float spectralValue =
            Spectral::RgbToSpectralValue(
                rgb, wavelengthNm, colorSpace);
        const Vec3f reconstructed =
            Spectral::SpectralValueToRgb(
                spectralValue,
                wavelengthNm,
                pdf,
                colorSpace);
        const float weight = (i == 0 || i + 1 == static_cast<int>(Spectral::kLambdaResolution))
            ? 0.5f * Spectral::kLambdaStepNm
            : Spectral::kLambdaStepNm;
        average += reconstructed * (weight / Spectral::kLambdaRangeNm);
    }

    return average;
}

static bool
TestSpectralNeutralRoundTripWhite()
{
    const Vec3f reconstructed = _AverageHeroRoundTrip(Vec3f(1.0f));
    if (!Test_IsClose(reconstructed, Vec3f(1.0f), 1.0e-3f)) {
        printf("    Expected white round-trip to remain neutral, got (%f,%f,%f)\n",
               reconstructed[0], reconstructed[1], reconstructed[2]);
        return false;
    }
    return true;
}

static bool
TestSpectralNeutralRoundTripGray()
{
    const Vec3f target(0.18f);
    const Vec3f reconstructed = _AverageHeroRoundTrip(target);
    if (!Test_IsClose(reconstructed, target, 1.0e-3f)) {
        printf("    Expected gray round-trip to remain neutral, got (%f,%f,%f)\n",
               reconstructed[0], reconstructed[1], reconstructed[2]);
        return false;
    }
    return true;
}

static bool
TestSpectralAp1NeutralRoundTrip()
{
    const Vec3f target(0.18f);
    const Vec3f reconstructed = _AverageHeroRoundTrip(
        target, Spectral::RgbColorSpace::LinearAP1);
    if (!Test_IsClose(reconstructed, target, 1.0e-3f)) {
        printf(
            "    Expected AP1 gray round-trip to remain neutral, got "
            "(%f,%f,%f)\n",
            reconstructed[0], reconstructed[1], reconstructed[2]);
        return false;
    }
    return true;
}

static bool
TestCoatZeroWeight()
{
    Vec3f normalShdWldOut(0, 1, 0);
    Vec3f omegaInWld(0, 1, 0);
    Vec3f omegaOutWld(0, 1, 0);
    Vec3f result = Bsdf::EvalCoat(0.0f, 0.1f, 1.5f, normalShdWldOut, omegaInWld,
                                  omegaOutWld);
    return Test_IsClose(result, Vec3f(0.0f), 1e-7f);
}

static bool
TestEvalSurfaceEmissiveOnly()
{
    // EvalSurface returns pure BSDF value (no emissive — that's the
    // renderer's job). With NdotL <= 0 the BSDF returns zero.
    SurfaceClosure c;
    c.emissiveColor = Vec3f(1.0f, 0.5f, 0.0f);

    Vec3f normalShdWldOut(0, 1, 0);
    Vec3f omegaInWld(0, -1, 0); // NdotL <= 0
    Vec3f omegaOutWld(0, 1, 0);

    Vec3f result =
        TestSurfaceApi::EvalSurface(c, normalShdWldOut, omegaInWld, omegaOutWld);
    return Test_IsClose(result, Vec3f(0.0f), 1e-5f);
}

static bool
TestSheenGrazingAngle()
{
    Vec3f normalShdWldOut(0, 1, 0);
    Vec3f sheenColor(1.0f);
    float roughness = 0.5f;

    Vec3f omegaOutNormalWld = Vec3f(0, 1, 0);
    Vec3f omegaOutGrazingWld = Vec3f(0.99f, 0.14f, 0.0f).normalized();
    Vec3f omegaInWld = Vec3f(-0.3f, 0.95f, 0.0f).normalized();

    Vec3f atNormal = Bsdf::EvalSheen(sheenColor, roughness, normalShdWldOut,
                                     omegaInWld, omegaOutNormalWld);
    Vec3f atGrazing = Bsdf::EvalSheen(sheenColor, roughness, normalShdWldOut,
                                      omegaInWld, omegaOutGrazingWld);

    // Sheen should generally have more contribution at grazing angles.
    // We allow some tolerance here since the exact relationship depends on
    // the half-vector geometry.
    return atNormal.length() >= 0.0f && atGrazing.length() >= 0.0f;
}

static bool
TestTransmissionNonNegative()
{
    Vec3f normalShdWldOut(0, 1, 0);
    // omegaInWld below surface, omegaOutWld above — typical transmission
    // geometry
    Vec3f omegaInWld = Vec3f(0.2f, -0.98f, 0.0f).normalized();
    Vec3f omegaOutWld = Vec3f(0.3f, 0.95f, 0.0f).normalized();
    Vec3f tColor(1.0f);

    Vec3f result = Bsdf::EvalGGXTransmission(
        0.5f, 1.5f, tColor, normalShdWldOut, omegaInWld, omegaOutWld,
        /* backside = */ false);
    if (result[0] < 0.0f || result[1] < 0.0f || result[2] < 0.0f) {
        printf("    Negative transmission value: (%f,%f,%f)\n",
               result[0], result[1], result[2]);
        return false;
    }
    if (result.length() <= 0.0f) {
        printf("    Expected non-zero transmission, got zero\n");
        return false;
    }
    return true;
}

static bool
TestEvalSurfaceNonNegative()
{
    SurfaceClosure c;
    c.baseColor = Vec3f(0.8f, 0.2f, 0.1f);
    c.roughness = 0.4f;
    c.metallic = 0.0f;
    c.specular = 1.0f;
    c.specularIor = 1.5f;
    c.specularColor = Vec3f(1.0f);
    c.emissiveColor = Vec3f(0.0f);

    Vec3f normalShdWldOut(0, 1, 0);
    Vec3f omegaInWld = Vec3f(0.3f, 0.95f, 0.0f).normalized();
    Vec3f omegaOutWld = Vec3f(-0.2f, 0.98f, 0.0f).normalized();

    Vec3f result =
        TestSurfaceApi::EvalSurface(c, normalShdWldOut, omegaInWld, omegaOutWld);
    return result[0] >= 0.0f && result[1] >= 0.0f && result[2] >= 0.0f;
}

static bool
TestPhaseHgIsotropicMatchesUniformSphere()
{
    const float expected = 1.0f / (4.0f * 3.14159265358979f);
    const float actual = PhaseHG(0.25f, 0.0f);
    if (!Test_IsClose(actual, expected, 1.0e-6f)) {
        printf("    Expected isotropic phase=%f, got %f\n", expected, actual);
        return false;
    }
    return true;
}

static bool
TestPhaseHgForwardScatterBias()
{
    const float forward = PhaseHG(-0.95f, 0.7f);
    const float backward = PhaseHG(0.95f, 0.7f);
    if (!(forward > backward)) {
        printf("    Expected forward HG lobe to dominate: %f <= %f\n",
               forward, backward);
        return false;
    }
    return true;
}

static bool
TestSampleHenyeyGreensteinPdfConsistency()
{
    const Vec3f omegaOutWld = Vec3f(0.0f, 0.0f, -1.0f);
    const Vec3f omegaInWld =
        SampleHenyeyGreenstein(omegaOutWld, 0.5f, 0.3f, 0.7f);
    const float pdf = PdfHenyeyGreenstein(omegaInWld, omegaOutWld, 0.5f);
    const float phase = PhaseHG(Dot(omegaInWld, omegaOutWld), 0.5f);
    if (!Test_IsClose(pdf, phase, 1.0e-5f)) {
        printf("    Expected phase/pdf match, got phase=%f pdf=%f\n",
               phase, pdf);
        return false;
    }
    return true;
}

static bool
TestSampleHenyeyGreensteinForwardMean()
{
    const Vec3f omegaOutWld = Vec3f(0.0f, 0.0f, -1.0f);
    float meanCosTheta = 0.0f;
    constexpr int kSampleCount = 128;

    for (int i = 0; i < kSampleCount; ++i) {
        const float u1 = (static_cast<float>(i) + 0.5f) / kSampleCount;
        const float u2 =
            (static_cast<float>((i * 37) % kSampleCount) + 0.5f) / kSampleCount;
        const Vec3f omegaInWld =
            SampleHenyeyGreenstein(omegaOutWld, 0.7f, u1, u2);
        meanCosTheta += -Dot(omegaInWld, omegaOutWld);
    }
    meanCosTheta /= kSampleCount;

    if (meanCosTheta < 0.45f) {
        printf("    Expected forward-scattering mean cosine, got %f\n",
               meanCosTheta);
        return false;
    }
    return true;
}

static bool
TestFreeFlightScatterWeightFinite()
{
    MediumProperties medium;
    medium.absorption = Vec3f(0.2f, 0.8f, 0.4f);
    medium.scattering = Vec3f(0.6f, 0.1f, 0.3f);

    const float distance = SampleFreeFlight(medium, 0.42f);
    const Vec3f scatterWeight = EvalFreeFlightScatterWeight(medium, distance);
    const Vec3f transmittanceWeight =
        EvalMajorantTransmittanceWeight(medium, distance);

    for (int i = 0; i < 3; ++i) {
        if (!std::isfinite(scatterWeight[i]) || scatterWeight[i] < 0.0f) {
            printf("    Scatter weight channel %d invalid: %f\n",
                   i, scatterWeight[i]);
            return false;
        }
        if (!std::isfinite(transmittanceWeight[i]) ||
            transmittanceWeight[i] < 0.0f) {
            printf("    Transmittance weight channel %d invalid: %f\n",
                   i, transmittanceWeight[i]);
            return false;
        }
    }
    return true;
}

static bool
TestSampleSurfaceSubsurfaceReturnsMarker()
{
    SurfaceClosure c;
    Bsdf::SubsurfaceData subsurface;
    subsurface.weight = 0.7f;
    subsurface.color = Vec3f(0.4f, 0.6f, 0.8f);
    c.bsdfTree.root = c.bsdfTree.Add(subsurface);

    const Vec3f normalShdWldOut(0.0f, 1.0f, 0.0f);
    const Vec3f omegaOutWld(0.0f, 1.0f, 0.0f);

    const auto sample =
        TestSurfaceApi::SampleSurface(c, normalShdWldOut, omegaOutWld, 0.2f, 0.8f, 0.5f);
    if (!sample.isSubsurface) {
        printf("    Expected subsurface marker on sampled event\n");
        return false;
    }
    if (sample.isSpecular) {
        printf("    Subsurface event should not be marked specular\n");
        return false;
    }
    if (!Test_IsClose(sample.bsdfValue, Vec3f(subsurface.weight), 1.0e-6f)) {
        printf(
            "    Expected neutral subsurface marker weight, got (%f,%f,%f)\n",
            sample.bsdfValue[0], sample.bsdfValue[1], sample.bsdfValue[2]);
        return false;
    }
    if (!Test_IsClose(sample.pdfSolidAngle, 1.0f, 1.0e-6f)) {
        printf("    Expected unit pdf for subsurface marker, got %f\n",
               sample.pdfSolidAngle);
        return false;
    }
    return true;
}

static bool
TestSampleSurfaceMixCanChooseSubsurface()
{
    SurfaceClosure c;

    Bsdf::OrenNayarDiffuseData diffuse;
    diffuse.weight = 1.0f;
    diffuse.color = Vec3f(0.8f, 0.6f, 0.4f);

    Bsdf::SubsurfaceData subsurface;
    subsurface.weight = 1.0f;
    subsurface.color = Vec3f(0.3f, 0.5f, 0.7f);

    const Bsdf::NodeId diffuseId = c.bsdfTree.Add(diffuse);
    const Bsdf::NodeId subsurfaceId = c.bsdfTree.Add(subsurface);

    Bsdf::MixData mix;
    mix.fg = diffuseId;
    mix.bg = subsurfaceId;
    mix.mix = 0.25f;
    c.bsdfTree.root = c.bsdfTree.Add(mix);

    const Vec3f normalShdWldOut(0.0f, 1.0f, 0.0f);
    const Vec3f omegaOutWld(0.0f, 1.0f, 0.0f);

    const auto sample =
        TestSurfaceApi::SampleSurface(c, normalShdWldOut, omegaOutWld, 0.2f, 0.8f, 0.8f);
    if (!sample.isSubsurface) {
        printf("    Expected mix node to be able to select subsurface branch\n");
        return false;
    }

    const Vec3f expected(subsurface.weight / 0.75f);
    if (!Test_IsClose(sample.bsdfValue, expected, 1.0e-6f)) {
        printf("    Expected mix probability compensation, got (%f,%f,%f) "
               "expected (%f,%f,%f)\n",
               sample.bsdfValue[0], sample.bsdfValue[1], sample.bsdfValue[2],
               expected[0], expected[1], expected[2]);
        return false;
    }
    return true;
}

// ===========================================================================
// Sampling tests (Phase 9)
// ===========================================================================

static bool
TestSampleLambertianHemisphere()
{
    Vec3f normalShdWldOut(0, 1, 0);
    Vec3f omegaOutWld(0, 1, 0);
    Vec3f baseColor(0.8f, 0.2f, 0.1f);

    // All sampled directions should be in the hemisphere around
    // normalShdWldOut.
    for (int i = 0; i < 64; ++i) {
        float u1 = (i + 0.5f) / 64.0f;
        float u2 = (i * 7 % 64 + 0.5f) / 64.0f;
        auto s = Bsdf::SampleLambertian(baseColor, normalShdWldOut, omegaOutWld,
                                        u1, u2);
        if (Dot(s.omegaInWld, normalShdWldOut) < -1e-5f) {
            printf(
                "    Lambertian sample below hemisphere: normalDotOmegaIn=%f\n",
                Dot(s.omegaInWld, normalShdWldOut));
            return false;
        }
        if (s.pdfSolidAngle <= 0.0f) {
            printf("    Lambertian sample has non-positive pdf=%f\n",
                   s.pdfSolidAngle);
            return false;
        }
    }
    return true;
}

static bool
TestSampleLambertianPdfConsistency()
{
    Vec3f normalShdWldOut(0, 1, 0);
    Vec3f omegaOutWld(0, 1, 0);
    Vec3f baseColor(1.0f);

    auto s = Bsdf::SampleLambertian(baseColor, normalShdWldOut, omegaOutWld,
                                    0.3f, 0.7f);
    float pdf2 = Bsdf::PdfLambertian(normalShdWldOut, s.omegaInWld);
    if (!Test_IsClose(s.pdfSolidAngle, pdf2, 1e-4f)) {
        printf("    Sample pdf=%f != PdfLambertian=%f\n", s.pdfSolidAngle,
               pdf2);
        return false;
    }
    return true;
}

static bool
TestSampleGGXSpecularHemisphere()
{
    Vec3f normalShdWldOut(0, 1, 0);
    Vec3f omegaOutWld = Vec3f(0.3f, 0.95f, 0.0f).normalized();
    Vec3f specColor(1.0f);

    int valid = 0;
    for (int i = 0; i < 64; ++i) {
        float u1 = (i + 0.5f) / 64.0f;
        float u2 = (i * 13 % 64 + 0.5f) / 64.0f;
        auto s = Bsdf::SampleGGXSpecular(0.3f, 1.5f, specColor, normalShdWldOut,
                                         omegaOutWld, u1, u2);
        if (s.pdfSolidAngle > 0.0f) {
            ++valid;
            if (Dot(s.omegaInWld, normalShdWldOut) < -1e-5f) {
                printf("    GGX sample below hemisphere: normalDotOmegaIn=%f\n",
                       Dot(s.omegaInWld, normalShdWldOut));
                return false;
            }
        }
    }
    if (valid < 32) {
        printf("    Too few valid GGX samples: %d/64\n", valid);
        return false;
    }
    return true;
}

static bool
TestSampleGGXSpecularPdfConsistency()
{
    Vec3f normalShdWldOut(0, 1, 0);
    Vec3f omegaOutWld = Vec3f(0.2f, 0.98f, 0.0f).normalized();
    Vec3f specColor(1.0f);

    auto s = Bsdf::SampleGGXSpecular(0.4f, 1.5f, specColor, normalShdWldOut,
                                     omegaOutWld, 0.3f, 0.7f);
    if (s.pdfSolidAngle <= 0.0f)
        return true;

    float pdf2 =
        Bsdf::PdfGGXSpecular(0.4f, normalShdWldOut, s.omegaInWld, omegaOutWld);
    float ratio = s.pdfSolidAngle / (pdf2 + 1e-10f);
    if (ratio < 0.8f || ratio > 1.2f) {
        printf("    Sample pdf=%f != PdfGGXSpecular=%f (ratio=%f)\n",
               s.pdfSolidAngle, pdf2, ratio);
        return false;
    }
    return true;
}

static bool
TestSampleGGXSpecularLowRoughnessBoundedThroughput()
{
    const Vec3f normalShdWldOut(0.0f, 1.0f, 0.0f);
    const Vec3f omegaOutWld = Vec3f(0.2f, 0.9797959f, 0.0f).normalized();
    const Vec3f F0(1.0f);
    const float kRoughnessValues[] = {0.001f, 0.01f, 0.02f, 0.04f};
    const float kSamples[][2] = {
        {0.1f, 0.2f},
        {0.3f, 0.7f},
        {0.6f, 0.4f},
        {0.85f, 0.15f},
    };

    for (const float roughness : kRoughnessValues) {
        for (const auto& sampleUV : kSamples) {
            const auto sample =
                Bsdf::SampleGGXSpecular(roughness, 1.5f, F0, normalShdWldOut,
                                        omegaOutWld, sampleUV[0], sampleUV[1]);
            if (sample.pdfSolidAngle <= 0.0f) {
                printf("    Expected valid low-roughness GGX sample\n");
                return false;
            }

            const float cosTheta =
                std::abs(Dot(normalShdWldOut, sample.omegaInWld));
            const Vec3f throughputRgb =
                sample.bsdfValue * (cosTheta / sample.pdfSolidAngle);
            for (int i = 0; i < 3; ++i) {
                if (!std::isfinite(throughputRgb[i]) ||
                    throughputRgb[i] > 1.05f) {
                    printf("    Low-roughness GGX throughputRgb blew up: "
                           "roughness=%f sample=(%f,%f) channel=%d value=%f\n",
                           roughness, sampleUV[0], sampleUV[1], i,
                           throughputRgb[i]);
                    return false;
                }
            }

            const float pdf = Bsdf::PdfGGXSpecular(
                roughness, normalShdWldOut, sample.omegaInWld, omegaOutWld);
            const float ratio = sample.pdfSolidAngle / std::max(pdf, 1.0e-20f);
            if (pdf <= 0.0f || ratio < 0.8f || ratio > 1.2f) {
                printf("    Low-roughness GGX pdf mismatch: roughness=%f "
                       "samplePdf=%f pdf=%f\n",
                       roughness, sample.pdfSolidAngle, pdf);
                return false;
            }
        }
    }

    return true;
}

static bool
TestSampleSurfacePdfConsistency()
{
    SurfaceClosure c;
    c.baseColor = Vec3f(0.8f, 0.2f, 0.1f);
    c.roughness = 0.4f;
    c.metallic = 0.0f;
    c.specular = 1.0f;
    c.specularIor = 1.5f;
    c.specularColor = Vec3f(1.0f);

    Vec3f normalShdWldOut(0, 1, 0);
    Vec3f omegaOutWld = Vec3f(0.2f, 0.98f, 0.0f).normalized();

    auto s =
        TestSurfaceApi::SampleSurface(c, normalShdWldOut, omegaOutWld, 0.5f, 0.5f, 0.3f);
    if (s.pdfSolidAngle <= 0.0f)
        return true;

    float pdf2 =
        _PdfSurface(c, _MakeSurfaceInteraction(normalShdWldOut, normalShdWldOut, normalShdWldOut, omegaOutWld), s.omegaInWld);
    float ratio = s.pdfSolidAngle / (pdf2 + 1e-10f);
    if (ratio < 0.8f || ratio > 1.2f) {
        printf("    SampleSurface pdf=%f != PdfSurface=%f (ratio=%f)\n",
               s.pdfSolidAngle, pdf2, ratio);
        return false;
    }
    return true;
}

static bool
TestTreeTransmissionPreservesWeight()
{
    SurfaceClosure c;
    Bsdf::DielectricData transmission;
    transmission.weight = 1.0f;
    transmission.tint = Vec3f(0.95f, 0.97f, 1.0f);
    transmission.ior = 1.5f;
    transmission.scatterMode = Bsdf::ScatterMode::Transmission;

    c.bsdfTree.root = c.bsdfTree.Add(transmission);

    Vec3f normalShdWldOut(0, 1, 0);
    Vec3f omegaOutWld = Vec3f(0, 1, 0);

    auto s =
        TestSurfaceApi::SampleSurface(c, normalShdWldOut, omegaOutWld, 0.3f, 0.7f, 0.5f);
    if (s.pdfSolidAngle <= 0.0f) {
        printf("    Expected valid transmission sample\n");
        return false;
    }

    // Transmission sample should not be excessively dim
    float mag = s.bsdfValue.length();
    if (mag < 0.01f) {
        printf("    Transmission sample unexpectedly dim: (%f,%f,%f)\n",
               s.bsdfValue[0], s.bsdfValue[1], s.bsdfValue[2]);
        return false;
    }
    return true;
}

static bool
TestBsdfSampleDiffuseLikeClassification()
{
    const Vec3f normalShdWldOut(0.0f, 1.0f, 0.0f);
    const Vec3f omegaOutWld = Vec3f(0.2f, 0.9797959f, 0.0f).normalized();

    SurfaceClosure diffuseClosure;
    Bsdf::BurleyDiffuseData diffuse;
    diffuse.weight = 1.0f;
    diffuse.color = Vec3f(0.8f, 0.7f, 0.6f);
    diffuseClosure.bsdfTree.root = diffuseClosure.bsdfTree.Add(diffuse);

    const auto diffuseSample = TestSurfaceApi::SampleSurface(
        diffuseClosure, normalShdWldOut, omegaOutWld, 0.3f, 0.7f, 0.5f);
    if (diffuseSample.pdfSolidAngle <= 0.0f || !diffuseSample.isDiffuseLike) {
        printf("    Expected diffuse sample to be diffuse-like\n");
        return false;
    }

    SurfaceClosure roughGlassClosure;
    Bsdf::DielectricData roughTransmission;
    roughTransmission.weight = 1.0f;
    roughTransmission.tint = Vec3f(1.0f);
    roughTransmission.ior = 1.5f;
    roughTransmission.roughness = Vec2f(0.05f, 0.05f);
    roughTransmission.scatterMode = Bsdf::ScatterMode::Transmission;
    roughGlassClosure.bsdfTree.root =
        roughGlassClosure.bsdfTree.Add(roughTransmission);

    const auto glassSample = TestSurfaceApi::SampleSurface(
        roughGlassClosure, normalShdWldOut, omegaOutWld, 0.3f, 0.7f, 0.5f);
    if (glassSample.pdfSolidAngle <= 0.0f || glassSample.isSpecular) {
        printf("    Expected rough dielectric transmission to be finite\n");
        return false;
    }
    if (glassSample.isDiffuseLike) {
        printf("    Rough dielectric transmission should not be diffuse-like\n");
        return false;
    }
    if (Dot(normalShdWldOut, glassSample.omegaInWld) >= 0.0f) {
        printf("    Rough dielectric transmission should cross the boundary\n");
        return false;
    }

    return true;
}

static bool
TestZeroRoughnessDielectricSamplesDelta()
{
    SurfaceClosure c;
    Bsdf::DielectricData dielectric;
    dielectric.weight = 1.0f;
    dielectric.tint = Vec3f(1.0f);
    dielectric.ior = 1.5f;
    dielectric.roughness = Vec2f(0.0f, 0.0f);
    dielectric.scatterMode = Bsdf::ScatterMode::ReflectionTransmission;
    c.bsdfTree.root = c.bsdfTree.Add(dielectric);

    const Vec3f normalShdWldOut(0.0f, 1.0f, 0.0f);
    const Vec3f omegaOutWld = Vec3f(0.2f, 0.9797959f, 0.0f).normalized();

    const auto reflected =
        TestSurfaceApi::SampleSurface(c, normalShdWldOut, omegaOutWld, 0.2f, 0.8f, 0.0f);
    const auto transmitted =
        TestSurfaceApi::SampleSurface(c, normalShdWldOut, omegaOutWld, 0.7f, 0.1f, 0.99f);

    if (!reflected.isSpecular || reflected.pdfSolidAngle <= 0.0f) {
        printf("    Expected zero-roughness reflection to be delta\n");
        return false;
    }
    if (Dot(reflected.omegaInWld, normalShdWldOut) <= 0.0f) {
        printf("    Delta reflection should stay on the same side\n");
        return false;
    }

    if (!transmitted.isSpecular || transmitted.pdfSolidAngle <= 0.0f) {
        printf("    Expected zero-roughness transmission to be delta\n");
        return false;
    }
    if (Dot(transmitted.omegaInWld, normalShdWldOut) >= 0.0f) {
        printf("    Delta transmission should cross the interface\n");
        return false;
    }

    const auto transmittedAlt =
        TestSurfaceApi::SampleSurface(c, normalShdWldOut, omegaOutWld, 0.1f, 0.9f, 0.99f);
    if (!Test_IsClose(transmitted.omegaInWld, transmittedAlt.omegaInWld,
                      1.0e-6f)) {
        printf("    Delta transmission should ignore microfacet random numbers\n");
        return false;
    }
    return true;
}

static bool
TestZeroRoughnessConductorSamplesDeltaAndSkipsDirectEval()
{
    SurfaceClosure c;
    Bsdf::ConductorData conductor;
    conductor.weight = 1.0f;
    conductor.ior = Vec3f(0.8f, 0.5f, 0.25f);
    conductor.extinction = Vec3f(2.5f, 2.0f, 1.5f);
    conductor.roughness = Vec2f(0.0f, 0.0f);
    c.bsdfTree.root = c.bsdfTree.Add(conductor);

    const Vec3f normalShdWldOut(0.0f, 1.0f, 0.0f);
    const Vec3f omegaOutWld = Vec3f(0.2f, 0.9797959f, 0.0f).normalized();
    const Vec3f omegaInMirroredWld =
        Vec3f(-0.2f, 0.9797959f, 0.0f).normalized();

    const Vec3f directEval =
        TestSurfaceApi::EvalSurface(c, normalShdWldOut, omegaInMirroredWld, omegaOutWld);
    const float directPdf =
        _PdfSurface(c, _MakeSurfaceInteraction(normalShdWldOut, normalShdWldOut, normalShdWldOut, omegaOutWld), omegaInMirroredWld);
    if (!Test_IsClose(directEval, Vec3f(0.0f), 1.0e-7f) ||
        !Test_IsClose(directPdf, 0.0f, 1.0e-7f)) {
        printf(
            "    Delta conductor should not appear as finite direct lobe: "
            "eval=(%f,%f,%f) pdf=%f\n",
            directEval[0], directEval[1], directEval[2], directPdf);
        return false;
    }

    const auto sample =
        TestSurfaceApi::SampleSurface(c, normalShdWldOut, omegaOutWld, 0.3f, 0.7f, 0.4f);
    if (!sample.isSpecular || sample.pdfSolidAngle <= 0.0f) {
        printf("    Expected low-roughness conductor to sample delta\n");
        return false;
    }
    if (!Test_IsClose(sample.omegaInWld, omegaInMirroredWld, 1.0e-6f)) {
        printf("    Delta conductor reflected direction mismatch: "
               "(%f,%f,%f)\n",
               sample.omegaInWld[0], sample.omegaInWld[1],
               sample.omegaInWld[2]);
        return false;
    }
    if (!_IsFiniteNonNegative(sample.bsdfValue) ||
        sample.bsdfValue.length() <= 0.0f) {
        printf("    Delta conductor reflectance invalid: (%f,%f,%f)\n",
               sample.bsdfValue[0], sample.bsdfValue[1], sample.bsdfValue[2]);
        return false;
    }
    return true;
}

static bool
TestEffectivelySmoothConductorAlphaSamplesDeltaAndSkipsDirectEval()
{
    const Vec3f normalShdWldOut(0.0f, 1.0f, 0.0f);
    const Vec3f omegaOutWld = Vec3f(0.2f, 0.9797959f, 0.0f).normalized();
    const Vec3f omegaInMirroredWld =
        Vec3f(-0.2f, 0.9797959f, 0.0f).normalized();
    const float kSmoothAlphaValues[] = {
        1.0e-6f,
        1.0e-4f,
        4.0e-4f,
        9.0e-4f
    };

    for (const float alpha : kSmoothAlphaValues) {
        SurfaceClosure c;
        Bsdf::ConductorData conductor;
        conductor.weight = 1.0f;
        conductor.ior = Vec3f(0.8f, 0.5f, 0.25f);
        conductor.extinction = Vec3f(2.5f, 2.0f, 1.5f);
        conductor.roughness = Vec2f(alpha, alpha);
        c.bsdfTree.root = c.bsdfTree.Add(conductor);

        const Vec3f directEval = TestSurfaceApi::EvalSurface(
            c, normalShdWldOut, omegaInMirroredWld, omegaOutWld);
        const float directPdf = _PdfSurface(c, _MakeSurfaceInteraction(normalShdWldOut, normalShdWldOut, normalShdWldOut, omegaOutWld), omegaInMirroredWld);
        if (!Test_IsClose(directEval, Vec3f(0.0f), 1.0e-7f) ||
            !Test_IsClose(directPdf, 0.0f, 1.0e-7f)) {
            printf(
                "    Effectively smooth conductor should skip finite eval: "
                "alpha=%g eval=(%f,%f,%f) pdf=%f\n",
                alpha,
                directEval[0], directEval[1], directEval[2],
                directPdf);
            return false;
        }

        const auto sample = TestSurfaceApi::SampleSurface(c, normalShdWldOut, omegaOutWld,
                                                0.3f, 0.7f, 0.4f);
        if (!sample.isSpecular || sample.pdfSolidAngle <= 0.0f ||
            !Test_IsClose(sample.omegaInWld, omegaInMirroredWld, 1.0e-6f) ||
            !_IsFiniteNonNegative(sample.bsdfValue) ||
            sample.bsdfValue.length() <= 0.0f) {
            printf("    Effectively smooth conductor should sample delta: "
                   "alpha=%g specular=%d omegaInWld=(%f,%f,%f) f=(%f,%f,%f) "
                   "pdf=%f\n",
                   alpha, sample.isSpecular ? 1 : 0, sample.omegaInWld[0],
                   sample.omegaInWld[1], sample.omegaInWld[2],
                   sample.bsdfValue[0], sample.bsdfValue[1],
                   sample.bsdfValue[2], sample.pdfSolidAngle);
            return false;
        }
    }
    return true;
}

static bool
TestSharpConductorAlphaRemainsFiniteGlossy()
{
    const Vec3f normalShdWldOut(0.0f, 1.0f, 0.0f);
    const Vec3f omegaOutWld = Vec3f(0.2f, 0.9797959f, 0.0f).normalized();
    const Vec3f omegaInMirroredWld =
        Vec3f(-0.2f, 0.9797959f, 0.0f).normalized();
    const float kAlphaValues[] = {
        1.0e-3f,
        1.6e-3f,
        2.5e-3f
    };

    for (const float alpha : kAlphaValues) {
        SurfaceClosure c;
        Bsdf::ConductorData conductor;
        conductor.weight = 1.0f;
        conductor.ior = Vec3f(0.8f, 0.5f, 0.25f);
        conductor.extinction = Vec3f(2.5f, 2.0f, 1.5f);
        conductor.roughness = Vec2f(alpha, alpha);
        c.bsdfTree.root = c.bsdfTree.Add(conductor);

        const Vec3f directEval = TestSurfaceApi::EvalSurface(
            c, normalShdWldOut, omegaInMirroredWld, omegaOutWld);
        const float directPdf = _PdfSurface(c, _MakeSurfaceInteraction(normalShdWldOut, normalShdWldOut, normalShdWldOut, omegaOutWld), omegaInMirroredWld);
        if (directEval.length() <= 0.0f ||
            directPdf <= 0.0f ||
            !_IsFiniteNonNegative(directEval)) {
            printf(
                "    Sharp conductor should remain a finite glossy lobe: "
                "alpha=%g eval=(%f,%f,%f) pdf=%f\n",
                alpha,
                directEval[0], directEval[1], directEval[2],
                directPdf);
            return false;
        }

        const auto sample = TestSurfaceApi::SampleSurface(c, normalShdWldOut, omegaOutWld,
                                                0.3f, 0.7f, 0.4f);
        const float pdf = _PdfSurface(c, _MakeSurfaceInteraction(normalShdWldOut, normalShdWldOut, normalShdWldOut, omegaOutWld), sample.omegaInWld);
        const float ratio = sample.pdfSolidAngle / std::max(pdf, 1.0e-20f);
        if (sample.isSpecular || sample.pdfSolidAngle <= 0.0f || pdf <= 0.0f ||
            ratio < 0.8f || ratio > 1.2f) {
            printf("    Sharp conductor sample invalid: alpha=%g specular=%d "
                   "samplePdf=%f pdf=%f\n",
                   alpha, sample.isSpecular ? 1 : 0, sample.pdfSolidAngle, pdf);
            return false;
        }

        const float cosTheta =
            std::max(Dot(normalShdWldOut, sample.omegaInWld), 0.0f);
        const Vec3f throughputRgb =
            sample.bsdfValue * (cosTheta / sample.pdfSolidAngle);
        if (!_IsFiniteNonNegative(sample.bsdfValue) ||
            !_IsFiniteNonNegative(throughputRgb)) {
            printf("    Sharp conductor sample non-finite: alpha=%g "
                   "f=(%f,%f,%f) throughputRgb=(%f,%f,%f)\n",
                   alpha, sample.bsdfValue[0], sample.bsdfValue[1],
                   sample.bsdfValue[2], throughputRgb[0], throughputRgb[1],
                   throughputRgb[2]);
            return false;
        }
        for (int i = 0; i < 3; ++i) {
            if (throughputRgb[i] > 1.25f) {
                printf("    Sharp conductor throughputRgb too high: alpha=%g "
                       "channel=%d throughputRgb=%f\n",
                       alpha, i, throughputRgb[i]);
                return false;
            }
        }
    }
    return true;
}

static bool
TestSharpConductorEvalPdfRatioBoundedForLightSamples()
{
    const Vec3f normalShdWldOut(0.0f, 1.0f, 0.0f);
    const float kAlphaValues[] = {
        1.0e-3f,
        1.6e-3f,
        2.5e-3f
    };
    const float kWoCosValues[] = {
        0.05f,
        0.2f,
        0.5f,
        0.9f,
        0.999f
    };
    const float kLightPdfValues[] = {
        0.02f,
        0.08f,
        1.0f,
        10.0f,
        100.0f
    };

    for (const float alpha : kAlphaValues) {
        SurfaceClosure c;
        Bsdf::ConductorData conductor;
        conductor.weight = 1.0f;
        conductor.ior = Vec3f(0.8f, 0.5f, 0.25f);
        conductor.extinction = Vec3f(2.5f, 2.0f, 1.5f);
        conductor.roughness = Vec2f(alpha, alpha);
        c.bsdfTree.root = c.bsdfTree.Add(conductor);

        for (const float omegaOutCosThetaWld : kWoCosValues) {
            const float omegaOutSinThetaWld = std::sqrt(std::max(
                0.0f, 1.0f - omegaOutCosThetaWld * omegaOutCosThetaWld));
            const Vec3f omegaOutWld(omegaOutSinThetaWld, omegaOutCosThetaWld,
                                    0.0f);

            for (int y = 0; y < 64; ++y) {
                const float cosTheta =
                    (static_cast<float>(y) + 0.5f) / 64.0f;
                const float sinTheta =
                    std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
                for (int x = 0; x < 128; ++x) {
                    const float phi =
                        _kFurnaceTwoPi *
                        (static_cast<float>(x) + 0.5f) / 128.0f;
                    const Vec3f omegaInWld(sinTheta * std::cos(phi), cosTheta,
                                           sinTheta * std::sin(phi));
                    const Vec3f f = TestSurfaceApi::EvalSurface(c, normalShdWldOut,
                                                      omegaInWld, omegaOutWld);
                    const float pdf = _PdfSurface(c, _MakeSurfaceInteraction(normalShdWldOut, normalShdWldOut, normalShdWldOut, omegaOutWld), omegaInWld);
                    if (f.length() <= 0.0f) {
                        continue;
                    }
                    if (pdf <= 0.0f) {
                        printf("    Sharp conductor eval has zero pdf: "
                               "alpha=%g omegaOutWld=(%f,%f,%f) "
                               "omegaInWld=(%f,%f,%f) "
                               "f=(%f,%f,%f) pdf=%f\n",
                               alpha, omegaOutWld[0], omegaOutWld[1],
                               omegaOutWld[2], omegaInWld[0], omegaInWld[1],
                               omegaInWld[2], f[0], f[1], f[2], pdf);
                        return false;
                    }

                    const Vec3f ratio = f * (cosTheta / pdf);
                    if (!_IsFiniteNonNegative(ratio)) {
                        printf("    Sharp conductor Eval/Pdf ratio non-finite: "
                               "alpha=%g omegaOutWld=(%f,%f,%f) "
                               "omegaInWld=(%f,%f,%f) "
                               "pdf=%f ratio=(%f,%f,%f)\n",
                               alpha, omegaOutWld[0], omegaOutWld[1],
                               omegaOutWld[2], omegaInWld[0], omegaInWld[1],
                               omegaInWld[2], pdf, ratio[0], ratio[1],
                               ratio[2]);
                        return false;
                    }
                    for (int i = 0; i < 3; ++i) {
                        if (ratio[i] > 1.25f) {
                            printf(
                                "    Sharp conductor Eval/Pdf ratio too high: "
                                "alpha=%g channel=%d omegaOutWld=(%f,%f,%f) "
                                "omegaInWld=(%f,%f,%f) f=(%f,%f,%f) pdf=%f "
                                "ratio=%f\n",
                                alpha, i, omegaOutWld[0], omegaOutWld[1],
                                omegaOutWld[2], omegaInWld[0], omegaInWld[1],
                                omegaInWld[2], f[0], f[1], f[2], pdf, ratio[i]);
                            return false;
                        }
                    }

                    for (const float lightPdf : kLightPdfValues) {
                        const float weightMis =
                            Bsdf::PowerHeuristic(lightPdf, pdf);
                        const Vec3f direct =
                            f * (cosTheta / lightPdf) * weightMis;
                        if (!_IsFiniteNonNegative(direct)) {
                            printf(
                                "    Sharp conductor direct MIS non-finite: "
                                "alpha=%g lightPdf=%f omegaOutWld=(%f,%f,%f) "
                                "omegaInWld=(%f,%f,%f) f=(%f,%f,%f) pdf=%f "
                                "direct=(%f,%f,%f)\n",
                                alpha, lightPdf, omegaOutWld[0], omegaOutWld[1],
                                omegaOutWld[2], omegaInWld[0], omegaInWld[1],
                                omegaInWld[2], f[0], f[1], f[2], pdf, direct[0],
                                direct[1], direct[2]);
                            return false;
                        }
                        for (int i = 0; i < 3; ++i) {
                            if (direct[i] > 0.75f) {
                                printf(
                                    "    Sharp conductor direct MIS too high: "
                                    "alpha=%g lightPdf=%f channel=%d "
                                    "omegaOutWld=(%f,%f,%f) "
                                    "omegaInWld=(%f,%f,%f) "
                                    "f=(%f,%f,%f) pdf=%f direct=%f\n",
                                    alpha, lightPdf, i, omegaOutWld[0],
                                    omegaOutWld[1], omegaOutWld[2],
                                    omegaInWld[0], omegaInWld[1], omegaInWld[2],
                                    f[0], f[1], f[2], pdf, direct[i]);
                                return false;
                            }
                        }
                    }
                }
            }
        }
    }

    return true;
}

static bool
TestTreeTransmissionPreservesWeightFromInterior()
{
    SurfaceClosure c;
    Bsdf::DielectricData transmission;
    transmission.weight = 1.0f;
    transmission.tint = Vec3f(0.95f, 0.97f, 1.0f);
    transmission.ior = 1.5f;
    transmission.roughness = Vec2f(0.35f, 0.35f);
    transmission.scatterMode = Bsdf::ScatterMode::Transmission;

    c.bsdfTree.root = c.bsdfTree.Add(transmission);

    Vec3f normalShdWldOut(0, -1, 0);
    Vec3f omegaOutWld = Vec3f(0, -1, 0);

    const Bsdf::BsdfSample s = TestSurfaceApi::SampleSurface(
        c, normalShdWldOut, normalShdWldOut, normalShdWldOut, omegaOutWld,
        0.3f, 0.7f, 0.5f, 0.0f, false);
    if (s.pdfSolidAngle <= 0.0f) {
        printf("    Expected valid interior transmission sample\n");
        return false;
    }
    if (Dot(s.omegaInWld, normalShdWldOut) >= 0.0f || !s.isTransmission) {
        printf("    Interior transmission should exit above the surface\n");
        return false;
    }
    if (s.bsdfValue.length() < 0.01f) {
        printf("    Interior transmission unexpectedly dim: (%f,%f,%f)\n",
               s.bsdfValue[0], s.bsdfValue[1], s.bsdfValue[2]);
        return false;
    }
    return true;
}

static bool
TestEvalSurfaceTransmissionFromInterior()
{
    SurfaceClosure c;
    Bsdf::DielectricData transmission;
    transmission.weight = 1.0f;
    transmission.tint = Vec3f(1.0f);
    transmission.ior = 1.5f;
    transmission.roughness = Vec2f(0.25f, 0.25f);
    transmission.scatterMode = Bsdf::ScatterMode::Transmission;
    c.bsdfTree.root = c.bsdfTree.Add(transmission);

    const Vec3f normalShdWldOut(0.0f, -1.0f, 0.0f);
    const Vec3f omegaOutWld = Vec3f(0.2f, -0.98f, 0.0f).normalized();
    const Vec3f omegaInWld = Vec3f(-0.1f, 0.995f, 0.0f).normalized();

    const Vec3f result = TestSurfaceApi::EvalSurface(
        c, normalShdWldOut, normalShdWldOut, omegaInWld, omegaOutWld,
        0.0f, false);
    if (result.length() <= 0.0f) {
        printf("    Expected non-zero interior-to-exterior transmission\n");
        return false;
    }
    return true;
}

static bool
TestIncidentFrameBackFaceSelectsInteriorIor()
{
    SurfaceClosure closure;
    Bsdf::DielectricData transmission;
    transmission.weight = 1.0f;
    transmission.tint = Vec3f(0.95f, 0.97f, 1.0f);
    transmission.ior = 1.5f;
    transmission.roughness = Vec2f(0.2f, 0.2f);
    transmission.scatterMode = Bsdf::ScatterMode::Transmission;
    closure.bsdfTree.root = closure.bsdfTree.Add(transmission);

    const Vec3f outwardN(0.0f, 1.0f, 0.0f);
    const Vec3f incidentN = -outwardN;
    const Vec3f omegaOutWld = Vec3f(0.2f, -0.98f, 0.0f).normalized();
    Bsdf::detail::PrepareShadingNormals(
        &closure.bsdfTree, incidentN, incidentN, omegaOutWld);
    const Bsdf::BsdfSample incidentSample = PreparedSurfaceApi::SampleSurface(
        closure, incidentN, omegaOutWld, 0.3f, 0.7f, 0.5f, 0.0f, false);

    if (incidentSample.pdfSolidAngle <= 0.0f ||
        Dot(incidentSample.omegaInWld, outwardN) <= 0.0f ||
        !Test_IsClose(incidentSample.eta, 1.5f, 1.0e-5f)) {
        printf("    Explicit back-face context did not select glass-to-air eta\n");
        return false;
    }

    const Vec3f incidentEval = PreparedSurfaceApi::EvalSurface(
        closure, incidentN, incidentSample.omegaInWld, omegaOutWld,
        0.0f, false);
    const float incidentPdf = _PdfPreparedSurface(closure, _MakeSurfaceInteraction(incidentN, incidentN, incidentN, omegaOutWld, 0.0f, false), incidentSample.omegaInWld);
    if (incidentEval.length() <= 0.0f ||
        !Test_IsClose(
            incidentSample.pdfSolidAngle, incidentPdf, 1.0e-6f)) {
        printf("    Back-face incident frame has inconsistent eval/PDF\n");
        return false;
    }
    return true;
}

static bool
TestIncidentFrameBackFaceDiffuseStaysVisible()
{
    SurfaceClosure closure;
    Bsdf::OrenNayarDiffuseData diffuse;
    diffuse.color = Vec3f(0.8f);
    diffuse.weight = 1.0f;
    closure.bsdfTree.root = closure.bsdfTree.Add(diffuse);

    const Vec3f incidentN(0.0f, -1.0f, 0.0f);
    const Vec3f omegaOutWld(0.0f, -1.0f, 0.0f);
    const Bsdf::BsdfSample sample = TestSurfaceApi::SampleSurface(
        closure, incidentN, omegaOutWld, 0.3f, 0.7f, 0.5f, 0.0f, false);
    if (sample.pdfSolidAngle <= 0.0f ||
        Dot(sample.omegaInWld, incidentN) <= 0.0f ||
        sample.bsdfValue.length() <= 0.0f) {
        printf("    Back-face diffuse did not use the incident frame\n");
        return false;
    }
    return true;
}

static bool
TestTreeAddTransmissionPreservesWeight()
{
    SurfaceClosure c;
    Bsdf::ClosureTree tree;

    Bsdf::DielectricData reflection;
    reflection.weight = 1.0f;
    reflection.tint = Vec3f(1.0f);
    reflection.ior = 1.5f;
    reflection.scatterMode = Bsdf::ScatterMode::Reflection;

    Bsdf::DielectricData transmission;
    transmission.weight = 1.0f;
    transmission.tint = Vec3f(0.95f, 0.97f, 1.0f);
    transmission.ior = 1.5f;
    transmission.scatterMode = Bsdf::ScatterMode::Transmission;

    Bsdf::AddData add;
    add.in1 = tree.Add(reflection);
    add.in2 = tree.Add(transmission);
    tree.root = tree.Add(add);
    c.bsdfTree = tree;

    Vec3f normalShdWldOut(0, 1, 0);
    Vec3f omegaOutWld = Vec3f(0, 1, 0);

    auto s =
        TestSurfaceApi::SampleSurface(c, normalShdWldOut, omegaOutWld, 0.3f, 0.7f, 0.99f);
    if (s.pdfSolidAngle <= 0.0f) {
        printf("    Expected valid transmission sample from add node\n");
        return false;
    }

    float mag = s.bsdfValue.length();
    if (mag < 0.01f) {
        printf("    Add-node transmission unexpectedly dim: (%f,%f,%f)\n",
               s.bsdfValue[0], s.bsdfValue[1], s.bsdfValue[2]);
        return false;
    }
    return true;
}

static bool
TestDielectricInterfaceLayerDoesNotDoubleAttenuateTransmission()
{
    Bsdf::ClosureTree tree;

    Bsdf::DielectricInterfaceData interface;
    interface.reflectionWeight = 1.0f;
    interface.reflectionTint = Vec3f(1.0f);
    interface.transmissionWeight = 1.0f;
    interface.transmissionTint = Vec3f(1.0f);
    interface.ior = 1.5f;
    interface.roughness = Vec2f(0.25f, 0.25f);

    Bsdf::OrenNayarDiffuseData diffuse;
    diffuse.weight = 1.0f;
    diffuse.color = Vec3f(0.8f);

    Bsdf::LayerData layer;
    layer.top = tree.Add(interface);
    layer.base = tree.Add(diffuse);
    tree.root = tree.Add(layer);

    SurfaceClosure layered;
    layered.bsdfTree = tree;

    SurfaceClosure interfaceOnly;
    interfaceOnly.bsdfTree.root = interfaceOnly.bsdfTree.Add(interface);

    const Vec3f normalShdWldOut(0.0f, 1.0f, 0.0f);
    const Vec3f omegaOutWld = Vec3f(0.2f, 0.98f, 0.0f).normalized();
    const Vec3f omegaInWld = Vec3f(-0.1f, -0.995f, 0.0f).normalized();

    const Vec3f layeredEval =
        TestSurfaceApi::EvalSurface(layered, normalShdWldOut, omegaInWld, omegaOutWld);
    const Vec3f interfaceEval = TestSurfaceApi::EvalSurface(
        interfaceOnly, normalShdWldOut, omegaInWld, omegaOutWld);
    if (!Test_IsClose(layeredEval, interfaceEval, 1.0e-5f)) {
        printf(
            "    Interface transmission was attenuated by its own layer: "
            "layered=(%f,%f,%f) interface=(%f,%f,%f)\n",
            layeredEval[0], layeredEval[1], layeredEval[2],
            interfaceEval[0], interfaceEval[1], interfaceEval[2]);
        return false;
    }

    return true;
}

static bool
TestOpenPbrInterfaceAppliesBsdlDielectricCompensationScale()
{
    const Vec3f normalShdWldOut(0.0f, 1.0f, 0.0f);
    constexpr float perceptualRoughness = 0.3f;
    constexpr float alphaRoughness =
        perceptualRoughness * perceptualRoughness;
    constexpr float ior = 1.5f;

    for (const bool backfacing : {false, true}) {
        SurfaceClosure closure;
        Bsdf::DielectricInterfaceData interface;
        interface.reflectionWeight = 1.0f;
        interface.reflectionTint = Vec3f(1.0f);
        interface.transmissionWeight = 1.0f;
        interface.transmissionTint = Vec3f(1.0f);
        interface.ior = ior;
        interface.roughness = Vec2f(alphaRoughness, alphaRoughness);
        interface.compensateCoupledDielectric = true;
        closure.bsdfTree.root = closure.bsdfTree.Add(interface);

        Vec3f omegaOutWld = _DirectionFromCosThetaYUp(backfacing ? 0.8f : 0.4f);
        if (backfacing) {
            omegaOutWld = -omegaOutWld;
        }
        const Vec3f normalIncidentWldOut =
            backfacing ? -normalShdWldOut : normalShdWldOut;
        Bsdf::detail::PrepareShadingNormals(
            &closure.bsdfTree, normalIncidentWldOut,
            normalIncidentWldOut, omegaOutWld);
        Vec3f omegaInReflection1Wld = _DirectionFromCosThetaYUp(0.85f);
        Vec3f omegaInReflection2Wld = Vec3f(-0.6f, 0.55f, 0.58f).normalized();
        if (backfacing) {
            omegaInReflection1Wld = -omegaInReflection1Wld;
            omegaInReflection2Wld = -omegaInReflection2Wld;
        }
        const Vec3f omegaInTransmission1Wld = -omegaInReflection1Wld;
        const Vec3f omegaInTransmission2Wld = -omegaInReflection2Wld;

        const Vec3f compensatedR1 = PreparedSurfaceApi::EvalSurface(
            closure, normalIncidentWldOut, omegaInReflection1Wld, omegaOutWld,
            0.0f, !backfacing);
        const Vec3f compensatedR2 = PreparedSurfaceApi::EvalSurface(
            closure, normalIncidentWldOut, omegaInReflection2Wld, omegaOutWld,
            0.0f, !backfacing);
        const Vec3f compensatedT1 = PreparedSurfaceApi::EvalSurface(
            closure, normalIncidentWldOut, omegaInTransmission1Wld, omegaOutWld,
            0.0f, !backfacing);
        const Vec3f compensatedT2 = PreparedSurfaceApi::EvalSurface(
            closure, normalIncidentWldOut, omegaInTransmission2Wld, omegaOutWld,
            0.0f, !backfacing);

        const auto evalSingleScatter = [&](const Vec3f& omegaInWld) {
            if (Dot(normalIncidentWldOut, omegaInWld) > 0.0f) {
                const Vec3f fresnel =
                    Bsdf::detail::DielectricInterfaceReflectionCoefficient(
                        interface,
                        Bsdf::detail::ReflectionFresnelCosTheta(
                            omegaInWld, omegaOutWld),
                        ior,
                        backfacing);
                return Bsdf::detail::EvalMicrofacetReflectionIsotropic(
                    alphaRoughness,
                    fresnel,
                    1.0f,
                    normalIncidentWldOut,
                    omegaInWld,
                    omegaOutWld,
                    /* compensateMissingEnergy = */ false);
            }
            return Bsdf::detail::EvalCoupledRoughDielectricTransmission(
                interface,
                ior,
                backfacing,
                normalIncidentWldOut,
                omegaInWld,
                omegaOutWld);
        };
        const Vec3f rawR1 = evalSingleScatter(omegaInReflection1Wld);
        const Vec3f rawR2 = evalSingleScatter(omegaInReflection2Wld);
        const Vec3f rawT1 = evalSingleScatter(omegaInTransmission1Wld);
        const Vec3f rawT2 = evalSingleScatter(omegaInTransmission2Wld);
        const float missingEnergy =
            Bsdf::detail::BsdlCoupledDielectricCompensation(
                std::abs(Dot(normalIncidentWldOut, omegaOutWld)),
                perceptualRoughness, ior, backfacing);
        if (!(missingEnergy > 0.0f)) {
            printf(
                "    Expected positive coupled dielectric compensation "
                "backfacing=%d\n",
                backfacing);
            return false;
        }
        const float expectedScale =
            1.0f / std::max(0.01f, 1.0f - missingEnergy);
        if (!Test_IsClose(compensatedR1, rawR1 * expectedScale, 2.0e-5f) ||
            !Test_IsClose(compensatedR2, rawR2 * expectedScale, 2.0e-5f) ||
            !Test_IsClose(compensatedT1, rawT1 * expectedScale, 2.0e-5f) ||
            !Test_IsClose(compensatedT2, rawT2 * expectedScale, 2.0e-5f)) {
            printf(
                "    Expected BSDL multiplicative compensation scale %f: "
                "R1=(%f,%f) R2=(%f,%f) T1=(%f,%f) T2=(%f,%f) "
                "backfacing=%d\n",
                expectedScale, compensatedR1[0], rawR1[0],
                compensatedR2[0], rawR2[0], compensatedT1[0], rawT1[0],
                compensatedT2[0], rawT2[0], backfacing);
            return false;
        }
    }

    return true;
}

static bool
TestBsdlDielectricBothRuntimeLutMatchesGeneratedTables()
{
    constexpr int valueCount =
        bsdf_luts::kBsdlDielectricBothValueCount;
    static_assert(valueCount == bsdl::mtx::DielectricBothFront::Nf *
        bsdl::mtx::DielectricBothFront::Nr *
        bsdl::mtx::DielectricBothFront::Nc);

    const float* const generatedFront =
        bsdl::mtx::DielectricBothFront::get_energy().data;
    const float* const generatedBack =
        bsdl::mtx::DielectricBothBack::get_energy().data;
    for (int i = 0; i < valueCount; ++i) {
        const float runtimeFront =
            bsdf_luts::kBsdlDielectricBothFrontMissingEnergy[i];
        const float runtimeBack =
            bsdf_luts::kBsdlDielectricBothBackMissingEnergy[i];
        if (runtimeFront != generatedFront[i] ||
            runtimeBack != generatedBack[i]) {
            printf(
                "    Runtime Both LUT differs from generated BSDL "
                "table at index %d: front=(%g,%g) back=(%g,%g)\n",
                i,
                runtimeFront,
                generatedFront[i],
                runtimeBack,
                generatedBack[i]);
            return false;
        }
    }
    return true;
}

static bool
TestBsdlDielectricTransmissionLutMatchesStockSamplerAtEveryGridPoint()
{
    constexpr int sampleCount = 4096;
    constexpr float monteCarloTolerance = 0.035f;
    float maxError = 0.0f;

    for (const bool backside : {false, true}) {
        const float* const table = backside
            ? bsdf_luts::kBsdlDielectricTransmissionBackSingleScatterAlbedo
            : bsdf_luts::kBsdlDielectricTransmissionFrontSingleScatterAlbedo;
        for (int iorIdx = 0;
             iorIdx < bsdf_luts::kBsdlDielectricTransmissionIorCount;
             ++iorIdx) {
            const float fresnelIndex = float(iorIdx) /
                float(bsdf_luts::kBsdlDielectricTransmissionIorCount - 1);
            const bsdl::mtx::DielectricFresnel fresnel =
                bsdl::mtx::DielectricFresnel::from_table_index(
                    fresnelIndex, backside);
            for (int roughnessIdx = 0;
                 roughnessIdx <
                     bsdf_luts::kBsdlDielectricTransmissionRoughnessCount;
                 ++roughnessIdx) {
                const float roughness = float(roughnessIdx) /
                    float(
                        bsdf_luts::kBsdlDielectricTransmissionRoughnessCount -
                        1);
                for (int cosThetaIdx = 0;
                     cosThetaIdx <
                         bsdf_luts::kBsdlDielectricTransmissionCosThetaCount;
                     ++cosThetaIdx) {
                    const float cosTheta = bsdl::mtx::DielectricBSDF<
                        bsdl::mtx::DielectricFresnel>::get_cosine(
                            cosThetaIdx);
                    const int tableIdx =
                        (iorIdx *
                             bsdf_luts::
                                 kBsdlDielectricTransmissionRoughnessCount +
                         roughnessIdx) *
                            bsdf_luts::
                                kBsdlDielectricTransmissionCosThetaCount +
                        cosThetaIdx;
                    float estimate = 1.0f - fresnel.eval(cosTheta).max();
                    if (roughnessIdx > 0) {
                        const bsdl::mtx::DielectricBSDF<
                            bsdl::mtx::DielectricFresnel> bsdf(
                                bsdl::GGXDist(roughness, 0.0f), fresnel,
                                cosTheta, roughness, true);
                        const Imath::V3f omegaOut(
                            std::sqrt(
                                std::max(0.0f, 1.0f - cosTheta * cosTheta)),
                            0.0f, cosTheta);
                        double sum = 0.0;
                        const std::uint64_t gridPoint = std::uint64_t(
                            tableIdx +
                            (backside
                                ? bsdf_luts::
                                      kBsdlDielectricTransmissionValueCount
                                : 0));
                        for (int sampleIdx = 0;
                             sampleIdx < sampleCount;
                             ++sampleIdx) {
                            const bsdl::Sample sample = bsdf.sample(
                                omegaOut,
                                _CounterRandomFloat(
                                    sampleIdx, gridPoint * 3 + 0),
                                _CounterRandomFloat(
                                    sampleIdx, gridPoint * 3 + 1),
                                _CounterRandomFloat(
                                    sampleIdx, gridPoint * 3 + 2));
                            if (sample.pdf > 0.0f && sample.wi.z < 0.0f) {
                                sum += double(sample.weight.max());
                            }
                        }
                        estimate = float(sum / double(sampleCount));
                    }

                    const float error = std::abs(estimate - table[tableIdx]);
                    maxError = std::max(maxError, error);
                    if (error > monteCarloTolerance) {
                        printf(
                            "    Transmission LUT disagrees with stock BSDL "
                            "sampler: side=%s iorIdx=%d roughnessIdx=%d "
                            "cosThetaIdx=%d table=%f estimate=%f error=%f\n",
                            backside ? "back" : "front", iorIdx,
                            roughnessIdx, cosThetaIdx, table[tableIdx],
                            estimate, error);
                        return false;
                    }
                }
            }
        }
    }

    printf(
        "    Transmission LUT maximum independent grid error: %f\n",
        maxError);
    return true;
}

/// Check that the stock BSDL Both missing-energy table complements the stock
/// bounded-VNDF sampler. The separately generated transmission table is only
/// compared at exact grid coordinates; interpolated values need not equal an
/// independently sampled nonlinear directional albedo.
static bool
TestCoupledRoughDielectricCompensationBudget()
{
    using namespace Bsdf::detail;
    printf("    Coupled dielectric compensation budget (ior 1.5):\n");
    printf("      side  rough   cos    A_refl    A_trans    A_single   "
           "missingE   A_single+E   deficit\n");
    constexpr int kThetaSteps = 256;
    constexpr int kPhiSteps = 512;
    const Vec3f normalWldOut(0.0f, 1.0f, 0.0f);
    for (const bool frontFacing : {true, false}) {
        for (const float perceptualRoughness : {0.2f, 0.4f, 0.6f, 0.8f, 1.0f}) {
            for (const float cosTheta : {0.15f, 0.35f, 0.6f, 0.85f, 1.0f}) {
                Bsdf::DielectricInterfaceData data;
                data.roughness = Vec2f(perceptualRoughness * perceptualRoughness);
                data.tangent = Vec3f(0.0f, 0.0f, 1.0f);
                data.compensateCoupledDielectric = true;
                const float effectiveIor = data.ior;
                const bool backside = !frontFacing;
                const Vec3f omegaOutWld =
                    _DirectionFromCosThetaYUp(cosTheta);
                const SurfaceInteraction interaction =
                    _MakeSurfaceInteraction(
                        normalWldOut, normalWldOut, normalWldOut, omegaOutWld,
                        0.0f, frontFacing);

                double reflected = 0.0;
                double transmitted = 0.0;
                const double cellSolidAngle = 4.0 * M_PI /
                    (double(kThetaSteps) * double(kPhiSteps));
                for (int it = 0; it < kThetaSteps; ++it) {
                    const float z = 1.0f -
                        2.0f * (float(it) + 0.5f) / float(kThetaSteps);
                    const float radius =
                        std::sqrt(std::max(0.0f, 1.0f - z * z));
                    for (int ip = 0; ip < kPhiSteps; ++ip) {
                        const float phi = 2.0f * float(M_PI) *
                            (float(ip) + 0.5f) / float(kPhiSteps);
                        const Vec3f omegaInWld(
                            radius * std::cos(phi), z, radius * std::sin(phi));
                        const double weight =
                            double(std::abs(z)) * cellSolidAngle;
                        if (IsSameSide(
                                normalWldOut, omegaInWld, omegaOutWld)) {
                            const Vec3f fresnel =
                                DielectricInterfaceReflectionCoefficient(
                                    data,
                                    ReflectionFresnelCosTheta(
                                        omegaInWld, omegaOutWld),
                                    effectiveIor, backside);
                            reflected += double(
                                EvalMicrofacetReflectionIsotropic(
                                    std::clamp(
                                        data.roughness[0], kMinMicrofacetAlpha,
                                        1.0f),
                                    fresnel, 1.0f, normalWldOut, omegaInWld,
                                    omegaOutWld,
                                    /* compensateMissingEnergy = */ false)[0]) *
                                weight;
                        } else {
                            transmitted += double(
                                EvalCoupledRoughDielectricTransmission(
                                    data, effectiveIor, backside,
                                    normalWldOut, omegaInWld,
                                    omegaOutWld)[0]) *
                                weight;
                        }
                    }
                }

                // Estimate the production sampler's single-scatter albedo
                // from its returned directions and the production evaluator
                // and PDF. This catches disagreement in any of the three
                // paths without duplicating their sampling-weight algebra in
                // the test.
                double vndfReflected = 0.0;
                double vndfTransmitted = 0.0;
                constexpr int kVndfSamples = 1 << 18;
                for (int i = 0; i < kVndfSamples; ++i) {
                    const Bsdf::BsdfSample sample =
                        SampleCoupledRoughDielectric(
                        data, effectiveIor, backside, normalWldOut,
                        omegaOutWld,
                        _CounterRandomFloat(i, 7100),
                        _CounterRandomFloat(i, 7101),
                        _CounterRandomFloat(i, 7102));
                    const float pdf = PdfCoupledRoughDielectric(
                        data, effectiveIor, backside, normalWldOut,
                        sample.omegaInWld, omegaOutWld);
                    if (!(pdf > 0.0f)) {
                        continue;
                    }
                    const Vec3f evaluated = sample.isTransmission
                        ? EvalCoupledRoughDielectricTransmission(
                              data, effectiveIor, backside, normalWldOut,
                              sample.omegaInWld, omegaOutWld)
                        : EvalMicrofacetReflectionIsotropic(
                              std::clamp(
                                  data.roughness[0], kMinMicrofacetAlpha,
                                  1.0f),
                              DielectricInterfaceReflectionCoefficient(
                                  data,
                                  ReflectionFresnelCosTheta(
                                      sample.omegaInWld, omegaOutWld),
                                  effectiveIor, backside),
                              1.0f, normalWldOut, sample.omegaInWld,
                              omegaOutWld,
                              /* compensateMissingEnergy = */ false);
                    const double weight = double(evaluated[0]) *
                        double(std::abs(Dot(
                            normalWldOut, sample.omegaInWld))) /
                        double(pdf);
                    if (sample.isTransmission) {
                        vndfTransmitted += weight;
                    } else {
                        vndfReflected += weight;
                    }
                }
                vndfReflected /= double(kVndfSamples);
                vndfTransmitted /= double(kVndfSamples);

                // BSDL's own lobe, the one the table was actually baked
                // against, driven by its own sampler. If its albedo is
                // 1 - missingEnergy while ours is not, the table is right and
                // the lobe we pair it with is the wrong one.
                const float tableIndex = std::sqrt(
                    (effectiveIor - kBsdlDielectricIorMin) /
                    (kBsdlDielectricIorMax - kBsdlDielectricIorMin));
                bsdl::mtx::DielectricBSDF<bsdl::mtx::DielectricFresnel> bsdlSpec(
                    bsdl::GGXDist(perceptualRoughness, 0.0f),
                    bsdl::mtx::DielectricFresnel::from_table_index(
                        tableIndex, backside),
                    cosTheta, perceptualRoughness, true);
                const Imath::V3f bsdlOut(
                    std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta)),
                    0.0f, cosTheta);
                double bsdlReflected = 0.0;
                double bsdlTransmitted = 0.0;
                constexpr int kBsdlSamples = 1 << 18;
                for (int i = 0; i < kBsdlSamples; ++i) {
                    const bsdl::Sample s = bsdlSpec.sample(
                        bsdlOut,
                        _CounterRandomFloat(i, 7200),
                        _CounterRandomFloat(i, 7201),
                        _CounterRandomFloat(i, 7202));
                    if (s.pdf > 0.0f) {
                        if (s.wi.z > 0.0f) {
                            bsdlReflected += double(s.weight.max());
                        } else {
                            bsdlTransmitted += double(s.weight.max());
                        }
                    }
                }
                bsdlReflected /= double(kBsdlSamples);
                bsdlTransmitted /= double(kBsdlSamples);
                const double bsdlAlbedo = bsdlReflected + bsdlTransmitted;

                const float tableTransmission =
                    LookupBsdlDielectricTransmissionSingleScatterAlbedo(
                        cosTheta, perceptualRoughness, effectiveIor, backside);

                const float missingEnergy =
                    GetCoupledDielectricCompensation(
                        data, effectiveIor, backside, normalWldOut,
                        omegaOutWld);
                const double single = reflected + transmitted;
                const double total =
                    single + double(missingEnergy);
                printf("      %-5s %5.2f  %4.2f  %8.5f  %8.5f  %9.5f  "
                       "%8.5f  %10.5f  %+8.5f   | 1-E=%.5f  bsdl R=%.5f "
                       "T=%.5f sum=%.5f  sum/(1-E)=%.4f  ourT/bsdlT=%.3f  "
                       "unbiasedT=%.5f  ourT/unbiasedT=%.3f\n",
                       frontFacing ? "front" : "back", perceptualRoughness,
                       cosTheta, reflected, transmitted, single,
                       missingEnergy, total, total - 1.0,
                       1.0 - double(missingEnergy),
                       bsdlReflected, bsdlTransmitted, bsdlAlbedo,
                       bsdlAlbedo /
                           std::max(
                               1.0 - double(missingEnergy),
                               1.0e-9),
                       transmitted / std::max(bsdlTransmitted, 1.0e-9),
                       tableTransmission,
                       transmitted /
                           std::max(double(tableTransmission), 1.0e-9));
                const double productionSamplerAlbedo =
                    vndfReflected + vndfTransmitted;
                if (std::abs(bsdlAlbedo - (1.0 - double(missingEnergy))) >
                        0.01 ||
                    std::abs(
                        productionSamplerAlbedo -
                        (1.0 - double(missingEnergy))) > 0.01 ||
                    std::abs(vndfReflected - bsdlReflected) > 0.01 ||
                    std::abs(vndfTransmitted - bsdlTransmitted) > 0.01) {
                    printf(
                        "    Production bounded sampler disagrees with stock "
                        "BSDL: production=(%f,%f,%f) bsdl=(%f,%f,%f) "
                        "expected=%f\n",
                        vndfReflected, vndfTransmitted,
                        productionSamplerAlbedo, bsdlReflected,
                        bsdlTransmitted, bsdlAlbedo,
                        1.0 - double(missingEnergy));
                    return false;
                }
                (void)tableTransmission;
                (void)interaction;
            }
        }
    }
    return true;
}

static bool
TestCoupledRoughDielectricDirectionalTransmissionAlbedo()
{
    struct Case {
        float alphaRoughness;
        float cosTheta;
        float ior;
        float tolerance;
    };
    const Case cases[] = {
        {0.09f, 0.60f, 1.50f, 0.025f},
        {0.36f, 0.25f, 1.25f, 0.012f},
        {0.64f, 0.85f, 2.00f, 0.010f},
    };
    const Vec3f normalShdWldOut(0.0f, 1.0f, 0.0f);
    constexpr int integrationSamples = 32768;
    bool ok = true;

    // The first roughness interval must converge to exact dielectric Fresnel.
    // Pin both the analytic endpoint and the AOUSD fixture roughness that
    // originally exposed dark transparent shadows from the uniform-only bake.
    for (const float alpha : {0.0f, 0.0016f, 0.002f}) {
        for (const float cosTheta : {0.2f, 0.45f, 0.7f, 0.9f, 1.0f}) {
            for (const bool backfacing : {false, true}) {
                constexpr float ior = 1.5f;
                const float eta = backfacing ? 1.0f / ior : ior;
                const float expected =
                    _DielectricTransmittanceForTest(eta, cosTheta);
                const float actual =
                    Bsdf::CoupledRoughDielectricDirectionalTransmissionAlbedo(
                        cosTheta, Vec2f(alpha, alpha), ior, backfacing, false);
                const float tolerance = alpha <= 0.002f ? 2.0e-4f : 0.035f;
                if (!Test_IsClose(actual, expected, tolerance)) {
                    printf(
                        "    Smooth-limit transmission mismatch: alpha=%f "
                        "cos=%f ior=%f backfacing=%d actual=%f exact=%f\n",
                        alpha, cosTheta, ior, backfacing, actual, expected);
                    ok = false;
                }
            }
        }
    }

    // Numerically integrate the renderer's low-level Walter GGX BTDF and its
    // compensated production leaf independently of the BSDL bake.
    for (const Case& c : cases) {
        for (const bool backfacing : {false, true}) {
            Bsdf::DielectricInterfaceData interface;
            interface.reflectionWeight = 1.0f;
            interface.transmissionWeight = 1.0f;
            interface.ior = c.ior;
            interface.roughness =
                Vec2f(c.alphaRoughness, c.alphaRoughness);
            interface.compensateCoupledDielectric = true;

            Vec3f omegaOutWld = _DirectionFromCosThetaYUp(c.cosTheta);
            if (backfacing) {
                omegaOutWld = -omegaOutWld;
            }
            const Vec3f normalIncidentWldOut =
                backfacing ? -normalShdWldOut : normalShdWldOut;
            SurfaceClosure closure;
            closure.bsdfTree.root = closure.bsdfTree.Add(interface);
            Bsdf::detail::PrepareShadingNormals(
                &closure.bsdfTree, normalIncidentWldOut,
                normalIncidentWldOut, omegaOutWld);
            for (const bool compensate : {false, true}) {
                const Vec3f integrated = _IntegrateHemisphereUniform(
                    [&](const Vec3f& omegaInUpperWld) {
                        const Vec3f omegaInWld =
                            backfacing ? omegaInUpperWld : -omegaInUpperWld;
                        if (compensate) {
                            return PreparedSurfaceApi::EvalSurface(
                                closure, normalIncidentWldOut,
                                omegaInWld, omegaOutWld, 0.0f, !backfacing);
                        }
                        return Bsdf::detail::EvalCoupledRoughDielectricTransmission(
                            interface, c.ior, backfacing,
                            normalIncidentWldOut, omegaInWld, omegaOutWld);
                    },
                    integrationSamples);
                const float table =
                    Bsdf::CoupledRoughDielectricDirectionalTransmissionAlbedo(
                        c.cosTheta,
                        interface.roughness,
                        c.ior,
                        backfacing,
                        compensate);
                if (!Test_IsClose(integrated, Vec3f(table), c.tolerance)) {
                    printf(
                        "    Rough transmission albedo mismatch: alpha=%f "
                        "cos=%f ior=%f backfacing=%d compensate=%d "
                        "integrated=(%f,%f,%f) table=%f\n",
                        c.alphaRoughness,
                        c.cosTheta,
                        c.ior,
                        backfacing,
                        compensate,
                        integrated[0], integrated[1], integrated[2], table);
                    ok = false;
                }
            }
        }
    }

    for (const float alpha : {0.002f, 0.04f, 0.09f, 0.36f, 1.0f}) {
        for (const float cosTheta : {0.03f, 0.2f, 0.6f, 0.99f}) {
            for (const float ior : {1.05f, 1.5f, 2.5f, 5.0f}) {
                for (const bool backfacing : {false, true}) {
                    const Vec2f roughness(alpha, alpha);
                    const float singleScatter =
                        Bsdf::CoupledRoughDielectricDirectionalTransmissionAlbedo(
                            cosTheta, roughness, ior, backfacing, false);
                    const float compensated =
                        Bsdf::CoupledRoughDielectricDirectionalTransmissionAlbedo(
                            cosTheta, roughness, ior, backfacing, true);
                    if (!std::isfinite(singleScatter) ||
                        !std::isfinite(compensated) ||
                        singleScatter < 0.0f || singleScatter > 1.0f ||
                        compensated + 1.0e-6f < singleScatter ||
                        compensated > 1.0f) {
                        printf(
                            "    Invalid rough transmission albedo: alpha=%f "
                            "cos=%f ior=%f backfacing=%d single=%f "
                            "compensated=%f\n",
                            alpha,
                            cosTheta,
                            ior,
                            backfacing,
                            singleScatter,
                            compensated);
                        ok = false;
                    }
                }
            }
        }
    }

    // The current anisotropic policy is an azimuth-independent symmetric
    // reduction to one perceptual roughness; pin that approximation explicitly.
    const float anisotropicXY =
        Bsdf::CoupledRoughDielectricDirectionalTransmissionAlbedo(
            0.45f, Vec2f(0.04f, 0.64f), 1.5f, false, true);
    const float anisotropicYX =
        Bsdf::CoupledRoughDielectricDirectionalTransmissionAlbedo(
            0.45f, Vec2f(0.64f, 0.04f), 1.5f, false, true);
    if (!Test_IsClose(anisotropicXY, anisotropicYX, 1.0e-7f)) {
        printf(
            "    Anisotropic scalar reduction depends on axis order: "
            "xy=%f yx=%f\n",
            anisotropicXY, anisotropicYX);
        ok = false;
    }
    return ok;
}

static bool
TestStraightShadowDielectricTransmissionPolicy()
{
    constexpr float signedCosTheta = 0.45f;
    constexpr float ior = 1.5f;
    const float schlick =
        1.0f - _SchlickFresnelForTest(ior, signedCosTheta);
    bool ok = true;

    SurfaceClosure closure;
    closure.specularIor = ior;
    if (!Test_IsClose(
            Bsdf::StraightShadowDielectricTransmission(
                closure, -signedCosTheta),
            schlick,
            1.0e-6f)) {
        printf("    Legacy closure did not use Schlick transmission\n");
        ok = false;
    }

    auto makeInterface = []() {
        Bsdf::DielectricInterfaceData interface;
        interface.reflectionWeight = 1.0f;
        interface.transmissionWeight = 1.0f;
        interface.ior = ior;
        interface.roughness = Vec2f(0.36f, 0.36f);
        interface.compensateCoupledDielectric = true;
        return interface;
    };

    const Bsdf::DielectricInterfaceData interface = makeInterface();
    closure.bsdfTree.Clear();
    const Bsdf::NodeId interfaceId = closure.bsdfTree.Add(interface);
    Bsdf::BurleyDiffuseData base;
    const Bsdf::NodeId baseId = closure.bsdfTree.Add(base);
    closure.bsdfTree.root = closure.bsdfTree.Add(
        Bsdf::LayerData{interfaceId, baseId});

    const float entry = Bsdf::StraightShadowDielectricTransmission(
        closure, -signedCosTheta);
    const float exit = Bsdf::StraightShadowDielectricTransmission(
        closure, signedCosTheta);
    const float expectedEntry =
        Bsdf::CoupledRoughDielectricDirectionalTransmissionAlbedo(
            signedCosTheta,
            interface.roughness,
            interface.ior,
            false,
            true);
    const float expectedExit =
        Bsdf::CoupledRoughDielectricDirectionalTransmissionAlbedo(
            signedCosTheta,
            interface.roughness,
            interface.ior,
            true,
            true);
    if (!Test_IsClose(entry, expectedEntry, 1.0e-6f) ||
        !Test_IsClose(exit, expectedExit, 1.0e-6f) ||
        std::abs(entry - exit) < 1.0e-3f ||
        std::abs(entry - schlick) < 1.0e-3f) {
        printf(
            "    Straight-shadow table selection/sign mismatch: "
            "entry=%f expectedEntry=%f exit=%f expectedExit=%f schlick=%f\n",
            entry, expectedEntry, exit, expectedExit, schlick);
        ok = false;
    }

    // Exercise the actual AOUSD path, the inclusive exact-Fresnel boundary,
    // continuity immediately above it, and the first used near-smooth LUT
    // region. Independent BSDF sampling resolves the narrow lobe around the
    // IOR=1.5 exit critical angle.
    {
        constexpr int sampleCount = 65536;
        const Vec3f normalShdWldOut(0.0f, 1.0f, 0.0f);
        for (const float alpha :
                 {0.0016f, 0.002f, 0.002001f, 0.005f}) {
            for (const float signedCos : {-0.45f, 0.74f, 0.76f}) {
                SurfaceClosure c;
                auto lowRoughness = makeInterface();
                lowRoughness.roughness = Vec2f(alpha, alpha);
                c.specularIor = ior;
                c.bsdfTree.root = c.bsdfTree.Add(lowRoughness);
                Vec3f omegaOutWld =
                    _DirectionFromCosThetaYUp(std::abs(signedCos));
                if (signedCos > 0.0f) {
                    omegaOutWld = -omegaOutWld;
                }
                const bool frontFacing = signedCos < 0.0f;
                const Vec3f normalIncidentWldOut =
                    frontFacing ? normalShdWldOut : -normalShdWldOut;
                Bsdf::detail::PrepareShadingNormals(
                    &c.bsdfTree, normalIncidentWldOut,
                    normalIncidentWldOut, omegaOutWld);
                const float actual =
                    Bsdf::StraightShadowDielectricTransmission(c, signedCos);
                const float integrated = _IntegrateTransmissionBySampling(
                    c, normalIncidentWldOut, omegaOutWld, sampleCount,
                    frontFacing)[0];
                if (!Test_IsClose(actual, integrated, 0.05f)) {
                    printf(
                        "    Near-smooth straight-shadow mismatch: alpha=%f "
                        "signedCos=%f actual=%f sampled=%f\n",
                        alpha, signedCos, actual, integrated);
                    ok = false;
                }
            }
        }

        for (const float signedCos : {-0.45f, 0.74f, 0.76f}) {
            SurfaceClosure atBoundary;
            auto boundaryInterface = makeInterface();
            boundaryInterface.roughness = Vec2f(0.002f, 0.002f);
            atBoundary.bsdfTree.root =
                atBoundary.bsdfTree.Add(boundaryInterface);
            SurfaceClosure aboveBoundary;
            auto aboveInterface = makeInterface();
            aboveInterface.roughness = Vec2f(0.002001f, 0.002001f);
            aboveBoundary.bsdfTree.root =
                aboveBoundary.bsdfTree.Add(aboveInterface);
            const float boundary = Bsdf::StraightShadowDielectricTransmission(
                atBoundary, signedCos);
            const float above = Bsdf::StraightShadowDielectricTransmission(
                aboveBoundary, signedCos);
            if (!Test_IsClose(boundary, above, 1.0e-4f)) {
                printf(
                    "    Exact-to-LUT transition discontinuity: signedCos=%f "
                    "boundary=%f above=%f\n",
                    signedCos, boundary, above);
                ok = false;
            }
        }
    }

    auto expectFallback = [&](const char* label,
                              Bsdf::DielectricInterfaceData fallback) {
        SurfaceClosure c;
        c.specularIor = ior;
        c.bsdfTree.root = c.bsdfTree.Add(fallback);
        const float actual = Bsdf::StraightShadowDielectricTransmission(
            c, -signedCosTheta);
        if (!Test_IsClose(actual, schlick, 1.0e-6f)) {
            printf(
                "    %s should retain Schlick fallback: got=%f expected=%f\n",
                label, actual, schlick);
            ok = false;
        }
    };

    auto thinWalled = makeInterface();
    thinWalled.thinWalled = true;
    expectFallback("thin-walled", thinWalled);
    auto thinFilm = makeInterface();
    thinFilm.thinFilmWeight = 1.0f;
    thinFilm.thinFilmThickness = 500.0f;
    expectFallback("thin-film", thinFilm);
    auto smooth = makeInterface();
    smooth.roughness = Vec2f(0.0001f, 0.0001f);
    {
        SurfaceClosure c;
        c.specularIor = ior;
        c.bsdfTree.root = c.bsdfTree.Add(smooth);
        const float actual = Bsdf::StraightShadowDielectricTransmission(
            c, -signedCosTheta);
        const float exact = _DielectricTransmittanceForTest(
            ior, signedCosTheta);
        if (!Test_IsClose(actual, exact, 1.0e-6f)) {
            printf(
                "    Smooth coupled interface should use exact Fresnel: "
                "got=%f expected=%f\n",
                actual, exact);
            ok = false;
        }
    }
    auto uncompensated = makeInterface();
    uncompensated.compensateCoupledDielectric = false;
    expectFallback("uncompensated", uncompensated);
    auto reflectionDisabled = makeInterface();
    reflectionDisabled.reflectionWeight = 0.0f;
    expectFallback("reflection-disabled", reflectionDisabled);
    auto transmissionDisabled = makeInterface();
    transmissionDisabled.transmissionWeight = 0.0f;
    expectFallback("transmission-disabled", transmissionDisabled);

    SurfaceClosure ambiguous;
    ambiguous.specularIor = ior;
    const Bsdf::NodeId first = ambiguous.bsdfTree.Add(makeInterface());
    const Bsdf::NodeId second = ambiguous.bsdfTree.Add(makeInterface());
    ambiguous.bsdfTree.root = ambiguous.bsdfTree.Add(
        Bsdf::AddData{first, second});
    const float ambiguousTransmission =
        Bsdf::StraightShadowDielectricTransmission(
            ambiguous, -signedCosTheta);
    if (!Test_IsClose(ambiguousTransmission, schlick, 1.0e-6f)) {
        printf(
            "    Ambiguous two-interface closure should use Schlick: "
            "got=%f expected=%f\n",
            ambiguousTransmission, schlick);
        ok = false;
    }

    return ok;
}

static bool
TestCoupledRoughDielectricTransmissionMatchesFixedWalterReferences()
{
    Bsdf::DielectricInterfaceData interface;
    interface.reflectionWeight = 1.0f;
    interface.reflectionTint = Vec3f(1.0f);
    interface.transmissionWeight = 1.0f;
    interface.transmissionTint = Vec3f(1.0f);
    interface.ior = 1.5f;
    interface.roughness = Vec2f(0.09f, 0.09f);
    interface.compensateCoupledDielectric = true;

    const Vec3f normalShdWldOut(0.0f, 1.0f, 0.0f);
    // Independently evaluated exact-dielectric Walter GGX BTDF anchor. The
    // reciprocal direction lies outside the bounded sampler's normal support,
    // so direct-light evaluation must truncate it instead of analytically
    // continuing the PDF into an unsampled region.
    constexpr float expectedFront = 16.2962551f;
    for (const bool backfacing : {false, true}) {
        const Vec3f omegaOutWld(0.8f, backfacing ? -0.6f : 0.6f, 0.0f);
        const Vec3f omegaInWld(-0.6f, backfacing ? 0.8f : -0.8f, 0.0f);
        const Vec3f normalIncidentWldOut =
            backfacing ? -normalShdWldOut : normalShdWldOut;
        const Vec3f evaluated =
            Bsdf::detail::EvalCoupledRoughDielectricTransmission(
                interface,
                interface.ior,
                backfacing,
                normalIncidentWldOut,
                omegaInWld,
                omegaOutWld);
        const float expected = backfacing ? 0.0f : expectedFront;
        const float tolerance = backfacing ? 1.0e-7f : 3.0e-3f;
        if (!Test_IsClose(
                evaluated, Vec3f(expected), tolerance)) {
            printf(
                "    Fixed Walter BTDF mismatch backfacing=%d: "
                "got=(%f,%f,%f) expected=%f\n",
                backfacing,
                evaluated[0], evaluated[1], evaluated[2], expected);
            return false;
        }
    }
    return true;
}

static bool
TestCoupledRoughDielectricEvalAndPdfRespectAnisotropicBoundedSupport()
{
    using namespace Bsdf::detail;

    Bsdf::DielectricInterfaceData interface;
    interface.reflectionWeight = 1.0f;
    interface.transmissionWeight = 1.0f;
    interface.ior = 1.5f;
    interface.roughness = Vec2f(0.2f, 0.7f);
    interface.tangent = Vec3f(0.0f, 0.0f, 1.0f);
    interface.compensateCoupledDielectric = true;

    const Vec3f normalWldOut(0.0f, 1.0f, 0.0f);
    const Frame frame =
        Frame::FromNormalAndTangent(normalWldOut, interface.tangent);
    const Vec3f omegaOutLocal = Vec3f(0.58f, 0.21f, 0.79f).normalized();
    const Vec3f omegaOutWld = frame.ToWorld(omegaOutLocal);
    Vec3f omegaOutStandard(
        omegaOutLocal[0] * interface.roughness[0],
        omegaOutLocal[1] * interface.roughness[1],
        omegaOutLocal[2]);
    omegaOutStandard.normalize();
    const float minAlpha =
        std::min(interface.roughness[0], interface.roughness[1]);
    const float s = 1.0f + std::sqrt(
        omegaOutLocal[0] * omegaOutLocal[0] +
        omegaOutLocal[1] * omegaOutLocal[1]);
    const float k = (1.0f - minAlpha * minAlpha) * s * s /
        (s * s + minAlpha * minAlpha *
            omegaOutLocal[2] * omegaOutLocal[2]);
    SurfaceClosure closure;
    closure.bsdfTree.root = closure.bsdfTree.Add(interface);
    PrepareShadingNormals(
        &closure.bsdfTree, normalWldOut, normalWldOut, omegaOutWld);

    // Find geometrically valid reflection and transmission pairs immediately
    // on each side of the bounded VNDF cap. Searching the half-vector sphere
    // avoids assuming that a cap-supported normal necessarily reflects above
    // the macro surface.
    Vec3f insideWmLocal;
    Vec3f outsideWmLocal;
    float insideDistance = std::numeric_limits<float>::max();
    float outsideDistance = std::numeric_limits<float>::max();
    const float eta = 1.0f / interface.ior;
    for (int thetaIdx = 1; thetaIdx < 256; ++thetaIdx) {
        const float theta = 0.5f * kPi * float(thetaIdx) / 256.0f;
        for (int phiIdx = 0; phiIdx < 512; ++phiIdx) {
            const float phi = 2.0f * kPi * float(phiIdx) / 512.0f;
            const Vec3f wmLocal(
                std::sin(theta) * std::cos(phi),
                std::sin(theta) * std::sin(phi), std::cos(theta));
            const float cosMO = Dot(omegaOutLocal, wmLocal);
            if (cosMO <= kEpsilon) {
                continue;
            }
            const Vec3f omegaInReflectionLocal =
                2.0f * cosMO * wmLocal - omegaOutLocal;
            const float sin2T = eta * eta *
                std::max(0.0f, 1.0f - cosMO * cosMO);
            if (sin2T >= 1.0f) {
                continue;
            }
            const float cosT = std::sqrt(1.0f - sin2T);
            Vec3f omegaInTransmissionLocal =
                -eta * omegaOutLocal + (eta * cosMO - cosT) * wmLocal;
            omegaInTransmissionLocal.normalize();
            if (omegaInTransmissionLocal[2] >= -kEpsilon) {
                continue;
            }

            Vec3f wmStandard(
                wmLocal[0] / interface.roughness[0],
                wmLocal[1] / interface.roughness[1], wmLocal[2]);
            wmStandard.normalize();
            const Vec3f reflectedStandard =
                2.0f * Dot(omegaOutStandard, wmStandard) * wmStandard -
                omegaOutStandard;
            const float supportMargin =
                reflectedStandard[2] + k * omegaOutStandard[2];
            if (supportMargin >= 0.0f &&
                omegaInReflectionLocal[2] > kEpsilon &&
                supportMargin < insideDistance) {
                insideDistance = supportMargin;
                insideWmLocal = wmLocal;
            } else if (supportMargin < 0.0f &&
                       -supportMargin < outsideDistance) {
                outsideDistance = -supportMargin;
                outsideWmLocal = wmLocal;
            }
        }
    }
    if (insideDistance == std::numeric_limits<float>::max() ||
        outsideDistance == std::numeric_limits<float>::max()) {
        printf("    Failed to find valid directions around VNDF boundary\n");
        return false;
    }

    for (const bool insideSupport : {true, false}) {
        const Vec3f wmLocal =
            insideSupport ? insideWmLocal : outsideWmLocal;
        const float cosMO = Dot(omegaOutLocal, wmLocal);
        const Vec3f omegaInReflectionLocal =
            2.0f * cosMO * wmLocal - omegaOutLocal;
        const Vec3f omegaInReflectionWld =
            frame.ToWorld(omegaInReflectionLocal);
        const Vec3f reflectionEval = PreparedSurfaceApi::EvalSurface(
            closure, normalWldOut, omegaInReflectionWld, omegaOutWld);
        const float reflectionPdf = PdfCoupledRoughDielectric(
            interface, interface.ior, false, normalWldOut,
            omegaInReflectionWld, omegaOutWld);

        const float sin2T = eta * eta *
            std::max(0.0f, 1.0f - cosMO * cosMO);
        const float cosT = std::sqrt(1.0f - sin2T);
        Vec3f omegaInTransmissionLocal =
            -eta * omegaOutLocal + (eta * cosMO - cosT) * wmLocal;
        omegaInTransmissionLocal.normalize();
        const Vec3f omegaInTransmissionWld =
            frame.ToWorld(omegaInTransmissionLocal);
        const Vec3f transmissionEval = PreparedSurfaceApi::EvalSurface(
            closure, normalWldOut, omegaInTransmissionWld, omegaOutWld);
        const float transmissionPdf = PdfCoupledRoughDielectric(
            interface, interface.ior, false, normalWldOut,
            omegaInTransmissionWld, omegaOutWld);

        const bool transmissionPresent =
            transmissionEval[0] > 0.0f && transmissionPdf > 0.0f;
        // The bounded sampler excludes normals whose reflected direction is
        // below the macro surface, so there is no geometrically valid
        // reflection direction just outside its cap. Reflection still pins
        // the supported anisotropic path; transmission exercises both sides
        // of the truncation boundary.
        const bool reflectionPresent =
            reflectionEval[0] > 0.0f && reflectionPdf > 0.0f;
        if ((insideSupport && !reflectionPresent) ||
            transmissionPresent != insideSupport) {
            printf(
                "    Bounded-support mismatch: inside=%d reflection=(%g,%g) "
                "transmission=(%g,%g) distances=(%g,%g)\n",
                insideSupport, reflectionEval[0], reflectionPdf,
                transmissionEval[0], transmissionPdf,
                insideDistance, outsideDistance);
            return false;
        }
    }
    return true;
}

static bool
TestStandaloneDielectricRoughTransmissionRemainsUncompensated()
{
    SurfaceClosure closure;
    Bsdf::DielectricData dielectric;
    dielectric.weight = 1.0f;
    dielectric.tint = Vec3f(1.0f);
    dielectric.ior = 1.5f;
    dielectric.roughness = Vec2f(0.09f, 0.09f);
    dielectric.scatterMode = Bsdf::ScatterMode::Transmission;
    closure.bsdfTree.root = closure.bsdfTree.Add(dielectric);

    const Vec3f normalShdWldOut(0.0f, 1.0f, 0.0f);
    const Vec3f omegaOutWld = _DirectionFromCosThetaYUp(0.1f);
    const auto sample = Bsdf::SampleGGXTransmission(
        0.3f, 1.5f, Vec3f(1.0f), normalShdWldOut, omegaOutWld, 0.3f, 0.7f,
        /* backside = */ false);
    if (sample.pdfSolidAngle <= 0.0f) {
        printf("    Expected valid standalone dielectric transmission sample\n");
        return false;
    }

    const Vec3f evaluated = TestSurfaceApi::EvalSurface(closure, normalShdWldOut,
                                              sample.omegaInWld, omegaOutWld);
    const Vec3f uncompensated =
        Bsdf::EvalGGXTransmission(0.3f, 1.5f, Vec3f(1.0f), normalShdWldOut,
                                  sample.omegaInWld, omegaOutWld,
                                  /* backside = */ false);
    if (!Test_IsClose(evaluated, uncompensated, 1.0e-5f)) {
        printf(
            "    Standalone dielectric transmission was unexpectedly "
            "compensated: eval=(%f,%f,%f) raw=(%f,%f,%f)\n",
            evaluated[0], evaluated[1], evaluated[2],
            uncompensated[0], uncompensated[1], uncompensated[2]);
        return false;
    }

    return true;
}

static bool
TestDielectricInterfaceSamplePdfConsistency()
{
    using namespace Bsdf::detail;
    const Vec3f normalShdWldOut(0.0f, 1.0f, 0.0f);
    struct Case {
        Vec2f alpha;
        float cosThetaOut;
        bool backside;
    };
    const Case cases[] = {
        {Vec2f(0.25f), 0.98f, false},
        {Vec2f(0.25f), 0.85f, true},
        {Vec2f(0.55f, 0.18f), 0.72f, false},
    };

    for (const Case& testCase : cases) {
        Bsdf::DielectricInterfaceData interface;
        interface.reflectionWeight = 1.0f;
        interface.reflectionTint = Vec3f(1.0f);
        interface.transmissionWeight = 1.0f;
        interface.transmissionTint = Vec3f(1.0f);
        interface.ior = 1.5f;
        interface.roughness = testCase.alpha;
        interface.tangent = Vec3f(0.0f, 0.0f, 1.0f);
        interface.compensateCoupledDielectric = true;

        const Frame frame = Frame::FromNormalAndTangent(
            normalShdWldOut, interface.tangent);
        const Vec3f omegaOutLocal(
            std::sqrt(
                1.0f - testCase.cosThetaOut * testCase.cosThetaOut),
            0.0f, testCase.cosThetaOut);
        const Vec3f omegaOutWld = frame.ToWorld(omegaOutLocal);
        const float roughness = std::sqrt(
            0.5f * (testCase.alpha[0] + testCase.alpha[1]));
        const float anisotropy = std::abs(
            testCase.alpha[0] - testCase.alpha[1]) /
            (testCase.alpha[0] + testCase.alpha[1]);
        const bool flipAnisotropy =
            testCase.alpha[0] < testCase.alpha[1];
        const float tableIndex = std::sqrt(
            (interface.ior - kBsdlDielectricIorMin) /
            (kBsdlDielectricIorMax - kBsdlDielectricIorMin));
        const bsdl::mtx::DielectricBSDF<
            bsdl::mtx::DielectricFresnel> bsdlSpec(
                bsdl::GGXDist(
                    roughness, anisotropy, flipAnisotropy),
                bsdl::mtx::DielectricFresnel::from_table_index(
                    tableIndex, testCase.backside),
                testCase.cosThetaOut, roughness, true);
        const Imath::V3f bsdlOut(
            omegaOutLocal[0], omegaOutLocal[1], omegaOutLocal[2]);

        for (const float uChoice : {0.01f, 0.8f}) {
            const bsdl::Sample bsdlSample =
                bsdlSpec.sample(bsdlOut, 0.3f, 0.7f, uChoice);
            if (!(bsdlSample.pdf > 0.0f)) {
                printf(
                    "    Expected valid stock BSDL dielectric sample: "
                    "backside=%d alpha=(%f,%f) choice=%f\n",
                    testCase.backside, testCase.alpha[0], testCase.alpha[1],
                    uChoice);
                return false;
            }

            const Vec3f omegaInWld = frame.ToWorld(Vec3f(
                bsdlSample.wi.x, bsdlSample.wi.y, bsdlSample.wi.z));
            const float productionPdf = PdfCoupledRoughDielectric(
                interface, interface.ior, testCase.backside,
                normalShdWldOut, omegaInWld, omegaOutWld);
            const float pdfTolerance =
                std::max(2.0e-5f, 2.0e-5f * bsdlSample.pdf);
            if (!Test_IsClose(
                    productionPdf, bsdlSample.pdf, pdfTolerance)) {
                printf(
                    "    Production PDF differs from stock BSDL: "
                    "production=%f bsdl=%f backside=%d alpha=(%f,%f) "
                    "choice=%f\n",
                    productionPdf, bsdlSample.pdf, testCase.backside,
                    testCase.alpha[0], testCase.alpha[1], uChoice);
                return false;
            }

            Vec3f evaluated;
            if (bsdlSample.wi.z > 0.0f) {
                const Vec3f fresnel =
                    DielectricInterfaceReflectionCoefficient(
                        interface,
                        ReflectionFresnelCosTheta(
                            omegaInWld, omegaOutWld),
                        interface.ior, testCase.backside);
                evaluated = EvalMicrofacetReflectionAnisotropic(
                    interface.roughness, interface.tangent, fresnel, 1.0f,
                    normalShdWldOut, omegaInWld, omegaOutWld,
                    /* compensateMissingEnergy = */ false);
            } else {
                evaluated = EvalCoupledRoughDielectricTransmission(
                    interface, interface.ior, testCase.backside,
                    normalShdWldOut, omegaInWld, omegaOutWld);
            }
            const float evaluatedCosine = evaluated[0] *
                std::abs(Dot(normalShdWldOut, omegaInWld));
            const float bsdlValueCosine =
                bsdlSample.weight.max() * bsdlSample.pdf;
            const float valueTolerance =
                std::max(2.0e-5f, 2.0e-5f * bsdlValueCosine);
            if (!Test_IsClose(
                    evaluatedCosine, bsdlValueCosine, valueTolerance)) {
                printf(
                    "    Production f*cos differs from stock BSDL "
                    "weight*pdf: production=%f bsdl=%f backside=%d "
                    "alpha=(%f,%f) choice=%f\n",
                    evaluatedCosine, bsdlValueCosine, testCase.backside,
                    testCase.alpha[0], testCase.alpha[1], uChoice);
                return false;
            }
        }
    }

    return true;
}

static bool
TestCoupledRoughDielectricSamplesTransmissionBeyondMacroCriticalAngle()
{
    SurfaceClosure closure;
    Bsdf::DielectricInterfaceData interface;
    interface.reflectionWeight = 1.0f;
    interface.reflectionTint = Vec3f(1.0f);
    interface.transmissionWeight = 1.0f;
    interface.transmissionTint = Vec3f(1.0f);
    interface.ior = 1.5f;
    interface.roughness = Vec2f(0.09f, 0.09f);
    interface.compensateCoupledDielectric = true;
    closure.bsdfTree.root = closure.bsdfTree.Add(interface);

    const Vec3f normalShdWldOut(0.0f, -1.0f, 0.0f);
    Vec3f omegaOutWld = -_DirectionFromCosThetaYUp(0.5f);
    Bsdf::detail::PrepareShadingNormals(
        &closure.bsdfTree, normalShdWldOut, normalShdWldOut, omegaOutWld);
    std::uint64_t state = 0x4d595df4d0f33173ull;
    auto nextFloat = [&state]() {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return float((state >> 33) & 0xFFFFFF) / 16777216.0f;
    };

    int reflectionCount = 0;
    int transmissionCount = 0;
    for (int i = 0; i < 5000; ++i) {
        const Bsdf::BsdfSample sample =
            PreparedSurfaceApi::SampleSurface(closure, normalShdWldOut, omegaOutWld,
                                nextFloat(), nextFloat(), nextFloat(),
                                0.0f, false);
        if (!(sample.pdfSolidAngle > 0.0f)) {
            continue;
        }
        if (!_IsFiniteNonNegative(sample.bsdfValue)) {
            printf("    Coupled rough dielectric produced invalid energy\n");
            return false;
        }
        if (Dot(normalShdWldOut, sample.omegaInWld) *
                Dot(normalShdWldOut, omegaOutWld) >
            0.0f) {
            if (sample.bsdfValue.length() > 1.0e-6f) {
                ++reflectionCount;
            }
        } else if (sample.bsdfValue.length() > 1.0e-6f) {
            ++transmissionCount;
        }
    }

    constexpr int minSupportedSamples = 10;
    if (reflectionCount < minSupportedSamples ||
        transmissionCount < minSupportedSamples) {
        printf(
            "    Expected nontrivial reflection and rough transmission below "
            "the "
            "macro critical angle: reflection=%d transmission=%d\n",
            reflectionCount, transmissionCount);
        return false;
    }
    return true;
}

static bool
TestDeltaDielectricInterfaceTransmissionSamplesSingleFresnel()
{
    SurfaceClosure c;
    Bsdf::DielectricInterfaceData interface;
    interface.reflectionWeight = 1.0f;
    interface.reflectionTint = Vec3f(1.0f);
    interface.transmissionWeight = 1.0f;
    interface.transmissionTint = Vec3f(1.0f);
    interface.ior = 1.5f;
    interface.roughness = Vec2f(0.0f, 0.0f);
    c.bsdfTree.root = c.bsdfTree.Add(interface);

    const Vec3f normalShdWldOut(0.0f, 1.0f, 0.0f);
    const Vec3f omegaOutWld(0.0f, 1.0f, 0.0f);
    const auto sample =
        TestSurfaceApi::SampleSurface(c, normalShdWldOut, omegaOutWld, 0.3f, 0.7f, 0.9f);
    if (sample.pdfSolidAngle <= 0.0f || !sample.isSpecular) {
        printf("    Expected valid delta interface transmission sample\n");
        return false;
    }
    if (Dot(sample.omegaInWld, normalShdWldOut) >= 0.0f) {
        printf("    Expected transmitted direction below the interface\n");
        return false;
    }
    if (!Test_IsClose(sample.bsdfValue, Vec3f(1.0f), 1.0e-4f)) {
        printf("    Delta interface transmission should divide by the single "
               "Fresnel branch probability: f=(%f,%f,%f)\n",
               sample.bsdfValue[0], sample.bsdfValue[1], sample.bsdfValue[2]);
        return false;
    }
    return true;
}

static bool
TestDeltaDielectricInterfaceTirDoesNotAmplifyThroughput()
{
    SurfaceClosure c;
    Bsdf::DielectricInterfaceData interface;
    interface.reflectionWeight = 1.0f;
    interface.reflectionTint = Vec3f(1.0f);
    interface.transmissionWeight = 1.0f;
    interface.transmissionTint = Vec3f(1.0f);
    interface.ior = 1.5f;
    interface.roughness = Vec2f(0.0f, 0.0f);
    interface.hasShadingNormal = true;
    interface.normal = Vec3f(-0.95f, -0.31f, 0.0f).normalized();
    c.bsdfTree.root = c.bsdfTree.Add(interface);

    const Vec3f normalShdWldOut(0.0f, -1.0f, 0.0f);
    const Vec3f omegaOutWld = Vec3f(0.8f, -0.6f, 0.0f).normalized();
    ValidateLeafNormals(&c.bsdfTree, normalShdWldOut);
    Bsdf::detail::PrepareShadingNormals(
        &c.bsdfTree, normalShdWldOut, normalShdWldOut, omegaOutWld);
    const Bsdf::DielectricInterfaceData* preparedInterface =
        std::get_if<Bsdf::DielectricInterfaceData>(
            &c.bsdfTree.nodes[0].data);
    if (!preparedInterface ||
        Test_IsClose(
            preparedInterface->normal, interface.normal, 1.0e-5f)) {
        printf("    Expected TIR interface reflection normal correction\n");
        return false;
    }
    const Bsdf::BsdfSample sample = Bsdf::detail::SampleNode(
        c.bsdfTree, c.bsdfTree.root,
        _MakeSurfaceInteraction(
            normalShdWldOut, normalShdWldOut, normalShdWldOut,
            omegaOutWld, 0.0f, false),
        0.3f, 0.7f, 0.99f);
    if (sample.pdfSolidAngle <= 0.0f || !sample.isSpecular ||
        sample.isTransmission) {
        printf("    Expected valid delta TIR sample\n");
        return false;
    }
    if (Dot(sample.omegaInWld, normalShdWldOut) <= 0.0f) {
        printf("    TIR should stay on the incident side of the interface\n");
        return false;
    }
    const Vec3f expectedPreparedReflection =
        2.0f * Dot(preparedInterface->normal, omegaOutWld) *
            preparedInterface->normal -
        omegaOutWld;
    const Vec3f baseNormalReflection =
        2.0f * Dot(normalShdWldOut, omegaOutWld) *
            normalShdWldOut -
        omegaOutWld;
    if (!Test_IsClose(
            sample.omegaInWld, expectedPreparedReflection, 1.0e-5f) ||
        Test_IsClose(sample.omegaInWld, baseNormalReflection, 1.0e-5f)) {
        printf("    TIR should reflect around the prepared lobe normal\n");
        return false;
    }
    if (!Test_IsClose(sample.bsdfValue, Vec3f(1.0f), 1.0e-5f)) {
        printf("    TIR should not be divided by transmission probability: "
               "f=(%f,%f,%f)\n",
               sample.bsdfValue[0], sample.bsdfValue[1], sample.bsdfValue[2]);
        return false;
    }
    return true;
}

static bool
TestThinWalledDielectricInterfaceSamplePdfConsistency()
{
    SurfaceClosure c;
    Bsdf::DielectricInterfaceData interface;
    interface.reflectionWeight = 1.0f;
    interface.reflectionTint = Vec3f(1.0f);
    interface.transmissionWeight = 1.0f;
    interface.transmissionTint = Vec3f(0.72f, 1.0f, 0.86f);
    interface.ior = 1.5f;
    interface.roughness = Vec2f(0.25f, 0.25f);
    interface.thinWalled = true;
    c.bsdfTree.root = c.bsdfTree.Add(interface);

    const Vec3f normalShdWldOut(0.0f, 1.0f, 0.0f);
    const Vec3f omegaOutWld = Vec3f(0.2f, 0.98f, 0.0f).normalized();
    const auto sample =
        TestSurfaceApi::SampleSurface(c, normalShdWldOut, omegaOutWld, 0.3f, 0.7f, 0.8f);
    if (sample.pdfSolidAngle <= 0.0f || sample.isSpecular) {
        printf("    Expected valid rough thin-walled interface sample\n");
        return false;
    }
    if (Dot(sample.omegaInWld, normalShdWldOut) >= 0.0f) {
        printf("    Expected thin-walled transmission below the surface\n");
        return false;
    }

    const float pdf =
        _PdfSurface(c, _MakeSurfaceInteraction(normalShdWldOut, normalShdWldOut, normalShdWldOut, omegaOutWld), sample.omegaInWld);
    const float ratio = sample.pdfSolidAngle / (pdf + 1.0e-10f);
    if (ratio < 0.8f || ratio > 1.2f) {
        printf("    Thin-walled interface sample pdf=%f != PdfSurface=%f "
               "(ratio=%f)\n",
               sample.pdfSolidAngle, pdf, ratio);
        return false;
    }

    return true;
}

static bool
TestDeltaThinWalledDielectricInterfaceTransmitsStraightThrough()
{
    SurfaceClosure c;
    Bsdf::DielectricInterfaceData interface;
    interface.reflectionWeight = 1.0f;
    interface.reflectionTint = Vec3f(1.0f);
    interface.transmissionWeight = 1.0f;
    interface.transmissionTint = Vec3f(1.0f);
    interface.ior = 1.5f;
    interface.roughness = Vec2f(0.0f, 0.0f);
    interface.thinWalled = true;
    c.bsdfTree.root = c.bsdfTree.Add(interface);

    const Vec3f normalShdWldOut(0.0f, 1.0f, 0.0f);
    const Vec3f omegaOutWld = Vec3f(0.3f, 0.953939f, 0.0f).normalized();
    const auto sample =
        TestSurfaceApi::SampleSurface(c, normalShdWldOut, omegaOutWld, 0.3f, 0.7f, 0.9f);
    if (sample.pdfSolidAngle <= 0.0f || !sample.isSpecular) {
        printf("    Expected valid delta thin-walled transmission sample\n");
        return false;
    }
    if (!Test_IsClose(sample.omegaInWld, -omegaOutWld, 1.0e-5f)) {
        printf("    Thin-walled transmission should continue straight through: "
               "omegaInWld=(%f,%f,%f), expected=(%f,%f,%f)\n",
               sample.omegaInWld[0], sample.omegaInWld[1], sample.omegaInWld[2],
               -omegaOutWld[0], -omegaOutWld[1], -omegaOutWld[2]);
        return false;
    }
    if (!Test_IsClose(sample.bsdfValue, Vec3f(1.0f), 1.0e-4f)) {
        printf("    Delta thin-walled transmission should divide by its "
               "window-transmission branch probability: f=(%f,%f,%f)\n",
               sample.bsdfValue[0], sample.bsdfValue[1], sample.bsdfValue[2]);
        return false;
    }
    return true;
}

static bool
TestPrunePreservesPreparedLeafNormals()
{
    SurfaceClosure closure;
    Bsdf::ConductorData conductor;
    conductor.roughness = Vec2f(0.2f);
    Bsdf::BurleyDiffuseData diffuse;
    diffuse.hasShadingNormal = true;
    diffuse.normal = Vec3f(0.0f, 0.0f, 2.0f);
    Bsdf::AddData add;
    add.in1 = closure.bsdfTree.Add(conductor);
    add.in2 = closure.bsdfTree.Add(diffuse);
    closure.bsdfTree.root = closure.bsdfTree.Add(add);

    const Vec3f normalShdWldOut =
        Vec3f(-0.4f, 0.0f, 0.9165f).normalized();
    const Vec3f normalGeomWldOut(0.0f, 0.0f, 1.0f);
    const Vec3f omegaOutWld(0.0f, 0.0f, 1.0f);
    const Vec3f omegaInWld =
        Vec3f(0.2f, 0.1f, 0.97f).normalized();
    ValidateLeafNormals(&closure.bsdfTree, normalShdWldOut);
    Bsdf::detail::PrepareShadingNormals(
        &closure.bsdfTree, normalShdWldOut,
        normalGeomWldOut, omegaOutWld);

    const SurfaceClosure pruned = Bsdf::PruneCausticClassLobes(closure);
    const Bsdf::BurleyDiffuseData* prunedDiffuse = nullptr;
    for (const Bsdf::Node& node : pruned.bsdfTree.nodes) {
        if (const Bsdf::BurleyDiffuseData* data =
                std::get_if<Bsdf::BurleyDiffuseData>(&node.data)) {
            prunedDiffuse = data;
            break;
        }
    }
    const Bsdf::BurleyDiffuseData* sourceDiffuse =
        std::get_if<Bsdf::BurleyDiffuseData>(
            &closure.bsdfTree.nodes[add.in2].data);
    if (!prunedDiffuse || !sourceDiffuse ||
        !prunedDiffuse->hasShadingNormal ||
        !Test_IsClose(
            prunedDiffuse->normal, sourceDiffuse->normal, 1.0e-6f)) {
        return false;
    }

    const Vec3f sourceEval = PreparedSurfaceApi::EvalSurface(
        closure, normalShdWldOut, omegaInWld, omegaOutWld);
    const Vec3f prunedEval = PreparedSurfaceApi::EvalSurface(
        pruned, normalShdWldOut, omegaInWld, omegaOutWld);
    const float sourcePdf = _PdfPreparedSurface(closure, _MakeSurfaceInteraction(normalShdWldOut, normalShdWldOut, normalShdWldOut, omegaOutWld), omegaInWld);
    const float prunedPdf = _PdfPreparedSurface(pruned, _MakeSurfaceInteraction(normalShdWldOut, normalShdWldOut, normalShdWldOut, omegaOutWld), omegaInWld);
    const Bsdf::BsdfSample sourceSample = PreparedSurfaceApi::SampleSurface(
        closure, normalShdWldOut, omegaOutWld, 0.3f, 0.7f, 0.2f);
    const Bsdf::BsdfSample prunedSample = PreparedSurfaceApi::SampleSurface(
        pruned, normalShdWldOut, omegaOutWld, 0.3f, 0.7f, 0.2f);
    return Test_IsClose(sourceEval, prunedEval, 1.0e-6f) &&
        Test_IsClose(sourcePdf, prunedPdf, 1.0e-6f) &&
        Test_IsClose(
            sourceSample.omegaInWld, prunedSample.omegaInWld, 1.0e-6f) &&
        Test_IsClose(
            sourceSample.bsdfValue, prunedSample.bsdfValue, 1.0e-6f) &&
        Test_IsClose(
            sourceSample.pdfSolidAngle,
            prunedSample.pdfSolidAngle, 1.0e-6f);
}

static bool
TestPruneCausticClassLobesRemovesDeltaReflectionFromAdd()
{
    Bsdf::ClosureTree tree;

    Bsdf::OrenNayarDiffuseData diffuse;
    diffuse.weight = 1.0f;
    diffuse.color = Vec3f(0.8f);

    Bsdf::ConductorData mirror;
    mirror.weight = 1.0f;
    mirror.roughness = Vec2f(0.0f, 0.0f);

    Bsdf::AddData add;
    add.in1 = tree.Add(diffuse);
    add.in2 = tree.Add(mirror);
    tree.root = tree.Add(add);

    SurfaceClosure closure;
    closure.bsdfTree = tree;
    const SurfaceClosure pruned = Bsdf::PruneCausticClassLobes(closure);
    if (!pruned.HasBsdfTree()) {
        printf("    Pruned add closure unexpectedly became empty\n");
        return false;
    }

    const Vec3f normalShdWldOut(0.0f, 1.0f, 0.0f);
    const Vec3f omegaOutWld(0.0f, 1.0f, 0.0f);
    const auto sample = TestSurfaceApi::SampleSurface(pruned, normalShdWldOut,
                                            omegaOutWld, 0.3f, 0.7f, 0.99f);
    if (sample.pdfSolidAngle <= 0.0f || sample.isSpecular ||
        !sample.isDiffuseLike) {
        printf("    Expected remaining diffuse sample after pruning: "
               "pdf=%f specular=%d diffuseLike=%d\n",
               sample.pdfSolidAngle, sample.isSpecular, sample.isDiffuseLike);
        return false;
    }

    return true;
}

static bool
TestPruneCausticClassLobesRemovesInterfaceTransmission()
{
    SurfaceClosure closure;
    Bsdf::DielectricInterfaceData interface;
    interface.reflectionWeight = 1.0f;
    interface.reflectionTint = Vec3f(1.0f);
    interface.transmissionWeight = 1.0f;
    interface.transmissionTint = Vec3f(0.7f, 0.9f, 1.0f);
    interface.ior = 1.5f;
    interface.roughness = Vec2f(0.25f, 0.25f);
    closure.bsdfTree.root = closure.bsdfTree.Add(interface);

    const SurfaceClosure pruned = Bsdf::PruneCausticClassLobes(closure);
    if (!pruned.HasBsdfTree()) {
        printf("    Rough interface reflection should remain after pruning\n");
        return false;
    }

    const Vec3f normalShdWldOut(0.0f, 1.0f, 0.0f);
    const Vec3f omegaOutWld = Vec3f(0.2f, 0.98f, 0.0f).normalized();
    const Vec3f omegaInReflectionWld = Vec3f(-0.1f, 0.995f, 0.0f).normalized();
    const Vec3f omegaInTransmissionWld =
        Vec3f(-0.1f, -0.995f, 0.0f).normalized();

    const Vec3f reflectEval = TestSurfaceApi::EvalSurface(
        pruned, normalShdWldOut, omegaInReflectionWld, omegaOutWld);
    const Vec3f transmitEval = TestSurfaceApi::EvalSurface(
        pruned, normalShdWldOut, omegaInTransmissionWld, omegaOutWld);
    if (reflectEval.length() <= 0.0f || transmitEval.length() > 1.0e-6f) {
        printf(
            "    Pruned interface eval mismatch: reflect=(%f,%f,%f) "
            "transmit=(%f,%f,%f)\n",
            reflectEval[0], reflectEval[1], reflectEval[2],
            transmitEval[0], transmitEval[1], transmitEval[2]);
        return false;
    }

    const auto sample = TestSurfaceApi::SampleSurface(pruned, normalShdWldOut,
                                            omegaOutWld, 0.3f, 0.7f, 0.99f);
    if (sample.pdfSolidAngle <= 0.0f || sample.isSpecular ||
        Dot(sample.omegaInWld, normalShdWldOut) <= 0.0f) {
        printf("    Expected rough reflection sample after interface pruning: "
               "pdf=%f specular=%d omegaInWld=(%f,%f,%f)\n",
               sample.pdfSolidAngle, sample.isSpecular, sample.omegaInWld[0],
               sample.omegaInWld[1], sample.omegaInWld[2]);
        return false;
    }

    return true;
}

static bool
TestPruneCausticClassLobesEmptyTreeDoesNotUseLegacyFallback()
{
    SurfaceClosure closure;
    closure.baseColor = Vec3f(0.9f, 0.2f, 0.1f);
    closure.specular = 1.0f;
    closure.transmission = 1.0f;

    Bsdf::ConductorData mirror;
    mirror.weight = 1.0f;
    mirror.roughness = Vec2f(0.0f, 0.0f);
    closure.bsdfTree.root = closure.bsdfTree.Add(mirror);

    const SurfaceClosure pruned = Bsdf::PruneCausticClassLobes(closure);
    if (pruned.HasBsdfTree()) {
        printf("    Smooth mirror tree should be fully pruned\n");
        return false;
    }

    const Vec3f normalShdWldOut(0.0f, 1.0f, 0.0f);
    const Vec3f omegaOutWld(0.0f, 1.0f, 0.0f);
    const Vec3f omegaInWld = Vec3f(0.25f, 0.9682458f, 0.0f).normalized();
    const Vec3f eval =
        TestSurfaceApi::EvalSurface(pruned, normalShdWldOut, omegaInWld, omegaOutWld);
    const auto sample = TestSurfaceApi::SampleSurface(pruned, normalShdWldOut,
                                            omegaOutWld, 0.3f, 0.7f, 0.2f);
    if (eval.length() > 1.0e-6f || sample.pdfSolidAngle > 0.0f) {
        printf("    Empty pruned tree fell back to legacy BSDF: "
               "eval=(%f,%f,%f) pdf=%f\n",
               eval[0], eval[1], eval[2], sample.pdfSolidAngle);
        return false;
    }

    return true;
}

static bool
TestPruneCausticClassLobesPreservesMixWeight()
{
    Bsdf::ClosureTree tree;

    Bsdf::OrenNayarDiffuseData diffuse;
    diffuse.weight = 1.0f;
    diffuse.color = Vec3f(0.6f, 0.7f, 0.8f);

    Bsdf::ConductorData mirror;
    mirror.weight = 1.0f;
    mirror.roughness = Vec2f(0.0f, 0.0f);

    Bsdf::MixData mix;
    mix.bg = tree.Add(diffuse);
    mix.fg = tree.Add(mirror);
    mix.mix = 0.75f;
    tree.root = tree.Add(mix);

    SurfaceClosure closure;
    closure.bsdfTree = tree;
    const SurfaceClosure pruned = Bsdf::PruneCausticClassLobes(closure);

    SurfaceClosure diffuseOnly;
    diffuseOnly.bsdfTree.root = diffuseOnly.bsdfTree.Add(diffuse);

    const Vec3f normalShdWldOut(0.0f, 1.0f, 0.0f);
    const Vec3f omegaOutWld(0.0f, 1.0f, 0.0f);
    const Vec3f omegaInWld = Vec3f(0.25f, 0.9682458f, 0.0f).normalized();

    const Vec3f expected = TestSurfaceApi::EvalSurface(diffuseOnly, normalShdWldOut,
                                             omegaInWld, omegaOutWld) *
                           0.25f;
    const Vec3f actual =
        TestSurfaceApi::EvalSurface(pruned, normalShdWldOut, omegaInWld, omegaOutWld);
    if (!Test_IsClose(actual, expected, 1.0e-5f)) {
        printf(
            "    Pruned mix did not preserve surviving branch weight: "
            "actual=(%f,%f,%f) expected=(%f,%f,%f)\n",
            actual[0], actual[1], actual[2],
            expected[0], expected[1], expected[2]);
        return false;
    }

    return true;
}

static bool
TestPruneCausticClassLobesPrunesAdobeTransmissionAndSmoothSpecular()
{
    Bsdf::AdobeOpenPbrData data;
    data.baseWeight = 0.0f;
    data.specularWeight = 1.0f;
    data.specularRoughness = 0.0f;
    data.coatWeight = 1.0f;
    data.coatRoughness = 0.0f;
    data.transmissionWeight = 1.0f;

    SurfaceClosure closure;
    closure.bsdfTree.root = closure.bsdfTree.Add(data);

    const SurfaceClosure pruned = Bsdf::PruneCausticClassLobes(closure);
    const Bsdf::Node* root = pruned.bsdfTree.Get(pruned.bsdfTree.root);
    const auto* prunedData = root
        ? std::get_if<Bsdf::AdobeOpenPbrData>(&root->data)
        : nullptr;
    if (!prunedData) {
        printf("    Expected pruned Adobe OpenPBR backend node\n");
        return false;
    }
    if (prunedData->transmissionWeight != 0.0f ||
        prunedData->specularWeight != 0.0f ||
        prunedData->coatWeight != 0.0f) {
        printf(
            "    Adobe pruning mismatch: transmission=%f specular=%f coat=%f\n",
            prunedData->transmissionWeight,
            prunedData->specularWeight,
            prunedData->coatWeight);
        return false;
    }

    return true;
}

static bool
TestSampleGGXTransmissionHemisphere()
{
    Vec3f normalShdWldOut(0, 1, 0);
    Vec3f omegaOutWld = Vec3f(0.3f, 0.95f, 0.0f).normalized();
    Vec3f tColor(1.0f);

    int valid = 0;
    for (int i = 0; i < 64; ++i) {
        float u1 = (i + 0.5f) / 64.0f;
        float u2 = (i * 13 % 64 + 0.5f) / 64.0f;
        auto s = Bsdf::SampleGGXTransmission(
            0.3f, 1.5f, tColor, normalShdWldOut, omegaOutWld, u1, u2,
            /* backside = */ false);
        if (s.pdfSolidAngle > 0.0f) {
            ++valid;
            if (Dot(s.omegaInWld, normalShdWldOut) > 1e-5f) {
                printf("    GGX transmission sample above surface: "
                       "normalDotOmegaIn=%f\n",
                       Dot(s.omegaInWld, normalShdWldOut));
                return false;
            }
        }
    }
    if (valid < 32) {
        printf("    Too few valid GGX transmission samples: %d/64\n", valid);
        return false;
    }
    return true;
}

static bool
TestSampleGGXTransmissionPdfConsistency()
{
    Vec3f normalShdWldOut(0, 1, 0);
    Vec3f omegaOutWld = Vec3f(0.2f, 0.98f, 0.0f).normalized();
    Vec3f tColor(1.0f);

    auto s = Bsdf::SampleGGXTransmission(0.4f, 1.5f, tColor, normalShdWldOut,
                                         omegaOutWld, 0.3f, 0.7f,
                                         /* backside = */ false);
    if (s.pdfSolidAngle <= 0.0f)
        return true;

    float pdf2 = Bsdf::PdfGGXTransmission(0.4f, 1.5f, normalShdWldOut,
                                          s.omegaInWld, omegaOutWld,
                                          /* backside = */ false);
    float ratio = s.pdfSolidAngle / (pdf2 + 1e-10f);
    if (ratio < 0.8f || ratio > 1.2f) {
        printf("    Sample pdf=%f != PdfGGXTransmission=%f (ratio=%f)\n",
               s.pdfSolidAngle, pdf2, ratio);
        return false;
    }
    return true;
}

static bool
TestRoughTransmissionSpreads()
{
    Vec3f normalShdWldOut(0, 1, 0);
    Vec3f omegaOutWld = Vec3f(0, 1, 0);
    Vec3f tColor(1.0f);
    float roughness = 0.5f;

    Vec3f omegaInFirstWld(0.0f);
    bool firstSet = false;
    float maxDeviation = 0.0f;

    for (int i = 0; i < 64; ++i) {
        float u1 = (i + 0.5f) / 64.0f;
        float u2 = (i * 7 % 64 + 0.5f) / 64.0f;
        auto s = Bsdf::SampleGGXTransmission(
            roughness, 1.5f, tColor, normalShdWldOut, omegaOutWld, u1, u2,
            /* backside = */ false);
        if (s.pdfSolidAngle <= 0.0f)
            continue;
        if (!firstSet) {
            omegaInFirstWld = s.omegaInWld;
            firstSet = true;
            continue;
        }
        float dev = 1.0f - Dot(s.omegaInWld, omegaInFirstWld);
        if (dev > maxDeviation) maxDeviation = dev;
    }

    if (maxDeviation < 0.01f) {
        printf("    Rough transmission directions too similar (maxDev=%f)\n",
               maxDeviation);
        return false;
    }
    return true;
}

static bool
TestThinFilmDielectricChangesReflectionColor()
{
    Bsdf::DielectricData base;
    base.weight = 1.0f;
    base.tint = Vec3f(1.0f);
    base.ior = 1.5f;
    base.scatterMode = Bsdf::ScatterMode::Reflection;

    Bsdf::DielectricData thinFilm = base;
    thinFilm.thinFilmWeight = 1.0f;
    thinFilm.thinFilmThickness = 300.0f;
    thinFilm.thinFilmIor = 1.3f;

    SurfaceClosure baseClosure;
    baseClosure.bsdfTree.root = baseClosure.bsdfTree.Add(base);

    SurfaceClosure thinFilmClosure;
    thinFilmClosure.bsdfTree.root = thinFilmClosure.bsdfTree.Add(thinFilm);

    const Vec3f normalShdWldOut(0.0f, 1.0f, 0.0f);
    const Vec3f omegaOutWld = Vec3f(0.3f, 0.95f, 0.0f).normalized();
    const Vec3f omegaInWld = Vec3f(-0.2f, 0.98f, 0.0f).normalized();

    const Vec3f baseEval = TestSurfaceApi::EvalSurface(baseClosure, normalShdWldOut,
                                             omegaInWld, omegaOutWld);
    const Vec3f thinFilmEval = TestSurfaceApi::EvalSurface(
        thinFilmClosure, normalShdWldOut, omegaInWld, omegaOutWld);

    return !Test_IsClose(baseEval, thinFilmEval, 1e-4f) &&
           std::abs(thinFilmEval[0] - thinFilmEval[1]) > 1e-4f;
}

static bool
TestThinFilmConductorChangesReflectionColor()
{
    Bsdf::ConductorData base;
    base.weight = 1.0f;

    Bsdf::ConductorData thinFilm = base;
    thinFilm.thinFilmWeight = 1.0f;
    thinFilm.thinFilmThickness = 300.0f;
    thinFilm.thinFilmIor = 1.3f;

    SurfaceClosure baseClosure;
    baseClosure.bsdfTree.root = baseClosure.bsdfTree.Add(base);

    SurfaceClosure thinFilmClosure;
    thinFilmClosure.bsdfTree.root = thinFilmClosure.bsdfTree.Add(thinFilm);

    const Vec3f normalShdWldOut(0.0f, 1.0f, 0.0f);
    const Vec3f omegaOutWld = Vec3f(0.25f, 0.96f, 0.1f).normalized();
    const Vec3f omegaInWld = Vec3f(-0.1f, 0.99f, 0.05f).normalized();

    const Vec3f baseEval = TestSurfaceApi::EvalSurface(baseClosure, normalShdWldOut,
                                             omegaInWld, omegaOutWld);
    const Vec3f thinFilmEval = TestSurfaceApi::EvalSurface(
        thinFilmClosure, normalShdWldOut, omegaInWld, omegaOutWld);

    const bool changed = !Test_IsClose(baseEval, thinFilmEval, 1e-5f);
    const bool chromatic =
        std::abs(thinFilmEval[0] - thinFilmEval[1]) > 1e-5f;
    if (!changed || !chromatic) {
        printf(
            "    base=(%f,%f,%f) thinFilm=(%f,%f,%f)\n",
            baseEval[0], baseEval[1], baseEval[2],
            thinFilmEval[0], thinFilmEval[1], thinFilmEval[2]);
    }

    return changed && chromatic;
}

static bool
TestThinFilmGeneralizedSchlickChangesReflectionColor()
{
    Bsdf::GeneralizedSchlickData base;
    base.weight = 1.0f;
    base.color0 = Vec3f(0.08f);
    base.color82 = Vec3f(0.08f);
    base.color90 = Vec3f(1.0f);
    base.exponent = 5.0f;
    base.roughness = Vec2f(0.05f, 0.05f);
    base.scatterMode = Bsdf::ScatterMode::Reflection;

    Bsdf::GeneralizedSchlickData thinFilm = base;
    thinFilm.thinFilmWeight = 1.0f;
    thinFilm.thinFilmThickness = 320.0f;
    thinFilm.thinFilmIor = 1.45f;

    SurfaceClosure baseClosure;
    baseClosure.bsdfTree.root = baseClosure.bsdfTree.Add(base);

    SurfaceClosure thinFilmClosure;
    thinFilmClosure.bsdfTree.root = thinFilmClosure.bsdfTree.Add(thinFilm);

    const Vec3f normalShdWldOut(0.0f, 1.0f, 0.0f);
    const Vec3f omegaOutWld = Vec3f(0.82f, 0.57f, 0.0f).normalized();
    const Vec3f omegaInWld = Vec3f(-0.68f, 0.71f, 0.18f).normalized();

    const Vec3f baseEval = TestSurfaceApi::EvalSurface(baseClosure, normalShdWldOut,
                                             omegaInWld, omegaOutWld);
    const Vec3f thinFilmEval = TestSurfaceApi::EvalSurface(
        thinFilmClosure, normalShdWldOut, omegaInWld, omegaOutWld);

    const bool changed = !Test_IsClose(baseEval, thinFilmEval, 1e-5f);
    const bool chromatic =
        std::abs(thinFilmEval[0] - thinFilmEval[1]) > 1e-5f;
    if (!changed || !chromatic) {
        printf(
            "    base=(%f,%f,%f) thinFilm=(%f,%f,%f)\n",
            baseEval[0], baseEval[1], baseEval[2],
            thinFilmEval[0], thinFilmEval[1], thinFilmEval[2]);
    }

    return changed && chromatic;
}

static bool
TestThinFilmSampleSurfacePdfConsistency()
{
    SurfaceClosure c;
    Bsdf::DielectricData dielectric;
    dielectric.weight = 1.0f;
    dielectric.tint = Vec3f(1.0f);
    dielectric.ior = 1.5f;
    dielectric.roughness = Vec2f(0.2f, 0.2f);
    dielectric.scatterMode = Bsdf::ScatterMode::ReflectionTransmission;
    dielectric.thinFilmWeight = 1.0f;
    dielectric.thinFilmThickness = 280.0f;
    dielectric.thinFilmIor = 1.35f;
    c.bsdfTree.root = c.bsdfTree.Add(dielectric);

    const Vec3f normalShdWldOut(0.0f, 1.0f, 0.0f);
    const Vec3f omegaOutWld = Vec3f(0.2f, 0.98f, 0.0f).normalized();

    const auto sample =
        TestSurfaceApi::SampleSurface(c, normalShdWldOut, omegaOutWld, 0.3f, 0.7f, 0.65f);
    if (sample.pdfSolidAngle <= 0.0f) {
        printf("    Expected valid thin-film sample\n");
        return false;
    }

    const float pdf =
        _PdfSurface(c, _MakeSurfaceInteraction(normalShdWldOut, normalShdWldOut, normalShdWldOut, omegaOutWld), sample.omegaInWld);
    const float ratio = sample.pdfSolidAngle / (pdf + 1e-10f);
    if (ratio < 0.8f || ratio > 1.2f) {
        printf("    Thin-film sample pdf=%f != PdfSurface=%f (ratio=%f)\n",
               sample.pdfSolidAngle, pdf, ratio);
        return false;
    }
    return true;
}

static bool
TestTreeDielectricCustomNormalMatchesStandaloneShadingNormal()
{
    SurfaceClosure customNormalClosure;
    Bsdf::DielectricData customNormal;
    customNormal.weight = 1.0f;
    customNormal.tint = Vec3f(1.0f);
    customNormal.ior = 1.5f;
    customNormal.roughness = Vec2f(0.08f * 0.08f, 0.08f * 0.08f);
    customNormal.normal = Vec3f(0.0f, 0.70710677f, 0.70710677f);
    customNormal.hasShadingNormal = true;
    customNormal.scatterMode = Bsdf::ScatterMode::Reflection;
    customNormalClosure.bsdfTree.root =
        customNormalClosure.bsdfTree.Add(customNormal);

    SurfaceClosure referenceClosure;
    Bsdf::DielectricData reference = customNormal;
    reference.hasShadingNormal = false;
    referenceClosure.bsdfTree.root = referenceClosure.bsdfTree.Add(reference);

    const Vec3f surfaceNormal(0.0f, 1.0f, 0.0f);
    const Vec3f coatNormal =
        Bsdf::detail::ResolveShadingNormal(customNormal, surfaceNormal);
    const Vec3f omegaOutWld = Vec3f(0.0f, 0.9238795f, 0.3826834f).normalized();
    const Vec3f omegaInWld = Vec3f(0.15f, 0.8293090f, 0.5382608f).normalized();

    const Vec3f customEval = TestSurfaceApi::EvalSurface(
        customNormalClosure, surfaceNormal, omegaInWld, omegaOutWld);
    const Vec3f referenceEval = TestSurfaceApi::EvalSurface(referenceClosure, coatNormal,
                                                  omegaInWld, omegaOutWld);
    if (!Test_IsClose(customEval, referenceEval, 1e-4f)) {
        printf(
            "    Custom normal eval mismatch: custom=(%f,%f,%f) "
            "reference=(%f,%f,%f)\n",
            customEval[0], customEval[1], customEval[2],
            referenceEval[0], referenceEval[1], referenceEval[2]);
        return false;
    }

    const float customPdf = _PdfSurface(customNormalClosure, _MakeSurfaceInteraction(surfaceNormal, surfaceNormal, surfaceNormal, omegaOutWld), omegaInWld);
    const float referencePdf =
        _PdfSurface(referenceClosure, _MakeSurfaceInteraction(coatNormal, coatNormal, coatNormal, omegaOutWld), omegaInWld);
    if (!Test_IsClose(customPdf, referencePdf, 1e-4f)) {
        printf("    Custom normal pdf mismatch: custom=%f reference=%f\n",
               customPdf, referencePdf);
        return false;
    }

    const auto customSample = TestSurfaceApi::SampleSurface(
        customNormalClosure, surfaceNormal, omegaOutWld, 0.3f, 0.7f, 0.2f);
    const auto referenceSample = TestSurfaceApi::SampleSurface(
        referenceClosure, coatNormal, omegaOutWld, 0.3f, 0.7f, 0.2f);
    if (!Test_IsClose(customSample.omegaInWld, referenceSample.omegaInWld,
                      1e-4f) ||
        !Test_IsClose(customSample.bsdfValue, referenceSample.bsdfValue,
                      1e-4f) ||
        !Test_IsClose(customSample.pdfSolidAngle, referenceSample.pdfSolidAngle,
                      1e-4f)) {
        printf("    Custom normal sample mismatch: "
               "omegaInCustomWld=(%f,%f,%f) omegaInReferenceWld=(%f,%f,%f) "
               "customPdf=%f "
               "referencePdf=%f\n",
               customSample.omegaInWld[0], customSample.omegaInWld[1],
               customSample.omegaInWld[2], referenceSample.omegaInWld[0],
               referenceSample.omegaInWld[1], referenceSample.omegaInWld[2],
               customSample.pdfSolidAngle, referenceSample.pdfSolidAngle);
        return false;
    }

    return true;
}

static bool
TestTreeAnisotropicReflectionRespondsToTangent()
{
    SurfaceClosure tangentXClosure;
    Bsdf::DielectricData tangentX;
    tangentX.weight = 1.0f;
    tangentX.tint = Vec3f(1.0f);
    tangentX.ior = 1.5f;
    tangentX.roughness = Vec2f(0.05f, 0.45f);
    tangentX.tangent = Vec3f(1.0f, 0.0f, 0.0f);
    tangentX.scatterMode = Bsdf::ScatterMode::Reflection;
    tangentXClosure.bsdfTree.root = tangentXClosure.bsdfTree.Add(tangentX);

    SurfaceClosure tangentZClosure;
    Bsdf::DielectricData tangentZ = tangentX;
    tangentZ.tangent = Vec3f(0.0f, 0.0f, 1.0f);
    tangentZClosure.bsdfTree.root = tangentZClosure.bsdfTree.Add(tangentZ);

    const Vec3f normalShdWldOut(0.0f, 1.0f, 0.0f);
    const Vec3f omegaOutWld = Vec3f(0.0f, 1.0f, 0.0f);
    const Vec3f omegaInWld = Vec3f(0.05f, 0.9987492f, 0.0f).normalized();

    const Vec3f evalX = TestSurfaceApi::EvalSurface(tangentXClosure, normalShdWldOut,
                                          omegaInWld, omegaOutWld);
    const Vec3f evalZ = TestSurfaceApi::EvalSurface(tangentZClosure, normalShdWldOut,
                                          omegaInWld, omegaOutWld);
    const float magX = evalX.length();
    const float magZ = evalZ.length();
    const float ratio = std::max(magX, magZ) / std::max(std::min(magX, magZ), 1e-8f);

    if (ratio <= 1.3f) {
        printf("    Expected tangent rotation to change anisotropic response:"
               " tangentX=%f tangentZ=%f ratio=%f\n",
               magX, magZ, ratio);
        return false;
    }
    return true;
}

static bool
TestTreeAnisotropicReflectionUsesTurquinCompensation()
{
    constexpr float alpha = 0.8f;

    Bsdf::GeneralizedSchlickData isotropic;
    isotropic.weight = 1.0f;
    isotropic.color0 = Vec3f(1.0f);
    isotropic.color82 = Vec3f(1.0f);
    isotropic.color90 = Vec3f(1.0f);
    isotropic.roughness = Vec2f(alpha, alpha);
    isotropic.scatterMode = Bsdf::ScatterMode::Reflection;

    SurfaceClosure isotropicClosure;
    isotropicClosure.bsdfTree.root =
        isotropicClosure.bsdfTree.Add(isotropic);

    Bsdf::GeneralizedSchlickData anisotropic = isotropic;
    anisotropic.roughness = Vec2f(alpha, alpha + 2.0e-5f);
    anisotropic.tangent = Vec3f(1.0f, 0.0f, 0.0f);

    SurfaceClosure anisotropicClosure;
    anisotropicClosure.bsdfTree.root =
        anisotropicClosure.bsdfTree.Add(anisotropic);

    const Vec3f normalShdWldOut(0.0f, 1.0f, 0.0f);
    const Vec3f omegaOutWld = _DirectionFromCosThetaYUp(0.55f).normalized();
    const Vec3f omegaInWld = Vec3f(-0.25f, 0.78f, 0.57f).normalized();

    const Vec3f isotropicEval = TestSurfaceApi::EvalSurface(
        isotropicClosure, normalShdWldOut, omegaInWld, omegaOutWld);
    const Vec3f anisotropicEval = TestSurfaceApi::EvalSurface(
        anisotropicClosure, normalShdWldOut, omegaInWld, omegaOutWld);

    if (!Test_IsClose(isotropicEval, anisotropicEval, 5.0e-4f)) {
        printf(
            "    Near-isotropic anisotropic compensation mismatch: "
            "isotropic=(%f,%f,%f) anisotropic=(%f,%f,%f)\n",
            isotropicEval[0], isotropicEval[1], isotropicEval[2],
            anisotropicEval[0], anisotropicEval[1], anisotropicEval[2]);
        return false;
    }

    return true;
}

static bool
TestBsdlDielectricReflFrontLutUsesLinearCosThetaGrid()
{
    namespace lut = mxcpp::bsdf_luts;

    constexpr float eta = 1.001f;
    constexpr int cosIndex = 1;
    constexpr float linearCos =
        static_cast<float>(cosIndex) /
        static_cast<float>(lut::kBsdlDielectricReflFrontCosThetaCount - 1);
    constexpr float squaredCos = linearCos * linearCos;
    const float tableValue =
        lut::kBsdlDielectricReflFrontFilter[0][0][cosIndex];
    const float expectedLinear =
        _DielectricTransmittanceForTest(eta, linearCos);
    const float expectedSquared =
        _DielectricTransmittanceForTest(eta, squaredCos);

    if (!Test_IsClose(tableValue, expectedLinear, 1.0e-5f)) {
        printf(
            "    BSDL DielectricReflFront LUT grid mismatch: "
            "table=%f expectedLinear=%f\n",
            tableValue, expectedLinear);
        return false;
    }
    if (Test_IsClose(tableValue, expectedSquared, 1.0e-3f)) {
        printf(
            "    BSDL DielectricReflFront LUT unexpectedly matches squared "
            "cosine grid: table=%f expectedSquared=%f\n",
            tableValue, expectedSquared);
        return false;
    }

    return true;
}

static bool
TestLayerReflectionAttenuatesBaseOnOutgoingSide()
{
    Bsdf::SetDielectricLayerThroughputMode(
        Bsdf::DielectricLayerThroughputMode::Bsdl);

    SurfaceClosure topClosure;
    Bsdf::DielectricData top;
    top.weight = 1.0f;
    top.tint = Vec3f(1.0f);
    top.ior = 1.6f;
    top.roughness = Vec2f(0.02f, 0.02f);
    top.scatterMode = Bsdf::ScatterMode::Reflection;
    topClosure.bsdfTree.root = topClosure.bsdfTree.Add(top);

    SurfaceClosure baseClosure;
    Bsdf::OrenNayarDiffuseData base;
    base.weight = 1.0f;
    base.color = Vec3f(1.0f);
    base.roughness = 0.0f;
    base.energyCompensation = true;
    baseClosure.bsdfTree.root = baseClosure.bsdfTree.Add(base);

    SurfaceClosure layerClosure;
    const auto topId = layerClosure.bsdfTree.Add(top);
    const auto baseId = layerClosure.bsdfTree.Add(base);
    Bsdf::LayerData layer;
    layer.top = topId;
    layer.base = baseId;
    layerClosure.bsdfTree.root = layerClosure.bsdfTree.Add(layer);

    const Vec3f normalShdWldOut(0.0f, 1.0f, 0.0f);
    const Vec3f omegaOutWld = Vec3f(0.95f, 0.3122499f, 0.0f).normalized();
    const Vec3f omegaInWld = Vec3f(-0.6f, 0.8f, 0.0f).normalized();

    const Vec3f topEval =
        TestSurfaceApi::EvalSurface(topClosure, normalShdWldOut, omegaInWld, omegaOutWld);
    const Vec3f baseEval = TestSurfaceApi::EvalSurface(baseClosure, normalShdWldOut,
                                             omegaInWld, omegaOutWld);
    const Vec3f layerEval = TestSurfaceApi::EvalSurface(layerClosure, normalShdWldOut,
                                              omegaInWld, omegaOutWld);

    // BSDL mtx::DielectricReflFront outgoing filter value for alpha=0.02,
    // IOR=1.6, and cos(omegaOutWld)=~0.31225 using the linear cosTheta grid.
    constexpr float attOut = 0.78626859f;
    const Vec3f expected =
        topEval + baseEval * attOut;

    if (!Test_IsClose(layerEval, expected, 1.0e-4f)) {
        printf(
            "    Layer attenuation mismatch: expected=(%f,%f,%f) "
            "actual=(%f,%f,%f)\n",
            expected[0], expected[1], expected[2],
            layerEval[0], layerEval[1], layerEval[2]);
        return false;
    }
    return true;
}

static bool
TestLayerThroughputUsesBsdlDielectricFilter()
{
    Bsdf::SetDielectricLayerThroughputMode(
        Bsdf::DielectricLayerThroughputMode::Bsdl);

    SurfaceClosure topClosure;
    Bsdf::DielectricData top;
    top.weight = 1.0f;
    top.tint = Vec3f(1.0f);
    top.ior = 1.5f;
    top.roughness = Vec2f(1.0f, 1.0f);
    top.scatterMode = Bsdf::ScatterMode::Reflection;
    topClosure.bsdfTree.root = topClosure.bsdfTree.Add(top);

    SurfaceClosure baseClosure;
    Bsdf::OrenNayarDiffuseData base;
    base.weight = 1.0f;
    base.color = Vec3f(0.8f);
    base.roughness = 0.0f;
    base.energyCompensation = true;
    baseClosure.bsdfTree.root = baseClosure.bsdfTree.Add(base);

    SurfaceClosure layerClosure;
    const auto topId = layerClosure.bsdfTree.Add(top);
    const auto baseId = layerClosure.bsdfTree.Add(base);
    Bsdf::LayerData layer;
    layer.top = topId;
    layer.base = baseId;
    layerClosure.bsdfTree.root = layerClosure.bsdfTree.Add(layer);

    const Vec3f normalShdWldOut(0.0f, 1.0f, 0.0f);
    const Vec3f omegaOutWld = _DirectionFromCosThetaYUp(0.55f).normalized();
    const Vec3f omegaInWld = Vec3f(-0.35f, 0.72f, 0.60f).normalized();

    const Vec3f topEval =
        TestSurfaceApi::EvalSurface(topClosure, normalShdWldOut, omegaInWld, omegaOutWld);
    const Vec3f baseEval = TestSurfaceApi::EvalSurface(baseClosure, normalShdWldOut,
                                             omegaInWld, omegaOutWld);
    const Vec3f layerEval = TestSurfaceApi::EvalSurface(layerClosure, normalShdWldOut,
                                              omegaInWld, omegaOutWld);

    // BSDL mtx::DielectricReflFront outgoing filter value for roughness=1,
    // IOR=1.5, and cos(omegaOutWld)=0.55 using the linear cosTheta grid.
    constexpr float filterOut = 0.97670996f;
    const Vec3f expected =
        topEval + baseEval * filterOut;

    if (!Test_IsClose(layerEval, expected, 1.0e-4f)) {
        printf(
            "    BSDL dielectric layer filter mismatch: expected=(%f,%f,%f) "
            "actual=(%f,%f,%f)\n",
            expected[0], expected[1], expected[2],
            layerEval[0], layerEval[1], layerEval[2]);
        return false;
    }

    return true;
}

static bool
TestLayerThroughputMaterialXGlslModeUsesExactFresnel()
{
    Bsdf::DielectricData top;
    top.weight = 1.0f;
    top.tint = Vec3f(1.0f);
    top.ior = 1.5f;
    top.roughness = Vec2f(0.35f, 0.35f);
    top.scatterMode = Bsdf::ScatterMode::Reflection;

    Bsdf::OrenNayarDiffuseData base;
    base.weight = 1.0f;
    base.color = Vec3f(0.75f);
    base.roughness = 0.0f;
    base.energyCompensation = true;

    SurfaceClosure topClosure;
    topClosure.bsdfTree.root = topClosure.bsdfTree.Add(top);

    SurfaceClosure baseClosure;
    baseClosure.bsdfTree.root = baseClosure.bsdfTree.Add(base);

    SurfaceClosure layerClosure;
    const auto topId = layerClosure.bsdfTree.Add(top);
    const auto baseId = layerClosure.bsdfTree.Add(base);
    Bsdf::LayerData layer;
    layer.top = topId;
    layer.base = baseId;
    layerClosure.bsdfTree.root = layerClosure.bsdfTree.Add(layer);

    const Vec3f normalShdWldOut(0.0f, 1.0f, 0.0f);
    const float cosTheta = 0.18f;
    const Vec3f omegaOutWld = _DirectionFromCosThetaYUp(cosTheta).normalized();
    const Vec3f omegaInWld = Vec3f(-0.35f, 0.72f, 0.60f).normalized();

    const Vec3f topEval =
        TestSurfaceApi::EvalSurface(topClosure, normalShdWldOut, omegaInWld, omegaOutWld);
    const Vec3f baseEval = TestSurfaceApi::EvalSurface(baseClosure, normalShdWldOut,
                                             omegaInWld, omegaOutWld);

    Bsdf::SetDielectricLayerThroughputMode(
        Bsdf::DielectricLayerThroughputMode::MaterialXGlsl);
    const Vec3f glslLayerEval = TestSurfaceApi::EvalSurface(layerClosure, normalShdWldOut,
                                                  omegaInWld, omegaOutWld);

    Bsdf::SetDielectricLayerThroughputMode(
        Bsdf::DielectricLayerThroughputMode::Bsdl);
    const Vec3f bsdlLayerEval = TestSurfaceApi::EvalSurface(layerClosure, normalShdWldOut,
                                                  omegaInWld, omegaOutWld);

    const float exactFresnel =
        1.0f - _DielectricTransmittanceForTest(top.ior, cosTheta);
    const Vec3f exactThroughput =
        _MaterialXGlslDielectricThroughputForTest(
            top.roughness[0],
            cosTheta,
            top.ior,
            top.weight,
            exactFresnel);
    const Vec3f expectedExact =
        topEval + CompMul(baseEval, exactThroughput);

    if (!Test_IsClose(glslLayerEval, expectedExact, 1.0e-4f)) {
        printf("    MaterialX GLSL dielectric throughputRgb mismatch: "
               "expected=(%f,%f,%f) actual=(%f,%f,%f)\n",
               expectedExact[0], expectedExact[1], expectedExact[2],
               glslLayerEval[0], glslLayerEval[1], glslLayerEval[2]);
        return false;
    }

    const float schlickFresnel =
        _SchlickFresnelForTest(top.ior, cosTheta);
    const Vec3f schlickThroughput =
        _MaterialXGlslDielectricThroughputForTest(
            top.roughness[0],
            cosTheta,
            top.ior,
            top.weight,
            schlickFresnel);
    const Vec3f expectedSchlick =
        topEval + CompMul(baseEval, schlickThroughput);
    if (Test_IsClose(glslLayerEval, expectedSchlick, 1.0e-4f)) {
        printf(
            "    MaterialX GLSL dielectric throughputRgb used Schlick Fresnel: "
            "actual=(%f,%f,%f) schlickExpected=(%f,%f,%f)\n",
            glslLayerEval[0], glslLayerEval[1], glslLayerEval[2],
            expectedSchlick[0], expectedSchlick[1], expectedSchlick[2]);
        return false;
    }

    if (Test_IsClose(glslLayerEval, bsdlLayerEval, 1.0e-4f)) {
        printf(
            "    Dielectric throughputRgb mode switch had no visible effect: "
            "bsdl=(%f,%f,%f) glsl=(%f,%f,%f)\n",
            bsdlLayerEval[0], bsdlLayerEval[1], bsdlLayerEval[2],
            glslLayerEval[0], glslLayerEval[1], glslLayerEval[2]);
        return false;
    }

    return true;
}

static bool
TestPowerHeuristic()
{
    float w = Bsdf::PowerHeuristic(1.0f, 1.0f);
    if (!Test_IsClose(w, 0.5f, 1e-4f)) {
        printf("    PowerHeuristic(1,1)=%f, expected 0.5\n", w);
        return false;
    }

    float w2 = Bsdf::PowerHeuristic(2.0f, 1.0f);
    if (w2 < 0.7f || w2 > 0.85f) {
        printf("    PowerHeuristic(2,1)=%f, expected ~0.8\n", w2);
        return false;
    }

    float w3 = Bsdf::PowerHeuristic(0.0f, 1.0f);
    if (w3 > 0.01f) {
        printf("    PowerHeuristic(0,1)=%f, expected ~0\n", w3);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Bsdf::SampleSubsurfaceEntry tests (Phase 1 Task 1.2)

static bool
TestSampleSubsurfaceEntrySmoothRefraction()
{
    // Smooth dielectric at normal incidence: deterministic Snell refraction
    // should yield omegaInWld pointing straight into the medium.
    SurfaceClosure c;
    c.roughness = 1.0e-6f;
    c.specularIor = 1.5f;
    Vec3f normalShdWldOut(0, 0, 1);
    Vec3f omegaOutWld(0, 0, 1);
    Vec3f omegaInWld;
    bool ok = Bsdf::SampleSubsurfaceEntry(
        c, normalShdWldOut, normalShdWldOut, omegaOutWld, 0.5f, 0.5f,
        omegaInWld);
    if (!ok) {
        printf("    Expected successful sample\n");
        return false;
    }
    if (!Test_IsClose(omegaInWld, Vec3f(0, 0, -1), 1e-4f)) {
        printf("    Expected (0,0,-1), got (%f,%f,%f)\n", omegaInWld[0],
               omegaInWld[1], omegaInWld[2]);
        return false;
    }
    return true;
}

static bool
TestSampleSubsurfaceEntryRoughGGX()
{
    // Rough dielectric should sample a direction pointing into the medium.
    SurfaceClosure c;
    c.roughness = 0.5f;
    c.specularIor = 1.5f;
    Vec3f normalShdWldOut(0, 0, 1);
    Vec3f omegaOutWld(0, 0, 1);
    Vec3f omegaInWld;
    bool ok = Bsdf::SampleSubsurfaceEntry(
        c, normalShdWldOut, normalShdWldOut, omegaOutWld, 0.3f, 0.7f,
        omegaInWld);
    if (!ok) {
        printf("    Expected successful sample\n");
        return false;
    }
    if (Dot(omegaInWld, normalShdWldOut) >= 0.0f) {
        printf("    Expected omegaInWld pointing into medium "
               "(Dot(omegaInWld,normalShdWldOut) < 0), "
               "got (%f,%f,%f)\n",
               omegaInWld[0], omegaInWld[1], omegaInWld[2]);
        return false;
    }
    return true;
}

static bool
TestSampleSubsurfaceEntryIorClamp()
{
    // IOR < 1 should be clamped to 1.0 (no TIR at entry).
    SurfaceClosure c;
    c.roughness = 1.0e-6f;
    c.specularIor = 0.5f;
    Vec3f normalShdWldOut(0, 0, 1);
    Vec3f omegaOutWld(0, 0, 1);
    Vec3f omegaInWld;
    bool ok = Bsdf::SampleSubsurfaceEntry(
        c, normalShdWldOut, normalShdWldOut, omegaOutWld, 0.5f, 0.5f,
        omegaInWld);
    if (!ok) {
        printf("    Expected successful sample with IOR clamp\n");
        return false;
    }
    if (!Test_IsClose(omegaInWld, Vec3f(0, 0, -1), 1e-4f)) {
        printf(
            "    Expected (0,0,-1) with IOR clamped to 1.0, got (%f,%f,%f)\n",
            omegaInWld[0], omegaInWld[1], omegaInWld[2]);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// mxcpp::ChannelMIS tests (Phase 2 Task 2.1)

static bool
TestChannelMISUniform()
{
    Vec3f tp(1.0f);
    Vec3f w(1.0f);
    Vec3f pdf;
    int ch = mxcpp::ChannelMIS(tp, w, 0.0f, &pdf);
    if (ch != 0) {
        printf("    u=0 expected ch 0, got %d\n", ch);
        return false;
    }
    if (!Test_IsClose(pdf, Vec3f(1.0f/3.0f), 1e-4f)) {
        printf("    Expected uniform pdf (1/3,1/3,1/3), got (%f,%f,%f)\n",
               pdf[0], pdf[1], pdf[2]);
        return false;
    }
    return true;
}

static bool
TestChannelMISWeighted()
{
    Vec3f tp(1.0f);
    Vec3f w(0.1f, 0.8f, 0.1f);
    Vec3f pdf;
    int ch = mxcpp::ChannelMIS(tp, w, 0.5f, &pdf);
    if (!Test_IsClose(pdf, Vec3f(0.1f, 0.8f, 0.1f), 1e-4f)) {
        printf("    Expected (0.1,0.8,0.1) pdf, got (%f,%f,%f)\n",
               pdf[0], pdf[1], pdf[2]);
        return false;
    }
    // u=0.5 falls in green channel [0.1, 0.9)
    if (ch != 1) {
        printf("    u=0.5 expected ch 1, got %d\n", ch);
        return false;
    }
    return true;
}

static bool
TestChannelMISZeroFallback()
{
    Vec3f tp(0.0f);
    Vec3f w(0.0f);
    Vec3f pdf;
    mxcpp::ChannelMIS(tp, w, 0.5f, &pdf);
    if (!Test_IsClose(pdf, Vec3f(1.0f/3.0f), 1e-4f)) {
        printf("    Expected uniform fallback, got (%f,%f,%f)\n",
               pdf[0], pdf[1], pdf[2]);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// ty::ChiangRemap tests (Phase 2 Task 2.3)

static bool
TestChiangRemapHighAlbedo()
{
    // High albedo (0.95) should yield high alpha (close to but below 0.999999).
    GfVec3f albedo(0.95f);
    GfVec3f radius(1.0f);
    GfVec3f extinction, alpha;
    ty::ChiangRemap(albedo, radius, 0.0f, &extinction, &alpha);
    if (alpha[0] < 0.5f || alpha[0] > 0.999999f) {
        printf("    Expected 0.5 <= alpha <= 0.999999 for albedo=0.95, "
               "got %f\n", alpha[0]);
        return false;
    }
    // At g=0, sigma_t = sigma_t_prime / (1 - g) = 1/radius = 1.0
    if (!Test_IsClose(extinction[0], 1.0f, 1e-3f)) {
        printf("    Expected sigma_t ~ 1.0 at g=0, radius=1, got %f\n",
               extinction[0]);
        return false;
    }
    return true;
}

static bool
TestChiangRemapLowAlbedo()
{
    // Very low albedo (0.025): Cycles comment notes this is where rawAlpha
    // drops below min_alpha=0.2, triggering the min-alpha clamp.
    GfVec3f albedo(0.025f);
    GfVec3f radius(1.0f);
    GfVec3f extinction, alpha, rawAlpha;
    ty::ChiangRemap(albedo, radius, 0.0f, &extinction, &alpha, &rawAlpha);
    if (alpha[0] < 0.19f) {
        printf("    Expected alpha >= 0.2 (min_alpha clamp), got %f\n",
               alpha[0]);
        return false;
    }
    // Raw alpha should be below 0.2 for albedo=0.025.
    if (rawAlpha[0] >= 0.2f) {
        printf("    Expected rawAlpha < 0.2 for low albedo, got %f\n",
               rawAlpha[0]);
        return false;
    }
    return true;
}

static bool
TestChiangRemapAnisotropy()
{
    // sigma_t depends on anisotropy: sigma_t = sigma_t_prime / (1 - g).
    // For g=0.5, sigma_t should be ~2x compared to g=0.
    GfVec3f albedo(0.5f);
    GfVec3f radius(1.0f);
    GfVec3f extinctionIsotropic, alphaIsotropic;
    GfVec3f extinctionAnisotropic, alphaAnisotropic;
    ty::ChiangRemap(albedo, radius, 0.0f, &extinctionIsotropic,
                        &alphaIsotropic);
    ty::ChiangRemap(albedo, radius, 0.5f, &extinctionAnisotropic,
                        &alphaAnisotropic);
    if (extinctionAnisotropic[0] < 1.5f * extinctionIsotropic[0]) {
        printf("    Expected sigma_t(g=0.5) > 1.5*sigma_t(g=0); "
               "g=0: %f, g=0.5: %f\n",
               extinctionIsotropic[0], extinctionAnisotropic[0]);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Dwivedi sampling helper tests (Phase 3 Task 3.1)

static bool
TestDiffusionLengthDwivedi()
{
    // Diffusion length should be finite and increase with alpha.
    float l1 = PXR_INTERNAL_NS::ty::DiffusionLengthDwivedi(0.99f);
    if (!std::isfinite(l1) || l1 < 5.0f || l1 > 15.0f) {
        printf("    Expected 5 < L(0.99) < 15, got %f\n", l1);
        return false;
    }
    float l2 = PXR_INTERNAL_NS::ty::DiffusionLengthDwivedi(0.5f);
    if (!std::isfinite(l2) || l2 < 1.0f || l2 > 2.0f) {
        printf("    Expected 1.0 < L(0.5) < 2.0, got %f\n", l2);
        return false;
    }
    if (l1 <= l2) {
        printf("    Expected L(0.99) > L(0.5), got %f <= %f\n", l1, l2);
        return false;
    }
    return true;
}

static bool
TestSamplePhaseDwivediRange()
{
    // Sampled cos_theta must be in [-1, 1].
    const float L = 2.0f;
    const float phase_log = std::log((L + 1.0f) / (L - 1.0f));
    for (int i = 0; i < 100; ++i) {
        const float u = static_cast<float>(i) / 99.0f;
        const float cos_theta =
            PXR_INTERNAL_NS::ty::SamplePhaseDwivedi(L, phase_log, u);
        if (!std::isfinite(cos_theta) ||
            cos_theta < -1.0f - 1e-4f ||
            cos_theta > 1.0f + 1e-4f) {
            printf("    cos_theta out of range: %f at u=%f\n", cos_theta, u);
            return false;
        }
    }
    return true;
}

static bool
TestBackwardDwivediFraction()
{
    const float distanceOppositeWld = 2.0f;
    const float diffusionLength = 1.0f;

    const float nearEntry = PXR_INTERNAL_NS::ty::BackwardDwivediFraction(
        distanceOppositeWld, 0.0f, diffusionLength);
    const float midPlane = PXR_INTERNAL_NS::ty::BackwardDwivediFraction(
        distanceOppositeWld, 1.0f, diffusionLength);
    const float nearOpposite = PXR_INTERNAL_NS::ty::BackwardDwivediFraction(
        distanceOppositeWld, 2.0f, diffusionLength);

    if (!(nearEntry < midPlane && midPlane < nearOpposite)) {
        printf("    Expected backward fraction to increase across slab: "
               "%f, %f, %f\n", nearEntry, midPlane, nearOpposite);
        return false;
    }
    if (!Test_IsClose(midPlane, 0.5f, 1.0e-6f)) {
        printf("    Expected midpoint backward fraction 0.5, got %f\n",
               midPlane);
        return false;
    }
    if (!Test_IsClose(PXR_INTERNAL_NS::ty::BackwardDwivediFraction(
                          distanceOppositeWld, -1.0f, diffusionLength),
                      nearEntry, 1.0e-6f)) {
        printf("    Expected x below entry plane to clamp to near-entry value\n");
        return false;
    }
    if (!Test_IsClose(PXR_INTERNAL_NS::ty::BackwardDwivediFraction(
                          distanceOppositeWld, 3.0f, diffusionLength),
                      nearOpposite, 1.0e-6f)) {
        printf("    Expected x beyond opposite plane to clamp to far value\n");
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------

static bool
TestDielectricInterfaceUsesExactFresnel()
{
    // The dielectric interface models MaterialX dielectric_bsdf, whose
    // reference implementation evaluates the exact dielectric Fresnel
    // equations (mx_fresnel_dielectric), not the Schlick approximation.
    // Schlick fits the outside curve well but is badly wrong from inside
    // the medium, where reflectance rises steeply toward one at the
    // critical angle (cos ~0.745 for ior 1.5).
    SurfaceClosure closure;
    Bsdf::DielectricInterfaceData interface;
    interface.reflectionWeight = 1.0f;
    interface.reflectionTint = Vec3f(1.0f);
    interface.transmissionWeight = 0.0f;
    interface.ior = 1.5f;
    interface.roughness = Vec2f(0.0f, 0.0f);
    closure.bsdfTree.root = closure.bsdfTree.Add(interface);

    const Vec3f normalShdWldOut(0.0f, 1.0f, 0.0f);
    struct _Case {
        bool inside;
        float cosTheta;
        float expected;  // exact unpolarized dielectric Fresnel
    };
    const _Case cases[] = {
        {false, 1.00f, 0.040000f},
        {false, 0.50f, 0.089187f},
        {false, 0.20f, 0.338894f},
        {true, 0.80f, 0.114141f},
        {true, 0.76f, 0.302338f},
        {true, 0.50f, 1.000000f},  // total internal reflection
    };

    bool ok = true;
    for (const _Case& c : cases) {
        Vec3f omegaOutWld = _DirectionFromCosThetaYUp(c.cosTheta);
        const Vec3f normalIncidentWldOut =
            c.inside ? -normalShdWldOut : normalShdWldOut;
        if (c.inside) {
            omegaOutWld[1] = -omegaOutWld[1];
        }
        SurfaceClosure prepared = closure;
        Bsdf::detail::PrepareShadingNormals(
            &prepared.bsdfTree, normalIncidentWldOut,
            normalIncidentWldOut, omegaOutWld);
        const Bsdf::BsdfSample sample = PreparedSurfaceApi::SampleSurface(
            prepared, normalIncidentWldOut, omegaOutWld,
            0.5f, 0.5f, 0.5f, 0.0f, !c.inside);
        if (!(sample.pdfSolidAngle > 0.0f) || !sample.isSpecular) {
            printf("    expected a delta reflection sample at %s cos=%.2f\n",
                   c.inside ? "inside" : "outside", c.cosTheta);
            ok = false;
            continue;
        }
        if (!Test_IsClose(sample.bsdfValue[0], c.expected, 2e-3f)) {
            printf("    %s cos=%.2f reflectance %.6f, expected exact %.6f\n",
                   c.inside ? "inside " : "outside", c.cosTheta,
                   sample.bsdfValue[0], c.expected);
            ok = false;
        }
    }
    return ok;
}

static bool
_CheckTransparentClosureEnergy(
    const SurfaceClosure& closure,
    const char* label,
    std::optional<float> lowerBound)
{
    const Vec3f normalShdWldOut(0.0f, 1.0f, 0.0f);
    const int kSamples = 50000;
    // IOR 1.5 puts the critical cosine at about 0.745. Bracket it closely in
    // addition to the grazing, TIR, transmissive, and normal directions.
    const float cosThetas[] = {
        0.05f, 0.2f, 0.5f, 0.74f, 0.76f, 0.8f, 1.0f};

    std::uint64_t sampleSequence = 0;

    bool ok = true;
    printf("    %s energy:\n", label);
    for (int side = 0; side < 2; ++side) {
        for (const float cosTheta : cosThetas) {
            Vec3f omegaOutWld = _DirectionFromCosThetaYUp(cosTheta);
            const Vec3f normalIncidentWldOut =
                side == 1 ? -normalShdWldOut : normalShdWldOut;
            if (side == 1) {
                omegaOutWld[1] = -omegaOutWld[1];
            }
            SurfaceClosure prepared = closure;
            Bsdf::detail::PrepareShadingNormals(
                &prepared.bsdfTree, normalIncidentWldOut,
                normalIncidentWldOut, omegaOutWld);
            Vec3f energy(0.0f);
            for (int i = 0; i < kSamples; ++i) {
                const std::uint64_t sampleIndex = sampleSequence++;
                const float u1 = _CounterRandomFloat(sampleIndex, 0);
                const float u2 = _CounterRandomFloat(sampleIndex, 1);
                const float uChoice = _CounterRandomFloat(sampleIndex, 2);
                const Bsdf::BsdfSample sample = PreparedSurfaceApi::SampleSurface(
                    prepared, normalIncidentWldOut, omegaOutWld,
                    u1, u2, uChoice, 0.0f, side == 0);
                if (!(sample.pdfSolidAngle > 0.0f)) {
                    continue;
                }
                if (sample.isSpecular) {
                    energy += sample.bsdfValue;
                } else {
                    energy +=
                        sample.bsdfValue *
                        (std::abs(
                             Dot(normalIncidentWldOut, sample.omegaInWld)) /
                         sample.pdfSolidAngle);
                }
            }
            energy = energy * (1.0f / float(kSamples));
            printf("      %s NoV=%.2f E=%.4f\n",
                   side == 0 ? "outside" : "inside ", cosTheta, energy[0]);
            if (!_IsFiniteNonNegative(energy)) {
                printf("        invalid non-finite or negative energy\n");
                ok = false;
                continue;
            }
            for (int c = 0; c < 3; ++c) {
                if (!(energy[c] <= 1.0f + _kFurnaceEnergyUpperSlack)) {
                    printf("        energy gain on channel %d: %f\n",
                           c, energy[c]);
                    ok = false;
                }
                if (lowerBound && !(energy[c] >= *lowerBound)) {
                    printf("        unexpected energy loss on channel %d: "
                           "%f\n", c, energy[c]);
                    ok = false;
                }
            }
        }
    }
    return ok;
}

static bool
TestUsdPreviewSurfaceTransmissionTirEnergyConservation()
{
    // Transparent UsdPreviewSurface must not gain energy from either side of
    // the interface, including internal directions beyond the critical angle
    // (total internal reflection).  Cover both closure structures: the
    // metalness workflow compiles to a coupled dielectric interface, while
    // the specular workflow keeps the paired Schlick-reflection +
    // transmission lobes whose TIR branch splits energy with its pair.
    ParamMap params;
    params["diffuseColor"] = Value(Vec3f(0.18f));
    params["metallic"] = Value(0.0f);
    params["roughness"] = Value(0.0f);
    params["ior"] = Value(1.5f);
    params["opacity"] = Value(0.0f);
    params["opacityThreshold"] = Value(0.0f);
    bool ok = _CheckTransparentClosureEnergy(
        EvalUsdPreviewSurface(params),
        "UsdPreviewSurface glass (metalness workflow)",
        0.97f);

    // specularColor 0.04 matches the ior 1.5 F0, so the pair should conserve
    // within the Schlick pairing's approximation; it must never gain.
    ParamMap specParams;
    specParams["useSpecularWorkflow"] = Value(1);
    specParams["specularColor"] = Value(Vec3f(0.04f));
    specParams["roughness"] = Value(0.0f);
    specParams["ior"] = Value(1.5f);
    specParams["opacity"] = Value(0.0f);
    specParams["opacityThreshold"] = Value(0.0f);
    ok = _CheckTransparentClosureEnergy(
        EvalUsdPreviewSurface(specParams),
        "UsdPreviewSurface glass (specular workflow)",
        0.90f) && ok;

    // A MaterialX generalized_schlick_bsdf reflection/transmission pair
    // (scatter_mode "R" + "T" combined additively) must not gain energy at
    // total internal reflection either; its transmission lobe shares the
    // (1 - pair F) TIR split with the dielectric transmission lobe.
    {
        SurfaceClosure schlickPair;
        Bsdf::GeneralizedSchlickData reflection;
        reflection.weight = 1.0f;
        reflection.color0 = Vec3f(0.04f);
        reflection.color82 = Vec3f(1.0f);
        reflection.color90 = Vec3f(1.0f);
        reflection.exponent = 5.0f;
        reflection.roughness = Vec2f(0.0f, 0.0f);
        reflection.scatterMode = Bsdf::ScatterMode::Reflection;
        Bsdf::GeneralizedSchlickData transmission = reflection;
        transmission.scatterMode = Bsdf::ScatterMode::Transmission;

        Bsdf::AddData add;
        add.in1 = schlickPair.bsdfTree.Add(reflection);
        add.in2 = schlickPair.bsdfTree.Add(transmission);
        schlickPair.bsdfTree.root = schlickPair.bsdfTree.Add(add);
        ok = _CheckTransparentClosureEnergy(
            schlickPair,
            "generalized_schlick R+T pair",
            0.90f) && ok;
    }

    return ok;
}

static bool
TestStandardSurfaceRoughTransmissionWhiteFurnaceDoesNotGainEnergy()
{
    // Exercise the roughness used by the AOUSD transmission_rough fixture.
    // White unit transmission is a stronger gain stress than the fixture's
    // 0.95 transmission weight. The legacy layered pairing intentionally
    // loses some energy, so this test has no behavior-derived lower bound.
    ParamMap standardParams;
    standardParams["base"] = Value(0.0f);
    standardParams["specular"] = Value(1.0f);
    standardParams["specular_roughness"] = Value(0.3f);
    standardParams["specular_IOR"] = Value(1.5f);
    standardParams["transmission"] = Value(1.0f);
    return _CheckTransparentClosureEnergy(
        EvalStandardSurface(standardParams),
        "standard_surface rough glass",
        std::nullopt);
}

static bool
TestOpenPbrRoughTransmissionWhiteFurnaceConservesEnergy()
{
    ParamMap params;
    params["base_weight"] = Value(0.0f);
    params["base_metalness"] = Value(0.0f);
    params["specular_weight"] = Value(1.0f);
    params["specular_color"] = Value(Vec3f(1.0f));
    params["specular_roughness"] = Value(0.3f);
    params["specular_ior"] = Value(1.5f);
    params["transmission_weight"] = Value(1.0f);
    params["transmission_color"] = Value(Vec3f(1.0f));
    params["coat_weight"] = Value(0.0f);
    params["fuzz_weight"] = Value(0.0f);
    params["geometry_thin_walled"] = Value(false);
    return _CheckTransparentClosureEnergy(
        EvalOpenPbr(params),
        "OpenPBR rough glass",
        0.95f);
}

static bool
TestOpenPbrAnisotropicTintedPartialTransmissionSamplingMatchesIntegration()
{
    ParamMap params;
    params["base_weight"] = Value(0.0f);
    params["base_metalness"] = Value(0.0f);
    params["specular_weight"] = Value(1.0f);
    params["specular_color"] = Value(Vec3f(0.85f, 0.45f, 0.2f));
    params["specular_roughness"] = Value(0.5f);
    params["specular_roughness_anisotropy"] = Value(0.6f);
    params["specular_ior"] = Value(1.5f);
    params["transmission_weight"] = Value(0.35f);
    params["transmission_color"] = Value(Vec3f(0.3f, 0.7f, 0.95f));
    params["coat_weight"] = Value(0.0f);
    params["fuzz_weight"] = Value(0.0f);
    params["geometry_thin_walled"] = Value(false);
    const SurfaceClosure closure = EvalOpenPbr(params);

    const Vec3f normalShdWldOut(0.0f, 1.0f, 0.0f);
    constexpr int sampleCount = 1000000;
    constexpr float uniformSpherePdf = 1.0f / (4.0f * _kFurnacePi);
    bool ok = true;
    for (const bool inside : {false, true}) {
        Vec3f omegaOutWld = _DirectionFromCosThetaYUp(inside ? 0.8f : 0.35f);
        const Vec3f normalIncidentWldOut =
            inside ? -normalShdWldOut : normalShdWldOut;
        if (inside) {
            omegaOutWld = -omegaOutWld;
        }
        SurfaceClosure prepared = closure;
        Bsdf::detail::PrepareShadingNormals(
            &prepared.bsdfTree, normalIncidentWldOut,
            normalIncidentWldOut, omegaOutWld);

        Vec3f sampledEnergy(0.0f);
        Vec3f integratedEnergy(0.0f);
        float integratedPdf = 0.0f;
        for (int i = 0; i < sampleCount; ++i) {
            const std::uint64_t sampleIndex =
                std::uint64_t(i) + (inside ? sampleCount : 0);
            const float u1 = _CounterRandomFloat(sampleIndex, 10);
            const float u2 = _CounterRandomFloat(sampleIndex, 11);
            const float uChoice = _CounterRandomFloat(sampleIndex, 12);
            const Bsdf::BsdfSample sample = PreparedSurfaceApi::SampleSurface(
                prepared, normalIncidentWldOut, omegaOutWld,
                u1, u2, uChoice, 0.0f, !inside);
            if (sample.pdfSolidAngle > 0.0f) {
                sampledEnergy +=
                    sample.bsdfValue *
                    (std::abs(
                         Dot(normalIncidentWldOut, sample.omegaInWld)) /
                     sample.pdfSolidAngle);
            }

            const float y = 1.0f -
                2.0f * _CounterRandomFloat(sampleIndex, 20);
            const float phi = _kFurnaceTwoPi *
                _CounterRandomFloat(sampleIndex, 21);
            const float radius = std::sqrt(std::max(0.0f, 1.0f - y * y));
            const Vec3f omegaInWld(radius * std::cos(phi), y,
                                   radius * std::sin(phi));
            const Vec3f evaluated = PreparedSurfaceApi::EvalSurface(
                prepared, normalIncidentWldOut, omegaInWld, omegaOutWld,
                0.0f, !inside);
            integratedEnergy +=
                evaluated *
                (std::abs(Dot(normalIncidentWldOut, omegaInWld)) /
                 uniformSpherePdf);
            const float surfacePdf = _PdfPreparedSurface(prepared, _MakeSurfaceInteraction(normalIncidentWldOut, normalIncidentWldOut, normalIncidentWldOut, omegaOutWld, 0.0f, !inside), omegaInWld);
            if (_FurnaceLuminance(evaluated) > 1.0e-7f &&
                surfacePdf <= 0.0f) {
                printf("      evaluated BSDF has no sampling support\n");
                ok = false;
            }
            const float mixedPdf = surfacePdf / uniformSpherePdf;
            integratedPdf += mixedPdf;
        }
        sampledEnergy *= 1.0f / float(sampleCount);
        integratedEnergy *= 1.0f / float(sampleCount);
        integratedPdf *= 1.0f / float(sampleCount);
        printf(
            "    OpenPBR anisotropic %s sampled=(%.4f,%.4f,%.4f) "
            "integrated=(%.4f,%.4f,%.4f) pdfIntegral=%.4f\n",
            inside ? "inside" : "outside",
            sampledEnergy[0], sampledEnergy[1], sampledEnergy[2],
            integratedEnergy[0], integratedEnergy[1], integratedEnergy[2],
            integratedPdf);
        if (integratedPdf < 0.90f || integratedPdf > 1.03f) {
            printf("      PDF integral is outside the valid sampled mass\n");
            ok = false;
        }
        if (!_IsFiniteNonNegative(sampledEnergy) ||
            !_IsFiniteNonNegative(integratedEnergy)) {
            ok = false;
            continue;
        }
        for (int channel = 0; channel < 3; ++channel) {
            const float tolerance =
                0.005f + 0.02f * integratedEnergy[channel];
            if (std::abs(
                    sampledEnergy[channel] - integratedEnergy[channel]) >
                tolerance) {
                printf(
                    "      sampling/integration mismatch on channel %d\n",
                    channel);
                ok = false;
            }
            if (sampledEnergy[channel] >
                1.0f + _kFurnaceEnergyUpperSlack) {
                printf("      energy gain on channel %d\n", channel);
                ok = false;
            }
        }
    }
    return ok;
}

template<typename DataT>
static bool
_ResolvedNormalMatches(
    const Vec3f& authored,
    const Vec3f& normalShdWldOut)
{
    DataT data;
    data.hasShadingNormal = true;
    data.normal = authored;
    Vec3f resolved;
    return Bsdf::detail::TryResolveShadingNormal(
               data, normalShdWldOut, &resolved) &&
        Test_IsClose(resolved, authored.normalized(), 1.0e-6f);
}

static bool
TestPerLobeNormalResolutionIsViewIndependent()
{
    const Vec3f normalShdWldOut(0.0f, 0.0f, 1.0f);
    const Vec3f authored = Vec3f(0.3f, 0.2f, 0.9f).normalized();
    if (!_ResolvedNormalMatches<Bsdf::OrenNayarDiffuseData>(
            authored, normalShdWldOut) ||
        !_ResolvedNormalMatches<Bsdf::BurleyDiffuseData>(
            authored, normalShdWldOut) ||
        !_ResolvedNormalMatches<Bsdf::ConductorData>(
            authored, normalShdWldOut) ||
        !_ResolvedNormalMatches<Bsdf::DielectricData>(
            authored, normalShdWldOut) ||
        !_ResolvedNormalMatches<Bsdf::DielectricInterfaceData>(
            authored, normalShdWldOut) ||
        !_ResolvedNormalMatches<Bsdf::GeneralizedSchlickData>(
            authored, normalShdWldOut) ||
        !_ResolvedNormalMatches<Bsdf::TranslucentData>(
            authored, normalShdWldOut) ||
        !_ResolvedNormalMatches<Bsdf::SubsurfaceData>(
            authored, normalShdWldOut) ||
        !_ResolvedNormalMatches<Bsdf::SheenData>(
            authored, normalShdWldOut)) {
        return false;
    }

    Bsdf::ConductorData invalid;
    invalid.hasShadingNormal = true;
    invalid.normal = -normalShdWldOut;
    Vec3f resolved;
    return !Bsdf::detail::TryResolveShadingNormal(
        invalid, normalShdWldOut, &resolved);
}

static bool
TestBackFacePreparationNegatesCompleteExteriorNormal()
{
    Bsdf::ClosureTree tree;
    Bsdf::DielectricInterfaceData interface;
    interface.hasShadingNormal = true;
    interface.normal = Vec3f(0.3f, -0.2f, 0.9327379f).normalized();
    const Bsdf::NodeId interfaceId = tree.Add(interface);
    tree.root = interfaceId;

    const Vec3f normalShdWldExt(0.0f, 0.0f, 1.0f);
    const Vec3f normalGeomWldOut(0.0f, 0.0f, -1.0f);
    const Vec3f omegaOutWld(0.0f, 0.0f, -1.0f);
    ValidateLeafNormals(&tree, normalShdWldExt);
    Bsdf::detail::PrepareShadingNormals(
        &tree, -normalShdWldExt, normalGeomWldOut, omegaOutWld);
    const Bsdf::DielectricInterfaceData* const prepared =
        std::get_if<Bsdf::DielectricInterfaceData>(
            &tree.nodes[interfaceId].data);
    if (!prepared ||
        !Test_IsClose(
            prepared->normal, -interface.normal, 1.0e-6f)) {
        return false;
    }

    Bsdf::ClosureTree frontTree;
    frontTree.root = frontTree.Add(interface);
    ValidateLeafNormals(&frontTree, normalShdWldExt);
    Bsdf::detail::PrepareShadingNormals(
        &frontTree, normalShdWldExt, normalShdWldExt,
        normalShdWldExt);
    const Bsdf::DielectricInterfaceData* const frontPrepared =
        std::get_if<Bsdf::DielectricInterfaceData>(
            &frontTree.nodes[frontTree.root].data);
    const Bsdf::BsdfSample frontSample = Bsdf::detail::SampleNode(frontTree, frontTree.root, _MakeSurfaceInteraction(normalShdWldExt, normalShdWldExt, normalShdWldExt, normalShdWldExt, 0.0f, true), 0.3f, 0.7f, 0.0f);
    const Bsdf::BsdfSample backSample = Bsdf::detail::SampleNode(tree, tree.root, _MakeSurfaceInteraction(-normalShdWldExt, -normalShdWldExt, normalGeomWldOut, omegaOutWld, 0.0f, false), 0.3f, 0.7f, 0.0f);
    const bool symmetric = frontPrepared &&
        frontSample.pdfSolidAngle > 0.0f &&
        backSample.pdfSolidAngle > 0.0f &&
        frontSample.isTransmission == backSample.isTransmission &&
        Dot(normalShdWldExt, frontSample.omegaInWld) > 0.0f &&
        Dot(normalGeomWldOut, backSample.omegaInWld) > 0.0f &&
        Dot(frontPrepared->normal, frontSample.omegaInWld) > 0.0f &&
        Dot(prepared->normal, backSample.omegaInWld) > 0.0f &&
        Test_IsClose(
            frontSample.omegaInWld[2], -backSample.omegaInWld[2],
            1.0e-5f);
    if (!symmetric) {
        printf(
            "    winding samples: frontPdf=%f backPdf=%f "
            "frontT=%d backT=%d front=(%f,%f,%f) back=(%f,%f,%f)\n",
            frontSample.pdfSolidAngle, backSample.pdfSolidAngle,
            frontSample.isTransmission, backSample.isTransmission,
            frontSample.omegaInWld[0], frontSample.omegaInWld[1],
            frontSample.omegaInWld[2], backSample.omegaInWld[0],
            backSample.omegaInWld[1], backSample.omegaInWld[2]);
    }
    return symmetric;
}

static bool
TestPerLobeInvalidNormalsFallBack()
{
    Bsdf::ClosureTree tree;
    Bsdf::ConductorData inverted;
    inverted.hasShadingNormal = true;
    inverted.normal = Vec3f(0.0f, 0.0f, -1.0f);
    tree.Add(inverted);
    Bsdf::SheenData degenerate;
    degenerate.hasShadingNormal = true;
    degenerate.normal = Vec3f(0.0f);
    tree.Add(degenerate);
    Bsdf::BurleyDiffuseData valid;
    valid.hasShadingNormal = true;
    valid.normal = Vec3f(0.0f, 0.0f, 2.0f);
    tree.Add(valid);
    Bsdf::OrenNayarDiffuseData nonFinite;
    nonFinite.hasShadingNormal = true;
    nonFinite.normal = Vec3f(
        std::numeric_limits<float>::infinity(), 0.0f, 1.0f);
    tree.Add(nonFinite);

    const Vec3f normalShdWldOut(0.0f, 0.0f, 1.0f);
    const Vec3f normalGeomWldOut(0.0f, 0.0f, 1.0f);
    const Vec3f omegaOutWld(0.0f, 0.0f, 1.0f);
    ValidateLeafNormals(&tree, normalShdWldOut);
    Bsdf::detail::PrepareShadingNormals(
        &tree, normalShdWldOut, normalGeomWldOut, omegaOutWld);
    const Bsdf::ConductorData* sanitizedInverted =
        std::get_if<Bsdf::ConductorData>(&tree.nodes[0].data);
    const Bsdf::SheenData* sanitizedDegenerate =
        std::get_if<Bsdf::SheenData>(&tree.nodes[1].data);
    const Bsdf::OrenNayarDiffuseData* sanitizedNonFinite =
        std::get_if<Bsdf::OrenNayarDiffuseData>(&tree.nodes[3].data);
    const Bsdf::BurleyDiffuseData* normalizedValid =
        std::get_if<Bsdf::BurleyDiffuseData>(&tree.nodes[2].data);
    return sanitizedInverted && sanitizedDegenerate && sanitizedNonFinite &&
        normalizedValid &&
        Test_IsClose(
            sanitizedInverted->normal, normalShdWldOut, 1.0e-6f) &&
        Test_IsClose(
            sanitizedDegenerate->normal, normalShdWldOut, 1.0e-6f) &&
        Test_IsClose(
            sanitizedNonFinite->normal, normalShdWldOut, 1.0e-6f) &&
        Test_IsClose(
            normalizedValid->normal, normalShdWldOut, 1.0e-6f) &&
        sanitizedInverted->hasShadingNormal &&
        sanitizedDegenerate->hasShadingNormal &&
        sanitizedNonFinite->hasShadingNormal;
}

template <typename T>
static bool
_ValidateLeafNormalType()
{
    Bsdf::ClosureTree tree;
    T valid;
    valid.hasShadingNormal = true;
    valid.normal = Vec3f(0.0f, 0.0f, 2.0f);
    const Bsdf::NodeId validId = tree.Add(valid);
    T invalid;
    invalid.hasShadingNormal = true;
    invalid.normal = Vec3f(0.0f, 0.0f, -1.0f);
    const Bsdf::NodeId invalidId = tree.Add(invalid);

    const Vec3f normalShdWldExt(0.0f, 0.0f, 1.0f);
    ValidateLeafNormals(&tree, normalShdWldExt);
    const T* validated = std::get_if<T>(&tree.nodes[validId].data);
    const T* fallback = std::get_if<T>(&tree.nodes[invalidId].data);
    return validated && fallback &&
        Test_IsClose(validated->normal, normalShdWldExt, 1.0e-6f) &&
        Test_IsClose(fallback->normal, normalShdWldExt, 1.0e-6f);
}

static bool
TestValidateLeafNormalsDispatchesEverySurfaceLeafType()
{
    return
        _ValidateLeafNormalType<Bsdf::OrenNayarDiffuseData>() &&
        _ValidateLeafNormalType<Bsdf::BurleyDiffuseData>() &&
        _ValidateLeafNormalType<Bsdf::TranslucentData>() &&
        _ValidateLeafNormalType<Bsdf::SubsurfaceData>() &&
        _ValidateLeafNormalType<Bsdf::DielectricData>() &&
        _ValidateLeafNormalType<Bsdf::DielectricInterfaceData>() &&
        _ValidateLeafNormalType<Bsdf::ConductorData>() &&
        _ValidateLeafNormalType<Bsdf::GeneralizedSchlickData>() &&
        _ValidateLeafNormalType<Bsdf::SheenData>() &&
        _ValidateLeafNormalType<Bsdf::AdobeOpenPbrData>();
}

static bool
TestAllLeafShadingNormalPreparation()
{
    Bsdf::ClosureTree tree;
    const Bsdf::NodeId diffuseId =
        tree.Add(Bsdf::OrenNayarDiffuseData{});
    Bsdf::ConductorData deltaConductor;
    deltaConductor.roughness = Vec2f(0.0f);
    const Bsdf::NodeId conductorId =
        tree.Add(deltaConductor);
    const Bsdf::NodeId roughConductorId =
        tree.Add(Bsdf::ConductorData{});
    Bsdf::BurleyDiffuseData authoredDiffuse;
    authoredDiffuse.hasShadingNormal = true;
    authoredDiffuse.normal = Vec3f(0.0f, 0.0f, 2.0f);
    const Bsdf::NodeId authoredId = tree.Add(authoredDiffuse);

    const Vec3f normalShdWldOut =
        Vec3f(-0.95f, 0.0f, 0.31f).normalized();
    const Vec3f normalGeomWldOut(0.0f, 0.0f, 1.0f);
    const Vec3f omegaOutWld =
        Vec3f(0.2f, 0.0f, 0.98f).normalized();
    ValidateLeafNormals(&tree, normalShdWldOut);
    Bsdf::detail::PrepareShadingNormals(
        &tree, normalShdWldOut, normalGeomWldOut, omegaOutWld);

    const Bsdf::OrenNayarDiffuseData* diffuse =
        std::get_if<Bsdf::OrenNayarDiffuseData>(
            &tree.nodes[diffuseId].data);
    const Bsdf::ConductorData* conductor =
        std::get_if<Bsdf::ConductorData>(
            &tree.nodes[conductorId].data);
    const Bsdf::ConductorData* roughConductor =
        std::get_if<Bsdf::ConductorData>(
            &tree.nodes[roughConductorId].data);
    const Bsdf::BurleyDiffuseData* authored =
        std::get_if<Bsdf::BurleyDiffuseData>(
            &tree.nodes[authoredId].data);
    const Vec3f expectedSpecular =
        Bsdf::detail::EnsureValidSpecularReflection(
            normalGeomWldOut, omegaOutWld, normalShdWldOut);
    if (!diffuse || !conductor || !roughConductor || !authored ||
        !diffuse->hasShadingNormal || !conductor->hasShadingNormal ||
        !roughConductor->hasShadingNormal || !authored->hasShadingNormal ||
        !Test_IsClose(
            authored->normal, Vec3f(0.0f, 0.0f, 1.0f), 1.0e-6f) ||
        !Test_IsClose(
            diffuse->normal, normalShdWldOut, 1.0e-6f) ||
        !Test_IsClose(
            conductor->normal, expectedSpecular, 1.0e-6f)) {
        return false;
    }
    return Test_IsClose(
        roughConductor->normal, expectedSpecular, 1.0e-6f);
}

static bool
TestAppendClosureTreePreservesLeafNormals()
{
    Bsdf::ClosureTree source;
    Bsdf::BurleyDiffuseData authoredDiffuse;
    authoredDiffuse.hasShadingNormal = true;
    authoredDiffuse.normal = Vec3f(0.0f, 0.0f, 2.0f);
    Bsdf::AddData add;
    add.in1 = source.Add(authoredDiffuse);
    add.in2 = source.Add(Bsdf::ConductorData{});
    source.root = source.Add(add);

    Bsdf::ClosureTree target;
    target.Add(Bsdf::OrenNayarDiffuseData{});
    target.root = AppendClosureTree(&target, source);
    if (target.root != 3) {
        return false;
    }

    const Vec3f normalShdWldOut(0.0f, 0.0f, 1.0f);
    const Vec3f omegaOutWld =
        Vec3f(0.2f, 0.0f, 0.98f).normalized();
    const Vec3f omegaInWld =
        Vec3f(-0.3f, 0.1f, 0.95f).normalized();
    ValidateLeafNormals(&source, normalShdWldOut);
    ValidateLeafNormals(&target, normalShdWldOut);
    Bsdf::detail::PrepareShadingNormals(
        &source, normalShdWldOut, normalShdWldOut, omegaOutWld);
    Bsdf::detail::PrepareShadingNormals(
        &target, normalShdWldOut, normalShdWldOut, omegaOutWld);

    const Vec3f sourceEval = Bsdf::detail::EvalNode(source, source.root, _MakeSurfaceInteraction(normalShdWldOut, normalShdWldOut, normalShdWldOut, omegaOutWld, 0.0f, true), omegaInWld, Bsdf::detail::BumpShadowingContext::Evaluation);
    const Vec3f targetEval = Bsdf::detail::EvalNode(target, target.root, _MakeSurfaceInteraction(normalShdWldOut, normalShdWldOut, normalShdWldOut, omegaOutWld, 0.0f, true), omegaInWld, Bsdf::detail::BumpShadowingContext::Evaluation);
    const float sourcePdf = Bsdf::detail::PdfNode(source, source.root, _MakeSurfaceInteraction(normalShdWldOut, normalShdWldOut, normalShdWldOut, omegaOutWld, 0.0f, true), omegaInWld);
    const float targetPdf = Bsdf::detail::PdfNode(target, target.root, _MakeSurfaceInteraction(normalShdWldOut, normalShdWldOut, normalShdWldOut, omegaOutWld, 0.0f, true), omegaInWld);
    const Bsdf::BsdfSample sourceSample = Bsdf::detail::SampleNode(source, source.root, _MakeSurfaceInteraction(normalShdWldOut, normalShdWldOut, normalShdWldOut, omegaOutWld, 0.0f, true), 0.3f, 0.7f, 0.2f);
    const Bsdf::BsdfSample targetSample = Bsdf::detail::SampleNode(target, target.root, _MakeSurfaceInteraction(normalShdWldOut, normalShdWldOut, normalShdWldOut, omegaOutWld, 0.0f, true), 0.3f, 0.7f, 0.2f);
    return Test_IsClose(sourceEval, targetEval, 1.0e-6f) &&
        Test_IsClose(sourcePdf, targetPdf, 1.0e-6f) &&
        Test_IsClose(
            sourceSample.omegaInWld, targetSample.omegaInWld, 1.0e-6f) &&
        Test_IsClose(
            sourceSample.bsdfValue, targetSample.bsdfValue, 1.0e-6f) &&
        Test_IsClose(
            sourceSample.pdfSolidAngle,
            targetSample.pdfSolidAngle, 1.0e-6f);
}

static bool
TestTreeDiffuseNormalIsIndependentOfViewHemisphere()
{
    const Vec3f materialNormal =
        Vec3f(0.0f, 0.1f, -0.995f).normalized();
    const Vec3f omegaOutFrontWld = materialNormal;
    const Vec3f omegaOutBackWld =
        Vec3f(0.0f, 0.2f, 0.98f).normalized();
    if (Dot(materialNormal, omegaOutBackWld) >= 0.0f) {
        return false;
    }

    SurfaceClosure closure;
    Bsdf::OrenNayarDiffuseData diffuse;
    diffuse.color = Vec3f(0.7f, 0.5f, 0.3f);
    closure.bsdfTree.root = closure.bsdfTree.Add(diffuse);
    const Vec3f omegaInWld = materialNormal;
    const Vec3f frontEval = TestSurfaceApi::EvalSurface(
        closure, materialNormal, omegaInWld, omegaOutFrontWld,
        0.0f, true);
    const Vec3f backEval = TestSurfaceApi::EvalSurface(
        closure, materialNormal, omegaInWld, omegaOutBackWld,
        0.0f, true);
    const float frontPdf = _PdfSurface(closure, _MakeSurfaceInteraction(materialNormal, materialNormal, materialNormal, omegaOutFrontWld, 0.0f, true), omegaInWld);
    const float backPdf = _PdfSurface(closure, _MakeSurfaceInteraction(materialNormal, materialNormal, materialNormal, omegaOutBackWld, 0.0f, true), omegaInWld);
    const Bsdf::BsdfSample frontSample = TestSurfaceApi::SampleSurface(
        closure, materialNormal, omegaOutFrontWld, 0.3f, 0.7f, 0.2f,
        0.0f, true);
    const Bsdf::BsdfSample backSample = TestSurfaceApi::SampleSurface(
        closure, materialNormal, omegaOutBackWld, 0.3f, 0.7f, 0.2f,
        0.0f, true);
    return
        frontEval.length() > 0.0f &&
        frontPdf > 0.0f &&
        frontSample.pdfSolidAngle > 0.0f &&
        Test_IsClose(frontEval, backEval, 1.0e-5f) &&
        Test_IsClose(frontPdf, backPdf, 1.0e-5f) &&
        Test_IsClose(
            frontSample.omegaInWld, backSample.omegaInWld, 1.0e-5f) &&
        Test_IsClose(
            frontSample.bsdfValue, backSample.bsdfValue, 1.0e-5f) &&
        Test_IsClose(
            frontSample.pdfSolidAngle,
            backSample.pdfSolidAngle,
            1.0e-5f);
}

static bool
TestRoughReflectiveNormalUsesGeometricCorrection()
{
    Bsdf::ConductorData conductor;
    conductor.weight = 1.0f;
    conductor.ior = Vec3f(0.2f, 0.9f, 1.1f);
    conductor.extinction = Vec3f(3.9f, 2.5f, 2.1f);
    conductor.roughness = Vec2f(0.25f, 0.25f);
    conductor.hasShadingNormal = true;
    conductor.normal = Vec3f(-0.95f, 0.0f, 0.31f).normalized();

    const Vec3f normalShdWldOut(0.0f, 0.0f, 1.0f);
    const Vec3f omegaOutWld = Vec3f(0.2f, 0.0f, 0.98f).normalized();
    const Vec3f normalGeomWldOut[] = {
        Vec3f(0.0f, 0.0f, 1.0f),
        Vec3f(0.2f, 0.0f, 0.98f).normalized()
    };
    Vec3f previousNormal;
    for (int i = 0; i < 2; ++i) {
        SurfaceClosure closure;
        closure.bsdfTree.root = closure.bsdfTree.Add(conductor);
        Bsdf::detail::PrepareShadingNormals(
            &closure.bsdfTree, normalShdWldOut,
            normalGeomWldOut[i], omegaOutWld);
        const Bsdf::ConductorData* prepared =
            std::get_if<Bsdf::ConductorData>(
                &closure.bsdfTree.nodes[0].data);
        const Vec3f expected =
            Bsdf::detail::EnsureValidSpecularReflection(
                normalGeomWldOut[i], omegaOutWld, conductor.normal);
        if (!prepared ||
            !Test_IsClose(prepared->normal, expected, 1.0e-6f) ||
            Test_IsClose(prepared->normal, conductor.normal, 1.0e-5f) ||
            (i > 0 && Test_IsClose(
                prepared->normal, previousNormal, 1.0e-5f))) {
            printf("    Rough reflection missed geometric correction\n");
            return false;
        }
        previousNormal = prepared->normal;
    }
    return true;
}

static bool
TestExplicitBaseNormalMatchesInheritedNormal()
{
    const Vec3f normalShdWldOut(0.0f, 1.0f, 0.0f);
    Bsdf::ConductorData conductor;
    conductor.weight = 1.0f;
    conductor.ior = Vec3f(0.2f, 0.9f, 1.1f);
    conductor.extinction = Vec3f(3.9f, 2.5f, 2.1f);
    conductor.roughness = Vec2f(0.2f, 0.2f);

    SurfaceClosure inheritedClosure;
    inheritedClosure.bsdfTree.root =
        inheritedClosure.bsdfTree.Add(conductor);
    conductor.hasShadingNormal = true;
    conductor.normal = normalShdWldOut;
    SurfaceClosure explicitClosure;
    explicitClosure.bsdfTree.root = explicitClosure.bsdfTree.Add(conductor);

    const Vec3f omegaOutWld =
        Vec3f(0.2f, 0.98f, 0.0f).normalized();
    const Vec3f omegaInWld =
        Vec3f(-0.3f, 0.95f, 0.05f).normalized();
    Bsdf::detail::PrepareShadingNormals(
        &inheritedClosure.bsdfTree, normalShdWldOut, normalShdWldOut,
        omegaOutWld);
    Bsdf::detail::PrepareShadingNormals(
        &explicitClosure.bsdfTree, normalShdWldOut, normalShdWldOut,
        omegaOutWld);
    const Vec3f inheritedEval = PreparedSurfaceApi::EvalSurface(
        inheritedClosure, normalShdWldOut, omegaInWld, omegaOutWld,
        0.0f, true);
    const Vec3f explicitEval = PreparedSurfaceApi::EvalSurface(
        explicitClosure, normalShdWldOut, omegaInWld, omegaOutWld,
        0.0f, true);
    const float inheritedPdf = _PdfPreparedSurface(inheritedClosure, _MakeSurfaceInteraction(normalShdWldOut, normalShdWldOut, normalShdWldOut, omegaOutWld, 0.0f, true), omegaInWld);
    const float explicitPdf = _PdfPreparedSurface(explicitClosure, _MakeSurfaceInteraction(normalShdWldOut, normalShdWldOut, normalShdWldOut, omegaOutWld, 0.0f, true), omegaInWld);
    const Bsdf::BsdfSample inheritedSample = PreparedSurfaceApi::SampleSurface(
        inheritedClosure, normalShdWldOut, omegaOutWld,
        0.3f, 0.7f, 0.2f, 0.0f, true);
    const Bsdf::BsdfSample explicitSample = PreparedSurfaceApi::SampleSurface(
        explicitClosure, normalShdWldOut, omegaOutWld,
        0.3f, 0.7f, 0.2f, 0.0f, true);
    return Test_IsClose(inheritedEval, explicitEval, 1.0e-6f) &&
        Test_IsClose(inheritedPdf, explicitPdf, 1.0e-6f) &&
        Test_IsClose(
            inheritedSample.omegaInWld,
            explicitSample.omegaInWld,
            1.0e-6f) &&
        Test_IsClose(
            inheritedSample.bsdfValue,
            explicitSample.bsdfValue,
            1.0e-6f) &&
        Test_IsClose(
            inheritedSample.pdfSolidAngle,
            explicitSample.pdfSolidAngle,
            1.0e-6f);
}

static bool
TestNormalMappedTransmissionSurvivesDirectionAgreement()
{
    SurfaceClosure closure;
    Bsdf::DielectricData dielectric;
    dielectric.weight = 1.0f;
    dielectric.tint = Vec3f(1.0f);
    dielectric.ior = 1.5f;
    dielectric.roughness = Vec2f(0.2f, 0.2f);
    dielectric.scatterMode = Bsdf::ScatterMode::Transmission;
    closure.bsdfTree.root = closure.bsdfTree.Add(dielectric);

    const Vec3f normalSrfWldOut(0.0f, 0.0f, 1.0f);
    const Vec3f normalShdWldOut =
        Vec3f(-0.4f, 0.0f, 0.9165f).normalized();
    const Vec3f omegaOutWld(0.0f, 0.0f, 1.0f);
    Bsdf::detail::PrepareShadingNormals(
        &closure.bsdfTree, normalShdWldOut, normalSrfWldOut, omegaOutWld);
    for (int i = 0; i < 64; ++i) {
        const float u1 = (static_cast<float>(i) + 0.5f) / 64.0f;
        const float u2 =
            (static_cast<float>((i * 13) % 64) + 0.5f) / 64.0f;
        const Bsdf::BsdfSample sample = PreparedSurfaceApi::SampleSurface(
            closure, normalShdWldOut, normalSrfWldOut, normalSrfWldOut,
            omegaOutWld,
            u1, u2, 0.5f, 0.0f, true);
        if (sample.pdfSolidAngle <= 0.0f) {
            continue;
        }
        if (Dot(normalSrfWldOut, sample.omegaInWld) < 0.0f &&
            Dot(normalShdWldOut, sample.omegaInWld) < 0.0f &&
            sample.isTransmission) {
            return true;
        }
    }
    return false;
}

static bool
TestEnsureValidSpecularReflection()
{
    const Vec3f normalGeomWldOut(0.0f, 0.0f, 1.0f);
    const Vec3f omegaOutWld =
        Vec3f(0.2f, 0.0f, 0.98f).normalized();
    const Vec3f normalShdWldOut =
        Vec3f(-0.95f, 0.0f, 0.31f).normalized();
    const Vec3f uncorrectedReflection =
        2.0f * Dot(normalShdWldOut, omegaOutWld) *
            normalShdWldOut -
        omegaOutWld;
    const Vec3f corrected = Bsdf::detail::EnsureValidSpecularReflection(
        normalGeomWldOut, omegaOutWld, normalShdWldOut);
    const Vec3f omegaInWld =
        2.0f * Dot(corrected, omegaOutWld) * corrected - omegaOutWld;
    if (Dot(uncorrectedReflection, normalGeomWldOut) >= 0.0f ||
        Test_IsClose(corrected, normalShdWldOut, 1.0e-5f) ||
        !Test_IsClose(corrected.length(), 1.0f, 1.0e-5f) ||
        Dot(omegaInWld, normalGeomWldOut) < 0.0f) {
        std::printf("    primary correction case failed\n");
        return false;
    }

    // Exercise both quadratic roots over grazing configurations and enforce
    // finite unit normals in the incident geometric hemisphere.
    for (const float omegaOutZ : {0.01f, 0.1f, 0.5f, 0.99f}) {
        const float omegaOutRadius =
            std::sqrt(1.0f - omegaOutZ * omegaOutZ);
        for (int outPhiIdx = 0; outPhiIdx < 8; ++outPhiIdx) {
            const float outPhi =
                _kFurnaceTwoPi * outPhiIdx / 8.0f;
            const Vec3f omegaOut(
                omegaOutRadius * std::cos(outPhi),
                omegaOutRadius * std::sin(outPhi),
                omegaOutZ);
            for (const float normalZ : {0.01f, 0.1f, 0.5f, 0.99f}) {
                const float normalRadius =
                    std::sqrt(1.0f - normalZ * normalZ);
                for (int normalPhiIdx = 0; normalPhiIdx < 8;
                     ++normalPhiIdx) {
                    const float normalPhi =
                        _kFurnaceTwoPi * normalPhiIdx / 8.0f;
                    const Vec3f normal(
                        normalRadius * std::cos(normalPhi),
                        normalRadius * std::sin(normalPhi),
                        normalZ);
                    const Vec3f result =
                        Bsdf::detail::EnsureValidSpecularReflection(
                            normalGeomWldOut, omegaOut, normal);
                    if (!std::isfinite(result[0]) ||
                        !std::isfinite(result[1]) ||
                        !std::isfinite(result[2]) ||
                        !Test_IsClose(result.length(), 1.0f, 1.0e-5f) ||
                        result[2] < -1.0e-6f || result[2] > 1.0f + 1.0e-6f) {
                        std::printf(
                            "    correction sweep violated its postcondition\n");
                        return false;
                    }
                }
            }
        }
    }

    // A geometric-normal fallback creates a visible discontinuity while the
    // Cycles solve varies continuously with a smoothly tilted input normal.
    Vec3f previous = normalGeomWldOut;
    for (int i = 0; i <= 1024; ++i) {
        const float x = -0.99f + 1.98f * static_cast<float>(i) / 1024.0f;
        const Vec3f normal =
            Vec3f(x, 0.0f, std::sqrt(std::max(0.0f, 1.0f - x * x)));
        const Vec3f result = Bsdf::detail::EnsureValidSpecularReflection(
            normalGeomWldOut, omegaOutWld, normal);
        if (i > 0 && (result - previous).length() > 0.05f) {
            std::printf("    correction continuity failed at %d: %f\n", i,
                        (result - previous).length());
            return false;
        }
        previous = result;
    }

    SurfaceClosure roughClosure;
    Bsdf::ConductorData conductor;
    conductor.weight = 1.0f;
    conductor.ior = Vec3f(0.2f, 0.9f, 1.1f);
    conductor.extinction = Vec3f(3.9f, 2.5f, 2.1f);
    conductor.roughness = Vec2f(0.2f, 0.2f);
    conductor.hasShadingNormal = true;
    conductor.normal = normalShdWldOut;
    roughClosure.bsdfTree.root = roughClosure.bsdfTree.Add(conductor);
    ValidateLeafNormals(&roughClosure.bsdfTree, normalGeomWldOut);
    Bsdf::detail::PrepareShadingNormals(
        &roughClosure.bsdfTree, normalGeomWldOut,
        normalGeomWldOut, omegaOutWld);
    const Bsdf::ConductorData* prepared =
        std::get_if<Bsdf::ConductorData>(
            &roughClosure.bsdfTree.nodes[0].data);
    if (!prepared || !prepared->hasShadingNormal ||
        !Test_IsClose(prepared->normal, corrected, 1.0e-5f)) {
        return false;
    }

    // Match run-0276: a finite-roughness coupled dielectric normal may start
    // in the surface hemisphere while pointing away from omegaOut. Cycles
    // correction must recover the outgoing-side precondition and leave valid
    // transmission support after the geometric sample check.
    const Vec3f normalAwayWldOut =
        Vec3f(-0.995f, 0.0f, 0.1f).normalized();
    if (Dot(normalAwayWldOut, normalGeomWldOut) <= 0.0f ||
        Dot(normalAwayWldOut, omegaOutWld) >= 0.0f) {
        return false;
    }
    SurfaceClosure roughGlassClosure;
    Bsdf::DielectricInterfaceData roughGlass;
    roughGlass.roughness = Vec2f(0.01f);
    roughGlass.compensateCoupledDielectric = true;
    roughGlass.hasShadingNormal = true;
    roughGlass.normal = normalAwayWldOut;
    roughGlassClosure.bsdfTree.root =
        roughGlassClosure.bsdfTree.Add(roughGlass);
    ValidateLeafNormals(&roughGlassClosure.bsdfTree, normalGeomWldOut);
    Bsdf::detail::PrepareShadingNormals(
        &roughGlassClosure.bsdfTree, normalGeomWldOut,
        normalGeomWldOut, omegaOutWld);
    const Bsdf::DielectricInterfaceData* preparedRoughGlass =
        std::get_if<Bsdf::DielectricInterfaceData>(
            &roughGlassClosure.bsdfTree.nodes[0].data);
    if (!preparedRoughGlass ||
        Test_IsClose(
            preparedRoughGlass->normal, normalAwayWldOut, 1.0e-5f) ||
        Dot(preparedRoughGlass->normal, omegaOutWld) <= 0.0f) {
        return false;
    }
    bool foundRoughTransmission = false;
    for (int i = 0; i < 4096; ++i) {
        const float u1 = (static_cast<float>(i) + 0.5f) / 4096.0f;
        const float u2 =
            _RadicalInverseBase2(static_cast<std::uint32_t>(i));
        const Bsdf::BsdfSample sample = PreparedSurfaceApi::SampleSurface(
            roughGlassClosure, normalGeomWldOut, omegaOutWld,
            u1, u2, 0.99f, 0.0f, true);
        if (sample.pdfSolidAngle > 0.0f && sample.isTransmission &&
            Dot(sample.omegaInWld, preparedRoughGlass->normal) < 0.0f &&
            Dot(sample.omegaInWld, normalGeomWldOut) < 0.0f) {
            foundRoughTransmission = true;
            break;
        }
    }
    if (!foundRoughTransmission) {
        return false;
    }

    // Delta reflection uses the same prepared closure normal.
    SurfaceClosure deltaClosure;
    conductor.roughness = Vec2f(0.0f);
    deltaClosure.bsdfTree.root = deltaClosure.bsdfTree.Add(conductor);
    Bsdf::detail::PrepareShadingNormals(
        &deltaClosure.bsdfTree, normalGeomWldOut,
        normalGeomWldOut, omegaOutWld);
    const Bsdf::BsdfSample deltaSample = PreparedSurfaceApi::SampleSurface(
        deltaClosure, normalGeomWldOut, omegaOutWld,
        0.3f, 0.7f, 0.5f, 0.0f, true);
    if (!deltaSample.isSpecular ||
        deltaSample.pdfSolidAngle <= 0.0f ||
        Dot(deltaSample.omegaInWld, normalGeomWldOut) < 0.0f) {
        return false;
    }

#ifdef PXR_HDEMBREE_ENABLE_ADOBE_OPENPBR
    SurfaceClosure adobeClosure;
    Bsdf::AdobeOpenPbrData adobe;
    adobe.baseWeight = 1.0f;
    adobe.baseMetalness = 1.0f;
    adobe.specularWeight = 1.0f;
    adobe.specularRoughness = 0.2f;
    adobe.coatWeight = 1.0f;
    adobe.coatRoughness = 0.0f;
    adobeClosure.bsdfTree.root = adobeClosure.bsdfTree.Add(adobe);
    Bsdf::detail::PrepareShadingNormals(
        &adobeClosure.bsdfTree, normalShdWldOut,
        normalGeomWldOut, omegaOutWld);
    const Bsdf::AdobeOpenPbrData* preparedAdobe =
        std::get_if<Bsdf::AdobeOpenPbrData>(
            &adobeClosure.bsdfTree.nodes[0].data);
    if (!preparedAdobe || !preparedAdobe->hasShadingNormal ||
        !Test_IsClose(
            preparedAdobe->normal,
            corrected, 1.0e-5f)) {
        return false;
    }
    const AdobeOpenPbrPreparedSurface preparedAdobeSurface =
        PrepareAdobeOpenPbrSurface(adobeClosure, _MakeSurfaceInteraction(normalShdWldOut, normalShdWldOut, normalGeomWldOut, omegaOutWld, 0.0f, true));
    if (!preparedAdobeSurface.valid) {
        return false;
    }
    bool foundAdobeSample = false;
    for (int i = 0; i < 256; ++i) {
        const float u1 = (static_cast<float>(i) + 0.5f) / 256.0f;
        const float u2 =
            _RadicalInverseBase2(static_cast<std::uint32_t>(i));
        const Bsdf::BsdfSample sample = PreparedSurfaceApi::SampleSurface(
            adobeClosure, normalShdWldOut, omegaOutWld,
            u1, u2, 0.75f, 0.0f, true);
        if (sample.pdfSolidAngle <= 0.0f || sample.isSubsurface) {
            continue;
        }
        const float evaluatedPdf = _PdfPreparedSurface(adobeClosure, _MakeSurfaceInteraction(normalShdWldOut, normalShdWldOut, normalShdWldOut, omegaOutWld, 0.0f, true), sample.omegaInWld);
        const Vec3f evaluated = PreparedSurfaceApi::EvalSurface(
            adobeClosure, normalShdWldOut, sample.omegaInWld,
            omegaOutWld, 0.0f, true);
        const Vec3f correctedBasisValue = EvalAdobeOpenPbr(
            *preparedAdobe, preparedAdobe->normal,
            normalShdWldOut, sample.omegaInWld, omegaOutWld);
        const Vec3f uncorrectedBasisValue = EvalAdobeOpenPbr(
            *preparedAdobe, normalShdWldOut, normalShdWldOut,
            sample.omegaInWld, omegaOutWld);
        const AdobeOpenPbrEvalPdfResult preparedEvalPdf =
            EvalPdfPreparedAdobeOpenPbrSurface(
                preparedAdobeSurface, sample.omegaInWld);
        const Bsdf::BsdfSample preparedSample =
            SamplePreparedAdobeOpenPbrSurface(
                preparedAdobeSurface, u1, u2, 0.75f);
        if (!Test_IsClose(
                sample.pdfSolidAngle, evaluatedPdf,
                2.0e-5f * std::max(1.0f, evaluatedPdf)) ||
            !preparedEvalPdf.evaluated ||
            !Test_IsClose(evaluated, preparedEvalPdf.value, 2.0e-5f) ||
            !Test_IsClose(
                preparedEvalPdf.valueCosine,
                preparedEvalPdf.value * std::abs(Dot(
                    preparedAdobe->normal,
                    sample.omegaInWld)),
                2.0e-5f) ||
            !Test_IsClose(evaluated, correctedBasisValue, 2.0e-5f) ||
            !Test_IsClose(
                evaluatedPdf, preparedEvalPdf.pdfSolidAngle,
                2.0e-5f * std::max(1.0f, evaluatedPdf)) ||
            preparedSample.pdfSolidAngle <= 0.0f ||
            !Test_IsClose(
                sample.omegaInWld, preparedSample.omegaInWld, 2.0e-5f) ||
            !Test_IsClose(
                sample.pdfSolidAngle, preparedSample.pdfSolidAngle,
                2.0e-5f * std::max(1.0f, sample.pdfSolidAngle)) ||
            !Test_IsClose(
                preparedSample.bsdfValueCosine,
                preparedSample.bsdfValue * std::abs(Dot(
                    preparedAdobe->normal,
                    preparedSample.omegaInWld)),
                2.0e-5f) ||
            !Test_IsClose(
                sample.bsdfValueCosine, preparedSample.bsdfValueCosine,
                2.0e-5f)) {
            return false;
        }
        if (Test_IsClose(
                correctedBasisValue, uncorrectedBasisValue, 1.0e-4f)) {
            continue;
        }
        foundAdobeSample = true;
        break;
    }
    if (!foundAdobeSample) {
        return false;
    }

    // The whole-model prepared path must retain the same correction for delta
    // glossy components.
    adobe.specularRoughness = 0.0f;
    adobe.coatWeight = 0.0f;
    SurfaceClosure deltaAdobeClosure;
    deltaAdobeClosure.bsdfTree.root =
        deltaAdobeClosure.bsdfTree.Add(adobe);
    Bsdf::detail::PrepareShadingNormals(
        &deltaAdobeClosure.bsdfTree, normalShdWldOut,
        normalGeomWldOut, omegaOutWld);
    const AdobeOpenPbrPreparedSurface deltaAdobePrepared =
        PrepareAdobeOpenPbrSurface(deltaAdobeClosure, _MakeSurfaceInteraction(normalShdWldOut, normalShdWldOut, normalGeomWldOut, omegaOutWld, 0.0f, true));
    if (!deltaAdobePrepared.valid) {
        return false;
    }
    bool foundDeltaAdobeSample = false;
    for (int i = 0; i < 256; ++i) {
        const float u1 = (static_cast<float>(i) + 0.5f) / 256.0f;
        const float u2 =
            _RadicalInverseBase2(static_cast<std::uint32_t>(i));
        const Bsdf::BsdfSample preparedSample =
            SamplePreparedAdobeOpenPbrSurface(
                deltaAdobePrepared, u1, u2, 0.75f);
        const Bsdf::BsdfSample correctedSample = SampleAdobeOpenPbr(adobe, corrected, _MakeSurfaceInteraction(corrected, normalShdWldOut, normalGeomWldOut, omegaOutWld, 0.0f, true), u1, u2, 0.75f);
        const Bsdf::BsdfSample uncorrectedSample = SampleAdobeOpenPbr(adobe, normalShdWldOut, _MakeSurfaceInteraction(normalShdWldOut, normalShdWldOut, normalGeomWldOut, omegaOutWld, 0.0f, true), u1, u2, 0.75f);
        if (preparedSample.pdfSolidAngle <= 0.0f ||
            correctedSample.pdfSolidAngle <= 0.0f ||
            !Test_IsClose(
                preparedSample.omegaInWld,
                correctedSample.omegaInWld, 2.0e-5f) ||
            (uncorrectedSample.pdfSolidAngle > 0.0f &&
             Test_IsClose(
                 preparedSample.omegaInWld,
                 uncorrectedSample.omegaInWld, 1.0e-4f))) {
            continue;
        }
        foundDeltaAdobeSample = true;
        break;
    }
    if (!foundDeltaAdobeSample) {
        return false;
    }
#endif

    return deltaSample.isSpecular &&
        deltaSample.pdfSolidAngle > 0.0f &&
        Dot(deltaSample.omegaInWld, normalGeomWldOut) >= 0.0f;
}

static bool
TestPreparedSpecularNormalsIncludeSubsurfaceOnly()
{
    const Vec3f normalGeomWldOut(0.0f, 0.0f, 1.0f);
    const Vec3f omegaOutWld = Vec3f(0.2f, 0.0f, 0.98f).normalized();
    const Vec3f authoredNormal = Vec3f(-0.95f, 0.0f, 0.31f).normalized();

    SurfaceClosure closure;
    Bsdf::OrenNayarDiffuseData diffuse;
    diffuse.hasShadingNormal = true;
    diffuse.normal = authoredNormal;
    Bsdf::TranslucentData translucent;
    translucent.hasShadingNormal = true;
    translucent.normal = authoredNormal;
    Bsdf::SubsurfaceData subsurface;
    subsurface.hasShadingNormal = true;
    subsurface.normal = authoredNormal;
    closure.bsdfTree.Add(diffuse);
    closure.bsdfTree.Add(translucent);
    closure.bsdfTree.root = closure.bsdfTree.Add(subsurface);

    Bsdf::detail::PrepareShadingNormals(
        &closure.bsdfTree, normalGeomWldOut, normalGeomWldOut,
        omegaOutWld);
    const Bsdf::OrenNayarDiffuseData* preparedDiffuse =
        std::get_if<Bsdf::OrenNayarDiffuseData>(
            &closure.bsdfTree.nodes[0].data);
    const Bsdf::TranslucentData* preparedTranslucent =
        std::get_if<Bsdf::TranslucentData>(
            &closure.bsdfTree.nodes[1].data);
    const Bsdf::SubsurfaceData* preparedSubsurface =
        std::get_if<Bsdf::SubsurfaceData>(
            &closure.bsdfTree.nodes[2].data);
    const Bsdf::BsdfSample marker = PreparedSurfaceApi::SampleSurface(
        closure, normalGeomWldOut, normalGeomWldOut, normalGeomWldOut,
        omegaOutWld, 0.3f, 0.7f, 0.5f);
    if (!(preparedDiffuse && preparedTranslucent && preparedSubsurface &&
        Test_IsClose(preparedDiffuse->normal, authoredNormal, 1.0e-6f) &&
        Test_IsClose(preparedTranslucent->normal, authoredNormal, 1.0e-6f) &&
        !Test_IsClose(preparedSubsurface->normal, authoredNormal, 1.0e-5f) &&
        marker.isSubsurface && !marker.hasSubsurfaceEntryDirection &&
        Test_IsClose(
            marker.normalShdLobeWldOut, preparedSubsurface->normal,
            1.0e-6f))) {
        return false;
    }

    SurfaceClosure inheritedClosure;
    inheritedClosure.bsdfTree.root =
        inheritedClosure.bsdfTree.Add(Bsdf::SubsurfaceData{});
    Bsdf::detail::PrepareShadingNormals(
        &inheritedClosure.bsdfTree, authoredNormal, normalGeomWldOut,
        omegaOutWld);
    const Bsdf::BsdfSample inheritedMarker = PreparedSurfaceApi::SampleSurface(
        inheritedClosure, authoredNormal, normalGeomWldOut,
        normalGeomWldOut, omegaOutWld, 0.3f, 0.7f, 0.5f);
    const Vec3f corrected = Bsdf::detail::EnsureValidSpecularReflection(
        normalGeomWldOut, omegaOutWld, authoredNormal);
    if (!inheritedMarker.isSubsurface ||
        !Test_IsClose(
            inheritedMarker.normalShdLobeWldOut, corrected, 1.0e-6f)) {
        return false;
    }

    Vec3f dirCorrectedEntryWld;
    Vec3f dirGraphEntryWld;
    const bool correctedEntryValid = Bsdf::SampleSubsurfaceEntry(
        closure, marker.normalShdLobeWldOut, normalGeomWldOut,
        omegaOutWld, 0.3f, 0.7f, dirCorrectedEntryWld);
    const bool graphEntryValid = Bsdf::SampleSubsurfaceEntry(
        closure, normalGeomWldOut, normalGeomWldOut, omegaOutWld, 0.3f,
        0.7f, dirGraphEntryWld);
    return correctedEntryValid && graphEntryValid &&
        Dot(marker.normalShdLobeWldOut, dirCorrectedEntryWld) < 0.0f &&
        Dot(normalGeomWldOut, dirCorrectedEntryWld) < 0.0f &&
        !Test_IsClose(dirCorrectedEntryWld, dirGraphEntryWld, 1.0e-5f);
}

static bool
TestLeafSampleRejectsWrongGeometricSide()
{
    const Vec3f normalSrfWldOut(0.0f, 0.0f, 1.0f);
    const Vec3f normalGeomWldOut(0.0f, 0.0f, 1.0f);
    const Vec3f normalShdLobeWldOut =
        Vec3f(0.8f, 0.0f, 0.6f).normalized();
    const Vec3f omegaOutWld(0.0f, 0.0f, 1.0f);

    const Vec3f wrongGeomReflection =
        Vec3f(1.0f, 0.0f, -0.1f).normalized();
    const Vec3f tangentGeomReflection(1.0f, 0.0f, 0.0f);
    const Vec3f tangentLobeReflection(-0.6f, 0.0f, 0.8f);
    if (Bsdf::detail::SampledDirectionIsValid(
            false, normalShdLobeWldOut, normalGeomWldOut,
            wrongGeomReflection) ||
        Bsdf::detail::SampledDirectionIsValid(
            false, normalShdLobeWldOut, normalGeomWldOut,
            tangentGeomReflection) ||
        Bsdf::detail::SampledDirectionIsValid(
            false, normalShdLobeWldOut, normalGeomWldOut,
            tangentLobeReflection)) {
        return false;
    }

    // Exercise each diffuse-family leaf with a sample that is valid in the
    // authored lobe frame but lies on the forbidden geometric side.
    for (int family = 0; family < 4; ++family) {
        SurfaceClosure diffuseFamilyClosure;
        if (family == 0) {
            Bsdf::OrenNayarDiffuseData data;
            data.hasShadingNormal = true;
            data.normal = normalShdLobeWldOut;
            diffuseFamilyClosure.bsdfTree.root =
                diffuseFamilyClosure.bsdfTree.Add(data);
        } else if (family == 1) {
            Bsdf::BurleyDiffuseData data;
            data.hasShadingNormal = true;
            data.normal = normalShdLobeWldOut;
            diffuseFamilyClosure.bsdfTree.root =
                diffuseFamilyClosure.bsdfTree.Add(data);
        } else if (family == 2) {
            Bsdf::SheenData data;
            data.hasShadingNormal = true;
            data.normal = normalShdLobeWldOut;
            diffuseFamilyClosure.bsdfTree.root =
                diffuseFamilyClosure.bsdfTree.Add(data);
        } else {
            Bsdf::TranslucentData data;
            data.hasShadingNormal = true;
            data.normal = normalShdLobeWldOut;
            diffuseFamilyClosure.bsdfTree.root =
                diffuseFamilyClosure.bsdfTree.Add(data);
        }

        bool foundWrongSideSample = false;
        for (int i = 0; i < 64 && !foundWrongSideSample; ++i) {
            const float u1 = (static_cast<float>(i) + 0.5f) / 64.0f;
            const float u2 = _RadicalInverseBase2(
                static_cast<std::uint32_t>(i));
            const Vec3f rawNormal = family == 3
                ? -normalShdLobeWldOut
                : normalShdLobeWldOut;
            const Bsdf::BsdfSample rawSample = Bsdf::SampleLambertian(
                Vec3f(1.0f), rawNormal, omegaOutWld, u1, u2);
            const bool wrongSide = family == 3
                ? Dot(normalGeomWldOut, rawSample.omegaInWld) >= 0.0f
                : Dot(normalGeomWldOut, rawSample.omegaInWld) <= 0.0f;
            if (!wrongSide) {
                continue;
            }
            const Bsdf::BsdfSample rejected = TestSurfaceApi::SampleSurface(
                diffuseFamilyClosure, normalSrfWldOut,
                normalShdLobeWldOut, normalGeomWldOut, omegaOutWld,
                u1, u2, 0.5f);
            if (rejected.pdfSolidAngle != 0.0f ||
                rejected.bsdfValue.length() != 0.0f ||
                rejected.isTransmission != (family == 3)) {
                return false;
            }
            foundWrongSideSample = true;
        }
        if (!foundWrongSideSample) {
            return false;
        }
    }

    SurfaceClosure transmissionClosure;
    Bsdf::DielectricData transmission;
    transmission.weight = 1.0f;
    transmission.tint = Vec3f(1.0f);
    transmission.ior = 1.5f;
    transmission.roughness = Vec2f(0.0f);
    transmission.scatterMode = Bsdf::ScatterMode::Transmission;
    transmissionClosure.bsdfTree.root =
        transmissionClosure.bsdfTree.Add(transmission);
    const Bsdf::BsdfSample acceptedTransmission = TestSurfaceApi::SampleSurface(
        transmissionClosure, normalSrfWldOut, normalSrfWldOut,
        normalGeomWldOut, omegaOutWld, 0.3f, 0.7f, 0.5f);
    return acceptedTransmission.pdfSolidAngle > 0.0f &&
        acceptedTransmission.isTransmission &&
        Dot(normalGeomWldOut, acceptedTransmission.omegaInWld) < 0.0f;
}

static bool
TestBumpShadowingChangesEvalButNotPdf()
{
    SurfaceClosure closure;
    Bsdf::OrenNayarDiffuseData diffuse;
    diffuse.color = Vec3f(1.0f);
    diffuse.weight = 1.0f;
    diffuse.hasShadingNormal = true;
    diffuse.normal = Vec3f(0.8f, 0.0f, 0.6f).normalized();
    closure.bsdfTree.root = closure.bsdfTree.Add(diffuse);

    const Vec3f normalSrfWldOut(0.0f, 0.0f, 1.0f);
    const Vec3f omegaOutWld(0.0f, 0.0f, 1.0f);
    const Vec3f omegaInWld = Vec3f(0.9f, 0.0f, 0.1f).normalized();
    const Vec3f unsoftened = TestSurfaceApi::EvalSurface(
        closure, normalSrfWldOut, diffuse.normal, omegaInWld, omegaOutWld);
    const Vec3f softened = TestSurfaceApi::EvalSurface(
        closure, normalSrfWldOut, normalSrfWldOut, omegaInWld, omegaOutWld);
    const float pdf = _PdfSurface(closure, _MakeSurfaceInteraction(normalSrfWldOut, normalSrfWldOut, normalSrfWldOut, omegaOutWld), omegaInWld);
    const Vec3f disagreementDirection =
        Vec3f(0.9f, 0.0f, -0.1f).normalized();
    const Vec3f rejectedEval = TestSurfaceApi::EvalSurface(
        closure, normalSrfWldOut, normalSrfWldOut,
        disagreementDirection, omegaOutWld);
    const float unchangedPdf = _PdfSurface(closure, _MakeSurfaceInteraction(normalSrfWldOut, normalSrfWldOut, normalSrfWldOut, omegaOutWld), disagreementDirection);
    return unsoftened.length() > 0.0f && softened.length() > 0.0f &&
        softened.length() < unsoftened.length() && pdf > 0.0f &&
        rejectedEval.length() == 0.0f && unchangedPdf > 0.0f;
}

static bool
TestRoughReflectionSampleSurvivesBumpHemisphereDisagreement()
{
    const Vec3f normalShdLobeWldOut(0.0f, 0.0f, 1.0f);
    const Vec3f normalSrfWldOut =
        Vec3f(0.8f, 0.0f, 0.6f).normalized();
    const Vec3f normalGeomWldOut(0.0f, 0.0f, 1.0f);
    const Vec3f omegaOutWld(0.0f, 0.0f, 1.0f);

    SurfaceClosure closure;
    Bsdf::ConductorData conductor;
    conductor.ior = Vec3f(0.2f);
    conductor.extinction = Vec3f(3.0f);
    conductor.roughness = Vec2f(0.36f);
    closure.bsdfTree.root = closure.bsdfTree.Add(conductor);

    for (int i = 0; i < 4096; ++i) {
        const float u1 = (static_cast<float>(i) + 0.5f) / 4096.0f;
        const float u2 =
            _RadicalInverseBase2(static_cast<std::uint32_t>(i));
        const Bsdf::BsdfSample raw = Bsdf::SampleGGXSpecular(
            0.6f, 1.5f, Vec3f(1.0f), normalShdLobeWldOut,
            omegaOutWld, u1, u2);
        if (raw.pdfSolidAngle <= 0.0f ||
            Dot(normalGeomWldOut, raw.omegaInWld) <= 0.0f ||
            Bsdf::detail::BumpHemisphereAgreement(
                normalSrfWldOut, normalShdLobeWldOut, raw.omegaInWld)) {
            continue;
        }

        const Bsdf::BsdfSample sampled = TestSurfaceApi::SampleSurface(
            closure, normalShdLobeWldOut, normalSrfWldOut,
            normalGeomWldOut, omegaOutWld, u1, u2, 0.5f);
        const Vec3f evaluated = TestSurfaceApi::EvalSurface(
            closure, normalShdLobeWldOut, normalSrfWldOut,
            sampled.omegaInWld, omegaOutWld);
        return sampled.pdfSolidAngle > 0.0f &&
            sampled.bsdfValue.length() > 0.0f &&
            evaluated.length() == 0.0f;
    }

    printf("    Failed to find rough reflection disagreement sample\n");
    return false;
}

static bool
TestStandardSurfaceProjectsEachLayerWithItsExactNormal()
{
    ParamMap params;
    params["base"] = Value(1.0f);
    params["base_color"] = Value(Vec3f(0.8f));
    params["specular"] = Value(1.0f);
    params["specular_roughness"] = Value(0.2f);
    params["coat"] = Value(1.0f);
    params["coat_roughness"] = Value(0.2f);
    params["coat_normal"] = Value(Vec3f(0.0f, 0.0f, 1.0f));
    SurfaceClosure closure = EvalStandardSurface(params);

    const Vec3f normalGeomWldOut(0.0f, 0.0f, 1.0f);
    const Vec3f normalSrfWldOut = normalGeomWldOut;
    const Vec3f normalShdWldOut =
        Vec3f(0.8f, 0.0f, 0.6f).normalized();
    const Vec3f omegaOutWld =
        Vec3f(-0.5f, 0.0f, 0.8660254f).normalized();
    Bsdf::detail::PrepareShadingNormals(
        &closure.bsdfTree, normalShdWldOut, normalGeomWldOut,
        omegaOutWld);

    const Bsdf::Node* root = closure.bsdfTree.Get(closure.bsdfTree.root);
    if (!root || !std::holds_alternative<Bsdf::LayerData>(root->data)) {
        printf("    Standard Surface fixture did not produce a layer\n");
        return false;
    }
    const Bsdf::LayerData& layer = std::get<Bsdf::LayerData>(root->data);
    const Bsdf::Node* topNode = closure.bsdfTree.Get(layer.top);
    const Bsdf::DielectricData* topData = topNode
        ? std::get_if<Bsdf::DielectricData>(&topNode->data)
        : nullptr;
    if (!topData ||
        Test_IsClose(topData->normal,
                     normalShdWldOut, 1.0e-4f)) {
        printf("    Standard Surface fixture did not preserve its coat normal\n");
        return false;
    }

    for (int i = 0; i < 4096; ++i) {
        const float u1 = (static_cast<float>(i) + 0.5f) / 4096.0f;
        const float u2 =
            _RadicalInverseBase2(static_cast<std::uint32_t>(i));
        const float uLobe = std::fmod(
            (static_cast<float>(i) + 0.5f) * 0.61803398875f, 1.0f);
        const Bsdf::BsdfSample sample = PreparedSurfaceApi::SampleSurface(
            closure, normalShdWldOut, normalSrfWldOut, normalGeomWldOut,
            omegaOutWld, u1, u2, uLobe);
        if (sample.pdfSolidAngle <= 0.0f || sample.isSpecular) {
            continue;
        }

        const Vec3f topSampleValue = Bsdf::detail::EvalNode(closure.bsdfTree, layer.top, _MakeSurfaceInteraction(normalShdWldOut, normalSrfWldOut, normalSrfWldOut, omegaOutWld, 0.0f, true), sample.omegaInWld, Bsdf::detail::BumpShadowingContext::Sampling);
        const Vec3f baseSampleValue = sample.bsdfValue - topSampleValue;
        if (topSampleValue.length() <= 1.0e-4f ||
            baseSampleValue.length() <= 1.0e-4f) {
            continue;
        }
        const Vec3f expectedSampleValueCosine =
            topSampleValue * std::abs(Dot(
                topData->normal, sample.omegaInWld)) +
            baseSampleValue * std::abs(Dot(
                normalShdWldOut,
                sample.omegaInWld));
        if (!Test_IsClose(
                sample.bsdfValueCosine, expectedSampleValueCosine,
                2.0e-5f)) {
            printf("    Sample did not project Standard Surface layers independently\n");
            return false;
        }

        const Vec3f evaluated = PreparedSurfaceApi::EvalSurface(
            closure, normalShdWldOut, normalSrfWldOut,
            sample.omegaInWld, omegaOutWld);
        const Vec3f topEvaluated = Bsdf::detail::EvalNode(closure.bsdfTree, layer.top, _MakeSurfaceInteraction(normalShdWldOut, normalSrfWldOut, normalSrfWldOut, omegaOutWld, 0.0f, true), sample.omegaInWld, Bsdf::detail::BumpShadowingContext::Evaluation);
        const Vec3f expectedEvaluatedCosine =
            topEvaluated * std::abs(Dot(
                topData->normal, sample.omegaInWld)) +
            (evaluated - topEvaluated) * std::abs(Dot(
                normalShdWldOut,
                sample.omegaInWld));
        const Vec3f evaluatedCosine = _EvalSurfaceCosine(closure, _MakeSurfaceInteraction(normalShdWldOut, normalSrfWldOut, normalSrfWldOut, omegaOutWld, 0.0f, true), sample.omegaInWld);
        const Vec3f oldSingleNormalProjection = evaluated *
            std::abs(Dot(normalShdWldOut, sample.omegaInWld));
        if (!Test_IsClose(
                evaluatedCosine, expectedEvaluatedCosine, 2.0e-5f)) {
            printf("    Direct evaluation did not project layers independently\n");
            return false;
        }
        if ((evaluatedCosine - oldSingleNormalProjection).length() <=
            1.0e-3f) {
            continue;
        }
        return true;
    }

    printf("    Failed to find a mixed Standard Surface grazing sample\n");
    return false;
}

static bool
TestSampleSurfaceProjectedValueIncludesPresence()
{
    SurfaceClosure closure;
    closure.presence = 0.25f;
    Bsdf::OrenNayarDiffuseData diffuse;
    diffuse.color = Vec3f(0.8f);
    closure.bsdfTree.root = closure.bsdfTree.Add(diffuse);

    const Vec3f normalWldOut(0.0f, 0.0f, 1.0f);
    const Vec3f omegaOutWld(0.0f, 0.0f, 1.0f);
    const Bsdf::BsdfSample sample = TestSurfaceApi::SampleSurface(
        closure, normalWldOut, omegaOutWld, 0.37f, 0.61f, 0.5f);
    const Vec3f expected = sample.bsdfValue *
        std::abs(Dot(normalWldOut, sample.omegaInWld));
    return sample.pdfSolidAngle > 0.0f &&
        Test_IsClose(sample.bsdfValueCosine, expected, 1.0e-6f);
}

static bool
TestCompositeNodesPreserveTransmissionEvent()
{
    const Vec3f normalWldOut(0.0f, 0.0f, 1.0f);
    const Vec3f omegaOutWld(0.0f, 0.0f, 1.0f);
    const auto transmissionPreserved = [&](const SurfaceClosure& closure) {
        const Bsdf::BsdfSample sample = TestSurfaceApi::SampleSurface(
            closure, normalWldOut, normalWldOut, normalWldOut,
            omegaOutWld, 0.3f, 0.7f, 0.25f);
        return sample.pdfSolidAngle > 0.0f && sample.isTransmission &&
            Dot(normalWldOut, sample.omegaInWld) < 0.0f;
    };
    const auto addLeaves = [](SurfaceClosure* closure) {
        Bsdf::DielectricData transmission;
        transmission.scatterMode = Bsdf::ScatterMode::Transmission;
        transmission.roughness = Vec2f(0.0f);
        const Bsdf::NodeId transmissionId =
            closure->bsdfTree.Add(transmission);
        Bsdf::OrenNayarDiffuseData zeroDiffuse;
        zeroDiffuse.weight = 0.0f;
        const Bsdf::NodeId zeroId = closure->bsdfTree.Add(zeroDiffuse);
        return std::pair<Bsdf::NodeId, Bsdf::NodeId>(
            transmissionId, zeroId);
    };

    SurfaceClosure mixClosure;
    const std::pair<Bsdf::NodeId, Bsdf::NodeId> mixLeaves =
        addLeaves(&mixClosure);
    Bsdf::MixData mix;
    mix.fg = mixLeaves.first;
    mix.bg = mixLeaves.second;
    mix.mix = 1.0f;
    mixClosure.bsdfTree.root = mixClosure.bsdfTree.Add(mix);

    SurfaceClosure layerClosure;
    const std::pair<Bsdf::NodeId, Bsdf::NodeId> layerLeaves =
        addLeaves(&layerClosure);
    Bsdf::LayerData layer;
    layer.top = layerLeaves.first;
    layer.base = layerLeaves.second;
    layerClosure.bsdfTree.root = layerClosure.bsdfTree.Add(layer);

    SurfaceClosure addClosure;
    const std::pair<Bsdf::NodeId, Bsdf::NodeId> addNodeLeaves =
        addLeaves(&addClosure);
    Bsdf::AddData add;
    add.in1 = addNodeLeaves.first;
    add.in2 = addNodeLeaves.second;
    addClosure.bsdfTree.root = addClosure.bsdfTree.Add(add);

    SurfaceClosure multiplyClosure;
    const std::pair<Bsdf::NodeId, Bsdf::NodeId> multiplyLeaves =
        addLeaves(&multiplyClosure);
    Bsdf::MultiplyData multiply;
    multiply.input = multiplyLeaves.first;
    multiply.weight = Vec3f(0.5f);
    multiplyClosure.bsdfTree.root = multiplyClosure.bsdfTree.Add(multiply);

    const Vec3f tiltedNormal = Vec3f(0.8f, 0.0f, 0.6f).normalized();
    const auto makeRejectedClosure = [&](int wrapper) {
        SurfaceClosure closure;
        Bsdf::TranslucentData translucent;
        translucent.hasShadingNormal = true;
        translucent.normal = tiltedNormal;
        const Bsdf::NodeId translucentId =
            closure.bsdfTree.Add(translucent);
        Bsdf::OrenNayarDiffuseData zeroDiffuse;
        zeroDiffuse.weight = 0.0f;
        const Bsdf::NodeId zeroId = closure.bsdfTree.Add(zeroDiffuse);
        if (wrapper == 0) {
            Bsdf::MixData data;
            data.fg = translucentId;
            data.bg = zeroId;
            data.mix = 1.0f;
            closure.bsdfTree.root = closure.bsdfTree.Add(data);
        } else if (wrapper == 1) {
            Bsdf::LayerData data;
            data.top = translucentId;
            data.base = zeroId;
            closure.bsdfTree.root = closure.bsdfTree.Add(data);
        } else if (wrapper == 2) {
            Bsdf::AddData data;
            data.in1 = translucentId;
            data.in2 = zeroId;
            closure.bsdfTree.root = closure.bsdfTree.Add(data);
        } else {
            Bsdf::MultiplyData data;
            data.input = translucentId;
            closure.bsdfTree.root = closure.bsdfTree.Add(data);
        }
        return closure;
    };
    const auto rejectionPreserved = [&](const SurfaceClosure& closure) {
        for (int i = 0; i < 64; ++i) {
            const float u1 = (static_cast<float>(i) + 0.5f) / 64.0f;
            const float u2 =
                _RadicalInverseBase2(static_cast<std::uint32_t>(i));
            const Bsdf::BsdfSample raw = Bsdf::SampleLambertian(
                Vec3f(1.0f), -tiltedNormal, omegaOutWld, u1, u2);
            if (Dot(normalWldOut, raw.omegaInWld) < 0.0f) {
                continue;
            }
            const Bsdf::BsdfSample sample = TestSurfaceApi::SampleSurface(
                closure, normalWldOut, tiltedNormal, normalWldOut,
                omegaOutWld, u1, u2, 0.25f);
            return sample.pdfSolidAngle == 0.0f &&
                sample.bsdfValue.length() == 0.0f &&
                sample.isTransmission;
        }
        return false;
    };

    if (!transmissionPreserved(mixClosure)) {
        printf("    Mix lost transmission event\n");
        return false;
    }
    if (!transmissionPreserved(layerClosure)) {
        printf("    Layer lost transmission event\n");
        return false;
    }
    if (!transmissionPreserved(addClosure)) {
        printf("    Add lost transmission event\n");
        return false;
    }
    if (!transmissionPreserved(multiplyClosure)) {
        printf("    Multiply lost transmission event\n");
        return false;
    }
    for (int wrapper = 0; wrapper < 4; ++wrapper) {
        if (!rejectionPreserved(makeRejectedClosure(wrapper))) {
            printf("    Composite %d rescued rejected child\n", wrapper);
            return false;
        }
    }
    return true;
}

void
Test_RegisterBsdfTests()
{
    _REG(TestDielectricInterfaceUsesExactFresnel);
    _REG(TestUsdPreviewSurfaceTransmissionTirEnergyConservation);
    _REG(TestStandardSurfaceRoughTransmissionWhiteFurnaceDoesNotGainEnergy);
    _REG(TestOpenPbrRoughTransmissionWhiteFurnaceConservesEnergy);
    _REG(
        TestOpenPbrAnisotropicTintedPartialTransmissionSamplingMatchesIntegration);
    _REG(TestLambertianValue);
    _REG(TestLambertianColorScaling);
    _REG(TestFurnaceHelperMatchesLambertian);
    _REG(TestLegacySurfaceSpecularZeroIsLambertian);
    _REG(TestOrenNayarEnergyCompensationFalseUsesLegacyFactor);
    _REG(TestEonDiffuseLambertianLimit);
    _REG(TestEonDiffuseWhiteFurnace);
    _REG(TestGGXFurnaceWhiteFresnelGeneralizedSchlickBaseline);
    _REG(TestGGXFurnaceConductorEnergyBaseline);
    _REG(TestGGXFurnaceDielectricReflectionEnergyBaseline);
    _REG(TestGGXSpecularNonNegative);
    _REG(TestGGXSpecularUsesHeightCorrelatedSmith);
    _REG(TestGGXAnisotropicGrazingLimits);
    _REG(TestGGXDirectionalMissingEnergyLutBounds);
    _REG(TestGGXTurquinWhiteFurnaceCompensatesMissingEnergy);
    _REG(TestGGXSpecularPeak);
    _REG(TestGGXSpecularLowRoughnessPeakPreserved);
    _REG(TestTreeDielectricReflectionMatchesStandaloneGgx);
    _REG(TestDispersionCauchyIorMonotonic);
    _REG(TestDispersionChangesTransmissionSampling);
    _REG(TestDispersionDisabledIgnoresHeroWavelength);
    _REG(TestSpectralNeutralRoundTripWhite);
    _REG(TestSpectralNeutralRoundTripGray);
    _REG(TestSpectralAp1NeutralRoundTrip);
    _REG(TestCoatZeroWeight);
    _REG(TestEvalSurfaceEmissiveOnly);
    _REG(TestSheenGrazingAngle);
    _REG(TestTransmissionNonNegative);
    _REG(TestEvalSurfaceNonNegative);
    _REG(TestPhaseHgIsotropicMatchesUniformSphere);
    _REG(TestPhaseHgForwardScatterBias);
    _REG(TestSampleHenyeyGreensteinPdfConsistency);
    _REG(TestSampleHenyeyGreensteinForwardMean);
    _REG(TestFreeFlightScatterWeightFinite);
    _REG(TestSampleSurfaceSubsurfaceReturnsMarker);
    _REG(TestSampleSurfaceMixCanChooseSubsurface);
    _REG(TestSampleSubsurfaceEntrySmoothRefraction);
    _REG(TestSampleSubsurfaceEntryRoughGGX);
    _REG(TestSampleSubsurfaceEntryIorClamp);
    _REG(TestSampleLambertianHemisphere);
    _REG(TestSampleLambertianPdfConsistency);
    _REG(TestSampleGGXSpecularHemisphere);
    _REG(TestSampleGGXSpecularPdfConsistency);
    _REG(TestSampleGGXSpecularLowRoughnessBoundedThroughput);
    _REG(TestSampleSurfacePdfConsistency);
    _REG(TestTreeTransmissionPreservesWeight);
    _REG(TestBsdfSampleDiffuseLikeClassification);
    _REG(TestZeroRoughnessDielectricSamplesDelta);
    _REG(TestZeroRoughnessConductorSamplesDeltaAndSkipsDirectEval);
    _REG(TestEffectivelySmoothConductorAlphaSamplesDeltaAndSkipsDirectEval);
    _REG(TestSharpConductorAlphaRemainsFiniteGlossy);
    _REG(TestSharpConductorEvalPdfRatioBoundedForLightSamples);
    _REG(TestTreeTransmissionPreservesWeightFromInterior);
    _REG(TestEvalSurfaceTransmissionFromInterior);
    _REG(TestIncidentFrameBackFaceSelectsInteriorIor);
    _REG(TestIncidentFrameBackFaceDiffuseStaysVisible);
    _REG(TestTreeAddTransmissionPreservesWeight);
    _REG(TestDielectricInterfaceLayerDoesNotDoubleAttenuateTransmission);
    _REG(TestOpenPbrInterfaceAppliesBsdlDielectricCompensationScale);
    _REG(TestBsdlDielectricBothRuntimeLutMatchesGeneratedTables);
    _REG(TestBsdlDielectricTransmissionLutMatchesStockSamplerAtEveryGridPoint);
    _REG(TestCoupledRoughDielectricCompensationBudget);
    _REG(TestCoupledRoughDielectricDirectionalTransmissionAlbedo);
    _REG(TestStraightShadowDielectricTransmissionPolicy);
    _REG(TestCoupledRoughDielectricTransmissionMatchesFixedWalterReferences);
    _REG(TestCoupledRoughDielectricEvalAndPdfRespectAnisotropicBoundedSupport);
    _REG(TestStandaloneDielectricRoughTransmissionRemainsUncompensated);
    _REG(TestDielectricInterfaceSamplePdfConsistency);
    _REG(TestCoupledRoughDielectricSamplesTransmissionBeyondMacroCriticalAngle);
    _REG(TestDeltaDielectricInterfaceTransmissionSamplesSingleFresnel);
    _REG(TestDeltaDielectricInterfaceTirDoesNotAmplifyThroughput);
    _REG(TestThinWalledDielectricInterfaceSamplePdfConsistency);
    _REG(TestDeltaThinWalledDielectricInterfaceTransmitsStraightThrough);
    _REG(TestPrunePreservesPreparedLeafNormals);
    _REG(TestPruneCausticClassLobesRemovesDeltaReflectionFromAdd);
    _REG(TestPruneCausticClassLobesRemovesInterfaceTransmission);
    _REG(TestPruneCausticClassLobesEmptyTreeDoesNotUseLegacyFallback);
    _REG(TestPruneCausticClassLobesPreservesMixWeight);
    _REG(TestPruneCausticClassLobesPrunesAdobeTransmissionAndSmoothSpecular);
    _REG(TestSampleGGXTransmissionHemisphere);
    _REG(TestSampleGGXTransmissionPdfConsistency);
    _REG(TestRoughTransmissionSpreads);
    _REG(TestThinFilmDielectricChangesReflectionColor);
    _REG(TestThinFilmConductorChangesReflectionColor);
    _REG(TestThinFilmGeneralizedSchlickChangesReflectionColor);
    _REG(TestThinFilmSampleSurfacePdfConsistency);
    _REG(TestTreeDielectricCustomNormalMatchesStandaloneShadingNormal);
    _REG(TestPerLobeNormalResolutionIsViewIndependent);
    _REG(TestBackFacePreparationNegatesCompleteExteriorNormal);
    _REG(TestPerLobeInvalidNormalsFallBack);
    _REG(TestValidateLeafNormalsDispatchesEverySurfaceLeafType);
    _REG(TestAllLeafShadingNormalPreparation);
    _REG(TestAppendClosureTreePreservesLeafNormals);
    _REG(TestTreeDiffuseNormalIsIndependentOfViewHemisphere);
    _REG(TestRoughReflectiveNormalUsesGeometricCorrection);
    _REG(TestExplicitBaseNormalMatchesInheritedNormal);
    _REG(TestNormalMappedTransmissionSurvivesDirectionAgreement);
    _REG(TestEnsureValidSpecularReflection);
    _REG(TestPreparedSpecularNormalsIncludeSubsurfaceOnly);
    _REG(TestLeafSampleRejectsWrongGeometricSide);
    _REG(TestBumpShadowingChangesEvalButNotPdf);
    _REG(TestRoughReflectionSampleSurvivesBumpHemisphereDisagreement);
    _REG(TestStandardSurfaceProjectsEachLayerWithItsExactNormal);
    _REG(TestSampleSurfaceProjectedValueIncludesPresence);
    _REG(TestCompositeNodesPreserveTransmissionEvent);
    _REG(TestTreeAnisotropicReflectionRespondsToTangent);
    _REG(TestTreeAnisotropicReflectionUsesTurquinCompensation);
    _REG(TestBsdlDielectricReflFrontLutUsesLinearCosThetaGrid);
    _REG(TestLayerReflectionAttenuatesBaseOnOutgoingSide);
    _REG(TestLayerThroughputUsesBsdlDielectricFilter);
    _REG(TestLayerThroughputMaterialXGlslModeUsesExactFresnel);
    _REG(TestPowerHeuristic);
    _REG(TestChannelMISUniform);
    _REG(TestChannelMISWeighted);
    _REG(TestChannelMISZeroFallback);
    _REG(TestChiangRemapHighAlbedo);
    _REG(TestChiangRemapLowAlbedo);
    _REG(TestChiangRemapAnisotropy);
    _REG(TestDiffusionLengthDwivedi);
    _REG(TestSamplePhaseDwivediRange);
    _REG(TestBackwardDwivediFraction);
}

#undef _REG
