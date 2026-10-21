//
// Adobe OpenPBR adapter for MaterialXCpp.
//
#ifndef MXCPP_MATERIALS_ADOBE_OPEN_PBR_H
#define MXCPP_MATERIALS_ADOBE_OPEN_PBR_H

#include "bsdf.h"

#include <renderer/materials/MaterialXCpp/paramMap.h>
#include <renderer/materials/MaterialXCpp/surfaceClosure.h>

#include <memory>

namespace mxcpp {

/// Evaluate the OpenPBR Surface material model using Adobe's OpenPBR BSDF as
/// a whole-model backend.  When Adobe support is not compiled in, this falls
/// back to the native hdEmbree OpenPBR evaluator.
SurfaceClosure EvalAdobeOpenPbr(const ParamMap& params);
SurfaceClosure EvalAdobeOpenPbrVisibility(const ParamMap& params);

Vec3f EvalAdobeOpenPbr(const Bsdf::AdobeOpenPbrData& data,
                       const Vec3f& normalShdWldOut,
                       const Vec3f& normalSrfWldOut,
                       const Vec3f& omegaInWld,
                       const Vec3f& omegaOutWld);

struct AdobeOpenPbrEvalPdfResult
{
    Vec3f value = Vec3f(0.0f);
    Vec3f valueCosine = Vec3f(0.0f);
    float pdfSolidAngle = 0.0f;
    bool evaluated = false;
};

struct AdobeOpenPbrPreparedSurfaceState;

struct AdobeOpenPbrPreparedSurface
{
    std::shared_ptr<const AdobeOpenPbrPreparedSurfaceState> state;
    bool valid = false;
};

AdobeOpenPbrPreparedSurface
PrepareAdobeOpenPbrSurface(const SurfaceClosure& closure,
                           const SurfaceInteraction& interaction);

AdobeOpenPbrEvalPdfResult
EvalPdfAdobeOpenPbr(const Bsdf::AdobeOpenPbrData& data,
                    const Vec3f& normalShdWldOut, const Vec3f& omegaInWld,
                    const Vec3f& omegaOutWld);

AdobeOpenPbrEvalPdfResult TryEvalPdfAdobeOpenPbrSurface(
    const SurfaceClosure& closure, const SurfaceInteraction& interaction,
    const Vec3f& omegaInWld);

AdobeOpenPbrEvalPdfResult EvalPdfPreparedAdobeOpenPbrSurface(
    const AdobeOpenPbrPreparedSurface& preparedSurface,
    const Vec3f& omegaInWld);

float PdfAdobeOpenPbr(const Bsdf::AdobeOpenPbrData& data,
                      const Vec3f& normalShdWldOut, const Vec3f& omegaInWld,
                      const Vec3f& omegaOutWld);

Bsdf::BsdfSample SampleAdobeOpenPbr(const Bsdf::AdobeOpenPbrData& data,
                                    const Vec3f& normalShdLobeWldOut,
                                    const SurfaceInteraction& interaction,
                                    float u1, float u2, float uLobe);

Bsdf::BsdfSample SamplePreparedAdobeOpenPbrSurface(
    const AdobeOpenPbrPreparedSurface& preparedSurface,
    float u1,
    float u2,
    float uLobe);

MediumProperties MakeAdobeOpenPbrInteriorMedium(
    const Bsdf::AdobeOpenPbrData& data);

Vec3f AdobeOpenPbrEvalVolumeTransmittance(
    const MediumProperties& medium,
    float distance);

float AdobeOpenPbrSampleVolumeEventDistance(const MediumProperties& medium,
                                            const Vec3f& throughputRgb,
                                            float u);

Vec3f AdobeOpenPbrCalculateVolumeEventWeight(const MediumProperties& medium,
                                             const Vec3f& throughputRgb,
                                             float distance);

Vec3f AdobeOpenPbrCalculateVolumeSurfaceWeight(const MediumProperties& medium,
                                               const Vec3f& throughputRgb,
                                               float distance);

Vec3f AdobeOpenPbrSampleVolumePhase(const MediumProperties& medium,
                                    const Vec3f& omegaOutWld, float u1,
                                    float u2);

float AdobeOpenPbrEvalVolumePhasePdf(const MediumProperties& medium,
                                     const Vec3f& omegaInWld,
                                     const Vec3f& omegaOutWld);

}  // namespace mxcpp

#endif  // MXCPP_MATERIALS_ADOBE_OPEN_PBR_H
