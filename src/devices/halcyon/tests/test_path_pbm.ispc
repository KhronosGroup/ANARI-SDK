// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// The ISPC side of test_path_pbm.cpp: native physicallyBased's lobe math
// (renderer/pathtracer/PathPbmLobes.isph) on arrays of inputs, which the C++
// side compares with the vendored MaterialXCpp functions it ports. Built
// without fused multiply-adds, as PathPbm.ispc is.

#include "renderer/pathtracer/PathMaterial.isph"
#include "renderer/pathtracer/PathPbmLobes.isph"

static inline float3 ld3(const uniform float *uniform src, int i, uniform int o)
{
  return make_float3(src[i + o], src[i + o + 1], src[i + o + 2]);
}

static inline void st3(uniform float *uniform out, int i, float3 v)
{
  out[i] = v.x;
  out[i + 1] = v.y;
  out[i + 2] = v.z;
}

// In: cosTheta, alpha. Out: missing energy, missing / single (Turquin).
export void kernel_missingEnergy(
    const uniform float *uniform src, uniform float *uniform out, uniform int n)
{
  foreach (i = 0 ... n) {
    const float c = src[2 * i], a = src[2 * i + 1];
    out[2 * i] = PathPbm_ggxMissingEnergy(c, a);
    out[2 * i + 1] = PathPbm_missingToSingle(a, c);
  }
}

// In: F0[3], F90[3], cosTheta, alpha. Out: generalized Schlick F[3], the
// layer's reflectance[3] (of F at that cosine and alpha).
export void kernel_fresnel(
    const uniform float *uniform src, uniform float *uniform out, uniform int n)
{
  foreach (i = 0 ... n) {
    const float3 F0 = ld3(src, 8 * i, 0);
    const float3 F90 = ld3(src, 8 * i, 3);
    const float c = src[8 * i + 6], a = src[8 * i + 7];
    const float3 F = PathPbm_schlick(F0, F90, c);
    st3(out, 6 * i, F);
    st3(out, 6 * i + 3, PathPbm_layerReflectance(a, c, F));
  }
}

// In: local wo[3] (z > 0), alpha, u1, u2, local wi[3]. Out: the sampled
// visible normal[3], its reflection pdf, PdfGGX_VNDF() of the normal halfway
// between wo and wi.
export void kernel_vndf(
    const uniform float *uniform src, uniform float *uniform out, uniform int n)
{
  foreach (i = 0 ... n) {
    const float3 wo = ld3(src, 9 * i, 0);
    const float a = src[9 * i + 3];
    const float3 wm = PathPbm_sampleVndf(wo, a, src[9 * i + 4], src[9 * i + 5]);
    st3(out, 5 * i, wm);
    out[5 * i + 3] = PathPbm_pdfVndf(wo, wm, a);
    const float3 h = PathPbm_normalized(wo + ld3(src, 9 * i, 6));
    out[5 * i + 4] = PathPbm_pdfVndf(wo, h, a);
  }
}

// In: Ng[3], wo[3], n[3]. Out: EnsureValidSpecularReflection()[3].
export void kernel_validNormal(
    const uniform float *uniform src, uniform float *uniform out, uniform int n)
{
  foreach (i = 0 ... n) {
    st3(out,
        3 * i,
        PathPbm_validSpecularNormal(
            ld3(src, 9 * i, 0), ld3(src, 9 * i, 3), ld3(src, 9 * i, 6)));
  }
}

// One GGX leaf, a generalized Schlick one (the base's) or a dielectric (the
// clearcoat). In: lobe normal[3] (prepared), smooth normal[3], Ng[3], wo[3],
// F0[3], F90[3], alpha, wi[3], u1, u2, weight, dielectric (0/1). Out: eval
// towards wi (value with cosine[3], evaluation context), pdf towards wi, then
// the sample: wi[3], value[3] (bsdfValue if specular, else with its
// cosine), pdf, specular (0/1).
#define LEAF_IN 26
#define LEAF_OUT 12
export void kernel_glossyLeaf(
    const uniform float *uniform src, uniform float *uniform out, uniform int n)
{
  foreach (i = 0 ... n) {
    const uniform int I = LEAF_IN, O = LEAF_OUT;
    PathPbmGlossy lobe;
    lobe.n = ld3(src, I * i, 0);
    const float3 smoothN = ld3(src, I * i, 3);
    const float3 Ng = ld3(src, I * i, 6);
    const float3 wo = ld3(src, I * i, 9);
    lobe.F0 = ld3(src, I * i, 12);
    lobe.F90 = ld3(src, I * i, 15);
    lobe.alpha = src[I * i + 18];
    lobe.weight = src[I * i + 24];
    lobe.dielectric = src[I * i + 25] != 0.f;
    lobe.missingToSingle = PathPbm_missingToSingle(
        clamp(lobe.alpha, PATH_PBM_MIN_ALPHA, 1.f),
        max(PathMatte_dot(lobe.n, wo), PATH_MATTE_EPSILON));
    const float3 wi = ld3(src, I * i, 19);

    st3(out, O * i, PathPbm_evalGlossy(lobe, smoothN, wo, wi, false));
    out[O * i + 3] = PathPbm_pdfGlossy(lobe, wo, wi);

    PathPbmSample s;
    PathPbmSample_clear(s);
    PathPbm_sampleGlossy(
        lobe, smoothN, Ng, wo, src[I * i + 22], src[I * i + 23], s);
    st3(out, O * i + 4, s.wi);
    st3(out, O * i + 7, s.specular ? s.value : s.valueCosine);
    out[O * i + 10] = s.pdf;
    out[O * i + 11] = s.specular ? 1.f : 0.f;
  }
}

// One sheen leaf. In: its normal[3], smooth normal[3], Ng[3], wo[3],
// colour[3], weight, roughness (squared), wi[3], u1, u2. Out: eval towards wi
// (value with cosine[3]), pdf, then the sample: wi[3], value with cosine[3],
// pdf; the throughput's directional albedo at |cos wo|.
#define SHEEN_IN 22
#define SHEEN_OUT 12
export void kernel_sheenLeaf(
    const uniform float *uniform src, uniform float *uniform out, uniform int n)
{
  foreach (i = 0 ... n) {
    const uniform int I = SHEEN_IN, O = SHEEN_OUT;
    PathMaterialState state;
    state.lobeN = ld3(src, I * i, 0);
    state.pbm.smoothN = ld3(src, I * i, 3);
    state.Ng = ld3(src, I * i, 6);
    state.wo = ld3(src, I * i, 9);
    state.pbm.sheenColor = ld3(src, I * i, 12);
    state.pbm.sheenWeight = src[I * i + 15];
    state.pbm.sheenRoughness = src[I * i + 16];
    const float3 wi = ld3(src, I * i, 17);

    float3 valueCosine;
    float pdf;
    PathPbm_evalSheen(state, wi, valueCosine, pdf);
    st3(out, O * i, valueCosine);
    out[O * i + 3] = pdf;

    PathPbmSample s;
    PathPbmSample_clear(s);
    PathPbm_sampleSheen(state, src[I * i + 20], src[I * i + 21], s);
    st3(out, O * i + 4, s.wi);
    st3(out, O * i + 7, s.valueCosine);
    out[O * i + 10] = s.pdf;
    out[O * i + 11] = PathPbm_sheenDirAlbedo(
        max(abs(PathMatte_dot(state.lobeN, state.wo)), PATH_MATTE_EPSILON),
        state.pbm.sheenRoughness);
  }
}

// In: cosTheta, perceptual roughness, ior. Out: BSDL's reflection-only
// dielectric layer filter.
export void kernel_reflFrontFilter(
    const uniform float *uniform src, uniform float *uniform out, uniform int n)
{
  foreach (i = 0 ... n) {
    out[i] = PathPbm_dielectricReflFrontFilter(
        src[3 * i], src[3 * i + 1], src[3 * i + 2]);
  }
}

// The transmission leaf. In: its normal[3] (prepared), smooth normal[3],
// Ng[3], wo[3], tint[3], alpha, ior (at least 1), backside (0/1), wi[3], u1,
// u2. Out: eval towards wi (value with cosine[3], evaluation context), pdf,
// then the sample: wi[3], value[3] (bsdfValue if specular, else with its
// cosine), pdf, specular, transmission (0/1), eta.
#define TRANSMISSION_IN 23
#define TRANSMISSION_OUT 14
export void kernel_transmissionLeaf(const uniform float *uniform src,
    uniform float *uniform out,
    uniform int n,
    uniform float luminanceR,
    uniform float luminanceG,
    uniform float luminanceB)
{
  uniform PathMaterialContext context;
  context.luminance[0] = luminanceR;
  context.luminance[1] = luminanceG;
  context.luminance[2] = luminanceB;
  foreach (i = 0 ... n) {
    const uniform int I = TRANSMISSION_IN, O = TRANSMISSION_OUT;
    PathMaterialState state;
    state.pbm.glossyN = ld3(src, I * i, 0);
    state.pbm.smoothN = ld3(src, I * i, 3);
    state.Ng = ld3(src, I * i, 6);
    state.wo = ld3(src, I * i, 9);
    state.lobeColor = ld3(src, I * i, 12);
    state.pbm.alpha = src[I * i + 15];
    state.pbm.ior = src[I * i + 16];
    state.pbm.backside = src[I * i + 17] != 0.f;
    const float3 wi = ld3(src, I * i, 18);

    st3(out, O * i, PathPbm_evalTransmission(state, wi, false));
    out[O * i + 3] = PathPbm_pdfTransmission(state, wi);

    PathPbmSample s;
    PathPbmSample_clear(s);
    PathPbm_sampleTransmission(
        &context, state, src[I * i + 21], src[I * i + 22], s);
    st3(out, O * i + 4, s.wi);
    st3(out, O * i + 7, s.specular ? s.value : s.valueCosine);
    out[O * i + 10] = s.pdf;
    out[O * i + 11] = s.specular ? 1.f : 0.f;
    out[O * i + 12] = s.transmission ? 1.f : 0.f;
    out[O * i + 13] = s.eta;
  }
}
