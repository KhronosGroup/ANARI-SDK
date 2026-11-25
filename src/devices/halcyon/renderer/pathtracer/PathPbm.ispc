// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: LicenseRef-TOST-1.0
//
// MATERIAL_KIND_PHYSICALLY_BASED's shade and eval (PathPbm.isph), in their own
// file compiled without fused multiply-adds and with the system's math
// library (--opt=disable-fma --math-lib=system, CMakeLists.txt), so that
// they round as the C++ they port does (x86-64 builds have no FMA, and call
// the same cosf(), sinf() and powf()): sharp GGX lobes turn the last bits of
// a dot product or a direction into visible differences.
//
// Ported from Typhoon (NVIDIA-Omniverse/OpenUSD typhoon/main at 70c45e83b,
// pxr/imaging/plugin/hdEmbree/renderer/materials/MaterialXCpp/: EvalGltfPbr()
// of materials/gltfPbr.cpp, SampleNode(), _FinalizeSubtreeSample(),
// PruneCausticClassLobes() and PrepareShadingNormals() of
// materials/bsdf/closureTraversal.cpp, ResolveGraphNormal() and
// ValidateLeafNormals() of graph.cpp, the normal map and geometric nodes of
// nodes/geometricNodes.cpp, StraightShadowDielectricTransmission() of
// materials/bsdf.cpp; see PathPbmLobes.isph for the rest), Copyright
// 2024-2026 Pixar, Tomorrow Open Source Technology License 1.0; see
// external/typhoon/LICENSE.txt. Keep in step with those when the pin in
// external/typhoon/PROVENANCE.md moves. A mix weighs delta samples as
// ClosureSampling.h says, not as Typhoon's SampleNode() does, so that native
// and graph shading stay the same.

#include "PathMaterial.isph"
#include "PathPbmLobes.isph"

// Normals ////////////////////////////////////////////////////////////////////

// _kFloatEps of MaterialXCpp's geometric nodes
#define PATH_PBM_NODE_EPSILON 1e-6f

// The world tangent or bitangent node (ND_tangent_vector3/ND_bitangent_vector3
// in world space) of the shading context's 'v', 'fallback' (its surface
// derivative) where 'v' vanishes: normalized unless it vanishes too
static inline float3 PathPbm_frameVector(float3 v, float3 fallback)
{
  float3 r = v;
  if (PathMatte_dot(r, r) < PATH_PBM_NODE_EPSILON * PATH_PBM_NODE_EPSILON)
    r = fallback;
  if (PathMatte_dot(r, r) > PATH_PBM_NODE_EPSILON * PATH_PBM_NODE_EPSILON)
    r = PathPbm_normalized(r);
  return r;
}

// MaterialConverter.cpp's normalMap() of a sampler's value 'map': encoded to
// [0, 1], then ND_normalmap_float at scale 1 in the frame of the world
// normal, tangent and bitangent nodes
static inline float3 PathPbm_normalMap(
    const varying PathShadeInput &input, float4 map)
{
  const float3 encoded = make_float3(map) * 0.5f + 0.5f;
  const float3 value = PathMatte_dot(encoded, encoded) == 0.f
      ? make_float3(0.f, 0.f, 1.f)
      : encoded * 2.f - make_float3(1.f);
  const float3 T = PathPbm_frameVector(input.tangent, input.dPdu);
  const float3 B = PathPbm_frameVector(input.bitangent, input.dPdv);
  return PathPbm_normalized(T * value.x + B * value.y + input.Nsrf * value.z);
}

// The graph's normal, exterior (ResolveGraphNormal()): the smooth normal, or
// with a normal map its normal map, normalized and kept if valid and on the
// smooth normal's side (TryResolveShadingNormal())
static inline float3 PathPbm_graphNormal(const varying PathShadeInput &input)
{
  const float3 N = input.Nsrf;
  if (!input.pbm.hasNormalMap)
    return N;
  const float3 mapped = PathPbm_normalMap(input, input.pbm.normalMap);
  // SurfaceClosure::ResolveNormal() of a world-space normal
  const float lengthSquared = PathMatte_dot(mapped, mapped);
  if (!isfinite(mapped.x) || !isfinite(mapped.y) || !isfinite(mapped.z)
      || !isfinite(lengthSquared) || lengthSquared < 1.0e-18f)
    return N;
  float3 resolved;
  if (!PathMatte_tryResolveNormal(PathPbm_normalized(mapped), N, resolved))
    return N;
  return resolved;
}

// The clearcoat's normal, prepared (ValidateLeafNormals(),
// PrepareShadingNormals()): with a normal map of either kind its own, the
// 'clearcoatNormal' map or else the unperturbed normal, validated against
// the graph's normal 'graphN'; else the shading normal 'Ns'. Faced as Ns,
// then raised towards Ng.
static inline float3 PathPbm_coatNormal(const varying PathShadeInput &input,
    float3 graphN,
    float3 Ns,
    float3 Ng)
{
  float3 n = Ns;
  if (input.pbm.hasCoatNormalMap || input.pbm.hasNormalMap) {
    const float3 authored = input.pbm.hasCoatNormalMap
        ? PathPbm_normalMap(input, input.pbm.coatNormalMap)
        : input.Nsrf;
    float3 validated;
    if (!PathMatte_tryResolveNormal(authored, graphN, validated))
      validated = graphN;
    n = PathMatte_dot(validated, Ns) < 0.f ? -validated : validated;
  }
  return PathPbm_equal(n, Ng) ? n : PathPbm_validSpecularNormal(Ng, input.wo, n);
}

// Sampling ///////////////////////////////////////////////////////////////////

// The composite nodes a sample is finalized at (_FinalizeSubtreeSample()),
// from the bottom up; PathPbmLevels has their values
enum PathPbmLevel
{
  PATH_PBM_LEVEL_DIFFUSE_MIX = 0,
  PATH_PBM_LEVEL_LAYER,
  PATH_PBM_LEVEL_BASE,
  PATH_PBM_LEVEL_SHEEN,
  PATH_PBM_LEVEL_COAT,
  PATH_PBM_LEVEL_NONE
};

// SampleNode()'s choice between a node's two children with probability 'p'
// for the first: whether 'u' takes it, and 'u' remapped for the chosen child
static inline bool PathPbm_choose(float p, float &u)
{
  const bool first = u < p;
  u = first ? (p > 0.f ? u / p : 0.f)
            : (1.f - p > 0.f ? (u - p) / (1.f - p) : 0.f);
  return first;
}

// Scales a delta sample by a composite node's choice of it: through the
// layer's top throughput if it came from the base, by the inverse of its
// probability
static inline void PathPbm_scaleSpecular(
    PathPbmSample &s, float p, bool throughTop, float3 throughput)
{
  if (s.specular && s.pdf > 0.f) {
    if (throughTop)
      s.value = s.value * throughput;
    if (p > 0.f)
      s.value = s.value / p;
  }
}

// SampleNode() of the transmission mix (or what there is of it) with lobe
// choice 'u'. Each sampler returns the lowest composite node (PathPbmLevel)
// above the sampled lobe, where its finalization starts (at the nodes of
// those that exist).
static inline int PathPbm_sampleDiffuseMix(
    const uniform PathMaterialContext *uniform context,
    const varying PathMaterialState &state,
    const varying PathShadeInput &input,
    float u,
    PathPbmSample &s)
{
  if (state.pbm.diffuseNode == PATH_PBM_NODE_WHOLE) {
    const float t = state.pbm.transmission;
    // a mix passes a delta sample up unscaled (ClosureSampling.h: Typhoon
    // divides it by the choice's probability)
    if (PathPbm_choose(t, u)) {
      PathPbm_sampleTransmission(context, state, input.u1, input.u2, s);
    } else {
      PathPbm_sampleDiffuse(state, input.u1, input.u2, s);
    }
  } else {
    // the diffuse lobe, alone or weighed (a Multiply node, which a delta
    // sample would take times its weight; the diffuse lobe gives none)
    PathPbm_sampleDiffuse(state, input.u1, input.u2, s);
  }
  return PATH_PBM_LEVEL_DIFFUSE_MIX;
}

// SampleNode() of the dielectric layer (or what there is of it)
static inline int PathPbm_sampleLayer(
    const uniform PathMaterialContext *uniform context,
    const varying PathMaterialState &state,
    const varying PathShadeInput &input,
    float u,
    PathPbmSample &s)
{
  const PathPbmNode node = state.pbm.layerNode;
  if (node == PATH_PBM_NODE_CHILD)
    return PathPbm_sampleDiffuseMix(context, state, input, u, s);
  const PathPbmGlossy reflection = PathPbm_dielectric(state);
  if (node == PATH_PBM_NODE_TOP) {
    PathPbm_sampleGlossy(reflection,
        state.pbm.smoothN,
        state.Ng,
        state.wo,
        input.u1,
        input.u2,
        s);
    return PATH_PBM_LEVEL_LAYER;
  }
  const float topWeight = state.pbm.topWeight;
  const float baseWeight =
      state.pbm.diffuseMixApproxWeight * state.pbm.throughputLuminance;
  const float total = topWeight + baseWeight;
  if (total <= 0.f)
    return PATH_PBM_LEVEL_NONE; // no sample
  const float pTop = topWeight / total;
  if (PathPbm_choose(pTop, u)) {
    PathPbm_sampleGlossy(reflection,
        state.pbm.smoothN,
        state.Ng,
        state.wo,
        input.u1,
        input.u2,
        s);
    PathPbm_scaleSpecular(s, pTop, false, make_float3(1.f));
    return PATH_PBM_LEVEL_LAYER;
  }
  const int level = PathPbm_sampleDiffuseMix(context, state, input, u, s);
  PathPbm_scaleSpecular(s, 1.f - pTop, true, state.pbm.throughput);
  return level;
}

// SampleNode() of the base (or what there is of it)
static inline int PathPbm_sampleBase(
    const uniform PathMaterialContext *uniform context,
    const varying PathMaterialState &state,
    const varying PathShadeInput &input,
    float u,
    PathPbmSample &s)
{
  const PathPbmNode node = state.pbm.baseNode;
  if (node == PATH_PBM_NODE_WHOLE) {
    // a mix passes a delta sample up unscaled, as above
    const float mix = state.pbm.mix;
    if (PathPbm_choose(mix, u)) {
      PathPbm_sampleGlossy(PathPbm_metal(state),
          state.pbm.smoothN,
          state.Ng,
          state.wo,
          input.u1,
          input.u2,
          s);
      return PATH_PBM_LEVEL_BASE;
    }
    return PathPbm_sampleLayer(context, state, input, u, s);
  }
  const int level = PathPbm_sampleLayer(context, state, input, u, s);
  // a Multiply node takes a delta sample times its weight
  if (node == PATH_PBM_NODE_SCALE && s.specular && s.pdf > 0.f)
    s.value = s.value * state.pbm.baseScale;
  return level;
}

// SampleNode() of the sheen layer (or what there is of it)
static inline int PathPbm_sampleSheenLayer(
    const uniform PathMaterialContext *uniform context,
    const varying PathMaterialState &state,
    const varying PathShadeInput &input,
    float u,
    PathPbmSample &s)
{
  const uint32 parts = state.pbm.parts;
  if (!(parts & PATH_PBM_SHEEN))
    return PathPbm_sampleBase(context, state, input, u, s);
  if (!(parts & PATH_PBM_BASE)) {
    // the sheen alone: its layer doesn't exist, the clearcoat's may
    PathPbm_sampleSheen(state, input.u1, input.u2, s);
    return PATH_PBM_LEVEL_SHEEN;
  }
  const float topWeight = state.pbm.sheenApproxWeight;
  const float baseWeight =
      state.pbm.baseApproxWeight * state.pbm.sheenThroughputLuminance;
  const float total = topWeight + baseWeight;
  if (total <= 0.f)
    return PATH_PBM_LEVEL_NONE;
  const float pTop = topWeight / total;
  if (PathPbm_choose(pTop, u)) {
    PathPbm_sampleSheen(state, input.u1, input.u2, s);
    return PATH_PBM_LEVEL_SHEEN;
  }
  const int level = PathPbm_sampleBase(context, state, input, u, s);
  PathPbm_scaleSpecular(
      s, 1.f - pTop, true, make_float3(state.pbm.sheenThroughput));
  return level;
}

// SampleNode() of the whole closure
static inline int PathPbm_sampleTree(
    const uniform PathMaterialContext *uniform context,
    const varying PathMaterialState &state,
    const varying PathShadeInput &input,
    PathPbmSample &s)
{
  const uint32 parts = state.pbm.parts;
  float u = input.uLobe;
  if (!(parts & PATH_PBM_COAT))
    return PathPbm_sampleSheenLayer(context, state, input, u, s);
  const PathPbmGlossy coat = PathPbm_coat(state);
  if (!(parts & (PATH_PBM_BASE | PATH_PBM_SHEEN))) {
    PathPbm_sampleGlossy(coat,
        state.pbm.smoothN,
        state.Ng,
        state.wo,
        input.u1,
        input.u2,
        s);
    return PATH_PBM_LEVEL_COAT;
  }
  const float topWeight = state.pbm.coatApproxWeight;
  const float baseWeight =
      state.pbm.sheenLayerApproxWeight * state.pbm.coatThroughputLuminance;
  const float total = topWeight + baseWeight;
  if (total <= 0.f)
    return PATH_PBM_LEVEL_NONE;
  const float pTop = topWeight / total;
  if (PathPbm_choose(pTop, u)) {
    PathPbm_sampleGlossy(coat,
        state.pbm.smoothN,
        state.Ng,
        state.wo,
        input.u1,
        input.u2,
        s);
    PathPbm_scaleSpecular(s, pTop, false, make_float3(1.f));
    return PATH_PBM_LEVEL_COAT;
  }
  const int level = PathPbm_sampleSheenLayer(context, state, input, u, s);
  PathPbm_scaleSpecular(
      s, 1.f - pTop, true, make_float3(state.pbm.coatThroughput));
  return level;
}

// _FinalizeSubtreeSample() at every composite node from 'level' up: a
// sample whose pdf is still positive takes each node's value with its cosine
// and pdf, a delta one keeps its (scaled) value
static inline void PathPbm_finalize(const varying PathMaterialState &state,
    int level,
    PathPbmSample &s)
{
  // a delta sample's pdf stays 1 (max(pdf, 1) at every node)
  if (s.specular || !(s.pdf > 0.f) || level == PATH_PBM_LEVEL_NONE)
    return;
  PathPbmLevels l;
  PathPbm_evalLevels(state, s.wi, true, l);
  const uint32 parts = state.pbm.parts;
  const bool base = (parts & PATH_PBM_BASE) != 0;
  for (uniform int k = 0; k < PATH_PBM_LEVEL_NONE; k++) {
    bool exists = false;
    float3 valueCosine;
    float pdf;
    if (k == PATH_PBM_LEVEL_DIFFUSE_MIX) {
      exists = base
          && (state.pbm.diffuseNode == PATH_PBM_NODE_WHOLE
              || state.pbm.diffuseNode == PATH_PBM_NODE_SCALE);
      valueCosine = l.diffuseMix;
      pdf = l.pdfDiffuseMix;
    } else if (k == PATH_PBM_LEVEL_LAYER) {
      exists = base && state.pbm.layerNode == PATH_PBM_NODE_WHOLE;
      valueCosine = l.layer;
      pdf = l.pdfLayer;
    } else if (k == PATH_PBM_LEVEL_BASE) {
      exists = base
          && (state.pbm.baseNode == PATH_PBM_NODE_WHOLE
              || state.pbm.baseNode == PATH_PBM_NODE_SCALE);
      valueCosine = l.base;
      pdf = l.pdfBase;
    } else if (k == PATH_PBM_LEVEL_SHEEN) {
      exists = (parts & PATH_PBM_SHEEN) && base;
      valueCosine = l.sheen;
      pdf = l.pdfSheen;
    } else {
      exists =
          (parts & PATH_PBM_COAT) && (parts & (PATH_PBM_BASE | PATH_PBM_SHEEN));
      valueCosine = l.coat;
      pdf = l.pdfCoat;
    }
    if (k >= level && exists && s.pdf > 0.f) {
      s.valueCosine = valueCosine;
      s.pdf = pdf;
    }
  }
}

// Shading ////////////////////////////////////////////////////////////////////

void PathPbm_shade(const uniform PathMaterialContext *uniform context,
    const uniform Material *varying material,
    const varying PathShadeInput &input,
    varying PathMaterialState &state,
    varying PathBsdfSample &sample)
{
  const bool front = (input.flags & PATH_SHADE_FRONT_FACING) != 0;
  // the base's and the sheen's lobes have no normal of their own: they
  // inherit the graph's normal, faced, the glossy ones raised towards Ng
  // (PrepareShadingNormals())
  const float3 graphN = PathPbm_graphNormal(input);
  state.Ns = PathMaterial_faceNormal(graphN, front);
  state.Ng = PathMaterial_faceNormal(input.Ng, front);
  state.pbm.smoothN = PathMaterial_faceNormal(input.Nsrf, front);
  state.wo = input.wo;
  state.lobeN = state.Ns;
  state.lobeColor = input.baseColor;
  state.pbm.glossyN = PathPbm_equal(state.Ns, state.Ng)
      ? state.Ns
      : PathPbm_validSpecularNormal(state.Ng, state.wo, state.Ns);
  state.pbm.parts = 0;
  state.pbm.backside = !front;

  // EvalGltfPbr()'s presence; the strict u < presence keeps u = 0 from
  // hitting an absent surface
  const float presence = PathPbm_clamp01(input.baseOpacity);
  if (presence < 1.f && (presence <= 0.f || input.uPresence >= presence)) {
    state.flags = PATH_MATERIAL_PRESENCE_SKIPPED;
    return;
  }

  // EvalGltfPbr()'s lobes: the base's
  const PathPbmInput params = input.pbm;
  const float r = params.roughness;
  state.pbm.alpha = clamp(r * r, 1.0e-5f, 1.f);
  state.pbm.mix = PathPbm_clamp01(params.metallic);
  const float oneMinusIor = 1.f - params.ior;
  const float onePlusIor = 1.f + params.ior;
  const float f0 =
      (oneMinusIor * oneMinusIor) / max(onePlusIor * onePlusIor, 1.0e-5f);
  const float specular = PathPbm_clamp01(params.specular);
  state.pbm.specularF0 = PathPbm_clamp01(params.specularColor * f0) * specular;
  state.pbm.specularF90 = specular;
  const bool delta = state.pbm.alpha < PATH_PBM_DELTA_ALPHA;
  // the transmission's
  state.pbm.transmission = PathPbm_clamp01(params.transmission);
  state.pbm.ior = max(params.ior, 1.f);
  // the sheen's
  const float3 sheenColor = params.sheenColor;
  const float sheenIntensity =
      PathPbm_clamp01(max(sheenColor.x, max(sheenColor.y, sheenColor.z)));
  const bool hasSheen = sheenIntensity > 0.f;
  if (hasSheen) {
    state.pbm.sheenWeight = sheenIntensity;
    state.pbm.sheenColor = make_float3(sheenColor.x / sheenIntensity,
        sheenColor.y / sheenIntensity,
        sheenColor.z / sheenIntensity);
    state.pbm.sheenRoughness = params.sheenRoughness * params.sheenRoughness;
  }
  // the clearcoat's
  state.pbm.coatWeight = PathPbm_clamp01(params.clearcoat);
  const bool hasCoat = state.pbm.coatWeight > 0.f;
  const float cr = params.clearcoatRoughness;
  state.pbm.coatAlpha = clamp(cr * cr, 1.0e-5f, 1.f);
  const bool coatDelta = state.pbm.coatAlpha < PATH_PBM_DELTA_ALPHA;
  if (hasCoat)
    state.pbm.coatN = PathPbm_coatNormal(input, graphN, state.Ns, state.Ng);

  // Caustic pruning (PruneCausticClassLobes()) removes the transmission and
  // delta glossy lobes, and a node that loses a child leaves the other: a
  // mix's weighed by its share (gone below kEpsilon, itself from 1 -
  // kEpsilon), a layer's alone. Otherwise every node is whole.
  const bool prune = (input.flags & PATH_SHADE_PRUNE_CAUSTICS) != 0;
  state.pbm.diffuseNode = PATH_PBM_NODE_WHOLE;
  state.pbm.layerNode = PATH_PBM_NODE_WHOLE;
  state.pbm.baseNode = PATH_PBM_NODE_WHOLE;
  state.pbm.diffuseScale = 1.f;
  state.pbm.baseScale = 1.f;
  if (prune) {
    // ScaleNode()
    const float diffuseWeight = 1.f - state.pbm.transmission;
    state.pbm.diffuseNode = diffuseWeight <= PATH_MATTE_EPSILON
        ? PATH_PBM_NODE_NONE
        : diffuseWeight >= 1.f - PATH_MATTE_EPSILON ? PATH_PBM_NODE_CHILD
                                                  : PATH_PBM_NODE_SCALE;
    state.pbm.diffuseScale = diffuseWeight;
    const bool glossy = !delta;
    const bool diffuseMix = state.pbm.diffuseNode != PATH_PBM_NODE_NONE;
    state.pbm.layerNode = glossy && diffuseMix ? PATH_PBM_NODE_WHOLE
        : glossy                               ? PATH_PBM_NODE_TOP
        : diffuseMix                           ? PATH_PBM_NODE_CHILD
                                               : PATH_PBM_NODE_NONE;
    // the metal goes with the reflection, so a layer left without it is
    // its transmission mix
    if (!glossy) {
      const float layerWeight = 1.f - state.pbm.mix;
      state.pbm.baseNode = !diffuseMix || layerWeight <= PATH_MATTE_EPSILON
          ? PATH_PBM_NODE_NONE
          : layerWeight >= 1.f - PATH_MATTE_EPSILON ? PATH_PBM_NODE_CHILD
                                                  : PATH_PBM_NODE_SCALE;
      state.pbm.baseScale = layerWeight;
    }
  }
  uint32 parts = state.pbm.baseNode != PATH_PBM_NODE_NONE ? PATH_PBM_BASE : 0;
  if (hasSheen)
    parts |= PATH_PBM_SHEEN;
  if (hasCoat && !(coatDelta && prune))
    parts |= PATH_PBM_COAT;
  state.pbm.parts = parts;
  const bool hasBsdf = parts != 0;

  // The closure's facts (ClosureClassification.cpp): every lobe but the
  // transmission reflects only; the closure as evaluated does while
  // effectively opaque and its transmission or the transmission mix's share
  // of the base is effectively 0, the pruned one (without transmission)
  // while effectively opaque. Thin-walled at thickness 0 or less.
  uint32 flags = PATH_MATERIAL_HAS_CLOSURE;
  if (hasBsdf)
    flags |= PATH_MATERIAL_HAS_BSDF;
  if (params.thickness <= 0.f)
    flags |= PATH_MATERIAL_THIN_WALLED;
  const bool opaque = input.baseOpacity >= PATH_PBM_OPAQUE;
  const bool reflectionOnly = opaque
      && (state.pbm.mix >= PATH_PBM_OPAQUE
          || state.pbm.transmission <= 1.0e-6f);
  if (reflectionOnly)
    flags |= PATH_MATERIAL_REFLECTION_ONLY;
  if (hasBsdf && (prune ? opaque : reflectionOnly))
    flags |= PATH_MATERIAL_BSDF_REFLECTION_ONLY;

  // ClosureAlbedo.cpp's summary of the closure as evaluated: the dielectric
  // layer (its reflection's F0 over the transmission mix of the diffuse
  // colour and the transmission's (1 - F0) times it) and the metal's colour,
  // mixed; under the sheen, then the clearcoat (each top's albedo t over the
  // rest: t + (1 - t) rest). Every lobe that contributes is a delta lobe only
  // for delta lobes where neither the diffuse lobe (the metal or the
  // transmission takes its place) nor the sheen nor a rough clearcoat is.
  {
    const float tm = state.pbm.transmission;
    const float rt = (state.pbm.ior - 1.f) / (state.pbm.ior + 1.f);
    const float ft = isfinite(rt) ? clamp(rt * rt, 0.f, 1.f) : 0.f;
    const float3 transmission = state.lobeColor * (1.f * (1.f - ft));
    const float3 diffuseMix = tm <= 0.f ? state.lobeColor
        : tm >= 1.f                     ? transmission
                    : state.lobeColor * (1.f - tm) + transmission * tm;
    const float3 t = PathPbm_clampFinite01(state.pbm.specularF0);
    const float3 layer = t + (make_float3(1.f) - t) * diffuseMix;
    const float3 metal = PathPbm_clampFinite01(state.lobeColor);
    const float m = state.pbm.mix;
    float3 a = m <= 0.f ? layer
        : m >= 1.f      ? metal
                        : layer * (1.f - m) + metal * m;
    if (hasSheen) {
      const float3 sheen = PathPbm_clampFinite01(state.pbm.sheenColor
          * (state.pbm.sheenWeight
              * PathPbm_sheenDirAlbedo(
                  1.f, clamp(state.pbm.sheenRoughness, 0.f, 1.f))));
      a = sheen + (make_float3(1.f) - sheen) * a;
    }
    if (hasCoat) {
      const float r = (PATH_PBM_COAT_IOR - 1.f) / (PATH_PBM_COAT_IOR + 1.f);
      const float f = isfinite(r) ? clamp(r * r, 0.f, 1.f) : 0.f;
      const float3 coat = PathPbm_clampFinite01(
          make_float3(1.f) * (state.pbm.coatWeight * f));
      a = coat + (make_float3(1.f) - coat) * a;
    }
    state.albedo = PathPbm_clampFinite01(a);
    if (delta && (m >= 1.f || tm >= 1.f) && !hasSheen
        && (!hasCoat || coatDelta) && (input.flags & PATH_SHADE_ALBEDO))
      flags |= PATH_MATERIAL_SPECULAR_ONLY;
  }
  state.flags = flags;
  state.emission = params.emissive;
  if (!hasBsdf)
    return;

  // What the lobe choices and the layers weigh, all of wo (_ApproxWeight(),
  // _EvalThroughput())
  if (parts & PATH_PBM_BASE) {
    const float diffuseApproxWeight =
        max(PathPbm_luminance(context, state.lobeColor), 0.f);
    // the transmission mix (the transmission's finite weight times 0 adds
    // nothing at transmission 0)
    float diffuseMixApproxWeight = diffuseApproxWeight;
    if (state.pbm.diffuseNode == PATH_PBM_NODE_WHOLE
        && state.pbm.transmission > 0.f) {
      const float t = state.pbm.transmission;
      diffuseMixApproxWeight = diffuseApproxWeight * (1.f - t)
          + PathPbm_transmissionApproxWeight(context, state) * t;
    } else if (state.pbm.diffuseNode == PATH_PBM_NODE_SCALE) {
      diffuseMixApproxWeight = diffuseApproxWeight
          * max(PathPbm_luminance(context, make_float3(state.pbm.diffuseScale)),
              0.f);
    }
    state.pbm.diffuseMixApproxWeight = diffuseMixApproxWeight;
    // the dielectric layer
    const float3 n = state.pbm.glossyN;
    const float NdotWo = PathMatte_dot(n, state.wo);
    const float NdotV = max(abs(NdotWo), PATH_MATTE_EPSILON);
    float layerApproxWeight = diffuseMixApproxWeight;
    if (state.pbm.layerNode != PATH_PBM_NODE_CHILD) {
      const float3 F = PathPbm_schlick(
          state.pbm.specularF0, make_float3(state.pbm.specularF90), NdotV);
      state.pbm.throughput = PathPbm_clamp01(make_float3(1.f)
          - PathPbm_layerReflectance(
              PathPbm_alphaForEnergy(state.pbm.alpha), NdotV, F));
      state.pbm.throughputLuminance =
          PathPbm_luminance(context, state.pbm.throughput);
      state.pbm.topWeight =
          max(PathPbm_luminance(context, F), PATH_PBM_MIN_APPROX_WEIGHT);
      state.pbm.missingToSingle = PathPbm_missingToSingle(
          clamp(state.pbm.alpha, PATH_PBM_MIN_ALPHA, 1.f),
          max(NdotWo, PATH_MATTE_EPSILON));
      layerApproxWeight = state.pbm.layerNode == PATH_PBM_NODE_WHOLE
          ? state.pbm.topWeight
              + diffuseMixApproxWeight * state.pbm.throughputLuminance
          : state.pbm.topWeight;
    }
    state.pbm.layerApproxWeight = layerApproxWeight;
    // the base
    if (state.pbm.baseNode == PATH_PBM_NODE_WHOLE) {
      const float3 Fmetal =
          PathPbm_schlick(state.lobeColor, make_float3(1.f), NdotV);
      const float metalWeight =
          max(PathPbm_luminance(context, Fmetal), PATH_PBM_MIN_APPROX_WEIGHT);
      state.pbm.baseApproxWeight = layerApproxWeight * (1.f - state.pbm.mix)
          + metalWeight * state.pbm.mix;
    } else if (state.pbm.baseNode == PATH_PBM_NODE_SCALE) {
      state.pbm.baseApproxWeight = layerApproxWeight
          * max(PathPbm_luminance(context, make_float3(state.pbm.baseScale)),
              0.f);
    } else {
      state.pbm.baseApproxWeight = layerApproxWeight;
    }
  }
  float below = state.pbm.baseApproxWeight;
  if (parts & PATH_PBM_SHEEN) {
    const float NdotV =
        max(abs(PathMatte_dot(state.lobeN, state.wo)), PATH_MATTE_EPSILON);
    const float dirAlbedo =
        PathPbm_sheenDirAlbedo(NdotV, state.pbm.sheenRoughness);
    state.pbm.sheenThroughput =
        PathPbm_clamp01(1.f - dirAlbedo * state.pbm.sheenWeight);
    state.pbm.sheenThroughputLuminance =
        PathPbm_luminance(context, make_float3(state.pbm.sheenThroughput));
    state.pbm.sheenApproxWeight = state.pbm.sheenWeight
        * max(PathPbm_luminance(context, state.pbm.sheenColor), 0.f) * 0.25f;
    below = (parts & PATH_PBM_BASE)
        ? state.pbm.sheenApproxWeight
            + state.pbm.baseApproxWeight * state.pbm.sheenThroughputLuminance
        : state.pbm.sheenApproxWeight;
  }
  state.pbm.sheenLayerApproxWeight = below;
  if (parts & PATH_PBM_COAT) {
    const float3 n = state.pbm.coatN;
    const float NdotWo = PathMatte_dot(n, state.wo);
    const float NdotV = max(abs(NdotWo), PATH_MATTE_EPSILON);
    const float3 F = PathPbm_dielectricSchlick(
        make_float3(PathPbm_dielectricF0(PATH_PBM_COAT_IOR)), NdotV);
    state.pbm.coatApproxWeight =
        max(PathPbm_luminance(context, F) * state.pbm.coatWeight,
            PATH_PBM_MIN_APPROX_WEIGHT);
    const float filter = PathPbm_dielectricReflFrontFilter(NdotV,
        PathPbm_bsdlLayerRoughness(state.pbm.coatAlpha),
        PATH_PBM_COAT_IOR);
    state.pbm.coatThroughput = PathPbm_clamp01(
        1.f * (1.f - state.pbm.coatWeight) + filter * state.pbm.coatWeight);
    state.pbm.coatThroughputLuminance =
        PathPbm_luminance(context, make_float3(state.pbm.coatThroughput));
    state.pbm.coatMissingToSingle = PathPbm_missingToSingle(
        clamp(state.pbm.coatAlpha, PATH_PBM_MIN_ALPHA, 1.f),
        max(NdotWo, PATH_MATTE_EPSILON));
  }

  // SampleNode() down the tree, scaling a delta sample by each layer's
  // choice, then _FinalizeSubtreeSample() up it
  PathPbmSample s;
  PathPbmSample_clear(s);
  const int level = PathPbm_sampleTree(context, state, input, s);
  PathPbm_finalize(state, level, s);

  sample.wi = s.wi;
  sample.f = s.specular ? s.value : s.valueCosine;
  sample.pdf = s.pdf;
  sample.eta = s.eta;
  sample.flags = (s.pdf > 0.f ? PATH_SAMPLE_VALID : 0)
      | (s.specular ? PATH_SAMPLE_SPECULAR : 0)
      | (s.transmission ? PATH_SAMPLE_TRANSMISSION : 0)
      | (s.diffuseLike ? PATH_SAMPLE_DIFFUSE_LIKE : 0);
}

void PathPbm_eval(const varying PathMaterialState &state,
    const varying float3 wi,
    varying PathBsdfEval &result)
{
  PathPbm_evalTree(state, wi, false, result.f, result.pdf);
}

void PathPbm_shadow(
    const varying PathShadeInput &input, varying PathShadowHit &hit)
{
  hit.flags = PATH_MATERIAL_HAS_CLOSURE;
  if (input.pbm.thickness <= 0.f)
    hit.flags |= PATH_MATERIAL_THIN_WALLED;
  hit.opacity = input.baseOpacity;
  hit.presence = input.baseOpacity;
  hit.transmission = input.pbm.transmission;
  hit.transmissionColor = input.baseColor;
  // StraightShadowDielectricTransmission() (no coupled interface: Schlick's)
  // of the shadow ray's cosine, -dot(wo, Ng)
  const float transmission = clamp(input.pbm.transmission, 0.f, 1.f);
  if (transmission > 0.f) {
    const float cosTheta =
        PathPbm_clamp01(abs(-PathMatte_dot(input.wo, input.Ng)));
    const float straight = PathPbm_clamp01(
        1.f - PathPbm_schlickScalar(max(input.pbm.ior, 1.f), cosTheta));
    hit.straightTransmission = clamp(transmission * straight, 0.f, 1.f);
  }
}
