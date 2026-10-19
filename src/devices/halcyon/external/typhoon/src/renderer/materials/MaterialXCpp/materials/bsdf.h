//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef MXCPP_MATERIALS_BSDF_H
#define MXCPP_MATERIALS_BSDF_H

#include <renderer/materials/MaterialXCpp/materials/surfaceInteraction.h>
#include <renderer/materials/MaterialXCpp/surfaceClosure.h>

namespace mxcpp {

/// Standalone BSDF evaluation functions.
///
/// Each function evaluates a single BSDF lobe for a given pair of
/// incident (omegaInWld) and outgoing (omegaOutWld) directions. Directions
/// point away from the surface.
namespace Bsdf
{
    enum class DielectricLayerThroughputMode
    {
        Bsdl,
        MaterialXGlsl
    };

    /// Selects the rough dielectric top-layer throughput estimate.
    void SetDielectricLayerThroughputMode(
        DielectricLayerThroughputMode mode);
    DielectricLayerThroughputMode GetDielectricLayerThroughputMode();

    // ------------------------------------------------------------------
    // Evaluation (Phase 4)
    // ------------------------------------------------------------------

    /// Lambertian diffuse BRDF: f = albedo / pi.
    Vec3f EvalLambertian(const Vec3f& baseColor, const Vec3f& normalShdWldOut,
                         const Vec3f& omegaInWld, const Vec3f& omegaOutWld);

    /// GGX microfacet specular BRDF (Cook-Torrance).
    Vec3f EvalGGXSpecular(float roughness, float ior,
                          const Vec3f& specularColor,
                          const Vec3f& normalShdWldOut, const Vec3f& omegaInWld,
                          const Vec3f& omegaOutWld);

    /// GGX microfacet transmission BTDF. The normal is incident-facing;
    /// `backside` selects glass-to-air rather than air-to-glass IOR order.
    Vec3f EvalGGXTransmission(float roughness, float ior,
                              const Vec3f& transmissionColor,
                              const Vec3f& normalShdWldOut,
                              const Vec3f& omegaInWld,
                              const Vec3f& omegaOutWld, bool backside);

    /// Charlie sheen BRDF (Imageworks model).
    Vec3f EvalSheen(const Vec3f& sheenColor, float roughness,
                    const Vec3f& normalShdWldOut, const Vec3f& omegaInWld,
                    const Vec3f& omegaOutWld);

    /// Clear-coat GGX specular lobe.
    Vec3f EvalCoat(float coatWeight, float coatRoughness, float coatIor,
                   const Vec3f& normalShdWldOut, const Vec3f& omegaInWld,
                   const Vec3f& omegaOutWld);

    /// Evaluate the full layered surface model from a closure.
    /// Combines all BSDF lobes with proper energy conservation.
    /// `interaction` carries the graph, smooth, and geometric normals plus the
    /// outgoing direction and immutable interface state. Tree traversal reads
    /// each surface leaf's prepared normal rather than the graph normal.
    /// Returns the outgoing radiance contribution for one light sample.
    Vec3f EvalSurface(const SurfaceClosure& closure,
                      const SurfaceInteraction& interaction,
                      const Vec3f& omegaInWld);

    /// Evaluate the full surface model with each leaf multiplied by the
    /// absolute incident cosine of that leaf's resolved shading normal.
    /// Composite closures may contain leaves with different corrected normals,
    /// so this projection cannot be applied once after EvalSurface().
    Vec3f EvalSurfaceCosine(const SurfaceClosure& closure,
                            const SurfaceInteraction& interaction,
                            const Vec3f& omegaInWld);

    // ------------------------------------------------------------------
    // Sampling & PDF (Phase 9)
    // ------------------------------------------------------------------

    struct BsdfSample {
        Vec3f omegaInWld;
        Vec3f bsdfValue;
        float pdfSolidAngle;
        bool    isSpecular;
        // Finite-PDF closure value projected per leaf by that leaf's exact
        // shading normal. Delta and subsurface samples leave this zero.
        Vec3f   bsdfValueCosine = Vec3f(0.0f);
        bool    isSubsurface = false;
        bool    hasSubsurfaceEntryDirection = false;
        // True for diffuse / translucent / subsurface-like scattering that
        // makes a later sharp specular or boundary-crossing event part of the
        // caustic-class path heuristic. Glossy dielectric traversal is not
        // diffuse-like even when it has a finite PDF.
        bool    isDiffuseLike = false;
        bool    isTransmission = false;
        // Selected lobe normal for a subsurface marker. Other samples must not
        // consume this payload.
        Vec3f   normalShdLobeWldOut = Vec3f(0.0f);
        float   eta = 1.0f;     // IOR ratio (incident/transmitted), for refraction differential propagation
    };

    /// Cosine-weighted hemisphere sampling for Lambertian diffuse.
    BsdfSample SampleLambertian(const Vec3f& baseColor,
                                const Vec3f& normalShdWldOut,
                                const Vec3f& omegaOutWld, float u1, float u2);

    float PdfLambertian(const Vec3f& normalShdWldOut, const Vec3f& omegaInWld);

    /// GGX visible-normal distribution function (VNDF) sampling for
    /// microfacet specular reflection.
    BsdfSample SampleGGXSpecular(float roughness, float ior,
                                 const Vec3f& specularColor,
                                 const Vec3f& normalShdWldOut,
                                 const Vec3f& omegaOutWld, float u1, float u2);

    float PdfGGXSpecular(float roughness, const Vec3f& normalShdWldOut,
                         const Vec3f& omegaInWld, const Vec3f& omegaOutWld);

    /// Directional energy helpers for the isotropic GGX reflection lobe.
    /// alphaRoughness is the GGX alpha parameter, not perceptual roughness.
    float GgxDirectionalMissingEnergy(
        float cosTheta,
        float alphaRoughness);

    float GgxDirectionalSingleScatterEnergy(
        float cosTheta,
        float alphaRoughness);

    /// Directional-hemispherical transmission albedo of the coupled rough
    /// dielectric interface. roughness contains GGX alpha values; anisotropy
    /// is reduced symmetrically to one azimuth-independent scalar. Compensation
    /// is applied when its argument is enabled.
    float CoupledRoughDielectricDirectionalTransmissionAlbedo(
        float cosTheta,
        const Vec2f& roughness,
        float ior,
        bool backfacing,
        bool compensateMultipleScattering);

    /// Per-interface dielectric factor used by straight transparent shadows.
    /// signedCosTheta is positive for an inside-to-outside crossing.
    float StraightShadowDielectricTransmission(
        const SurfaceClosure& closure,
        float signedCosTheta);

    /// GGX VNDF-based transmission sampling. The normal is incident-facing;
    /// `backside` selects glass-to-air rather than air-to-glass IOR order.
    BsdfSample SampleGGXTransmission(float roughness, float ior,
                                     const Vec3f& transmissionColor,
                                     const Vec3f& normalShdWldOut,
                                     const Vec3f& omegaOutWld, float u1,
                                     float u2, bool backside);

    float PdfGGXTransmission(float roughness, float ior,
                             const Vec3f& normalShdWldOut,
                             const Vec3f& omegaInWld,
                             const Vec3f& omegaOutWld, bool backside);

    /// Unified surface sampler: selects a lobe proportional to its
    /// approximate energy contribution, then importance-samples that lobe.
    /// The PDF accounts for all lobes (mixed PDF). The smooth and geometric
    /// normals must be incident-facing. A selected direction on the wrong
    /// geometric side is returned invalid with zero BSDF and PDF; it is not
    /// replaced by another lobe.
    BsdfSample SampleSurface(const SurfaceClosure& closure,
                             const SurfaceInteraction& interaction,
                             float u1, float u2, float uLobe);

    /// Evaluates the surface mixture PDF.
    float PdfSurface(const SurfaceClosure& closure,
                     const SurfaceInteraction& interaction,
                     const Vec3f& omegaInWld);

    /// Return a copy of `closure` with lobes that would be discarded by the
    /// caustic-class path heuristic removed from the BSDF tree.
    ///
    /// This is intended for enableCaustics=false after a diffuse-like
    /// ancestor. It removes transmission/boundary-crossing lobes and delta
    /// reflection lobes while keeping diffuse, translucent, sheen, subsurface,
    /// and rough reflection response available for sampling and NEE.
    SurfaceClosure PruneCausticClassLobes(
        const SurfaceClosure& closure);

    /// Sample a direction entering a subsurface medium using the surface's
    /// specular dielectric parameters (roughness + IOR).
    ///
    /// - Smooth surface (roughness < threshold): deterministic Snell
    /// refraction.
    /// - Rough surface: GGX VNDF samples microfacet normal H, then Snell about
    /// H.
    /// - IOR is clamped to >= 1.0 so there is no TIR at entry (matches Cycles).
    /// - Returns false on degenerate input or when the generated direction is
    /// not below both the selected lobe and geometric normals.
    ///
    /// Source: influenced by Cycles `subsurface_entry_bounce` in
    /// intern/cycles/kernel/integrator/subsurface.h (Apache 2.0).
    bool SampleSubsurfaceEntry(const SurfaceClosure& closure,
                               const Vec3f& normalShdLobeWldOut,
                               const Vec3f& normalGeomWldOut,
                               const Vec3f& omegaOutWld, float u1, float u2,
                               Vec3f& outDirEntryWld);

    // ------------------------------------------------------------------
    // MIS utilities
    // ------------------------------------------------------------------

    /// Power heuristic with exponent 2 (following Veach).
    inline float
    PowerHeuristic(float pdfFirst, float pdfSecond)
    {
        const float pdfFirstSquared = pdfFirst * pdfFirst;
        const float pdfSecondSquared = pdfSecond * pdfSecond;
        return pdfFirstSquared / (pdfFirstSquared + pdfSecondSquared + 1e-10f);
    }
}

}  // namespace mxcpp

#endif  // MXCPP_MATERIALS_BSDF_H
