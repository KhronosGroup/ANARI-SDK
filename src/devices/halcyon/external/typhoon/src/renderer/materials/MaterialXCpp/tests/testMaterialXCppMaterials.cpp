//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include <renderer/integrator/medium.h>
#include <renderer/materials/MaterialXCpp/graph.h>
#include <renderer/materials/MaterialXCpp/materials/adobeOpenPbr.h>
#include <renderer/materials/MaterialXCpp/materials/bsdf.h>
#include <renderer/materials/MaterialXCpp/materials/bsdf/closureTraversal.h>
#include <renderer/materials/MaterialXCpp/materials/bsdf/microfacet.h>
#include <renderer/materials/MaterialXCpp/materials/disneyPrincipled.h>
#include <renderer/materials/MaterialXCpp/materials/gltfPbr.h>
#include <renderer/materials/MaterialXCpp/materials/openPbr.h>
#include <renderer/materials/MaterialXCpp/materials/standardSurface.h>
#include <renderer/materials/MaterialXCpp/materials/usdPreviewSurface.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <variant>

using namespace mxcpp;

void Test_Register(const char* name, std::function<bool()> fn);
bool Test_IsClose(float a, float b, float eps = 1e-5f);
bool Test_IsClose(const Vec3f& a, const Vec3f& b, float eps = 1e-5f);

#define _REG(name) Test_Register("Materials." #name, &name)

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

static Vec3f
_EvalSurface(const SurfaceClosure& source,
             const SurfaceInteraction& interaction,
             const Vec3f& omegaInWld)
{
    const SurfaceClosure closure = _PrepareTestClosure(source, interaction);
    return Bsdf::EvalSurface(closure, interaction, omegaInWld);
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

template <typename T, typename Predicate>
static const T*
FindNodeIf(const mxcpp::Bsdf::ClosureTree& tree, Predicate predicate)
{
    for (const auto& node : tree.nodes) {
        if (const auto* data = std::get_if<T>(&node.data)) {
            if (predicate(*data)) {
                return data;
            }
        }
    }
    return nullptr;
}

static Vec3f
ExpectedAdobeOpenPbrSubsurfaceExtinction(
    float radius,
    const Vec3f& radiusScale)
{
    Vec3f result(0.0f);
    for (int i = 0; i < 3; ++i) {
        const float mfp = std::max(radius * radiusScale[i], 1.0e-3f);
        result[i] = 1.0f / mfp;
    }
    return result;
}

static Vec3f
ExpectedAdobeOpenPbrSubsurfaceAlbedo(
    const Vec3f& subsurfaceColor,
    float anisotropy)
{
    const float g = std::clamp(anisotropy, -0.999f, 0.999f);
    Vec3f result(0.0f);
    for (int i = 0; i < 3; ++i) {
        const float color = subsurfaceColor[i];
        const float s =
            4.09712f + 4.20863f * color -
            std::sqrt(9.59217f + 41.6808f * color +
                      17.7126f * color * color);
        const float s2 = s * s;
        result[i] = std::clamp((1.0f - s2) / (1.0f - g * s2),
                               0.0f, 1.0f);
    }
    return result;
}

static bool
IsFiniteNonNegative(const Vec3f& value)
{
    for (int i = 0; i < 3; ++i) {
        if (!std::isfinite(value[i]) || value[i] < -1.0e-5f) {
            return false;
        }
    }
    return true;
}

static bool
TestMaterialNormalSpaceContracts()
{
    ParamMap emptyParams;
    if (EvalStandardSurface(emptyParams).normalSpace !=
            SurfaceNormalSpace::None ||
        EvalOpenPbr(emptyParams).normalSpace !=
            SurfaceNormalSpace::None ||
        EvalGltfPbr(emptyParams).normalSpace !=
            SurfaceNormalSpace::None ||
        EvalUsdPreviewSurface(emptyParams).normalSpace !=
            SurfaceNormalSpace::None) {
        return false;
    }

    ParamMap standardParams;
    standardParams["normal"] = Value(Vec3f(0.0f, 1.0f, 0.0f));
    ParamMap openPbrParams;
    openPbrParams["geometry_normal"] = Value(Vec3f(0.0f, 1.0f, 0.0f));
    ParamMap gltfParams;
    gltfParams["normal"] = Value(Vec3f(0.0f, 1.0f, 0.0f));
    ParamMap previewParams;
    previewParams["normal"] = Value(Vec3f(0.0f, 1.0f, 0.0f));

    ParamMap legacyOpenPbrParams;
    legacyOpenPbrParams["normal"] = Value(Vec3f(0.0f, 1.0f, 0.0f));
    ParamMap adobeParams;
    adobeParams["geometry_normal"] = Value(Vec3f(0.0f, 1.0f, 0.0f));

    const SurfaceClosure standardClosure =
        EvalStandardSurface(standardParams);
    const SurfaceClosure previewClosure =
        EvalUsdPreviewSurface(previewParams);
    Vec3f resolvedWorldNormal;
    Vec3f resolvedTangentNormal;
    const Vec3f tangent(1.0f, 0.0f, 0.0f);
    const Vec3f bitangent(0.0f, 0.0f, 1.0f);
    const Vec3f shadingNormal(0.0f, 1.0f, 0.0f);
    if (!standardClosure.ResolveNormal(
            tangent, bitangent, shadingNormal, &resolvedWorldNormal) ||
        !previewClosure.ResolveNormal(
            tangent, bitangent, shadingNormal, &resolvedTangentNormal)) {
        return false;
    }

    return standardClosure.normalSpace == SurfaceNormalSpace::World &&
           EvalOpenPbr(openPbrParams).normalSpace == SurfaceNormalSpace::World &&
           EvalOpenPbr(legacyOpenPbrParams).normalSpace ==
               SurfaceNormalSpace::World &&
           EvalAdobeOpenPbr(adobeParams).normalSpace ==
               SurfaceNormalSpace::World &&
           EvalGltfPbr(gltfParams).normalSpace == SurfaceNormalSpace::World &&
           previewClosure.normalSpace == SurfaceNormalSpace::Tangent &&
           Test_IsClose(resolvedWorldNormal, shadingNormal, 1e-5f) &&
           Test_IsClose(resolvedTangentNormal, bitangent, 1e-5f);
}

// ---------------------------------------------------------------------------
// Standard Surface
// ---------------------------------------------------------------------------

static bool
TestStandardSurfaceDefaults()
{
    ParamMap params;
    SurfaceClosure c = EvalStandardSurface(params);

    // Default base=1.0, base_color=(0.8), so baseColor should be (0.8).
    if (!Test_IsClose(c.baseColor, Vec3f(0.8f), 1e-4f)) {
        printf("    baseColor: (%f,%f,%f)\n", c.baseColor[0], c.baseColor[1], c.baseColor[2]);
        return false;
    }
    if (!Test_IsClose(c.roughness, 0.2f)) {
        printf("    roughness: %f (expected 0.2)\n", c.roughness);
        return false;
    }
    if (!Test_IsClose(c.metallic, 0.0f)) return false;
    if (!Test_IsClose(c.specularIor, 1.5f)) return false;
    if (!Test_IsClose(c.coat, 0.0f)) return false;
    if (!Test_IsClose(c.sheen, 0.0f)) return false;
    if (!Test_IsClose(c.transmission, 0.0f)) return false;
    if (!c.HasBsdfTree()) return false;

    return true;
}

static bool
TestStandardSurfaceMetallic()
{
    ParamMap params;
    params["metalness"] = Value(1.0f);
    SurfaceClosure c = EvalStandardSurface(params);
    return Test_IsClose(c.metallic, 1.0f);
}

static bool
TestStandardSurfaceGoldMetallicSharpRoughnessStaysStable()
{
    const Vec3f normalShdWldOut(0.0f, 1.0f, 0.0f);
    const Vec3f omegaOutWld = Vec3f(0.2f, 0.9797959f, 0.0f).normalized();
    const Vec3f omegaInMirroredWld =
        Vec3f(-0.2f, 0.9797959f, 0.0f).normalized();
    const float roughnessValues[] = {0.0f, 0.01f, 0.02f, 0.03f, 0.04f};

    for (const float roughness : roughnessValues) {
        ParamMap params;
        params["base"] = Value(1.0f);
        params["base_color"] = Value(Vec3f(1.0f, 0.72f, 0.26f));
        params["metalness"] = Value(1.0f);
        params["specular"] = Value(1.0f);
        params["specular_color"] = Value(Vec3f(1.0f));
        params["specular_roughness"] = Value(roughness);
        params["coat"] = Value(0.0f);

        const SurfaceClosure c = EvalStandardSurface(params);
        const auto* conductor = FindNodeIf<Bsdf::ConductorData>(
            c.bsdfTree,
            [](const Bsdf::ConductorData& data) {
                return data.weight > 0.0f;
            });
        if (!conductor) {
            printf("    Expected Standard Surface metallic conductor node\n");
            return false;
        }

        const float clampedRoughness =
            std::clamp(roughness, 0.001f, 1.0f);
        const float expectedAlpha =
            std::clamp(clampedRoughness * clampedRoughness, 1.0e-6f, 1.0f);
        if (!Test_IsClose(conductor->roughness[0], expectedAlpha, 1.0e-8f) ||
            !Test_IsClose(conductor->roughness[1], expectedAlpha, 1.0e-8f)) {
            printf(
                "    Expected roughness %f to map to alpha %g, got "
                "(%f,%f)\n",
                roughness,
                expectedAlpha,
                conductor->roughness[0],
                conductor->roughness[1]);
            return false;
        }

        const Vec3f directEval = TestSurfaceApi::EvalSurface(
            c, normalShdWldOut, omegaInMirroredWld, omegaOutWld);
        const float directPdf = _PdfSurface(c, _MakeSurfaceInteraction(normalShdWldOut, normalShdWldOut, normalShdWldOut, omegaOutWld), omegaInMirroredWld);
        const bool effectivelySmooth = expectedAlpha < 1.0e-3f;
        if (effectivelySmooth) {
            if (!Test_IsClose(directEval, Vec3f(0.0f), 1.0e-7f) ||
                !Test_IsClose(directPdf, 0.0f, 1.0e-7f)) {
                printf(
                    "    Smooth Standard Surface metal should skip finite "
                    "direct eval: roughness=%f eval=(%f,%f,%f) pdf=%f\n",
                    roughness,
                    directEval[0], directEval[1], directEval[2],
                    directPdf);
                return false;
            }

            const auto sample = TestSurfaceApi::SampleSurface(
                c, normalShdWldOut, omegaOutWld, 0.3f, 0.7f, 0.4f);
            if (!sample.isSpecular || sample.pdfSolidAngle <= 0.0f ||
                !Test_IsClose(sample.omegaInWld, omegaInMirroredWld, 1.0e-6f) ||
                !IsFiniteNonNegative(sample.bsdfValue) ||
                sample.bsdfValue.length() <= 0.0f) {
                printf("    Smooth Standard Surface metal should sample delta: "
                       "roughness=%f specular=%d omegaInWld=(%f,%f,%f) "
                       "f=(%f,%f,%f) pdf=%f\n",
                       roughness, sample.isSpecular ? 1 : 0,
                       sample.omegaInWld[0], sample.omegaInWld[1],
                       sample.omegaInWld[2], sample.bsdfValue[0],
                       sample.bsdfValue[1], sample.bsdfValue[2],
                       sample.pdfSolidAngle);
                return false;
            }
            continue;
        }

        if (directEval.length() <= 0.0f ||
            directPdf <= 0.0f ||
            !IsFiniteNonNegative(directEval)) {
            printf(
                "    Sharp Standard Surface metal should remain finite: "
                "roughness=%f eval=(%f,%f,%f) pdf=%f\n",
                roughness,
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
            printf("    Sharp Standard Surface metal sample invalid: "
                   "roughness=%f specular=%d samplePdf=%f pdf=%f\n",
                   roughness, sample.isSpecular ? 1 : 0, sample.pdfSolidAngle,
                   pdf);
            return false;
        }

        const float cosTheta = std::max(sample.omegaInWld[1], 0.0f);
        const Vec3f throughputRgb =
            sample.bsdfValue * (cosTheta / sample.pdfSolidAngle);
        if (!IsFiniteNonNegative(sample.bsdfValue) ||
            !IsFiniteNonNegative(throughputRgb)) {
            printf("    Sharp Standard Surface metal sample non-finite: "
                   "roughness=%f f=(%f,%f,%f) throughputRgb=(%f,%f,%f)\n",
                   roughness, sample.bsdfValue[0], sample.bsdfValue[1],
                   sample.bsdfValue[2], throughputRgb[0], throughputRgb[1],
                   throughputRgb[2]);
            return false;
        }
        for (int i = 0; i < 3; ++i) {
            if (throughputRgb[i] > 1.25f) {
                printf(
                    "    Sharp Standard Surface metal throughputRgb too high: "
                    "roughness=%f channel=%d throughputRgb=%f\n",
                    roughness, i, throughputRgb[i]);
                return false;
            }
        }
    }

    return true;
}

static bool
TestStandardSurfaceCustomParams()
{
    ParamMap params;
    params["base"] = Value(0.5f);
    params["base_color"] = Value(Vec3f(1, 0, 0));
    params["specular_roughness"] = Value(0.8f);
    params["emission"] = Value(2.0f);
    params["emission_color"] = Value(Vec3f(0, 1, 0));

    SurfaceClosure c = EvalStandardSurface(params);

    if (!Test_IsClose(c.baseColor, Vec3f(0.5f, 0.0f, 0.0f), 1e-4f)) {
        printf("    baseColor: (%f,%f,%f)\n", c.baseColor[0], c.baseColor[1], c.baseColor[2]);
        return false;
    }
    if (!Test_IsClose(c.roughness, 0.8f)) return false;
    if (!Test_IsClose(c.emissiveColor, Vec3f(0, 2, 0), 1e-4f)) return false;

    return true;
}

static bool
TestStandardSurfaceThinFilmParametersReachBsdf()
{
    ParamMap params;
    params["thin_film_thickness"] = Value(350.0f);
    params["thin_film_IOR"] = Value(1.8f);

    const SurfaceClosure c = EvalStandardSurface(params);
    const auto* dielectric = FindNodeIf<Bsdf::DielectricData>(
        c.bsdfTree,
        [](const Bsdf::DielectricData& data) {
            return data.scatterMode == Bsdf::ScatterMode::Reflection;
        });

    if (!dielectric) {
        printf("    Failed to find Standard Surface reflection dielectric node\n");
        return false;
    }

    return Test_IsClose(dielectric->thinFilmWeight, 1.0f) &&
           Test_IsClose(dielectric->thinFilmThickness, 350.0f, 1e-4f) &&
           Test_IsClose(dielectric->thinFilmIor, 1.8f, 1e-4f);
}

static bool
TestStandardSurfaceDispersionParametersReachBsdf()
{
    ParamMap params;
    params["transmission"] = Value(1.0f);
    params["transmission_dispersion"] = Value(18.0f);

    const SurfaceClosure c = EvalStandardSurface(params);
    const auto* transmission = FindNodeIf<Bsdf::DielectricData>(
        c.bsdfTree,
        [](const Bsdf::DielectricData& data) {
            return data.scatterMode == Bsdf::ScatterMode::Transmission;
        });

    if (!transmission) {
        printf("    Expected Standard Surface transmission lobe for "
               "dispersion\n");
        return false;
    }

    return Test_IsClose(transmission->dispersionAbbe, 18.0f, 1e-4f) &&
           c.HasDispersion();
}

static bool
TestStandardSurfaceVolumeParametersReachClosure()
{
    ParamMap params;
    params["transmission"] = Value(1.0f);
    params["transmission_color"] = Value(Vec3f(0.8f, 0.6f, 0.4f));
    params["transmission_depth"] = Value(2.0f);
    params["transmission_scatter"] = Value(Vec3f(0.1f, 0.2f, 0.5f));
    params["transmission_scatter_anisotropy"] = Value(0.35f);
    params["subsurface"] = Value(0.75f);
    params["subsurface_color"] = Value(Vec3f(0.25f, 0.5f, 0.75f));
    params["subsurface_radius"] = Value(Vec3f(1.0f, 2.0f, 4.0f));
    params["subsurface_scale"] = Value(2.0f);
    params["subsurface_anisotropy"] = Value(0.4f);

    const SurfaceClosure c = EvalStandardSurface(params);
    const MediumProperties expectedTransmission = MakeTransmissionMedium(
        1.0f,
        Vec3f(0.8f, 0.6f, 0.4f),
        2.0f,
        Vec3f(0.1f, 0.2f, 0.5f),
        0.35f);

    return c.hasInteriorMedium &&
           Test_IsClose(c.interiorMedium.absorption,
                        expectedTransmission.absorption, 1e-5f) &&
           Test_IsClose(c.interiorMedium.scattering,
                        expectedTransmission.scattering, 1e-5f) &&
           Test_IsClose(c.interiorMedium.anisotropy, 0.35f, 1e-5f) &&
           Test_IsClose(c.subsurfaceWeight, 0.75f, 1e-5f) &&
           Test_IsClose(c.subsurfaceColor, Vec3f(0.25f, 0.5f, 0.75f), 1e-5f) &&
           Test_IsClose(c.subsurfaceRadius, Vec3f(1.0f, 2.0f, 4.0f), 1e-5f) &&
           Test_IsClose(c.subsurfaceRadiusScale, Vec3f(2.0f), 1e-5f) &&
           Test_IsClose(c.subsurfaceAnisotropy, 0.4f, 1e-5f);
}

static bool
TestStandardSurfaceLayersSpecularOverTransmissionMix()
{
    // Partial transmission layers the coupled dielectric interface over the
    // diffuse substrate scaled by (1 - transmission).
    ParamMap params;
    params["transmission"] = Value(0.6f);
    params["coat"] = Value(0.0f);
    params["metalness"] = Value(0.0f);

    const SurfaceClosure c = EvalStandardSurface(params);
    const auto* root = c.bsdfTree.Get(c.bsdfTree.root);
    const auto* layer = root ? std::get_if<Bsdf::LayerData>(&root->data) : nullptr;
    if (!layer) {
        printf("    Expected Standard Surface root layer for specular stack\n");
        return false;
    }

    const auto* top = c.bsdfTree.Get(layer->top);
    const auto* reflection = top
        ? std::get_if<Bsdf::DielectricData>(&top->data)
        : nullptr;
    if (!reflection ||
        reflection->scatterMode != Bsdf::ScatterMode::Reflection) {
        printf("    Expected dielectric reflection layer on top\n");
        return false;
    }

    const auto* base = c.bsdfTree.Get(layer->base);
    const auto* mix = base ? std::get_if<Bsdf::MixData>(&base->data) : nullptr;
    if (!mix || !Test_IsClose(mix->mix, 0.6f, 1e-5f)) {
        printf("    Expected substrate/transmission mix of 0.6\n");
        return false;
    }

    const auto* substrate = c.bsdfTree.Get(mix->bg);
    const auto* transmission = c.bsdfTree.Get(mix->fg);
    return substrate && transmission &&
           std::holds_alternative<Bsdf::OrenNayarDiffuseData>(substrate->data) &&
           std::holds_alternative<Bsdf::DielectricData>(transmission->data) &&
           std::get<Bsdf::DielectricData>(transmission->data).scatterMode ==
               Bsdf::ScatterMode::Transmission;
}

static bool
TestStandardSurfaceThinWalledUsesUnitIorTransmission()
{
    ParamMap params;
    params["transmission"] = Value(1.0f);
    params["thin_walled"] = Value(true);
    params["specular_IOR"] = Value(1.5f);
    params["specular_roughness"] = Value(0.2f);
    params["coat"] = Value(0.0f);
    params["metalness"] = Value(0.0f);

    const SurfaceClosure c = EvalStandardSurface(params);
    const auto* transmission = FindNodeIf<Bsdf::DielectricData>(
        c.bsdfTree,
        [](const Bsdf::DielectricData& data) {
            return data.scatterMode == Bsdf::ScatterMode::Transmission;
        });

    if (!transmission) {
        printf("    Expected Standard Surface thin_walled to build "
               "dielectric transmission\n");
        return false;
    }
    if (!Test_IsClose(transmission->ior, 1.0f, 1e-4f)) {
        printf("    Expected Standard Surface thin_walled transmission IOR "
               "to be 1.0\n");
        return false;
    }
    if (FindNodeIf<Bsdf::DielectricInterfaceData>(
            c.bsdfTree,
            [](const Bsdf::DielectricInterfaceData&) { return true; })) {
        printf("    Standard Surface thin_walled should not use the coupled "
               "dielectric interface\n");
        return false;
    }

    // IOR-1 rough transmission must remain straight through. This is the
    // behavioral property that prevents thin-walled Standard Surface from
    // blurring the background.
    SurfaceClosure transmissionOnly;
    transmissionOnly.bsdfTree.root =
        transmissionOnly.bsdfTree.Add(*transmission);
    const Vec3f normalShdWldOut(0.0f, 1.0f, 0.0f);
    const Vec3f omegaOutWld = Vec3f(0.3f, 0.953939f, 0.0f).normalized();
    for (const Vec2f& u : {Vec2f(0.2f, 0.3f), Vec2f(0.7f, 0.8f)}) {
        const auto sample = TestSurfaceApi::SampleSurface(
            transmissionOnly, normalShdWldOut, omegaOutWld, u[0], u[1], 0.5f);
        if (sample.pdfSolidAngle <= 0.0f ||
            !Test_IsClose(sample.omegaInWld, -omegaOutWld, 1.0e-5f)) {
            printf("    Rough IOR-1 thin-wall transmission bent: "
                   "omegaInWld=(%f,%f,%f), expected=(%f,%f,%f), pdf=%f\n",
                   sample.omegaInWld[0], sample.omegaInWld[1],
                   sample.omegaInWld[2], -omegaOutWld[0], -omegaOutWld[1],
                   -omegaOutWld[2], sample.pdfSolidAngle);
            return false;
        }
    }

    return true;
}

static bool
TestOpenPbrThinWalledUsesCombinedInterface()
{
    ParamMap params;
    params["base_weight"] = Value(0.0f);
    params["base_color"] = Value(Vec3f(1.0f));
    params["base_metalness"] = Value(0.0f);
    params["specular_weight"] = Value(1.0f);
    params["specular_color"] = Value(Vec3f(1.0f));
    params["specular_roughness"] = Value(0.0f);
    params["specular_ior"] = Value(1.5f);
    params["transmission_weight"] = Value(1.0f);
    params["transmission_color"] = Value(Vec3f(0.7f, 1.0f, 0.8f));
    params["geometry_thin_walled"] = Value(true);

    const SurfaceClosure c = EvalOpenPbr(params);
    const auto* interface = FindNodeIf<Bsdf::DielectricInterfaceData>(
        c.bsdfTree,
        [](const Bsdf::DielectricInterfaceData& data) {
            return data.thinWalled && data.transmissionWeight > 0.0f;
        });

    if (!interface) {
        printf("    Expected OpenPBR thin_walled combined interface\n");
        return false;
    }

    const auto* transmissionOnly = FindNodeIf<Bsdf::DielectricData>(
        c.bsdfTree,
        [](const Bsdf::DielectricData& data) {
            return data.scatterMode == Bsdf::ScatterMode::Transmission;
        });
    if (transmissionOnly) {
        printf("    Thin-walled OpenPBR should not build transmission-only dielectric\n");
        return false;
    }

    return interface->compensateCoupledDielectric &&
           Test_IsClose(interface->ior, 1.5f, 1e-4f) &&
           Test_IsClose(interface->transmissionTint,
                        Vec3f(0.7f, 1.0f, 0.8f),
                        1e-4f);
}

static bool
TestStandardSurfaceSpecularRotationUsesCanonicalName()
{
    ParamMap params;
    params["specular_anisotropy"] = Value(0.7f);
    params["specular_rotation"] = Value(0.25f);
    params["normal"] = Value(Vec3f(0.0f, 0.0f, 1.0f));
    params["tangent"] = Value(Vec3f(1.0f, 0.0f, 0.0f));
    params["coat"] = Value(0.0f);

    const SurfaceClosure c = EvalStandardSurface(params);
    const auto* dielectric = FindNodeIf<Bsdf::DielectricData>(
        c.bsdfTree,
        [](const Bsdf::DielectricData& data) {
            return data.scatterMode == Bsdf::ScatterMode::Reflection;
        });

    if (!dielectric) {
        printf("    Failed to find Standard Surface reflection dielectric node\n");
        return false;
    }

    return Test_IsClose(dielectric->tangent, Vec3f(0.0f, -1.0f, 0.0f), 1e-4f);
}

static bool
TestStandardSurfaceMetalThinFilmChangesReflectionColor()
{
    ParamMap standardParams;
    standardParams["base"] = Value(1.0f);
    standardParams["base_color"] = Value(Vec3f(0.8f, 0.8f, 0.8f));
    standardParams["metalness"] = Value(1.0f);
    standardParams["specular"] = Value(1.0f);
    standardParams["specular_color"] = Value(Vec3f(1.0f));
    standardParams["specular_roughness"] = Value(0.3f);
    standardParams["specular_IOR"] = Value(1.5f);
    standardParams["thin_film_thickness"] = Value(250.0f);
    standardParams["thin_film_IOR"] = Value(1.6f);
    standardParams["coat"] = Value(0.0f);

    ParamMap openPbrParams;
    openPbrParams["base_weight"] = Value(1.0f);
    openPbrParams["base_color"] = Value(Vec3f(0.8f, 0.8f, 0.8f));
    openPbrParams["base_metalness"] = Value(1.0f);
    openPbrParams["specular_weight"] = Value(1.0f);
    openPbrParams["specular_color"] = Value(Vec3f(1.0f));
    openPbrParams["specular_roughness"] = Value(0.3f);
    openPbrParams["specular_ior"] = Value(1.5f);
    openPbrParams["thin_film_weight"] = Value(1.0f);
    openPbrParams["thin_film_thickness"] = Value(0.25f);
    openPbrParams["thin_film_ior"] = Value(1.6f);

    const SurfaceClosure standardClosure = EvalStandardSurface(standardParams);
    const SurfaceClosure openPbrClosure = EvalOpenPbr(openPbrParams);

    const Vec3f normalShdWldOut(0.0f, 1.0f, 0.0f);
    const Vec3f omegaOutWld = Vec3f(0.25f, 0.96f, 0.1f).normalized();
    const Vec3f omegaInWld = Vec3f(-0.1f, 0.99f, 0.05f).normalized();

    const Vec3f standardEval = TestSurfaceApi::EvalSurface(
        standardClosure, normalShdWldOut, omegaInWld, omegaOutWld);
    const Vec3f openPbrEval = TestSurfaceApi::EvalSurface(openPbrClosure, normalShdWldOut,
                                                omegaInWld, omegaOutWld);

    const bool chromatic =
        std::abs(standardEval[0] - standardEval[1]) > 1e-5f;
    const bool matchesOpenPbr =
        Test_IsClose(standardEval, openPbrEval, 5e-3f);
    if (!chromatic || !matchesOpenPbr) {
        printf(
            "    standard=(%f,%f,%f) openpbr=(%f,%f,%f)\n",
            standardEval[0], standardEval[1], standardEval[2],
            openPbrEval[0], openPbrEval[1], openPbrEval[2]);
    }

    return chromatic && matchesOpenPbr;
}

static bool
TestStandardSurfaceThinFilmUsesNanometerUnits()
{
    ParamMap nanometerParams;
    nanometerParams["base"] = Value(0.0f);
    nanometerParams["base_color"] = Value(Vec3f(1.0f));
    nanometerParams["metalness"] = Value(0.0f);
    nanometerParams["specular"] = Value(1.0f);
    nanometerParams["specular_color"] = Value(Vec3f(1.0f));
    nanometerParams["specular_roughness"] = Value(0.04f);
    nanometerParams["specular_IOR"] = Value(2.5f);
    nanometerParams["thin_film_thickness"] = Value(550.0f);
    nanometerParams["thin_film_IOR"] = Value(1.5f);

    ParamMap subNanometerParams;
    subNanometerParams["base"] = Value(0.0f);
    subNanometerParams["base_color"] = Value(Vec3f(1.0f));
    subNanometerParams["metalness"] = Value(0.0f);
    subNanometerParams["specular"] = Value(1.0f);
    subNanometerParams["specular_color"] = Value(Vec3f(1.0f));
    subNanometerParams["specular_roughness"] = Value(0.04f);
    subNanometerParams["specular_IOR"] = Value(2.5f);
    subNanometerParams["thin_film_thickness"] = Value(0.55f);
    subNanometerParams["thin_film_IOR"] = Value(1.5f);

    const SurfaceClosure nanometerClosure = EvalStandardSurface(nanometerParams);
    const SurfaceClosure subNanometerClosure =
        EvalStandardSurface(subNanometerParams);

    const Vec3f normalShdWldOut(0.0f, 1.0f, 0.0f);
    const Vec3f omegaOutWld = Vec3f(0.25f, 0.96f, 0.1f).normalized();
    const Vec3f omegaInWld = Vec3f(-0.1f, 0.99f, 0.05f).normalized();

    const Vec3f nanometerEval = TestSurfaceApi::EvalSurface(
        nanometerClosure, normalShdWldOut, omegaInWld, omegaOutWld);
    const Vec3f subNanometerEval = TestSurfaceApi::EvalSurface(
        subNanometerClosure, normalShdWldOut, omegaInWld, omegaOutWld);

    const bool nanometerChromatic =
        std::abs(nanometerEval[0] - nanometerEval[1]) > 1e-6f ||
        std::abs(nanometerEval[1] - nanometerEval[2]) > 1e-6f;
    const bool differsFromSubNanometer = !Test_IsClose(
        nanometerEval, subNanometerEval, 1e-6f);

    if (!nanometerChromatic || !differsFromSubNanometer) {
        printf(
            "    550nm=(%.8f,%.8f,%.8f) 0.55nm=(%.8f,%.8f,%.8f)\n",
            nanometerEval[0], nanometerEval[1], nanometerEval[2],
            subNanometerEval[0], subNanometerEval[1], subNanometerEval[2]);
        }

    return nanometerChromatic && differsFromSubNanometer;
}

static bool
TestStandardSurfaceCoatAffectRoughnessMatchesMaterialXGraph()
{
    ParamMap params;
    params["specular_roughness"] = Value(0.2f);
    params["transmission"] = Value(1.0f);
    params["transmission_extra_roughness"] = Value(0.1f);
    params["coat"] = Value(1.0f);
    params["coat_roughness"] = Value(0.6f);
    params["coat_affect_roughness"] = Value(1.0f);

    const SurfaceClosure c = EvalStandardSurface(params);
    const auto* reflection = FindNodeIf<Bsdf::DielectricData>(
        c.bsdfTree,
        [](const Bsdf::DielectricData& data) {
            return data.scatterMode == Bsdf::ScatterMode::Reflection;
        });
    const auto* transmission = FindNodeIf<Bsdf::DielectricData>(
        c.bsdfTree,
        [](const Bsdf::DielectricData& data) {
            return data.scatterMode == Bsdf::ScatterMode::Transmission;
        });

    if (!reflection || !transmission) {
        printf("    Failed to find Standard Surface dielectric nodes\n");
        return false;
    }

    const float affectedSpecular = 0.2f * 0.4f + 0.6f;
    const float affectedTransmission = 0.3f * 0.4f + 0.6f;
    const float expectedSpecularAlpha = affectedSpecular * affectedSpecular;
    const float expectedTransmissionAlpha =
        affectedTransmission * affectedTransmission;

    return Test_IsClose(reflection->roughness[0], expectedSpecularAlpha, 1e-4f) &&
           Test_IsClose(reflection->roughness[1], expectedSpecularAlpha, 1e-4f) &&
           Test_IsClose(transmission->roughness[0], expectedTransmissionAlpha, 1e-4f) &&
           Test_IsClose(transmission->roughness[1], expectedTransmissionAlpha, 1e-4f);
}

static bool
TestRegularizeTreatsDielectricRoughnessAsAlpha()
{
    SurfaceClosure c;
    c.roughness = 0.02f;

    Bsdf::DielectricData dielectric;
    dielectric.roughness = Vec2f(0.09f, 0.04f);
    dielectric.scatterMode = Bsdf::ScatterMode::Transmission;
    c.bsdfTree.root = c.bsdfTree.Add(dielectric);

    c.Regularize();

    const auto* regularized = FindNodeIf<Bsdf::DielectricData>(
        c.bsdfTree,
        [](const Bsdf::DielectricData&) { return true; });
    if (!regularized) {
        printf("    Failed to find regularized dielectric node\n");
        return false;
    }

    // 0.09 alpha corresponds to perceptual roughness 0.3, so it should stay.
    // 0.04 alpha corresponds to perceptual roughness 0.2, so it widens to 0.3,
    // i.e. alpha 0.09.
    return Test_IsClose(c.roughness, 0.1f, 1e-4f) &&
           Test_IsClose(regularized->roughness[0], 0.09f, 1e-4f) &&
           Test_IsClose(regularized->roughness[1], 0.09f, 1e-4f);
}

static bool
TestStandardSurfaceCoatColorSemanticsMatchMaterialX()
{
    const Vec3f baseColor(0.25f, 0.5f, 0.75f);
    const Vec3f coatColor(0.2f, 0.4f, 0.8f);
    const Vec3f emissionColor(0.5f, 0.25f, 0.125f);

    ParamMap params;
    params["base_color"] = Value(baseColor);
    params["specular"] = Value(0.0f);
    params["coat"] = Value(1.0f);
    params["coat_color"] = Value(coatColor);
    params["coat_affect_color"] = Value(0.5f);
    params["emission"] = Value(2.0f);
    params["emission_color"] = Value(emissionColor);

    const SurfaceClosure c = EvalStandardSurface(params);
    const auto* diffuse = FindNodeIf<Bsdf::OrenNayarDiffuseData>(
        c.bsdfTree,
        [](const Bsdf::OrenNayarDiffuseData&) {
            return true;
        });
    const auto* attenuation = FindNodeIf<Bsdf::MultiplyData>(
        c.bsdfTree,
        [&](const Bsdf::MultiplyData& data) {
            return Test_IsClose(data.weight, coatColor, 1e-4f);
        });
    const auto* coat = FindNodeIf<Bsdf::DielectricData>(
        c.bsdfTree,
        [](const Bsdf::DielectricData& data) {
            return data.scatterMode == Bsdf::ScatterMode::Reflection;
        });

    if (!diffuse || !attenuation || !coat) {
        printf("    Failed to find Standard Surface coat semantic nodes\n");
        return false;
    }

    const Vec3f expectedDiffuse(
        std::pow(baseColor[0], 1.5f),
        std::pow(baseColor[1], 1.5f),
        std::pow(baseColor[2], 1.5f));
    const Vec3f expectedEmission(
        emissionColor[0] * 2.0f * coatColor[0],
        emissionColor[1] * 2.0f * coatColor[1],
        emissionColor[2] * 2.0f * coatColor[2]);

    return Test_IsClose(diffuse->color, expectedDiffuse, 1e-4f) &&
           Test_IsClose(c.emissiveColor, expectedEmission, 1e-4f) &&
           Test_IsClose(coat->tint, Vec3f(1.0f), 1e-4f);
}

static bool
TestStandardSurfaceCoatNormalAndRotationReachBsdf()
{
    constexpr float roughness = 0.45f;
    constexpr float anisotropy = 0.8f;

    ParamMap params;
    params["specular"] = Value(0.0f);
    params["coat"] = Value(1.0f);
    params["coat_roughness"] = Value(roughness);
    params["coat_anisotropy"] = Value(anisotropy);
    params["coat_rotation"] = Value(0.25f);
    params["tangent"] = Value(Vec3f(1.0f, 0.0f, 0.0f));
    params["coat_normal"] = Value(Vec3f(0.0f, 1.0f, 0.0f));

    const SurfaceClosure c = EvalStandardSurface(params);
    const auto* coat = FindNodeIf<Bsdf::DielectricData>(
        c.bsdfTree,
        [](const Bsdf::DielectricData& data) {
            return data.scatterMode == Bsdf::ScatterMode::Reflection;
        });

    if (!coat) {
        printf("    Failed to find Standard Surface coat dielectric node\n");
        return false;
    }

    const float roughnessSquared = std::clamp(
        roughness * roughness,
        1.0e-5f,
        1.0f);
    const float aspect = std::sqrt(1.0f - std::clamp(anisotropy, 0.0f, 0.98f));
    const Vec2f expected(
        std::min(roughnessSquared / aspect, 1.0f),
        roughnessSquared * aspect);

    return coat->hasShadingNormal &&
           Test_IsClose(coat->normal, Vec3f(0.0f, 1.0f, 0.0f), 1e-4f) &&
           Test_IsClose(coat->tangent, Vec3f(0.0f, 0.0f, 1.0f), 1e-4f) &&
           Test_IsClose(coat->roughness[0], expected[0], 1e-4f) &&
           Test_IsClose(coat->roughness[1], expected[1], 1e-4f);
}

// ---------------------------------------------------------------------------
// OpenPBR
// ---------------------------------------------------------------------------

static bool
TestOpenPbrDefaults()
{
    ParamMap params;
    SurfaceClosure c = EvalOpenPbr(params);

    if (!Test_IsClose(c.baseColor, Vec3f(0.8f), 1e-4f)) return false;
    if (!Test_IsClose(c.roughness, 0.3f)) return false;
    if (!Test_IsClose(c.metallic, 0.0f)) return false;
    if (!Test_IsClose(c.specular, 1.0f)) return false;
    if (!Test_IsClose(c.specularColor, Vec3f(1.0f), 1e-4f)) return false;
    if (!Test_IsClose(c.specularIor, 1.5f)) return false;
    if (!Test_IsClose(c.transmission, 0.0f)) return false;
    if (!Test_IsClose(c.transmissionColor, Vec3f(1.0f), 1e-4f)) return false;
    if (c.hasInteriorMedium) return false;
    if (!c.interiorMedium.IsVacuum()) return false;
    if (!Test_IsClose(c.subsurfaceWeight, 0.0f)) return false;
    if (!Test_IsClose(c.subsurfaceColor, Vec3f(0.8f), 1e-4f)) return false;
    if (!Test_IsClose(c.subsurfaceRadius, Vec3f(1.0f), 1e-4f)) return false;
    if (!Test_IsClose(c.subsurfaceRadiusScale,
                      Vec3f(1.0f, 0.5f, 0.25f), 1e-4f)) return false;
    if (!Test_IsClose(c.subsurfaceAnisotropy, 0.0f)) return false;
    if (!Test_IsClose(c.coat, 0.0f)) return false;
    if (!Test_IsClose(c.coatRoughness, 0.0f)) return false;
    if (!Test_IsClose(c.coatIor, 1.6f)) return false;
    if (!Test_IsClose(c.sheen, 0.0f)) return false;
    if (!Test_IsClose(c.sheenColor, Vec3f(1.0f), 1e-4f)) return false;
    if (!Test_IsClose(c.sheenRoughness, 0.5f)) return false;
    if (!Test_IsClose(c.emissiveColor, Vec3f(0.0f), 1e-4f)) return false;
    if (!Test_IsClose(c.opacity, 1.0f)) return false;
    if (!Test_IsClose(c.presence, 1.0f)) return false;
    if (c.thinWalled) return false;
    if (!Test_IsClose(c.normal, Vec3f(0.0f, 0.0f, 1.0f), 1e-4f)) {
        return false;
    }
    if (!c.HasBsdfTree()) return false;

    return true;
}

static bool
TestOpenPbrBuildsLayeredDielectricBase()
{
    ParamMap params;
    params["base_metalness"] = Value(0.0f);
    params["transmission_weight"] = Value(0.0f);
    params["coat_weight"] = Value(0.0f);
    params["fuzz_weight"] = Value(0.0f);

    const SurfaceClosure c = EvalOpenPbr(params);
    const auto* root = c.bsdfTree.Get(c.bsdfTree.root);
    if (!root) {
        printf("    Missing OpenPBR root node\n");
        return false;
    }

    const auto* layer = std::get_if<Bsdf::LayerData>(&root->data);
    if (!layer) {
        printf("    Expected layered dielectric base at the root\n");
        return false;
    }

    const auto* top = c.bsdfTree.Get(layer->top);
    const auto* base = c.bsdfTree.Get(layer->base);
    if (!top || !base) {
        printf("    Missing layered OpenPBR child nodes\n");
        return false;
    }

    const auto* dielectric = std::get_if<Bsdf::DielectricData>(&top->data);
    if (!dielectric ||
        dielectric->scatterMode != Bsdf::ScatterMode::Reflection) {
        printf("    Expected dielectric reflection layer on top of base\n");
        return false;
    }

    if (!std::holds_alternative<Bsdf::OrenNayarDiffuseData>(base->data)) {
        printf("    Expected diffuse base under dielectric reflection\n");
        return false;
    }

    return true;
}

static bool
TestOpenPbrModulatesSubstrateIorForCoatAndSpecularWeight()
{
    {
        ParamMap params;
        params["specular_ior"] = Value(1.5f);
        params["specular_weight"] = Value(0.25f);
        params["coat_weight"] = Value(0.0f);

        const SurfaceClosure c = EvalOpenPbr(params);
        const auto* dielectric = FindNodeIf<Bsdf::DielectricData>(
            c.bsdfTree,
            [](const Bsdf::DielectricData& data) {
                return data.scatterMode == Bsdf::ScatterMode::Reflection;
            });
        const float expectedIor = 1.2222222f;
        if (!dielectric ||
            !Test_IsClose(dielectric->ior, expectedIor, 1e-4f) ||
            !Test_IsClose(dielectric->weight, 1.0f, 1e-4f)) {
            printf("    OpenPBR specular weight did not modulate substrate IOR\n");
            return false;
        }
    }

    {
        ParamMap params;
        params["specular_ior"] = Value(1.5f);
        params["specular_weight"] = Value(1.0f);
        params["coat_ior"] = Value(1.6f);
        params["coat_weight"] = Value(1.0f);

        const SurfaceClosure c = EvalOpenPbr(params);
        const auto* dielectric = FindNodeIf<Bsdf::DielectricData>(
            c.bsdfTree,
            [](const Bsdf::DielectricData& data) {
                return data.scatterMode == Bsdf::ScatterMode::Reflection &&
                       data.ior < 1.2f;
            });
        const float expectedIor = 1.6f / 1.5f;
        if (!dielectric ||
            !Test_IsClose(dielectric->ior, expectedIor, 1e-4f) ||
            !Test_IsClose(dielectric->weight, 1.0f, 1e-4f)) {
            printf("    OpenPBR coat did not use a relative substrate IOR\n");
            return false;
        }
    }

    {
        ParamMap params;
        params["specular_ior"] = Value(1.5f);
        params["specular_weight"] = Value(0.25f);
        params["coat_ior"] = Value(1.6f);
        params["coat_weight"] = Value(1.0f);
        params["transmission_weight"] = Value(0.5f);

        const SurfaceClosure c = EvalOpenPbr(params);
        const auto* interface = FindNodeIf<Bsdf::DielectricInterfaceData>(
            c.bsdfTree,
            [](const Bsdf::DielectricInterfaceData&) {
                return true;
            });
        const float relativeIor = 1.6f / 1.5f;
        const float epsilon = (relativeIor - 1.0f) / (relativeIor + 1.0f);
        const float modulatedEpsilon = std::sqrt(0.25f * epsilon * epsilon);
        const float expectedIor =
            (1.0f + modulatedEpsilon) / (1.0f - modulatedEpsilon);
        if (!interface ||
            !Test_IsClose(interface->ior, expectedIor, 1e-4f) ||
            !Test_IsClose(interface->reflectionWeight, 1.0f, 1e-4f)) {
            printf("    OpenPBR combined interface used the wrong effective IOR\n");
            return false;
        }
    }

    return true;
}

static bool
TestOpenPbrCoatRoughensSubstrateSpecular()
{
    constexpr float specularRoughness = 0.18f;
    constexpr float coatRoughness = 0.5f;
    const float coatAffectedRoughness = std::pow(
        std::min(
            1.0f,
            2.0f * std::pow(coatRoughness, 4.0f) +
                std::pow(specularRoughness, 4.0f)),
        0.25f);

    const auto checkRoughness =
        [&](float coatWeight, bool transmission) {
            ParamMap params;
            params["specular_roughness"] = Value(specularRoughness);
            params["coat_weight"] = Value(coatWeight);
            params["coat_roughness"] = Value(coatRoughness);
            if (transmission) {
                params["transmission_weight"] = Value(0.5f);
            }

            const SurfaceClosure c = EvalOpenPbr(params);
            Vec2f actual(0.0f);
            if (transmission) {
                const auto* interface =
                    FindNodeIf<Bsdf::DielectricInterfaceData>(
                        c.bsdfTree,
                        [](const Bsdf::DielectricInterfaceData&) {
                            return true;
                        });
                if (!interface) {
                    return false;
                }
                actual = interface->roughness;
            } else {
                const auto* dielectric = FindNodeIf<Bsdf::DielectricData>(
                    c.bsdfTree,
                    [](const Bsdf::DielectricData& data) {
                        return data.scatterMode ==
                                   Bsdf::ScatterMode::Reflection &&
                               data.ior < 1.5f;
                    });
                if (!dielectric) {
                    return false;
                }
                actual = dielectric->roughness;
            }

            const float effectiveRoughness =
                specularRoughness * (1.0f - coatWeight) +
                coatAffectedRoughness * coatWeight;
            const float expectedAlpha =
                effectiveRoughness * effectiveRoughness;
            return Test_IsClose(actual[0], expectedAlpha, 1e-4f) &&
                   Test_IsClose(actual[1], expectedAlpha, 1e-4f);
        };

    if (!checkRoughness(1.0f, false)) {
        printf("    Full OpenPBR coat did not roughen substrate specular\n");
        return false;
    }
    if (!checkRoughness(0.5f, false)) {
        printf("    Partial OpenPBR coat did not blend substrate roughness\n");
        return false;
    }
    if (!checkRoughness(1.0f, true)) {
        printf("    OpenPBR transmission interface missed coat roughening\n");
        return false;
    }
    return true;
}

static bool
TestOpenPbrMetalUsesF82TintSemantics()
{
    const Vec3f baseColor(0.7f, 0.45f, 0.2f);
    const Vec3f specularColor(1.0f, 0.75f, 0.5f);
    constexpr float baseWeight = 0.5f;
    constexpr float specularWeight = 0.8f;

    ParamMap params;
    params["base_weight"] = Value(baseWeight);
    params["base_color"] = Value(baseColor);
    params["base_metalness"] = Value(1.0f);
    params["specular_weight"] = Value(specularWeight);
    params["specular_color"] = Value(specularColor);

    const SurfaceClosure c = EvalOpenPbr(params);
    const auto* metal = FindNodeIf<Bsdf::GeneralizedSchlickData>(
        c.bsdfTree,
        [](const Bsdf::GeneralizedSchlickData& data) {
            return data.scatterMode == Bsdf::ScatterMode::Reflection;
        });

    if (!metal) {
        printf("    Failed to find OpenPBR metallic GeneralizedSchlick node\n");
        return false;
    }

    return Test_IsClose(metal->weight, specularWeight, 1e-4f) &&
           Test_IsClose(metal->color0, baseColor * baseWeight, 1e-4f) &&
           Test_IsClose(metal->color82, specularColor, 1e-4f) &&
           Test_IsClose(metal->color90, Vec3f(1.0f), 1e-4f);
}

static bool
TestOpenPbrTransmission()
{
    ParamMap params;
    params["transmission_weight"] = Value(0.7f);
    params["transmission_color"] = Value(Vec3f(0.8f, 0.9f, 1.0f));
    SurfaceClosure c = EvalOpenPbr(params);

    if (!Test_IsClose(c.transmission, 0.7f)) return false;
    if (!Test_IsClose(c.transmissionColor, Vec3f(0.8f, 0.9f, 1.0f), 1e-4f))
        return false;
    return true;
}

static bool
TestAdobeOpenPbrBuildsWholeBackendNode()
{
    ParamMap params;
    params["base_color"] = Value(Vec3f(0.25f, 0.5f, 0.75f));
    params["transmission_weight"] = Value(0.5f);
    params["transmission_color"] = Value(Vec3f(0.7f, 0.85f, 1.0f));
    params["transmission_depth"] = Value(0.25f);

    const SurfaceClosure closure = EvalAdobeOpenPbr(params);
#ifdef PXR_HDEMBREE_ENABLE_ADOBE_OPENPBR
    const SurfaceClosure visibilityClosure = EvalAdobeOpenPbrVisibility(params);
    const auto* root = closure.bsdfTree.Get(closure.bsdfTree.root);
    if (!root ||
        !std::holds_alternative<Bsdf::AdobeOpenPbrData>(root->data)) {
        printf("    Expected Adobe OpenPBR whole-backend root node\n");
        return false;
    }
    if (!Test_IsClose(closure.baseColor, Vec3f(0.25f, 0.5f, 0.75f), 1e-4f)) {
        return false;
    }
    if (visibilityClosure.HasBsdfTree() ||
        !Test_IsClose(visibilityClosure.transmission, 0.5f, 1e-4f)) {
        printf("    Expected lightweight Adobe OpenPBR visibility closure\n");
        return false;
    }
    return closure.hasInteriorMedium &&
           closure.interiorMedium.transportModel ==
               MediumTransportModel::AdobeOpenPBR &&
           visibilityClosure.hasInteriorMedium &&
           visibilityClosure.interiorMedium.transportModel ==
               MediumTransportModel::AdobeOpenPBR &&
           visibilityClosure.interiorMedium.adobeOpenPbrVolume.valid;
#else
    return closure.HasBsdfTree();
#endif
}

static bool
TestAdobeOpenPbrEvalPdfSurfaceMatchesSeparateCalls()
{
    ParamMap params;
    params["base_color"] = Value(Vec3f(0.25f, 0.5f, 0.75f));
    params["specular_roughness"] = Value(0.35f);
    params["transmission_weight"] = Value(0.25f);

    const SurfaceClosure closure = EvalAdobeOpenPbr(params);
    const Vec3f normalShdWldOut(0.0f, 0.0f, 1.0f);
    Vec3f omegaInWld(0.2f, -0.1f, 0.97f);
    omegaInWld.normalize();
    const Vec3f omegaOutWld(0.0f, 0.0f, 1.0f);

    const AdobeOpenPbrEvalPdfResult combined = TryEvalPdfAdobeOpenPbrSurface(closure, _MakeSurfaceInteraction(normalShdWldOut, normalShdWldOut, normalShdWldOut, omegaOutWld, 0.0f, true), omegaInWld);
#ifdef PXR_HDEMBREE_ENABLE_ADOBE_OPENPBR
    if (!combined.evaluated) {
        printf("    Expected combined Adobe OpenPBR eval/pdf path\n");
        return false;
    }

    const auto* root = closure.bsdfTree.Get(closure.bsdfTree.root);
    const auto* data = root
        ? std::get_if<Bsdf::AdobeOpenPbrData>(&root->data)
        : nullptr;
    if (!data) {
        printf("    Expected Adobe OpenPBR backend data\n");
        return false;
    }

    const Vec3f separateValue =
        EvalAdobeOpenPbr(
            *data, normalShdWldOut, normalShdWldOut, omegaInWld,
            omegaOutWld);
    const float separatePdf =
        PdfAdobeOpenPbr(*data, normalShdWldOut, omegaInWld, omegaOutWld);
    const AdobeOpenPbrPreparedSurface prepared =
        PrepareAdobeOpenPbrSurface(closure, _MakeSurfaceInteraction(normalShdWldOut, normalShdWldOut, normalShdWldOut, omegaOutWld, 0.0f, true));
    const AdobeOpenPbrEvalPdfResult preparedEvalPdf =
        EvalPdfPreparedAdobeOpenPbrSurface(prepared, omegaInWld);
    const Bsdf::BsdfSample separateSample = SampleAdobeOpenPbr(*data, normalShdWldOut, _MakeSurfaceInteraction(normalShdWldOut, normalShdWldOut, normalShdWldOut, omegaOutWld, 0.0f, true), 0.23f, 0.47f, 0.61f);
    const Bsdf::BsdfSample preparedSample =
        SamplePreparedAdobeOpenPbrSurface(prepared, 0.23f, 0.47f, 0.61f);

    return prepared.valid &&
           Test_IsClose(combined.value, separateValue, 1e-5f) &&
           Test_IsClose(combined.pdfSolidAngle, separatePdf, 1e-5f) &&
           Test_IsClose(preparedEvalPdf.value, separateValue, 1e-5f) &&
           Test_IsClose(preparedEvalPdf.pdfSolidAngle, separatePdf, 1e-5f) &&
           Test_IsClose(preparedSample.omegaInWld, separateSample.omegaInWld,
                        1e-5f) &&
           Test_IsClose(preparedSample.bsdfValue, separateSample.bsdfValue,
                        1e-5f) &&
           Test_IsClose(preparedSample.pdfSolidAngle,
                        separateSample.pdfSolidAngle, 1e-5f) &&
           preparedSample.isSpecular == separateSample.isSpecular &&
           Test_IsClose(preparedSample.eta, separateSample.eta, 1e-5f);
#else
    return !combined.evaluated;
#endif
}

static bool
TestAdobeOpenPbrSampleSoftensOnlyDiffuseComponent()
{
#ifndef PXR_HDEMBREE_ENABLE_ADOBE_OPENPBR
    return true;
#else
    ParamMap params;
    params["base_color"] = Value(Vec3f(0.7f, 0.5f, 0.3f));
    params["base_weight"] = Value(1.0f);
    params["specular_weight"] = Value(0.6f);
    params["specular_roughness"] = Value(0.45f);
    const SurfaceClosure closure = EvalAdobeOpenPbr(params);
    const Vec3f normalShdWldOut(0.0f, 0.0f, 1.0f);
    const Vec3f normalSrfWldOut =
        Vec3f(0.6f, 0.0f, 0.8f).normalized();
    const Vec3f omegaOutWld(0.0f, 0.0f, 1.0f);
    const AdobeOpenPbrPreparedSurface prepared =
        PrepareAdobeOpenPbrSurface(closure, _MakeSurfaceInteraction(normalShdWldOut, normalSrfWldOut, normalShdWldOut, omegaOutWld, 0.0f, true));
    ParamMap specularParams = params;
    specularParams["base_weight"] = Value(0.0f);
    const SurfaceClosure specularClosure =
        EvalAdobeOpenPbr(specularParams);
    const AdobeOpenPbrPreparedSurface specularPrepared =
        PrepareAdobeOpenPbrSurface(specularClosure, _MakeSurfaceInteraction(normalShdWldOut, normalSrfWldOut, normalShdWldOut, omegaOutWld, 0.0f, true));
    for (int i = 0; i < 4096; ++i) {
        const float u1 = (static_cast<float>(i) + 0.5f) / 4096.0f;
        const float u2 = std::fmod(0.61803398875f * i, 1.0f);
        const float uLobe = std::fmod(0.41421356237f * i, 1.0f);
        const Bsdf::BsdfSample sample =
            SamplePreparedAdobeOpenPbrSurface(prepared, u1, u2, uLobe);
        const float bumpShadowing = Bsdf::detail::BumpShadowingTerm(
            normalSrfWldOut, normalShdWldOut, sample.omegaInWld, true,
            Bsdf::detail::BumpShadowingContext::Sampling);
        if (!sample.isDiffuseLike || sample.pdfSolidAngle <= 0.0f ||
            !Bsdf::detail::BumpHemisphereAgreement(
                normalSrfWldOut, normalShdWldOut, sample.omegaInWld) ||
            bumpShadowing <= 0.0f || bumpShadowing >= 0.99f) {
            continue;
        }
        const AdobeOpenPbrEvalPdfResult evaluated =
            EvalPdfPreparedAdobeOpenPbrSurface(
                prepared, sample.omegaInWld);
        const AdobeOpenPbrEvalPdfResult specularEvaluated =
            EvalPdfPreparedAdobeOpenPbrSurface(
                specularPrepared, sample.omegaInWld);
        return evaluated.value.length() > 0.0f &&
            specularEvaluated.value.length() > 1e-5f &&
            Test_IsClose(sample.bsdfValue, evaluated.value, 1e-4f);
    }
    printf("    Failed to find mixed Adobe diffuse sample\n");
    return false;
#endif
}

static bool
TestAdobeOpenPbrPureSubsurfaceUsesRandomWalkPayload()
{
    ParamMap params;
    params["base_color"] = Value(Vec3f(0.9f, 0.55f, 0.45f));
    params["specular_weight"] = Value(0.0f);
    params["transmission_weight"] = Value(0.0f);
    params["subsurface_weight"] = Value(1.0f);
    params["subsurface_color"] = Value(Vec3f(0.8f, 0.45f, 0.25f));
    params["subsurface_radius"] = Value(0.25f);
    params["subsurface_radius_scale"] =
        Value(Vec3f(1.0f, 0.5f, 0.25f));
    params["subsurface_scatter_anisotropy"] = Value(0.2f);
    params["geometry_thin_walled"] = Value(false);

    SurfaceClosure closure = EvalAdobeOpenPbr(params);
#ifdef PXR_HDEMBREE_ENABLE_ADOBE_OPENPBR
    const SurfaceClosure visibilityClosure = EvalAdobeOpenPbrVisibility(params);
    if (!closure.HasSubsurfaceScattering()) {
        printf("    Expected Adobe OpenPBR pure SSS summary fields\n");
        return false;
    }
    if (closure.hasInteriorMedium || !closure.interiorMedium.IsVacuum()) {
        printf("    Pure SSS should not activate Adobe interior medium\n");
        return false;
    }
    if (!closure.hasPrecomputedSubsurfaceMedium ||
        closure.precomputedSubsurfaceMedium.IsVacuum()) {
        printf("    Expected Adobe SSS precomputed medium coefficients\n");
        return false;
    }
    if (closure.precomputedSubsurfaceMedium.transportModel !=
        MediumTransportModel::AdobeOpenPBR) {
        printf("    Expected Adobe-origin SSS coefficients\n");
        return false;
    }
    if (!closure.precomputedSubsurfaceMedium.adobeOpenPbrVolume.valid) {
        printf("    Expected cached Adobe homogeneous volume payload\n");
        return false;
    }
    const Vec3f expectedExtinction =
        ExpectedAdobeOpenPbrSubsurfaceExtinction(
            0.25f, Vec3f(1.0f, 0.5f, 0.25f));
    const Vec3f expectedAlbedo =
        ExpectedAdobeOpenPbrSubsurfaceAlbedo(
            Vec3f(0.8f, 0.45f, 0.25f), 0.2f);
    const Vec3f expectedScattering =
        CompMul(expectedExtinction, expectedAlbedo);
    const Vec3f expectedAbsorption =
        CompMul(expectedExtinction, Vec3f(1.0f) - expectedAlbedo);
    if (!Test_IsClose(closure.precomputedSubsurfaceMedium.adobeOpenPbrVolume
                          .extinctionCoefficient,
                      expectedExtinction, 1e-4f) ||
        !Test_IsClose(
            closure.precomputedSubsurfaceMedium.adobeOpenPbrVolume.albedo,
            expectedAlbedo, 1e-4f) ||
        !Test_IsClose(closure.precomputedSubsurfaceMedium.scattering,
                      expectedScattering, 1e-4f) ||
        !Test_IsClose(closure.precomputedSubsurfaceMedium.absorption,
                      expectedAbsorption, 1e-4f)) {
        printf("    Expected Adobe van-de-Hulst SSS volume coefficients\n");
        return false;
    }
    if (!Test_IsClose(
            closure.precomputedSubsurfaceMedium.anisotropy, 0.2f, 1e-5f)) {
        printf("    Expected Adobe SSS anisotropy to reach payload\n");
        return false;
    }
    if (visibilityClosure.hasInteriorMedium ||
        !visibilityClosure.interiorMedium.IsVacuum()) {
        printf("    Pure SSS visibility closure should not Beer-attenuate shadows\n");
        return false;
    }

    const Vec3f normalShdWldOut(0.0f, 0.0f, 1.0f);
    const Vec3f normalShdLobeWldOut =
        Vec3f(0.3f, 0.0f, 0.9539392f).normalized();
    const Vec3f omegaOutWld(0.0f, 0.0f, 1.0f);
    Bsdf::AdobeOpenPbrData* data = std::get_if<Bsdf::AdobeOpenPbrData>(
        &closure.bsdfTree.nodes[closure.bsdfTree.root].data);
    if (!data) {
        return false;
    }
    data->hasShadingNormal = true;
    data->normal = normalShdLobeWldOut;
    const AdobeOpenPbrPreparedSurface prepared =
        PrepareAdobeOpenPbrSurface(closure, _MakeSurfaceInteraction(normalShdWldOut, normalShdWldOut, normalShdWldOut, omegaOutWld, 0.0f, true));
    const Bsdf::BsdfSample sample =
        SamplePreparedAdobeOpenPbrSurface(prepared, 0.23f, 0.47f, 0.61f);
    Vec3f dirEntryWld;
    const bool entryValid = Bsdf::SampleSubsurfaceEntry(
        closure, sample.normalShdLobeWldOut, normalShdWldOut,
        omegaOutWld, 0.23f, 0.47f, dirEntryWld);
    if (!prepared.valid || !sample.isSubsurface ||
        !Test_IsClose(
            sample.normalShdLobeWldOut, normalShdLobeWldOut, 1e-5f) ||
        !entryValid) {
        printf("    Expected pure Adobe SSS to synthesize subsurface marker\n");
        return false;
    }
    if (!Test_IsClose(sample.bsdfValue, Vec3f(1.0f), 1e-5f)) {
        return false;
    }

    // A backend-generated SSS entry must be inward relative to both its lobe
    // and the true geometric surface.
    ParamMap generatedParams = params;
    generatedParams["specular_weight"] = Value(1.0f);
    const SurfaceClosure generatedClosure =
        EvalAdobeOpenPbr(generatedParams);
    const Vec3f hostileNormalGeomWldOut =
        Vec3f(0.9987492f, 0.0f, 0.05f).normalized();
    const AdobeOpenPbrPreparedSurface acceptedPrepared =
        PrepareAdobeOpenPbrSurface(generatedClosure, _MakeSurfaceInteraction(normalShdWldOut, normalShdWldOut, normalShdWldOut, omegaOutWld, 0.0f, true));
    const AdobeOpenPbrPreparedSurface rejectedPrepared =
        PrepareAdobeOpenPbrSurface(generatedClosure, _MakeSurfaceInteraction(normalShdWldOut, normalShdWldOut, hostileNormalGeomWldOut, omegaOutWld, 0.0f, true));
    for (int i = 0; i < 4096; ++i) {
        const float u1 = (static_cast<float>(i) + 0.5f) / 4096.0f;
        const float u2 = std::fmod(0.61803398875f * i, 1.0f);
        const float uLobe = std::fmod(0.41421356237f * i, 1.0f);
        const Bsdf::BsdfSample accepted =
            SamplePreparedAdobeOpenPbrSurface(
                acceptedPrepared, u1, u2, uLobe);
        if (!accepted.isSubsurface ||
            !accepted.hasSubsurfaceEntryDirection ||
            Dot(hostileNormalGeomWldOut, accepted.omegaInWld) < 0.0f) {
            continue;
        }
        const Bsdf::BsdfSample rejected =
            SamplePreparedAdobeOpenPbrSurface(
                rejectedPrepared, u1, u2, uLobe);
        return rejected.pdfSolidAngle == 0.0f &&
            rejected.bsdfValue.length() == 0.0f;
    }
    printf("    Failed to exercise Adobe generated-entry rejection\n");
    return false;
#else
    return !closure.hasPrecomputedSubsurfaceMedium;
#endif
}

static bool
TestOpenPbrRegularVolumeDoesNotDoubleTintTransmission()
{
    const Vec3f tint(0.8f, 0.6f, 0.4f);

    ParamMap regularVolumeParams;
    regularVolumeParams["transmission_weight"] = Value(1.0f);
    regularVolumeParams["transmission_color"] = Value(tint);
    regularVolumeParams["transmission_depth"] = Value(0.5f);
    regularVolumeParams["geometry_thin_walled"] = Value(false);

    const SurfaceClosure regularVolume = EvalOpenPbr(regularVolumeParams);
    const auto* regularTransmission =
        FindNodeIf<Bsdf::DielectricInterfaceData>(
        regularVolume.bsdfTree,
        [](const Bsdf::DielectricInterfaceData& data) {
            return data.transmissionWeight > 0.0f;
        });

    if (!regularVolume.hasInteriorMedium || !regularTransmission) {
        printf("    Expected OpenPBR regular transmission volume\n");
        return false;
    }
    if (!Test_IsClose(
            regularTransmission->transmissionTint, Vec3f(1.0f), 1e-4f)) {
        printf("    Expected regular volume transmission interface tint to be white\n");
        return false;
    }

    ParamMap zeroDepthParams;
    zeroDepthParams["transmission_weight"] = Value(1.0f);
    zeroDepthParams["transmission_color"] = Value(tint);
    zeroDepthParams["transmission_depth"] = Value(0.0f);
    zeroDepthParams["geometry_thin_walled"] = Value(false);

    const SurfaceClosure zeroDepth = EvalOpenPbr(zeroDepthParams);
    const auto* zeroDepthTransmission =
        FindNodeIf<Bsdf::DielectricInterfaceData>(
        zeroDepth.bsdfTree,
        [](const Bsdf::DielectricInterfaceData& data) {
            return data.transmissionWeight > 0.0f;
        });

    if (zeroDepth.hasInteriorMedium || !zeroDepthTransmission) {
        printf("    Expected OpenPBR zero-depth transmission tint path\n");
        return false;
    }

    return Test_IsClose(
        zeroDepthTransmission->transmissionTint, tint, 1e-4f);
}

static bool
TestOpenPbrUsesCombinedInterfaceForThickTransmission()
{
    ParamMap params;
    params["transmission_weight"] = Value(0.7f);
    params["geometry_thin_walled"] = Value(false);
    params["coat_weight"] = Value(0.0f);
    params["fuzz_weight"] = Value(0.0f);

    const SurfaceClosure c = EvalOpenPbr(params);
    const auto* root = c.bsdfTree.Get(c.bsdfTree.root);
    const auto* layer =
        root ? std::get_if<Bsdf::LayerData>(&root->data) : nullptr;
    if (!layer) {
        printf("    Expected OpenPBR root layer for transparent interface\n");
        return false;
    }

    const auto* top = c.bsdfTree.Get(layer->top);
    const auto* interface =
        top ? std::get_if<Bsdf::DielectricInterfaceData>(&top->data) : nullptr;
    if (!interface) {
        printf("    Expected combined dielectric interface as layer top\n");
        return false;
    }

    if (!Test_IsClose(interface->reflectionWeight, 1.0f, 1e-4f) ||
        !Test_IsClose(interface->transmissionWeight, 0.7f, 1e-4f)) {
        printf("    Unexpected combined interface weights\n");
        return false;
    }

    const auto* transmissionOnly = FindNodeIf<Bsdf::DielectricData>(
        c.bsdfTree,
        [](const Bsdf::DielectricData& data) {
            return data.scatterMode == Bsdf::ScatterMode::Transmission;
        });
    if (transmissionOnly) {
        printf("    Thick OpenPBR should not build transmission-only dielectric\n");
        return false;
    }

    const auto* base = c.bsdfTree.Get(layer->base);
    if (!base || !std::holds_alternative<Bsdf::MultiplyData>(base->data)) {
        printf("    Expected opaque substrate to be weighted below interface\n");
        return false;
    }

    return true;
}

static bool
TestOpenPbrCoatDarkeningReachesBaseSubstrate()
{
    ParamMap params;
    params["base_color"] = Value(Vec3f(0.8f, 0.4f, 0.2f));
    params["base_metalness"] = Value(0.0f);
    params["specular_weight"] = Value(1.0f);
    params["subsurface_weight"] = Value(0.0f);
    params["coat_weight"] = Value(1.0f);
    params["coat_darkening"] = Value(1.0f);
    params["coat_ior"] = Value(1.6f);
    params["fuzz_weight"] = Value(0.0f);

    const SurfaceClosure c = EvalOpenPbr(params);
    const auto* multiply = FindNodeIf<Bsdf::MultiplyData>(
        c.bsdfTree,
        [](const Bsdf::MultiplyData& data) {
            return std::abs(data.weight[0] - 1.0f) > 1.0e-6f ||
                   std::abs(data.weight[1] - 1.0f) > 1.0e-6f ||
                   std::abs(data.weight[2] - 1.0f) > 1.0e-6f;
        });

    if (!multiply) {
        printf("    Expected non-identity substrate multiply for coat darkening\n");
        return false;
    }

    const float coatF0 = std::pow((1.6f - 1.0f) / (1.6f + 1.0f), 2.0f);
    const float kCoat = 1.0f - (1.0f - coatF0) / (1.6f * 1.6f);
    const Vec3f eBase(0.8f, 0.4f, 0.2f);
    const Vec3f expected(
        (1.0f - kCoat) / (1.0f - eBase[0] * kCoat),
        (1.0f - kCoat) / (1.0f - eBase[1] * kCoat),
        (1.0f - kCoat) / (1.0f - eBase[2] * kCoat));

    return Test_IsClose(multiply->weight, expected, 1e-4f);
}

static bool
TestOpenPbrCoatColorAttenuatesSubstrate()
{
    const Vec3f expectedCoatColor(0.25f, 0.5f, 0.75f);

    ParamMap params;
    params["coat_weight"] = Value(1.0f);
    params["coat_darkening"] = Value(0.0f);
    params["coat_color"] = Value(expectedCoatColor);
    params["fuzz_weight"] = Value(0.0f);

    const SurfaceClosure c = EvalOpenPbr(params);
    const auto* attenuation = FindNodeIf<Bsdf::MultiplyData>(
        c.bsdfTree,
        [&](const Bsdf::MultiplyData& data) {
            return Test_IsClose(data.weight, expectedCoatColor, 1e-4f);
        });
    if (!attenuation) {
        printf("    Expected coat_color attenuation multiply on substrate\n");
        return false;
    }

    const auto* coat = FindNodeIf<Bsdf::DielectricData>(
        c.bsdfTree,
        [](const Bsdf::DielectricData& data) {
            return data.scatterMode == Bsdf::ScatterMode::Reflection &&
                   std::abs(data.ior - 1.6f) < 1e-4f;
        });
    if (!coat) {
        printf("    Expected coat dielectric node\n");
        return false;
    }

    return Test_IsClose(coat->tint, Vec3f(1.0f), 1e-4f);
}

static bool
TestOpenPbrThinFilmParametersReachBsdf()
{
    ParamMap params;
    params["thin_film_weight"] = Value(0.25f);
    params["thin_film_thickness"] = Value(0.4f);
    params["thin_film_ior"] = Value(1.7f);

    const SurfaceClosure c = EvalOpenPbr(params);
    const auto* dielectric = FindNodeIf<Bsdf::DielectricData>(
        c.bsdfTree,
        [](const Bsdf::DielectricData& data) {
            return data.scatterMode == Bsdf::ScatterMode::Reflection;
        });

    if (!dielectric) {
        printf("    Failed to find OpenPBR reflection dielectric node\n");
        return false;
    }

    return Test_IsClose(dielectric->thinFilmWeight, 0.25f) &&
           Test_IsClose(dielectric->thinFilmThickness, 400.0f, 1e-4f) &&
           Test_IsClose(dielectric->thinFilmIor, 1.7f, 1e-4f);
}

static bool
TestOpenPbrDispersionParametersReachBsdf()
{
    ParamMap params;
    params["transmission_weight"] = Value(1.0f);
    params["transmission_dispersion_scale"] = Value(0.5f);
    params["transmission_dispersion_abbe_number"] = Value(20.0f);

    const SurfaceClosure c = EvalOpenPbr(params);
    const auto* interface = FindNodeIf<Bsdf::DielectricInterfaceData>(
        c.bsdfTree,
        [](const Bsdf::DielectricInterfaceData& data) {
            return data.transmissionWeight > 0.0f;
        });

    if (!interface) {
        printf("    Expected OpenPBR dielectric interface for dispersion\n");
        return false;
    }

    return Test_IsClose(interface->dispersionAbbe, 40.0f, 1e-4f) &&
           c.HasDispersion();
}

static bool
TestOpenPbrVolumeParametersReachClosure()
{
    ParamMap params;
    params["transmission_weight"] = Value(1.0f);
    params["transmission_color"] = Value(Vec3f(0.9f, 0.8f, 0.7f));
    params["transmission_depth"] = Value(0.5f);
    params["transmission_scatter"] = Value(Vec3f(0.02f, 0.03f, 0.04f));
    params["transmission_scatter_anisotropy"] = Value(-0.25f);
    params["subsurface_weight"] = Value(0.6f);
    params["subsurface_color"] = Value(Vec3f(0.4f, 0.3f, 0.2f));
    params["subsurface_radius"] = Value(Vec3f(3.0f, 2.0f, 1.0f));
    params["subsurface_radius_scale"] = Value(Vec3f(1.0f, 0.5f, 0.25f));
    params["subsurface_scatter_anisotropy"] = Value(0.2f);

    const SurfaceClosure c = EvalOpenPbr(params);
    const MediumProperties expectedTransmission = MakeTransmissionMedium(
        1.0f,
        Vec3f(0.9f, 0.8f, 0.7f),
        0.5f,
        Vec3f(0.02f, 0.03f, 0.04f),
        -0.25f);

    return c.hasInteriorMedium &&
           Test_IsClose(c.interiorMedium.absorption,
                        expectedTransmission.absorption, 1e-5f) &&
           Test_IsClose(c.interiorMedium.scattering,
                        expectedTransmission.scattering, 1e-5f) &&
           Test_IsClose(c.interiorMedium.anisotropy, -0.25f, 1e-5f) &&
           Test_IsClose(c.subsurfaceWeight, 0.6f, 1e-5f) &&
           Test_IsClose(c.subsurfaceRadiusScale, Vec3f(1.0f, 0.5f, 0.25f),
                        1e-5f) &&
           Test_IsClose(c.subsurfaceAnisotropy, 0.2f, 1e-5f);
}

static bool
TestTransmissionMediumShiftsNegativeAbsorptionLikeMaterialX()
{
    const MediumProperties medium = MakeTransmissionMedium(
        1.0f,
        Vec3f(0.98f, 0.99f, 1.0f),
        1.0f,
        Vec3f(0.5f, 0.5f, 0.5f),
        0.0f);

    const Vec3f extinction(
        -std::log(0.98f),
        -std::log(0.99f),
        -std::log(1.0f - 1.0e-6f));
    const Vec3f rawAbsorption = extinction - Vec3f(0.5f);
    const float minAbsorption = std::min(
        {rawAbsorption[0], rawAbsorption[1], rawAbsorption[2]});
    const Vec3f expected = rawAbsorption - Vec3f(minAbsorption);

    return Test_IsClose(medium.absorption[0], expected[0], 1e-5f) &&
           Test_IsClose(medium.absorption[1], expected[1], 1e-5f) &&
           Test_IsClose(medium.absorption[2], expected[2], 1e-5f);
}

static bool
TestDisneyPrincipledSubsurfaceBuildsMediumState()
{
    const Vec3f baseColor(0.86f, 0.53f, 0.45f);
    const Vec3f subsurfaceDistance(0.7f, 1.6f, 3.0f);

    ParamMap params;
    params["baseColor"] = Value(baseColor);
    params["subsurface"] = Value(0.85f);
    params["subsurfaceDistance"] = Value(subsurfaceDistance);

    const SurfaceClosure c = EvalDisneyPrincipled(params);

    return Test_IsClose(c.subsurfaceWeight, 0.85f, 1e-5f) &&
           Test_IsClose(c.subsurfaceColor, baseColor, 1e-5f) &&
           Test_IsClose(c.subsurfaceRadius, subsurfaceDistance, 1e-5f) &&
           Test_IsClose(c.subsurfaceRadiusScale, Vec3f(1.0f), 1e-5f) &&
           Test_IsClose(c.subsurfaceAnisotropy, 0.0f, 1e-5f);
}

static bool
TestOpenPbrBaseDiffuseRoughnessUsesCanonicalName()
{
    ParamMap params;
    params["base_diffuse_roughness"] = Value(0.65f);
    params["base_roughness"] = Value(0.1f);

    const SurfaceClosure c = EvalOpenPbr(params);
    const auto* diffuse = FindNodeIf<Bsdf::OrenNayarDiffuseData>(
        c.bsdfTree,
        [](const Bsdf::OrenNayarDiffuseData&) {
            return true;
        });

    if (!diffuse) {
        printf("    Failed to find OpenPBR diffuse node\n");
        return false;
    }

    return Test_IsClose(diffuse->roughness, 0.65f, 1e-4f);
}

static bool
TestOpenPbrSpecularRoughnessAnisotropyUsesCanonicalName()
{
    constexpr float roughness = 0.45f;
    constexpr float anisotropy = 0.75f;

    ParamMap params;
    params["specular_roughness"] = Value(roughness);
    params["specular_roughness_anisotropy"] = Value(anisotropy);
    params["specular_anisotropy"] = Value(0.0f);

    const SurfaceClosure c = EvalOpenPbr(params);
    const auto* dielectric = FindNodeIf<Bsdf::DielectricData>(
        c.bsdfTree,
        [](const Bsdf::DielectricData& data) {
            return data.scatterMode == Bsdf::ScatterMode::Reflection;
        });

    if (!dielectric) {
        printf("    Failed to find OpenPBR reflection dielectric node\n");
        return false;
    }

    const float clampedRoughness = std::clamp(roughness, 0.001f, 1.0f);
    const float clampedAnisotropy = std::clamp(anisotropy, 0.0f, 1.0f);
    const float alphaRoughness = clampedRoughness * clampedRoughness;
    const float oneMinusAnisotropy = 1.0f - clampedAnisotropy;
    const float alphaX = alphaRoughness * std::sqrt(
        2.0f /
        std::max(oneMinusAnisotropy * oneMinusAnisotropy + 1.0f, 1.0e-6f));
    const float alphaY = oneMinusAnisotropy * alphaX;
    const Vec2f expected(
        std::clamp(alphaX, 1.0e-5f, 1.0f),
        std::clamp(alphaY, 1.0e-5f, 1.0f));

    return Test_IsClose(dielectric->roughness[0], expected[0], 1e-4f) &&
           Test_IsClose(dielectric->roughness[1], expected[1], 1e-4f);
}

static bool
TestOpenPbrGeometryInputsUseCanonicalNames()
{
    const Vec3f expectedNormal(0.25f, 0.5f, 0.75f);
    const Vec3f expectedTangent(0.0f, 1.0f, 0.0f);

    ParamMap params;
    params["geometry_normal"] = Value(expectedNormal);
    params["normal"] = Value(Vec3f(0.0f, 0.0f, 1.0f));
    params["geometry_tangent"] = Value(expectedTangent);
    params["tangent"] = Value(Vec3f(1.0f, 0.0f, 0.0f));

    const SurfaceClosure c = EvalOpenPbr(params);
    const auto* dielectric = FindNodeIf<Bsdf::DielectricData>(
        c.bsdfTree,
        [](const Bsdf::DielectricData& data) {
            return data.scatterMode == Bsdf::ScatterMode::Reflection;
        });

    if (!dielectric) {
        printf("    Failed to find OpenPBR reflection dielectric node\n");
        return false;
    }

    return Test_IsClose(c.normal, expectedNormal, 1e-4f) &&
           Test_IsClose(dielectric->tangent, expectedTangent, 1e-4f);
}

static bool
TestOpenPbrGeometryCoatNormalUsesCanonicalName()
{
    const Vec3f expectedCoatNormal =
        Vec3f(0.0f, 0.70710677f, 0.70710677f);

    ParamMap params;
    params["specular_weight"] = Value(0.0f);
    params["coat_weight"] = Value(1.0f);
    params["geometry_normal"] = Value(Vec3f(0.0f, 0.0f, 1.0f));
    params["coat_normal"] = Value(Vec3f(1.0f, 0.0f, 0.0f));
    params["geometry_coat_normal"] = Value(expectedCoatNormal);

    const SurfaceClosure c = EvalOpenPbr(params);
    const auto* coat = FindNodeIf<Bsdf::DielectricData>(
        c.bsdfTree,
        [](const Bsdf::DielectricData& data) {
            return data.scatterMode == Bsdf::ScatterMode::Reflection;
        });

    if (!coat) {
        printf("    Failed to find OpenPBR coat dielectric node\n");
        return false;
    }

    return coat->hasShadingNormal &&
           Test_IsClose(coat->normal, expectedCoatNormal, 1e-4f);
}

static bool
TestOpenPbrCoatRoughnessAnisotropyUsesCanonicalName()
{
    constexpr float roughness = 0.45f;
    constexpr float anisotropy = 0.75f;

    ParamMap params;
    params["specular_weight"] = Value(0.0f);
    params["coat_weight"] = Value(1.0f);
    params["coat_roughness"] = Value(roughness);
    params["coat_roughness_anisotropy"] = Value(anisotropy);

    const SurfaceClosure c = EvalOpenPbr(params);
    const auto* coat = FindNodeIf<Bsdf::DielectricData>(
        c.bsdfTree,
        [](const Bsdf::DielectricData& data) {
            return data.scatterMode == Bsdf::ScatterMode::Reflection &&
                   std::abs(data.ior - 1.6f) < 1e-4f;
        });

    if (!coat) {
        printf("    Failed to find OpenPBR coat dielectric node\n");
        return false;
    }

    const float clampedRoughness = std::clamp(roughness, 0.001f, 1.0f);
    const float clampedAnisotropy = std::clamp(anisotropy, 0.0f, 1.0f);
    const float alphaRoughness = clampedRoughness * clampedRoughness;
    const float oneMinusAnisotropy = 1.0f - clampedAnisotropy;
    const float alphaX = alphaRoughness * std::sqrt(
        2.0f /
        std::max(oneMinusAnisotropy * oneMinusAnisotropy + 1.0f, 1.0e-6f));
    const float alphaY = oneMinusAnisotropy * alphaX;
    const Vec2f expected(
        std::clamp(alphaX, 1.0e-5f, 1.0f),
        std::clamp(alphaY, 1.0e-5f, 1.0f));

    return Test_IsClose(coat->roughness[0], expected[0], 1e-4f) &&
           Test_IsClose(coat->roughness[1], expected[1], 1e-4f);
}

static bool
TestOpenPbrGeometryCoatTangentUsesCanonicalName()
{
    const Vec3f expectedCoatTangent(0.0f, 1.0f, 0.0f);

    ParamMap params;
    params["specular_weight"] = Value(0.0f);
    params["coat_weight"] = Value(1.0f);
    params["geometry_tangent"] = Value(Vec3f(1.0f, 0.0f, 0.0f));
    params["geometry_coat_tangent"] = Value(expectedCoatTangent);

    const SurfaceClosure c = EvalOpenPbr(params);
    const auto* coat = FindNodeIf<Bsdf::DielectricData>(
        c.bsdfTree,
        [](const Bsdf::DielectricData& data) {
            return data.scatterMode == Bsdf::ScatterMode::Reflection;
        });

    if (!coat) {
        printf("    Failed to find OpenPBR coat dielectric node\n");
        return false;
    }

    return Test_IsClose(coat->tangent, expectedCoatTangent, 1e-4f);
}

// ---------------------------------------------------------------------------
// Disney Principled
// ---------------------------------------------------------------------------

static bool
TestDisneyPrincipledDefaults()
{
    ParamMap params;
    const SurfaceClosure c = EvalDisneyPrincipled(params);

    if (!Test_IsClose(c.baseColor, Vec3f(0.16f), 1e-4f)) return false;
    if (!Test_IsClose(c.roughness, 0.5f)) return false;
    if (!Test_IsClose(c.metallic, 0.0f)) return false;
    if (!Test_IsClose(c.transmission, 0.0f)) return false;
    if (!Test_IsClose(c.sheenRoughness, 0.3f)) return false;
    if (!c.HasBsdfTree()) return false;
    return true;
}

static bool
TestDisneyPrincipledSpecularTintReachesDielectricLayer()
{
    const Vec3f baseColor(0.2f, 0.4f, 0.7f);

    ParamMap params;
    params["baseColor"] = Value(baseColor);
    params["specular"] = Value(1.0f);
    params["specularTint"] = Value(1.0f);
    params["clearcoat"] = Value(0.0f);

    const SurfaceClosure c = EvalDisneyPrincipled(params);
    const auto* dielectric = FindNodeIf<Bsdf::GeneralizedSchlickData>(
        c.bsdfTree,
        [](const Bsdf::GeneralizedSchlickData& data) {
            return data.scatterMode == Bsdf::ScatterMode::Reflection &&
                   data.weight < 0.5f;
        });

    if (!dielectric) {
        printf("    Failed to find Disney Principled dielectric layer\n");
        return false;
    }

    return Test_IsClose(c.specularColor, baseColor, 1e-4f) &&
           Test_IsClose(dielectric->weight, 0.08f, 1e-4f) &&
           Test_IsClose(dielectric->color0, baseColor, 1e-4f) &&
           Test_IsClose(dielectric->color82, baseColor, 1e-4f) &&
           Test_IsClose(dielectric->color90, Vec3f(1.0f), 1e-4f);
}

static bool
TestDisneyPrincipledAnisotropyUsesMaterialXFormula()
{
    constexpr float roughness = 0.45f;
    constexpr float anisotropy = 0.75f;

    ParamMap params;
    params["roughness"] = Value(roughness);
    params["anisotropic"] = Value(anisotropy);
    params["specular"] = Value(1.0f);
    params["clearcoat"] = Value(0.0f);

    const SurfaceClosure c = EvalDisneyPrincipled(params);
    const auto* dielectric = FindNodeIf<Bsdf::GeneralizedSchlickData>(
        c.bsdfTree,
        [](const Bsdf::GeneralizedSchlickData& data) {
            return data.scatterMode == Bsdf::ScatterMode::Reflection &&
                   data.weight < 0.5f;
        });

    if (!dielectric) {
        printf("    Failed to find Disney Principled anisotropic dielectric layer\n");
        return false;
    }

    const float roughnessSqr =
        std::clamp(roughness * roughness, 1.0e-5f, 1.0f);
    const float clampedAnisotropy = std::clamp(anisotropy, 0.0f, 0.98f);
    const float aspect = std::sqrt(1.0f - clampedAnisotropy);
    const Vec2f expected(
        std::min(roughnessSqr / std::max(aspect, 1.0e-5f), 1.0f),
        roughnessSqr * aspect);

    return Test_IsClose(dielectric->roughness[0], expected[0], 1e-4f) &&
           Test_IsClose(dielectric->roughness[1], expected[1], 1e-4f);
}

static bool
TestDisneyPrincipledSheenUsesMaterialXDefaults()
{
    const Vec3f baseColor(0.82f, 0.28f, 0.14f);

    ParamMap params;
    params["baseColor"] = Value(baseColor);
    params["sheen"] = Value(0.7f);
    params["sheenTint"] = Value(1.0f);

    const SurfaceClosure c = EvalDisneyPrincipled(params);
    const auto* sheen = FindNodeIf<Bsdf::SheenData>(
        c.bsdfTree,
        [](const Bsdf::SheenData&) {
            return true;
        });

    if (!sheen) {
        printf("    Failed to find Disney Principled sheen layer\n");
        return false;
    }

    return Test_IsClose(c.sheenColor, baseColor, 1e-4f) &&
           Test_IsClose(c.sheenRoughness, 0.3f, 1e-4f) &&
           Test_IsClose(sheen->weight, 0.7f, 1e-4f) &&
           Test_IsClose(sheen->color, baseColor, 1e-4f) &&
           Test_IsClose(sheen->roughness, 0.3f, 1e-4f) &&
           sheen->mode == Bsdf::SheenMode::ContyKulla;
}

static bool
TestDisneyPrincipledTransmissionUsesSharpDielectric()
{
    const Vec3f baseColor(0.7f, 0.92f, 0.98f);

    ParamMap params;
    params["baseColor"] = Value(baseColor);
    params["specTrans"] = Value(0.65f);
    params["ior"] = Value(1.8f);

    const SurfaceClosure c = EvalDisneyPrincipled(params);
    const auto* transmission = FindNodeIf<Bsdf::DielectricData>(
        c.bsdfTree,
        [](const Bsdf::DielectricData& data) {
            return data.scatterMode == Bsdf::ScatterMode::Transmission;
        });

    if (!transmission) {
        printf("    Failed to find Disney Principled transmission layer\n");
        return false;
    }

    return Test_IsClose(c.transmission, 0.65f, 1e-4f) &&
           Test_IsClose(transmission->tint, baseColor, 1e-4f) &&
           Test_IsClose(transmission->ior, 1.8f, 1e-4f) &&
           Test_IsClose(transmission->roughness[0], 1.0e-5f, 1e-7f) &&
           Test_IsClose(transmission->roughness[1], 1.0e-5f, 1e-7f);
}

static bool
TestDisneyPrincipledClearcoatGlossControlsCoatRoughness()
{
    ParamMap params;
    params["baseColor"] = Value(Vec3f(0.08f, 0.22f, 0.75f));
    params["specular"] = Value(0.0f);
    params["clearcoat"] = Value(1.0f);
    params["clearcoatGloss"] = Value(0.2f);

    const SurfaceClosure c = EvalDisneyPrincipled(params);
    const auto* coat = FindNodeIf<Bsdf::GeneralizedSchlickData>(
        c.bsdfTree,
        [](const Bsdf::GeneralizedSchlickData& data) {
            return data.scatterMode == Bsdf::ScatterMode::Reflection &&
                   Test_IsClose(data.color0, Vec3f(1.0f), 1e-4f) &&
                   Test_IsClose(data.weight, 0.04f, 1e-4f);
        });

    if (!coat) {
        printf("    Failed to find Disney Principled clearcoat layer\n");
        return false;
    }

    const float expectedRoughness = 0.8f * 0.8f;
    return Test_IsClose(c.coat, 0.04f, 1e-4f) &&
           Test_IsClose(c.coatRoughness, 0.8f, 1e-4f) &&
           Test_IsClose(coat->roughness[0], expectedRoughness, 1e-4f) &&
           Test_IsClose(coat->roughness[1], expectedRoughness, 1e-4f);
}

// ---------------------------------------------------------------------------
// glTF PBR
// ---------------------------------------------------------------------------

static bool
TestGltfPbrDefaults()
{
    ParamMap params;
    const SurfaceClosure c = EvalGltfPbr(params);

    if (!Test_IsClose(c.baseColor, Vec3f(1.0f), 1e-4f)) return false;
    if (!Test_IsClose(c.roughness, 1.0f)) return false;
    if (!Test_IsClose(c.metallic, 1.0f)) return false;
    if (!Test_IsClose(c.opacity, 1.0f)) return false;
    if (!c.HasBsdfTree()) return false;
    return true;
}

static bool
TestGltfPbrAlphaMask()
{
    ParamMap params;
    params["alpha"] = Value(0.25f);
    params["alpha_mode"] = Value(1);
    params["alpha_cutoff"] = Value(0.5f);

    const SurfaceClosure c = EvalGltfPbr(params);
    return Test_IsClose(c.opacity, 0.0f) &&
           Test_IsClose(c.presence, 0.0f);
}

static bool
TestGltfPbrIridescenceParametersReachBsdf()
{
    ParamMap params;
    params["iridescence"] = Value(0.6f);
    params["iridescence_ior"] = Value(1.45f);
    params["iridescence_thickness"] = Value(220.0f);

    const SurfaceClosure c = EvalGltfPbr(params);
    const auto* reflective = FindNodeIf<Bsdf::GeneralizedSchlickData>(
        c.bsdfTree,
        [](const Bsdf::GeneralizedSchlickData& data) {
            return data.scatterMode == Bsdf::ScatterMode::Reflection &&
                   data.thinFilmWeight > 0.0f;
        });

    if (!reflective) {
        printf("    Failed to find glTF iridescent reflection node\n");
        return false;
    }

    return Test_IsClose(reflective->thinFilmWeight, 0.6f) &&
           Test_IsClose(reflective->thinFilmThickness, 220.0f, 1e-4f) &&
           Test_IsClose(reflective->thinFilmIor, 1.45f, 1e-4f);
}

static bool
TestGltfPbrUsesDefaultWhiteColor82()
{
    const Vec3f baseColor(0.2f, 0.4f, 0.7f);

    ParamMap params;
    params["base_color"] = Value(baseColor);
    params["metallic"] = Value(0.5f);
    params["specular"] = Value(0.5f);
    params["ior"] = Value(1.5f);

    const SurfaceClosure c = EvalGltfPbr(params);
    const auto* dielectric = FindNodeIf<Bsdf::GeneralizedSchlickData>(
        c.bsdfTree,
        [&baseColor](const Bsdf::GeneralizedSchlickData& data) {
            return data.scatterMode == Bsdf::ScatterMode::Reflection &&
                   !Test_IsClose(data.color0, baseColor, 1e-4f);
        });
    const auto* metal = FindNodeIf<Bsdf::GeneralizedSchlickData>(
        c.bsdfTree,
        [&baseColor](const Bsdf::GeneralizedSchlickData& data) {
            return Test_IsClose(data.color0, baseColor, 1e-4f);
        });

    if (!dielectric || !metal) {
        printf("    Failed to find both glTF generalized-Schlick branches\n");
        return false;
    }

    return Test_IsClose(dielectric->color82, Vec3f(1.0f), 1e-4f) &&
           Test_IsClose(metal->color82, Vec3f(1.0f), 1e-4f);
}

static bool
TestGltfPbrDispersionStrengthReachesTransmissionAsAbbeNumber()
{
    ParamMap params;
    params["metallic"] = Value(0.0f);
    params["transmission"] = Value(1.0f);
    params["dispersion"] = Value(0.5f);

    const SurfaceClosure c = EvalGltfPbr(params);
    const auto* transmission = FindNodeIf<Bsdf::DielectricData>(
        c.bsdfTree,
        [](const Bsdf::DielectricData& data) {
            return data.scatterMode == Bsdf::ScatterMode::Transmission;
        });

    if (!transmission) {
        printf("    Failed to find glTF transmission dielectric node\n");
        return false;
    }

    return Test_IsClose(transmission->dispersionAbbe, 40.0f, 1e-4f) &&
           c.HasDispersion();
}

static bool
TestGltfPbrClearcoatNormalReachesBsdf()
{
    const Vec3f clearcoatNormal(0.0f, 0.70710677f, 0.70710677f);

    ParamMap params;
    params["metallic"] = Value(0.0f);
    params["clearcoat"] = Value(1.0f);
    params["clearcoat_roughness"] = Value(0.15f);
    params["clearcoat_normal"] = Value(clearcoatNormal);
    params["tangent"] = Value(Vec3f(1.0f, 0.0f, 0.0f));

    const SurfaceClosure c = EvalGltfPbr(params);
    const auto* coat = FindNodeIf<Bsdf::DielectricData>(
        c.bsdfTree,
        [](const Bsdf::DielectricData& data) {
            return data.scatterMode == Bsdf::ScatterMode::Reflection &&
                   Test_IsClose(data.ior, 1.5f, 1e-4f) &&
                   Test_IsClose(data.weight, 1.0f, 1e-4f);
        });

    if (!coat) {
        printf("    Failed to find glTF clearcoat dielectric node\n");
        return false;
    }

    const float expectedRoughness = 0.15f * 0.15f;
    return coat->hasShadingNormal &&
           Test_IsClose(coat->normal, clearcoatNormal, 1e-4f) &&
           Test_IsClose(coat->tangent, Vec3f(1.0f, 0.0f, 0.0f), 1e-4f) &&
           Test_IsClose(coat->roughness[0], expectedRoughness, 1e-4f) &&
           Test_IsClose(coat->roughness[1], expectedRoughness, 1e-4f);
}

static bool
TestGltfPbrClearcoatInheritsGeometricNormalByDefault()
{
    ParamMap params;
    params["metallic"] = Value(0.0f);
    params["clearcoat"] = Value(1.0f);

    const SurfaceClosure c = EvalGltfPbr(params);
    const auto* coat = FindNodeIf<Bsdf::DielectricData>(
        c.bsdfTree,
        [](const Bsdf::DielectricData& data) {
            return data.scatterMode == Bsdf::ScatterMode::Reflection &&
                   Test_IsClose(data.ior, 1.5f, 1e-4f) &&
                   Test_IsClose(data.weight, 1.0f, 1e-4f);
        });

    if (!coat) {
        printf("    Failed to find default glTF clearcoat dielectric node\n");
        return false;
    }

    return !coat->hasShadingNormal;
}

static bool
TestGltfPbrAnisotropyRotationRotatesTangent()
{
    constexpr float kQuarterTurnRadians = 1.57079632679f;

    ParamMap params;
    params["metallic"] = Value(0.0f);
    params["roughness"] = Value(0.4f);
    params["anisotropy_strength"] = Value(0.8f);
    params["anisotropy_rotation"] = Value(kQuarterTurnRadians);
    params["tangent"] = Value(Vec3f(1.0f, 0.0f, 0.0f));
    params["normal"] = Value(Vec3f(0.0f, 0.0f, 1.0f));

    const SurfaceClosure c = EvalGltfPbr(params);
    const auto* reflective = FindNodeIf<Bsdf::GeneralizedSchlickData>(
        c.bsdfTree,
        [](const Bsdf::GeneralizedSchlickData& data) {
            return data.scatterMode == Bsdf::ScatterMode::Reflection &&
                   data.thinFilmWeight <= 0.0f;
        });

    if (!reflective) {
        printf("    Failed to find glTF anisotropic reflection node\n");
        return false;
    }

    const float roughnessSquared = std::clamp(0.4f * 0.4f, 1.0e-5f, 1.0f);
    const float strengthSquared = 0.8f * 0.8f;
    const float alphaX = std::clamp(
        roughnessSquared * (1.0f - strengthSquared) + strengthSquared,
        1.0e-5f,
        1.0f);
    const Vec2f expected(alphaX, roughnessSquared);

    return Test_IsClose(reflective->tangent, Vec3f(0.0f, 1.0f, 0.0f), 1e-4f) &&
           Test_IsClose(reflective->roughness[0], expected[0], 1e-4f) &&
           Test_IsClose(reflective->roughness[1], expected[1], 1e-4f);
}

// ---------------------------------------------------------------------------
// UsdPreviewSurface
// ---------------------------------------------------------------------------

static bool
TestUsdPreviewSurfaceDefaults()
{
    ParamMap params;
    SurfaceClosure c = EvalUsdPreviewSurface(params);

    if (!Test_IsClose(c.baseColor, Vec3f(0.18f), 1e-4f)) {
        printf("    baseColor: (%f,%f,%f)\n", c.baseColor[0], c.baseColor[1], c.baseColor[2]);
        return false;
    }
    if (!Test_IsClose(c.roughness, 0.5f)) return false;
    if (!Test_IsClose(c.metallic, 0.0f)) return false;
    if (!Test_IsClose(c.opacity, 1.0f)) return false;
    if (!Test_IsClose(c.coat, 0.0f)) return false;
    if (!Test_IsClose(c.transmission, 0.0f)) return false;
    if (!c.HasBsdfTree()) return false;
    return true;
}

static bool
TestUsdPreviewSurfaceMetallicWorkflow()
{
    ParamMap params;
    params["useSpecularWorkflow"] = Value(0);
    params["metallic"] = Value(1.0f);
    params["diffuseColor"] = Value(Vec3f(1, 0, 0));

    SurfaceClosure c = EvalUsdPreviewSurface(params);
    if (!Test_IsClose(c.metallic, 1.0f)) return false;
    if (!Test_IsClose(c.baseColor, Vec3f(1, 0, 0), 1e-4f)) return false;
    return true;
}

static bool
TestUsdPreviewSurfaceSpecularWorkflow()
{
    ParamMap params;
    params["useSpecularWorkflow"] = Value(1);
    params["specularColor"] = Value(Vec3f(0.5f, 0.5f, 0.5f));

    const SurfaceClosure c = EvalUsdPreviewSurface(params);
    const auto* specular = FindNodeIf<Bsdf::GeneralizedSchlickData>(
        c.bsdfTree,
        [](const Bsdf::GeneralizedSchlickData& data) {
            return data.scatterMode == Bsdf::ScatterMode::Reflection;
        });

    return specular &&
           Test_IsClose(c.metallic, 0.0f) &&
           Test_IsClose(c.specularColor, Vec3f(0.5f), 1e-4f) &&
           Test_IsClose(specular->color0, Vec3f(0.5f), 1e-4f) &&
           Test_IsClose(specular->color82, Vec3f(1.0f), 1e-4f) &&
           Test_IsClose(specular->color90, Vec3f(1.0f), 1e-4f);
}

static bool
TestUsdPreviewSurfaceIorOneKeepsGrazingSpecular()
{
    ParamMap params;
    params["useSpecularWorkflow"] = Value(0);
    params["diffuseColor"] = Value(Vec3f(0.0f));
    params["ior"] = Value(1.0f);
    params["roughness"] = Value(0.5f);

    const SurfaceClosure c = EvalUsdPreviewSurface(params);
    const auto* specular = FindNodeIf<Bsdf::GeneralizedSchlickData>(
        c.bsdfTree,
        [](const Bsdf::GeneralizedSchlickData& data) {
            return data.scatterMode == Bsdf::ScatterMode::Reflection;
        });

    if (!specular) {
        printf("    Failed to find UsdPreviewSurface specular node\n");
        return false;
    }
    if (!Test_IsClose(specular->color0, Vec3f(0.0f), 1e-4f) ||
        !Test_IsClose(specular->color82, Vec3f(1.0f), 1e-4f) ||
        !Test_IsClose(specular->color90, Vec3f(1.0f), 1e-4f)) {
        printf("    Expected ior=1 to produce F0=0 and white grazing terms\n");
        return false;
    }

    const Vec3f normalShdWldOut(0.0f, 0.0f, 1.0f);
    Vec3f omegaOutWld(1.0f, 0.0f, 0.1f);
    Vec3f omegaInWld(-1.0f, 0.0f, 0.1f);
    omegaOutWld.normalize();
    omegaInWld.normalize();
    const Vec3f f =
        TestSurfaceApi::EvalSurface(c, normalShdWldOut, omegaInWld, omegaOutWld);
    return IsFiniteNonNegative(f) &&
           (f[0] + f[1] + f[2]) > 1.0e-5f;
}

static bool
TestUsdPreviewSurfaceIorControlsDielectricF0()
{
    constexpr float ior = 1.2f;
    const float expectedF0 = ((1.0f - ior) / (1.0f + ior)) *
                             ((1.0f - ior) / (1.0f + ior));

    ParamMap params;
    params["useSpecularWorkflow"] = Value(0);
    params["diffuseColor"] = Value(Vec3f(0.0f));
    params["ior"] = Value(ior);

    const SurfaceClosure c = EvalUsdPreviewSurface(params);
    const auto* specular = FindNodeIf<Bsdf::GeneralizedSchlickData>(
        c.bsdfTree,
        [](const Bsdf::GeneralizedSchlickData& data) {
            return data.scatterMode == Bsdf::ScatterMode::Reflection;
        });

    return specular &&
           Test_IsClose(specular->color0, Vec3f(expectedF0), 1e-5f) &&
           Test_IsClose(specular->color82, Vec3f(1.0f), 1e-4f) &&
           Test_IsClose(specular->color90, Vec3f(1.0f), 1e-4f);
}

static bool
TestUsdPreviewSurfaceMetallicF90UsesAlbedo()
{
    const Vec3f albedo(0.8f, 0.2f, 0.1f);

    ParamMap params;
    params["useSpecularWorkflow"] = Value(0);
    params["diffuseColor"] = Value(albedo);
    params["metallic"] = Value(1.0f);

    const SurfaceClosure c = EvalUsdPreviewSurface(params);
    const auto* specular = FindNodeIf<Bsdf::GeneralizedSchlickData>(
        c.bsdfTree,
        [](const Bsdf::GeneralizedSchlickData& data) {
            return data.scatterMode == Bsdf::ScatterMode::Reflection;
        });

    return specular &&
           Test_IsClose(specular->color0, albedo, 1e-4f) &&
           Test_IsClose(specular->color82, Vec3f(1.0f), 1e-4f) &&
           Test_IsClose(specular->color90, albedo, 1e-4f);
}

static bool
TestUsdPreviewSurfaceMetallicInterpolatesF0AndF90()
{
    const Vec3f albedo(0.8f, 0.2f, 0.1f);
    constexpr float metallic = 0.25f;
    constexpr float ior = 1.5f;
    const float dielectricF0 = ((1.0f - ior) / (1.0f + ior)) *
                               ((1.0f - ior) / (1.0f + ior));
    const Vec3f expectedF0 = Vec3f(dielectricF0) * (1.0f - metallic) +
                             albedo * metallic;
    const Vec3f expectedF90 = Vec3f(1.0f) * (1.0f - metallic) +
                              albedo * metallic;

    ParamMap params;
    params["useSpecularWorkflow"] = Value(0);
    params["diffuseColor"] = Value(albedo);
    params["metallic"] = Value(metallic);
    params["ior"] = Value(ior);

    const SurfaceClosure c = EvalUsdPreviewSurface(params);
    const auto* specular = FindNodeIf<Bsdf::GeneralizedSchlickData>(
        c.bsdfTree,
        [](const Bsdf::GeneralizedSchlickData& data) {
            return data.scatterMode == Bsdf::ScatterMode::Reflection;
        });

    return specular &&
           Test_IsClose(specular->color0, expectedF0, 1e-5f) &&
           Test_IsClose(specular->color82, Vec3f(1.0f), 1e-4f) &&
           Test_IsClose(specular->color90, expectedF90, 1e-5f);
}

static bool
TestUsdPreviewSurfaceIorReachesClearcoat()
{
    ParamMap params;
    params["clearcoat"] = Value(1.0f);
    params["clearcoatRoughness"] = Value(0.25f);
    params["ior"] = Value(1.2f);

    const SurfaceClosure c = EvalUsdPreviewSurface(params);
    const auto* coat = FindNodeIf<Bsdf::DielectricData>(
        c.bsdfTree,
        [](const Bsdf::DielectricData& data) {
            return data.scatterMode == Bsdf::ScatterMode::Reflection &&
                   data.weight > 0.0f;
        });

    const float expectedAlpha = 0.25f * 0.25f;
    return coat && Test_IsClose(coat->ior, 1.2f, 1e-5f) &&
           Test_IsClose(coat->tint, Vec3f(1.0f), 1e-5f) &&
           Test_IsClose(coat->roughness[0], expectedAlpha, 1e-5f) &&
           Test_IsClose(coat->roughness[1], expectedAlpha, 1e-5f) &&
           Test_IsClose(c.coatIor, 1.2f, 1e-5f);
}


static bool
TestUsdPreviewSurfaceOpacityThreshold()
{
    ParamMap params;
    params["opacity"] = Value(0.3f);
    params["opacityThreshold"] = Value(0.5f);

    SurfaceClosure c = EvalUsdPreviewSurface(params);
    // opacity(0.3) < threshold(0.5) → opacity should be cutout to 0.
    return Test_IsClose(c.opacity, 0.0f) &&
           Test_IsClose(c.presence, 0.0f) &&
           Test_IsClose(c.transmission, 0.0f);
}

static bool
TestUsdPreviewSurfaceTransparentModeKeepsLightingResponse()
{
    ParamMap params;
    params["opacity"] = Value(0.0f);
    params["opacityMode"] = Value(std::string("transparent"));

    const SurfaceClosure c = EvalUsdPreviewSurface(params);
    return Test_IsClose(c.opacity, 0.0f) &&
           Test_IsClose(c.presence, 1.0f) &&
           Test_IsClose(c.transmission, 1.0f);
}

static bool
TestUsdPreviewSurfacePresenceModeCutsLightingResponse()
{
    ParamMap params;
    params["opacity"] = Value(0.25f);
    params["opacityMode"] = Value(std::string("presence"));

    const SurfaceClosure c = EvalUsdPreviewSurface(params);
    return Test_IsClose(c.opacity, 0.25f) &&
           Test_IsClose(c.presence, 0.25f) &&
           Test_IsClose(c.transmission, 0.0f);
}

static bool
TestUsdPreviewSurfaceMaterialXOpacityModeInteger()
{
    ParamMap params;
    params["opacity"] = Value(0.0f);
    params["opacityMode"] = Value(1);

    const SurfaceClosure c = EvalUsdPreviewSurface(params);
    return Test_IsClose(c.opacity, 0.0f) &&
           Test_IsClose(c.presence, 0.0f) &&
           Test_IsClose(c.transmission, 0.0f);
}

static bool
TestUsdPreviewSurfaceIgnoresOcclusion()
{
    ParamMap params;
    params["diffuseColor"] = Value(Vec3f(0.2f, 0.4f, 0.8f));
    params["occlusion"] = Value(0.0f);

    const SurfaceClosure c = EvalUsdPreviewSurface(params);
    return Test_IsClose(c.baseColor, Vec3f(0.2f, 0.4f, 0.8f), 1e-4f);
}

static bool
TestUsdPreviewSurfaceMetalnessTransmissionUsesDielectricInterface()
{
    // Metalness-workflow transparency maps onto the same coupled dielectric
    // interface OpenPBR uses, so both shade identically.  F0 is derived from
    // ior in this workflow, which is exactly the interface's parameterization.
    ParamMap params;
    params["diffuseColor"] = Value(Vec3f(0.18f));
    params["metallic"] = Value(0.0f);
    params["roughness"] = Value(0.0f);
    params["ior"] = Value(1.5f);
    params["opacity"] = Value(0.0f);
    params["opacityThreshold"] = Value(0.0f);
    const SurfaceClosure c = EvalUsdPreviewSurface(params);

    const auto* interface = FindNodeIf<Bsdf::DielectricInterfaceData>(
        c.bsdfTree, [](const Bsdf::DielectricInterfaceData&) {
            return true;
        });
    if (!interface) {
        printf("    expected a DielectricInterfaceData lobe\n");
        return false;
    }
    if (!Test_IsClose(interface->ior, 1.5f, 1e-5f) ||
        !Test_IsClose(interface->reflectionWeight, 1.0f, 1e-5f) ||
        !Test_IsClose(interface->transmissionWeight, 1.0f, 1e-5f) ||
        !interface->compensateCoupledDielectric) {
        printf("    interface parameters mismatch: ior=%f reflW=%f "
               "transW=%f compensate=%d\n",
               interface->ior,
               interface->reflectionWeight,
               interface->transmissionWeight,
               int(interface->compensateCoupledDielectric));
        return false;
    }

    const bool hasPairLobes =
        FindNodeIf<Bsdf::GeneralizedSchlickData>(
            c.bsdfTree, [](const Bsdf::GeneralizedSchlickData&) {
                return true;
            }) ||
        FindNodeIf<Bsdf::DielectricData>(
            c.bsdfTree, [](const Bsdf::DielectricData& d) {
                return d.scatterMode == Bsdf::ScatterMode::Transmission;
            });
    if (hasPairLobes) {
        printf("    metalness transparency should not keep the "
               "Schlick/transmission pair\n");
        return false;
    }
    return true;
}

static bool
TestUsdPreviewSurfaceSpecularWorkflowTransmissionKeepsSchlickPair()
{
    // The specular workflow decouples reflectivity (specularColor) from
    // refraction (ior), which a single ior-driven interface cannot express;
    // it stays on the paired Schlick-reflection + transmission closure.
    ParamMap params;
    params["useSpecularWorkflow"] = Value(1);
    params["specularColor"] = Value(Vec3f(0.2f, 0.3f, 0.4f));
    params["roughness"] = Value(0.0f);
    params["ior"] = Value(1.5f);
    params["opacity"] = Value(0.0f);
    const SurfaceClosure c = EvalUsdPreviewSurface(params);

    const bool hasSchlick = FindNodeIf<Bsdf::GeneralizedSchlickData>(
        c.bsdfTree, [](const Bsdf::GeneralizedSchlickData&) {
            return true;
        }) != nullptr;
    const bool hasTransmission = FindNodeIf<Bsdf::DielectricData>(
        c.bsdfTree, [](const Bsdf::DielectricData& d) {
            return d.scatterMode == Bsdf::ScatterMode::Transmission;
        }) != nullptr;
    const bool hasInterface = FindNodeIf<Bsdf::DielectricInterfaceData>(
        c.bsdfTree, [](const Bsdf::DielectricInterfaceData&) {
            return true;
        }) != nullptr;
    if (!hasSchlick || !hasTransmission || hasInterface) {
        printf("    specular workflow structure changed: schlick=%d "
               "transmission=%d interface=%d\n",
               int(hasSchlick), int(hasTransmission), int(hasInterface));
        return false;
    }
    return true;
}

static bool
TestUsdPreviewSurfaceMetallicTransmissionKeepsSchlickPair()
{
    // Metallic transparency has no physical dielectric-interface analog;
    // it stays on the legacy pair as well.
    ParamMap params;
    params["metallic"] = Value(0.5f);
    params["roughness"] = Value(0.0f);
    params["ior"] = Value(1.5f);
    params["opacity"] = Value(0.0f);
    const SurfaceClosure c = EvalUsdPreviewSurface(params);

    const bool hasInterface = FindNodeIf<Bsdf::DielectricInterfaceData>(
        c.bsdfTree, [](const Bsdf::DielectricInterfaceData&) {
            return true;
        }) != nullptr;
    if (hasInterface) {
        printf("    metallic transparency should not use the dielectric "
               "interface\n");
        return false;
    }
    return true;
}

static bool
TestStandardSurfaceTransmissionUsesSeparateDielectricLobes()
{
    // Standard Surface retains its legacy reflection layer over a separate
    // transmission lobe rather than using OpenPBR's coupled interface.
    ParamMap params;
    params["base"] = Value(0.0f);
    params["specular"] = Value(1.0f);
    params["specular_color"] = Value(Vec3f(0.9f, 0.95f, 1.0f));
    params["specular_roughness"] = Value(0.0f);
    params["specular_IOR"] = Value(1.5f);
    params["transmission"] = Value(1.0f);
    const SurfaceClosure c = EvalStandardSurface(params);

    const auto* reflection = FindNodeIf<Bsdf::DielectricData>(
        c.bsdfTree, [](const Bsdf::DielectricData& d) {
            return d.scatterMode == Bsdf::ScatterMode::Reflection;
        });
    const auto* transmission = FindNodeIf<Bsdf::DielectricData>(
        c.bsdfTree, [](const Bsdf::DielectricData& d) {
            return d.scatterMode == Bsdf::ScatterMode::Transmission;
        });
    if (!reflection || !transmission) {
        printf("    expected separate reflection and transmission lobes\n");
        return false;
    }
    if (!Test_IsClose(reflection->ior, 1.5f, 1e-5f) ||
        !Test_IsClose(transmission->ior, 1.5f, 1e-5f) ||
        !Test_IsClose(reflection->tint, Vec3f(0.9f, 0.95f, 1.0f), 1e-5f)) {
        printf("    Standard Surface dielectric lobe parameters mismatch\n");
        return false;
    }
    return FindNodeIf<Bsdf::DielectricInterfaceData>(
               c.bsdfTree,
               [](const Bsdf::DielectricInterfaceData&) { return true; }) ==
        nullptr;
}

static bool
TestStandardSurfaceExtraRoughnessKeepsTransmissionLobe()
{
    // transmission_extra_roughness authors a rougher transmission than
    // reflection, which the single-roughness interface cannot express;
    // that case stays on the legacy pairing.
    ParamMap params;
    params["base"] = Value(0.0f);
    params["specular"] = Value(1.0f);
    params["specular_roughness"] = Value(0.1f);
    params["specular_IOR"] = Value(1.5f);
    params["transmission"] = Value(1.0f);
    params["transmission_extra_roughness"] = Value(0.2f);
    const SurfaceClosure c = EvalStandardSurface(params);

    const bool hasTransmission = FindNodeIf<Bsdf::DielectricData>(
        c.bsdfTree, [](const Bsdf::DielectricData& d) {
            return d.scatterMode == Bsdf::ScatterMode::Transmission;
        }) != nullptr;
    const bool hasInterface = FindNodeIf<Bsdf::DielectricInterfaceData>(
        c.bsdfTree, [](const Bsdf::DielectricInterfaceData&) {
            return true;
        }) != nullptr;
    if (!hasTransmission || hasInterface) {
        printf("    extra-roughness structure changed: transmission=%d "
               "interface=%d\n",
               int(hasTransmission), int(hasInterface));
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------

void
Test_RegisterMaterialTests()
{
    _REG(TestStandardSurfaceTransmissionUsesSeparateDielectricLobes);
    _REG(TestStandardSurfaceExtraRoughnessKeepsTransmissionLobe);
    _REG(TestUsdPreviewSurfaceMetalnessTransmissionUsesDielectricInterface);
    _REG(TestUsdPreviewSurfaceSpecularWorkflowTransmissionKeepsSchlickPair);
    _REG(TestUsdPreviewSurfaceMetallicTransmissionKeepsSchlickPair);
    _REG(TestMaterialNormalSpaceContracts);
    _REG(TestStandardSurfaceDefaults);
    _REG(TestStandardSurfaceMetallic);
    _REG(TestStandardSurfaceGoldMetallicSharpRoughnessStaysStable);
    _REG(TestStandardSurfaceCustomParams);
    _REG(TestStandardSurfaceThinFilmParametersReachBsdf);
    _REG(TestStandardSurfaceDispersionParametersReachBsdf);
    _REG(TestStandardSurfaceVolumeParametersReachClosure);
    _REG(TestStandardSurfaceLayersSpecularOverTransmissionMix);
    _REG(TestStandardSurfaceThinWalledUsesUnitIorTransmission);
    _REG(TestStandardSurfaceSpecularRotationUsesCanonicalName);
    _REG(TestStandardSurfaceMetalThinFilmChangesReflectionColor);
    _REG(TestStandardSurfaceThinFilmUsesNanometerUnits);
    _REG(TestStandardSurfaceCoatAffectRoughnessMatchesMaterialXGraph);
    _REG(TestRegularizeTreatsDielectricRoughnessAsAlpha);
    _REG(TestStandardSurfaceCoatColorSemanticsMatchMaterialX);
    _REG(TestStandardSurfaceCoatNormalAndRotationReachBsdf);
    _REG(TestOpenPbrDefaults);
    _REG(TestOpenPbrBuildsLayeredDielectricBase);
    _REG(TestOpenPbrModulatesSubstrateIorForCoatAndSpecularWeight);
    _REG(TestOpenPbrCoatRoughensSubstrateSpecular);
    _REG(TestOpenPbrMetalUsesF82TintSemantics);
    _REG(TestOpenPbrTransmission);
    _REG(TestAdobeOpenPbrBuildsWholeBackendNode);
    _REG(TestAdobeOpenPbrEvalPdfSurfaceMatchesSeparateCalls);
    _REG(TestAdobeOpenPbrSampleSoftensOnlyDiffuseComponent);
    _REG(TestAdobeOpenPbrPureSubsurfaceUsesRandomWalkPayload);
    _REG(TestOpenPbrRegularVolumeDoesNotDoubleTintTransmission);
    _REG(TestOpenPbrUsesCombinedInterfaceForThickTransmission);
    _REG(TestOpenPbrThinWalledUsesCombinedInterface);
    _REG(TestOpenPbrCoatDarkeningReachesBaseSubstrate);
    _REG(TestOpenPbrCoatColorAttenuatesSubstrate);
    _REG(TestOpenPbrThinFilmParametersReachBsdf);
    _REG(TestOpenPbrDispersionParametersReachBsdf);
    _REG(TestOpenPbrVolumeParametersReachClosure);
    _REG(TestTransmissionMediumShiftsNegativeAbsorptionLikeMaterialX);
    _REG(TestOpenPbrBaseDiffuseRoughnessUsesCanonicalName);
    _REG(TestOpenPbrSpecularRoughnessAnisotropyUsesCanonicalName);
    _REG(TestOpenPbrGeometryInputsUseCanonicalNames);
    _REG(TestOpenPbrGeometryCoatNormalUsesCanonicalName);
    _REG(TestOpenPbrCoatRoughnessAnisotropyUsesCanonicalName);
    _REG(TestOpenPbrGeometryCoatTangentUsesCanonicalName);
    _REG(TestDisneyPrincipledDefaults);
    _REG(TestDisneyPrincipledSpecularTintReachesDielectricLayer);
    _REG(TestDisneyPrincipledAnisotropyUsesMaterialXFormula);
    _REG(TestDisneyPrincipledSheenUsesMaterialXDefaults);
    _REG(TestDisneyPrincipledTransmissionUsesSharpDielectric);
    _REG(TestDisneyPrincipledClearcoatGlossControlsCoatRoughness);
    _REG(TestDisneyPrincipledSubsurfaceBuildsMediumState);
    _REG(TestGltfPbrDefaults);
    _REG(TestGltfPbrAlphaMask);
    _REG(TestGltfPbrIridescenceParametersReachBsdf);
    _REG(TestGltfPbrUsesDefaultWhiteColor82);
    _REG(TestGltfPbrDispersionStrengthReachesTransmissionAsAbbeNumber);
    _REG(TestGltfPbrClearcoatNormalReachesBsdf);
    _REG(TestGltfPbrClearcoatInheritsGeometricNormalByDefault);
    _REG(TestGltfPbrAnisotropyRotationRotatesTangent);
    _REG(TestUsdPreviewSurfaceDefaults);
    _REG(TestUsdPreviewSurfaceMetallicWorkflow);
    _REG(TestUsdPreviewSurfaceSpecularWorkflow);
    _REG(TestUsdPreviewSurfaceIorOneKeepsGrazingSpecular);
    _REG(TestUsdPreviewSurfaceIorControlsDielectricF0);
    _REG(TestUsdPreviewSurfaceMetallicF90UsesAlbedo);
    _REG(TestUsdPreviewSurfaceMetallicInterpolatesF0AndF90);
    _REG(TestUsdPreviewSurfaceIorReachesClearcoat);
    _REG(TestUsdPreviewSurfaceOpacityThreshold);
    _REG(TestUsdPreviewSurfaceTransparentModeKeepsLightingResponse);
    _REG(TestUsdPreviewSurfacePresenceModeCutsLightingResponse);
    _REG(TestUsdPreviewSurfaceMaterialXOpacityModeInteger);
    _REG(TestUsdPreviewSurfaceIgnoresOcclusion);
}

#undef _REG
