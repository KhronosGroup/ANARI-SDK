// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// The ISPC side of test_path_material.cpp: kernels that shade points through
// the pathtracer's material interface (renderer/pathtracer/PathMaterial.isph)
// and write what it returns, so the C++ side can compare it with Typhoon's
// sequence run directly.

#include "renderer/pathtracer/PathMaterial.isph"
#include "shading/halcyon_sampler.isph"

// Must match test_path_material.cpp. Input per point: material index (-1:
// none), P[3], Ng[3], Nsrf[3], tangent[3], bitangent[3], wo[3], texcoord[2],
// hero wavelength, PathShadeFlag bits (as float), uPresence, uWavelength, u1,
// u2, uLobe, light direction[3], subsurface entry u1, u2.
#define IN_STRIDE 33
// Output per point: handle and flags (int bits), emission[3], Ns[3], hero
// wavelength and pdf, sample wi[3], f[3], pdf, flags (int bits), eta, eval
// towards the light f[3] and pdf, subsurface valid (int bits), entry[3],
// albedo[3], radius[3], anisotropy, precomputed (int bits), absorption[3],
// scattering[3], medium absorption[3], scattering[3], anisotropy, model (int
// bits), albedo[3].
#define OUT_STRIDE 52
// Shadow output per point: flags (int bits), opacity, presence, transmission,
// straight transmission, colour[3], medium absorption[3], scattering[3],
// anisotropy.
#define SHADOW_STRIDE 15

// The pathtracer's luminance coefficients (PathMaterialContext::luminance),
// which test_path_material.cpp sets
static uniform float g_luminance[3] = {0.f, 0.f, 0.f};

export void test_setLuminance(uniform float r, uniform float g, uniform float b)
{
  g_luminance[0] = r;
  g_luminance[1] = g;
  g_luminance[2] = b;
}

static inline void setContext(uniform PathMaterialContext &context,
    uniform bool useAdobeOpenPBR,
    uniform bool nativeMaterials)
{
  context.useAdobeOpenPBR = useAdobeOpenPBR;
  context.nativeMaterials = nativeMaterials;
  context.viewPosition[0] = context.viewPosition[1] =
      context.viewPosition[2] = 0.f;
  for (uniform int i = 0; i < 3; i++)
    context.luminance[i] = g_luminance[i];
}

export uniform int test_sizeofMaterial()
{
  return sizeof(uniform Material);
}

export uniform int test_programCount()
{
  return programCount;
}

export void test_setMaterial(void *uniform _m,
    uniform int kind,
    const void *uniform cppMaterial,
    uniform uint32 contextMask,
    uniform float r,
    uniform float g,
    uniform float b,
    uniform float opacity)
{
  uniform Material *uniform m = (uniform Material * uniform) _m;
  memset(m, 0, sizeof(uniform Material));
  m->kind = (uniform MaterialKind)kind;
  m->cppMaterial = cppMaterial;
  m->contextMask = contextMask;
  m->color = make_float4(r, g, b, 1.f);
  m->colorAttribute = ATTRIBUTE_NONE;
  m->opacity = opacity;
  m->opacityAttribute = ATTRIBUTE_NONE;
  m->colorAlphaIsOpacity = true;
}

// The attributes a material's graph reads (Material::attributeMask)
export void test_setAttributeMask(void *uniform _m, uniform uint32 mask)
{
  uniform Material *uniform m = (uniform Material * uniform) _m;
  m->attributeMask = mask;
}

// A MATERIAL_KIND_PHYSICALLY_BASED material's parameters
export void test_setPhysicallyBased(
    void *uniform _m, const uniform PhysicallyBasedParameters *uniform p)
{
  uniform Material *uniform m = (uniform Material * uniform) _m;
  m->physicallyBased = p;
}

static inline float ld(const uniform float *uniform src, int i, uniform int o)
{
  return src[i * IN_STRIDE + o];
}

static inline float3 ld3(const uniform float *uniform src, int i, uniform int o)
{
  return make_float3(ld(src, i, o), ld(src, i, o + 1), ld(src, i, o + 2));
}

static inline void st(uniform float *uniform out,
    uniform int stride,
    int i,
    uniform int o,
    float v)
{
  out[i * stride + o] = v;
}

static inline void st3(uniform float *uniform out,
    uniform int stride,
    int i,
    uniform int o,
    float3 v)
{
  st(out, stride, i, o, v.x);
  st(out, stride, i, o + 1, v.y);
  st(out, stride, i, o + 2, v.z);
}

static inline void stInt(uniform float *uniform out,
    uniform int stride,
    int i,
    uniform int o,
    int v)
{
  st(out, stride, i, o, floatbits(v));
}

static inline const uniform Material *varying materialOf(
    uniform Material *uniform materials,
    const uniform float *uniform src,
    int i)
{
  const int index = (int)ld(src, i, 0);
  return index >= 0 ? &materials[index] : NULL;
}

// The instance transform of every point, x' = -2y + 1, y' = 2x - 2,
// z' = 2z + 3, and its inverse (test_path_material.cpp has the same)
static inline affine3f objectToWorldOfPoints()
{
  return make_affine3f(make_linear3f(make_float3(0.f, 2.f, 0.f),
                           make_float3(-2.f, 0.f, 0.f),
                           make_float3(0.f, 0.f, 2.f)),
      make_float3(1.f, -2.f, 3.f));
}

static inline affine3f worldToObjectOfPoints()
{
  return make_affine3f(make_linear3f(make_float3(0.f, -0.5f, 0.f),
                           make_float3(0.5f, 0.f, 0.f),
                           make_float3(0.f, 0.f, 0.5f)),
      make_float3(1.f, 0.5f, -1.5f));
}

// Point i's shading input; its base colour and opacity are its material's
// constant colour and opacity (as alphaMode blend would make them), and a
// physicallyBased material's other parameters its constants, and its normal
// map's value its sampler's at the point's 'attribute0' (the texture
// coordinates, the samplers' input), as at a hit.
// The parts of the shading context in PathContextGroup are made up from the
// point (each component in one operation, so that test_path_material.cpp's
// contextOf() gets the same bits).
static inline void loadInput(uniform Material *uniform materials,
    const uniform float *uniform src,
    int i,
    varying PathShadeInput &input)
{
  const uniform Material *varying m = materialOf(materials, src, i);
  input.baseColor = m ? make_float3(m->color) : make_float3(0.f);
  input.baseOpacity = m ? m->opacity : 1.f;
  input.P = ld3(src, i, 1);
  input.objectP = input.P;
  input.Ng = ld3(src, i, 4);
  input.Nsrf = ld3(src, i, 7);
  input.tangent = ld3(src, i, 10);
  input.bitangent = ld3(src, i, 13);
  input.wo = ld3(src, i, 16);
  input.texcoord = make_float2(ld(src, i, 19), ld(src, i, 20));
  // 'attribute0', which the normal maps' sampler nodes read: the texture
  // coordinates (test_path_material.cpp's attributeOf() has the same)
  input.attributeMask = m ? m->attributeMask : 0u;
  input.attributePresent = input.attributeMask;
  input.attributes[ATTRIBUTE_0] =
      make_float4(ld(src, i, 19), ld(src, i, 20), 0.5f, 1.f);
  input.displayColor = make_float3(ld(src, i, 19), ld(src, i, 20), 0.25f);
  input.displayOpacity = 0.75f;
  input.faceId = 3;
  input.baryU = ld(src, i, 20);
  input.baryV = ld(src, i, 19);
  input.dPdu = input.tangent + input.bitangent;
  input.dPdv = input.bitangent - input.tangent;
  input.dPositiondu = input.tangent * 2.f;
  input.dPositiondv = input.bitangent * 3.f;
  input.dPdx = input.tangent * 0.01f;
  input.dPdy = input.bitangent * 0.02f;
  input.dPositiondx = input.tangent * 0.03f;
  input.dPositiondy = input.bitangent * 0.04f;
  input.dudx = 0.3f;
  input.dvdx = 0.05f;
  input.dudy = 0.2f;
  input.dvdy = 0.25f;
  input.objectToWorld = objectToWorldOfPoints();
  input.worldToObject = worldToObjectOfPoints();
  input.heroWavelengthNm = ld(src, i, 21);
  input.flags = (uint32)ld(src, i, 22);
  input.uPresence = ld(src, i, 23);
  input.uWavelength = ld(src, i, 24);
  input.u1 = ld(src, i, 25);
  input.u2 = ld(src, i, 26);
  input.uLobe = ld(src, i, 27);
  if (m && m->physicallyBased) {
#pragma ignore warning(perf)
    const uniform PhysicallyBasedParameters *varying p = m->physicallyBased;
    input.pbm = PathPbm_input(p->metallic.value,
        p->roughness.value,
        p->specular.value,
        p->specularColor.value,
        p->ior.value,
        p->emissive.value,
        p->clearcoat.value,
        p->clearcoatRoughness.value,
        p->sheenColor.value,
        p->sheenRoughness.value,
        p->transmission.value,
        p->thickness.value);
    // the samplers as PathTracer.ispc's makeShadeInput() samples them at a
    // hit and the graph's sampler nodes at this attribute value
#pragma ignore warning(perf)
    const uniform Sampler *varying normal = p->normal;
    input.pbm.hasNormalMap = normal != NULL;
    foreach_unique (s in normal) {
      if (Sampler_isValid(s))
        input.pbm.normalMap = Sampler_sampleAttribute(s,
            input.attributes[ATTRIBUTE_0],
            (uint32)input.faceId);
    }
#pragma ignore warning(perf)
    const uniform Sampler *varying coatNormal = p->clearcoatNormal;
    input.pbm.hasCoatNormalMap = coatNormal != NULL;
    foreach_unique (s in coatNormal) {
      if (Sampler_isValid(s))
        input.pbm.coatNormalMap = Sampler_sampleAttribute(s,
            input.attributes[ATTRIBUTE_0],
            (uint32)input.faceId);
    }
  }
}

// One path vertex per point, as the path loop will run it: shade, one eval
// towards the light direction, then the subsurface and medium queries
export void kernel_vertex(uniform bool useAdobeOpenPBR,
    uniform bool nativeMaterials,
    uniform Material *uniform materials,
    const uniform float *uniform src,
    uniform float *uniform out,
    uniform int n)
{
  uniform PathMaterialContext context;
  setContext(context, useAdobeOpenPBR, nativeMaterials);

  foreach (i = 0 ... n) {
    PathShadeInput input;
    loadInput(materials, src, i, input);
    PathMaterialState state;
    PathBsdfSample sample;
    PathMaterial_shade(
        &context, materialOf(materials, src, i), input, state, sample);

    PathBsdfEval eval;
    PathMaterial_eval(state, ld3(src, i, 28), eval);

    PathSubsurface sss;
    PathMaterial_subsurface(state, sample, ld(src, i, 31), ld(src, i, 32), sss);

    PathMedium medium;
    PathMaterial_medium(state, medium);

    const uniform int S = OUT_STRIDE;
    stInt(out, S, i, 0, state.handle);
    stInt(out, S, i, 1, state.flags);
    st3(out, S, i, 2, state.emission);
    st3(out, S, i, 5, state.Ns);
    st(out, S, i, 8, state.heroWavelengthNm);
    st(out, S, i, 9, state.heroWavelengthPdf);
    st3(out, S, i, 10, sample.wi);
    st3(out, S, i, 13, sample.f);
    st(out, S, i, 16, sample.pdf);
    stInt(out, S, i, 17, sample.flags);
    st(out, S, i, 18, sample.eta);
    st3(out, S, i, 19, eval.f);
    st(out, S, i, 22, eval.pdf);
    stInt(out, S, i, 23, sss.valid ? 1 : 0);
    st3(out, S, i, 24, sss.entryDir);
    st3(out, S, i, 27, sss.albedo);
    st3(out, S, i, 30, sss.radius);
    st(out, S, i, 33, sss.anisotropy);
    stInt(out, S, i, 34, sss.usePrecomputed ? 1 : 0);
    st3(out, S, i, 35, sss.absorption);
    st3(out, S, i, 38, sss.scattering);
    st3(out, S, i, 41, medium.absorption);
    st3(out, S, i, 44, medium.scattering);
    st(out, S, i, 47, medium.anisotropy);
    stInt(out, S, i, 48, medium.transportModel);
    st3(out, S, i, 49, state.albedo);
  }
}

// Each point as a shadow-ray blocker, the shadow ray running against wo
export void kernel_shadow(uniform bool useAdobeOpenPBR,
    uniform bool nativeMaterials,
    uniform Material *uniform materials,
    const uniform float *uniform src,
    uniform float *uniform out,
    uniform int n)
{
  uniform PathMaterialContext context;
  setContext(context, useAdobeOpenPBR, nativeMaterials);

  foreach (i = 0 ... n) {
    PathShadeInput input;
    loadInput(materials, src, i, input);
    PathShadowHit hit;
    PathMaterial_shadow(&context, materialOf(materials, src, i), input, hit);

    const uniform int S = SHADOW_STRIDE;
    stInt(out, S, i, 0, hit.flags);
    st(out, S, i, 1, hit.opacity);
    st(out, S, i, 2, hit.presence);
    st(out, S, i, 3, hit.transmission);
    st(out, S, i, 4, hit.straightTransmission);
    st3(out, S, i, 5, hit.transmissionColor);
    st3(out, S, i, 8, hit.interiorMedium.absorption);
    st3(out, S, i, 11, hit.interiorMedium.scattering);
    st(out, S, i, 14, hit.interiorMedium.anisotropy);
  }
}

// Shades the first gang of points 'a', then the first gang of points 'b', then
// evaluates a's closures towards a's light directions. Writes a's eval pdfs
// to out[lane].
export void kernel_twoVertices(uniform Material *uniform materials,
    const uniform float *uniform a,
    const uniform float *uniform b,
    uniform float *uniform out)
{
  uniform PathMaterialContext context;
  setContext(context, false, true);

  foreach (i = 0 ... programCount) {
    PathShadeInput input;
    PathMaterialState stateA, stateB;
    PathBsdfSample sample;
    loadInput(materials, a, i, input);
    PathMaterial_shade(
        &context, materialOf(materials, a, i), input, stateA, sample);
    loadInput(materials, b, i, input);
    PathMaterial_shade(
        &context, materialOf(materials, b, i), input, stateB, sample);
    PathBsdfEval eval;
    PathMaterial_eval(stateA, ld3(a, i, 28), eval);
    out[i] = eval.pdf;
  }
}
