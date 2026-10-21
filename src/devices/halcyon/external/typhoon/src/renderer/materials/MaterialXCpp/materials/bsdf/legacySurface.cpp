//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "legacySurface.h"
#include "dielectric.h"
#include "fresnel.h"
#include "mathPrimitives.h"
#include "microfacet.h"
#include "shadingFrame.h"

#include <algorithm>
#include <cmath>

namespace mxcpp {
namespace Bsdf {
namespace detail {

static Vec3f
_ComputeLegacyF0(const Vec3f& baseColor, float metallic,
                 float specular, float ior)
{
    float dielectricF0 = ((ior - 1.0f) / (ior + 1.0f));
    dielectricF0 *= dielectricF0;
    dielectricF0 *= specular;
    Vec3f F0 = Vec3f(dielectricF0);
    return F0 * (1.0f - metallic) + baseColor * metallic;
}

Vec3f
EvalLegacySurface(const SurfaceClosure& c,
                  const SurfaceInteraction& interaction,
                  const Vec3f& omegaInWld,
                  BumpShadowingContext bumpContext)
{
    const auto luminance = [&](const Vec3f& value) {
        return Bsdf::detail::Luminance(value, c.luminanceCoefficients);
    };
    const Vec3f normalShdLobeWldOut = interaction.normalShdWldOut;
    if (bumpContext == BumpShadowingContext::Evaluation &&
        !BumpHemisphereAgreement(
            interaction.normalSrfWldOut, normalShdLobeWldOut, omegaInWld)) {
        return Vec3f(0.0f);
    }
    const float diffuseBumpShadowing = BumpShadowingTerm(
        interaction.normalSrfWldOut, normalShdLobeWldOut, omegaInWld, true,
        bumpContext);
    float NdotL = Dot(normalShdLobeWldOut, omegaInWld);

    Vec3f reflected(0.0f);
    if (NdotL > 0.0f) {
        Vec3f F0 = _ComputeLegacyF0(
            c.baseColor, c.metallic, c.specular, c.specularIor);
        bool hasSpecularLobe = (luminance(F0) > kEpsilon);

        Vec3f H = (omegaInWld + interaction.omegaOutWld).normalized();
        float VdotH = std::max(Dot(interaction.omegaOutWld, H), 0.0f);

        Vec3f diffuse(0.0f);
        Vec3f specular(0.0f);

        if (hasSpecularLobe) {
            Vec3f fresnel = SchlickFresnel(F0, VdotH);
            Vec3f kD = CompMul(Vec3f(1.0f) - fresnel,
                               Vec3f(1.0f - c.metallic));
            kD = kD * (1.0f - c.transmission);
            diffuse = CompMul(
                kD, Bsdf::EvalLambertian(c.baseColor, normalShdLobeWldOut,
                                         omegaInWld, interaction.omegaOutWld));

            specular = Bsdf::EvalGGXSpecular(
                c.roughness, c.specularIor, CompMul(c.specularColor, F0),
                normalShdLobeWldOut, omegaInWld, interaction.omegaOutWld);
        } else {
            Vec3f kD = Vec3f(1.0f - c.metallic) * (1.0f - c.transmission);
            diffuse = CompMul(
                kD, Bsdf::EvalLambertian(c.baseColor, normalShdLobeWldOut,
                                         omegaInWld, interaction.omegaOutWld));
        }

        Vec3f sheen(0.0f);
        if (c.sheen > 0.0f) {
            sheen =
                Bsdf::EvalSheen(c.sheenColor, c.sheenRoughness,
                                normalShdLobeWldOut, omegaInWld, interaction.omegaOutWld) *
                c.sheen;
        }

        Vec3f coatContrib(0.0f);
        float coatAttenuation = 1.0f;
        if (c.coat > 0.0f) {
            coatContrib =
                Bsdf::EvalCoat(c.coat, c.coatRoughness, c.coatIor,
                               normalShdLobeWldOut, omegaInWld, interaction.omegaOutWld);
            float coatFresnel = SchlickFresnelScalar(c.coatIor, VdotH);
            coatAttenuation = 1.0f - c.coat * coatFresnel;
        }

        reflected =
            (diffuse * diffuseBumpShadowing + specular +
             sheen * diffuseBumpShadowing) * coatAttenuation +
                    coatContrib;
    }

    Vec3f transmitted(0.0f);
    if (c.transmission > 0.0f && NdotL < 0.0f) {
        float absNdotV = std::max(
            std::abs(Dot(normalShdLobeWldOut, interaction.omegaOutWld)), kEpsilon);
        float fresnel = SchlickFresnelScalar(c.specularIor, absNdotV);
        transmitted = c.transmissionColor *
            ((1.0f - fresnel) * c.transmission * kInvPi);
    }

    return SafeVec((reflected + transmitted) * c.presence);
}

float
PdfLegacySurface(const SurfaceClosure& c,
                 const SurfaceInteraction& interaction,
                 const Vec3f& omegaInWld)
{
    const auto luminance = [&](const Vec3f& value) {
        return Bsdf::detail::Luminance(value, c.luminanceCoefficients);
    };
    const Vec3f normalShdLobeWldOut = interaction.normalShdWldOut;
    Vec3f F0 = _ComputeLegacyF0(
        c.baseColor, c.metallic, c.specular, c.specularIor);
    bool hasSpecularLobe = (luminance(F0) > kEpsilon);

    float wDiffuse = (1.0f - c.metallic) * (1.0f - c.transmission) *
                     luminance(c.baseColor);
    float wSpecular = hasSpecularLobe ? (luminance(F0) + 0.05f) : 0.0f;
    float wCoat = (c.coat > 0.0f) ? c.coat * 0.04f : 0.0f;
    float wTransmission = (c.transmission > 0.0f)
        ? c.transmission * std::max(luminance(c.transmissionColor), 0.1f)
        : 0.0f;

    float total = wDiffuse + wSpecular + wCoat + wTransmission;
    if (total <= 0.0f) {
        return 0.0f;
    }

    float pDiffuse = wDiffuse / total;
    float pSpecular = wSpecular / total;
    float pCoat = wCoat / total;

    float pdfSolidAngle = 0.0f;
    pdfSolidAngle +=
        pDiffuse * Bsdf::PdfLambertian(normalShdLobeWldOut, omegaInWld);
    pdfSolidAngle +=
        pSpecular * Bsdf::PdfGGXSpecular(c.roughness, normalShdLobeWldOut,
                                         omegaInWld, interaction.omegaOutWld);
    if (c.coat > 0.0f) {
        pdfSolidAngle +=
            pCoat * Bsdf::PdfGGXSpecular(c.coatRoughness, normalShdLobeWldOut,
                                         omegaInWld, interaction.omegaOutWld);
    }
    return pdfSolidAngle;
}

Bsdf::BsdfSample
SampleLegacySurface(const SurfaceClosure& c,
                    const SurfaceInteraction& interaction,
                    float u1, float u2, float uLobe)
{
    const auto luminance = [&](const Vec3f& value) {
        return Bsdf::detail::Luminance(value, c.luminanceCoefficients);
    };
    const Vec3f normalShdLobeWldOut = interaction.normalShdWldOut;
    Vec3f F0 = _ComputeLegacyF0(
        c.baseColor, c.metallic, c.specular, c.specularIor);
    bool hasSpecularLobe = (luminance(F0) > kEpsilon);

    float wDiffuse = (1.0f - c.metallic) * (1.0f - c.transmission) *
                     luminance(c.baseColor);
    float wSpecular = hasSpecularLobe ? (luminance(F0) + 0.05f) : 0.0f;
    float wCoat = (c.coat > 0.0f) ? c.coat * 0.04f : 0.0f;
    float wTransmission = (c.transmission > 0.0f)
        ? c.transmission * std::max(luminance(c.transmissionColor), 0.1f)
        : 0.0f;

    float total = wDiffuse + wSpecular + wCoat + wTransmission;
    if (total <= 0.0f) {
        return Bsdf::BsdfSample{Vec3f(0.0f), Vec3f(0.0f), 0.0f, false};
    }

    float pDiffuse = wDiffuse / total;
    float pSpecular = wSpecular / total;
    float pCoat = wCoat / total;
    float cumDiffuse = pDiffuse;
    float cumSpecular = cumDiffuse + pSpecular;
    float cumCoat = cumSpecular + pCoat;

    const auto finalizeReflection = [&](Bsdf::BsdfSample sample,
                                        bool diffuseFamily) {
        sample.bsdfValueCosine = sample.bsdfValue *
            std::abs(Dot(normalShdLobeWldOut, sample.omegaInWld));
        const bool directionValid = SampledDirectionIsValid(
            false, normalShdLobeWldOut, interaction.normalGeomWldOut,
            sample.omegaInWld);
        const bool bumpValid = BumpHemisphereAgreement(
            interaction.normalSrfWldOut, normalShdLobeWldOut, sample.omegaInWld);
        if (!directionValid || (diffuseFamily && !bumpValid)) {
            sample.bsdfValue = Vec3f(0.0f);
            sample.bsdfValueCosine = Vec3f(0.0f);
            sample.pdfSolidAngle = 0.0f;
        }
        return sample;
    };

    if (uLobe < cumDiffuse) {
        auto sample = Bsdf::SampleLambertian(c.baseColor, normalShdLobeWldOut,
                                             interaction.omegaOutWld, u1, u2);
        if (sample.pdfSolidAngle <= 0.0f) {
            return Bsdf::BsdfSample{Vec3f(0.0f), Vec3f(0.0f), 0.0f, false};
        }
        sample.bsdfValue = EvalLegacySurface(c, interaction, sample.omegaInWld,
                                              BumpShadowingContext::Sampling);
        sample.pdfSolidAngle = PdfLegacySurface(c, interaction, sample.omegaInWld);
        return finalizeReflection(sample, true);
    }
    if (uLobe < cumSpecular) {
        Vec3f specCol = CompMul(c.specularColor, F0);
        auto sample =
            Bsdf::SampleGGXSpecular(c.roughness, c.specularIor, specCol,
                                    normalShdLobeWldOut, interaction.omegaOutWld, u1, u2);
        if (sample.pdfSolidAngle <= 0.0f) {
            return Bsdf::BsdfSample{Vec3f(0.0f), Vec3f(0.0f), 0.0f, false};
        }
        sample.bsdfValue = EvalLegacySurface(c, interaction, sample.omegaInWld,
                                              BumpShadowingContext::Sampling);
        sample.pdfSolidAngle = PdfLegacySurface(c, interaction, sample.omegaInWld);
        return finalizeReflection(sample, false);
    }
    if (uLobe < cumCoat) {
        float coatF0 = SchlickFresnelScalar(c.coatIor, 1.0f);
        auto sample =
            Bsdf::SampleGGXSpecular(c.coatRoughness, c.coatIor, Vec3f(coatF0),
                                    normalShdLobeWldOut, interaction.omegaOutWld, u1, u2);
        if (sample.pdfSolidAngle <= 0.0f) {
            return Bsdf::BsdfSample{Vec3f(0.0f), Vec3f(0.0f), 0.0f, false};
        }
        sample.bsdfValue = EvalLegacySurface(c, interaction, sample.omegaInWld,
                                              BumpShadowingContext::Sampling);
        sample.pdfSolidAngle = PdfLegacySurface(c, interaction, sample.omegaInWld);
        return finalizeReflection(sample, false);
    }
    Bsdf::BsdfSample sample = SampleDeltaTransmission(
        c.specularIor, c.transmissionColor * (c.transmission * c.presence),
        1.0f, interaction.normalShdWldOut, interaction.omegaOutWld, !interaction.frontFacing);
    const bool directionValid = SampledDirectionIsValid(
        true, normalShdLobeWldOut, interaction.normalGeomWldOut,
        sample.omegaInWld);
    if (!directionValid) {
        sample.bsdfValue = Vec3f(0.0f);
        sample.pdfSolidAngle = 0.0f;
    }
    return sample;
}

void
ClearLegacyBsdfSummary(SurfaceClosure* closure)
{
    closure->baseColor = Vec3f(0.0f);
    closure->metallic = 0.0f;
    closure->specular = 0.0f;
    closure->specularColor = Vec3f(0.0f);
    closure->transmission = 0.0f;
    closure->transmissionColor = Vec3f(0.0f);
    closure->coat = 0.0f;
    closure->sheen = 0.0f;
    closure->subsurfaceWeight = 0.0f;
}

}  // namespace detail
}  // namespace Bsdf
}  // namespace mxcpp
