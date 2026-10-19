// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: LicenseRef-TOST-1.0 AND Apache-2.0
//
// The pathtracer's random-walk subsurface scattering (Subsurface.isph), in
// its own file compiled without fused multiply-adds and with the system's
// math library (--opt=disable-fma --math-lib=system, CMakeLists.txt), so that
// it rounds as the C++ it ports does (x86-64 builds have no FMA, and call the
// same expf(), logf(), powf(), atanf(), sinf() and cosf()): the walk branches
// on its numbers at every step, so a last-bit difference would send it
// elsewhere.
//
// Ported from Typhoon (NVIDIA-Omniverse/OpenUSD typhoon/main at 70c45e83b,
// pxr/imaging/plugin/hdEmbree/renderer/integrator/: ty::RandomWalkSSS(),
// ty::ChiangRemap(), the Dwivedi helpers and the walk's private helpers of
// sss.cpp), Copyright 2018-2026 Pixar, Tomorrow Open Source Technology
// License 1.0; see external/typhoon/LICENSE.txt. The walk's Henyey-Greenstein
// sampling and channel MIS are medium.cpp's, ported in Medium.ispc.
// Typhoon takes Chiang's remap and the Dwivedi sampling from Blender Cycles'
// intern/cycles/kernel/integrator/subsurface_random_walk.h (Apache-2.0).
// Keep in step with those files when the pin in
// external/typhoon/PROVENANCE.md moves.
//
// Where the port differs from Typhoon: the walk traces the world's scene in
// world space with a filter that drops every hit but the owner's (Typhoon
// traces the owner's own prototype scene in its object space, which holds
// nothing else), and its rays start at epsilonFrom() of their origin (the
// entry ray at the entry's offset, SubsurfaceWalkInput::entryBias), where
// Typhoon's start at a fixed 1e-4, which also bounds the opposite
// interface's distance.

#include "renderer/pathtracer/CppRounding.isph"
#include "renderer/pathtracer/Medium.isph"
#include "renderer/pathtracer/Subsurface.isph"
#include "renderer/pathtracer/Transport.isph"

// Typhoon's constants (sss.cpp)
#define SSS_VOLUME_THROUGHPUT_EPS 1.0e-6f
#define SSS_EXTINCTION_EPS 1.0e-6f
#define SSS_MIN_ALPHA 0.2f
#define SSS_SIMILARITY_LEVEL 9
#define SSS_REDUCED_GUIDED_FRACTION 0.75f
#define SSS_PI 3.14159265358979323846f
// OpenUSD's GF_MIN_VECTOR_LENGTH
#define SSS_GF_MIN_VECTOR_LENGTH 1e-10f
#define SSS_FLT_MAX 3.402823466e+38f
// Room for an RTCRayHit and its 16-byte alignment
#define SSS_RAYHIT_STORAGE 144

// Vector arithmetic as Typhoon's types round it /////////////////////////////

// GfVec3f::Normalize(): times the reciprocal of the length (at least
// GF_MIN_VECTOR_LENGTH), taken in double
static inline float3 gfNormalize(float3 v)
{
  const float length = sqrt(cppDot(v, v));
  const float s =
      length > SSS_GF_MIN_VECTOR_LENGTH ? length : SSS_GF_MIN_VECTOR_LENGTH;
  const double r = 1.d / (double)s;
  return make_float3((float)((double)v.x * r),
      (float)((double)v.y * r),
      (float)((double)v.z * r));
}

// rendererMath.h's TryNormalizeDirection(): in double, scaled by the
// largest component
static inline bool sssTryNormalize(float3 v, float3 &n)
{
  if (!isfinite(v.x) || !isfinite(v.y) || !isfinite(v.z))
    return false;
  const double m =
      max((double)abs(v.x), max((double)abs(v.y), (double)abs(v.z)));
  if (!isfinite(m) || m == 0.d)
    return false;
  const double x = v.x / m;
  const double y = v.y / m;
  const double z = v.z / m;
  const double scaledLength = sqrt(x * x + y * y + z * z);
  if (!isfinite(scaledLength) || scaledLength == 0.d)
    return false;
  const float3 r = make_float3((float)(x / scaledLength),
      (float)(y / scaledLength),
      (float)(z / scaledLength));
  if (!isfinite(r.x) || !isfinite(r.y) || !isfinite(r.z))
    return false;
  n = r;
  return true;
}

// normalTransforms.h's TransformNormalToWorld(): through the normal matrix
// 'm', normalized; the normal itself normalized where the transform is
// singular, else +z
static inline float3 sssTransformNormal(const linear3f &m, float3 n)
{
  float3 r;
  if (sssTryNormalize(xfmVector(m, n), r))
    return r;
  if (sssTryNormalize(n, r))
    return r;
  return make_float3(0.f, 0.f, 1.f);
}

static inline float3 sssTransposeTimes(const linear3f &m, float3 v)
{
  return make_float3(cppDot(m.x, v), cppDot(m.y, v), cppDot(m.z, v));
}

static inline float sssMax3(float3 v)
{
  // std::max({a, b, c}): the first largest
  float r = v.x;
  if (r < v.y)
    r = v.y;
  if (r < v.z)
    r = v.z;
  return r;
}

static inline float sssComponent(float3 v, int i)
{
  return i == 0 ? v.x : (i == 1 ? v.y : v.z);
}

// sss.cpp: the remap and Dwivedi sampling ////////////////////////////////////

// _ChiangRemapChannel()
static inline void chiangRemapChannel(float albedo,
    float radius,
    float anisotropy,
    float &extinctionOutput,
    float &rawAlphaOutput)
{
  const float anisotropy2 = anisotropy * anisotropy;
  const float anisotropy3 = anisotropy2 * anisotropy;
  const float anisotropy4 = anisotropy3 * anisotropy;
  const float anisotropy5 = anisotropy4 * anisotropy;
  const float anisotropy6 = anisotropy5 * anisotropy;
  const float anisotropy7 = anisotropy6 * anisotropy;

  const float coefficientA = 1.8260523782f + -1.28451056436f * anisotropy
      + -1.79904629312f * anisotropy2 + 9.19393289202f * anisotropy3
      + -22.8215585862f * anisotropy4 + 32.0234874259f * anisotropy5
      + -23.6264803333f * anisotropy6 + 7.21067002658f * anisotropy7;
  const float coefficientB = 4.98511194385f
      + 0.127355959438f
          * exp(31.1491581433f * anisotropy + -201.847017512f * anisotropy2
              + 841.576016723f * anisotropy3 + -2018.09288505f * anisotropy4
              + 2731.71560286f * anisotropy5 + -1935.41424244f * anisotropy6
              + 559.009054474f * anisotropy7);
  const float coefficientC = 1.09686102424f + -0.394704063468f * anisotropy
      + 1.05258115941f * anisotropy2 + -8.83963712726f * anisotropy3
      + 28.8643230661f * anisotropy4 + -46.8802913581f * anisotropy5
      + 38.5402837518f * anisotropy6 + -12.7181042538f * anisotropy7;
  const float coefficientD = 0.496310210422f + 0.360146581622f * anisotropy
      + -2.15139309747f * anisotropy2 + 17.8896899217f * anisotropy3
      + -55.2984010333f * anisotropy4 + 82.065982243f * anisotropy5
      + -58.5106008578f * anisotropy6 + 15.8478295021f * anisotropy7;
  const float coefficientE = 4.23190299701f
      + 0.00310603949088f
          * exp(76.7316253952f * anisotropy + -594.356773233f * anisotropy2
              + 2448.8834203f * anisotropy3 + -5576.68528998f * anisotropy4
              + 7116.60171912f * anisotropy5 + -4763.54467887f * anisotropy6
              + 1303.5318055f * anisotropy7);
  const float coefficientF = 2.40602999408f + -2.51814844609f * anisotropy
      + 9.18494908356f * anisotropy2 + -79.2191708682f * anisotropy3
      + 259.082868209f * anisotropy4 + -403.613804597f * anisotropy5
      + 302.85712436f * anisotropy6 + -87.4370473567f * anisotropy7;

  const float blend = pow(albedo, 0.25f);

  float alpha = (1.f - blend) * coefficientA
          * pow(atan(coefficientB * albedo), coefficientC)
      + blend * coefficientD * pow(atan(coefficientE * albedo), coefficientF);
  alpha = clamp(alpha, 0.f, 0.999999f);

  const float extinctionPrime = 1.f / max(radius, 1.0e-16f);
  extinctionOutput = extinctionPrime / max(1.f - anisotropy, 1.0e-6f);
  rawAlphaOutput = alpha;
}

void Subsurface_chiangRemap(float3 albedo,
    float3 radius,
    float anisotropy,
    varying float3 &extinction,
    varying float3 &alpha,
    varying float3 &rawAlpha)
{
  const float g = clamp(anisotropy, -0.99f, 0.99f);
  chiangRemapChannel(clamp(albedo.x, 0.f, 1.f),
      max(radius.x, 1.0e-16f),
      g,
      extinction.x,
      rawAlpha.x);
  chiangRemapChannel(clamp(albedo.y, 0.f, 1.f),
      max(radius.y, 1.0e-16f),
      g,
      extinction.y,
      rawAlpha.y);
  chiangRemapChannel(clamp(albedo.z, 0.f, 1.f),
      max(radius.z, 1.0e-16f),
      g,
      extinction.z,
      rawAlpha.z);
  // the min-alpha clamp for numerical stability (Cycles')
  alpha = make_float3(max(rawAlpha.x, SSS_MIN_ALPHA),
      max(rawAlpha.y, SSS_MIN_ALPHA),
      max(rawAlpha.z, SSS_MIN_ALPHA));
}

float Subsurface_diffusionLengthDwivedi(float alpha)
{
  // Eq. 67 of d'Eon and Krivanek 2020
  alpha = clamp(alpha, 1.0e-6f, 0.999999f);
  const float denom =
      1.f - pow(alpha, 2.44294f - 0.0215813f * alpha + 0.578637f / alpha);
  return 1.f / sqrt(max(denom, 1.0e-12f));
}

float Subsurface_evalPhaseDwivedi(
    float diffusionLength, float phaseLog, float cosTheta)
{
  // Eq. 9 of Meng, Hanika and Dachsbacher 2016
  return 1.f / (max(diffusionLength - cosTheta, 1.0e-6f) * phaseLog);
}

float Subsurface_samplePhaseDwivedi(
    float diffusionLength, float phaseLog, float u1)
{
  // Eq. 10 of Meng, Hanika and Dachsbacher 2016: the inverse CDF
  const float uClamped = clamp(u1, 0.f, 1.f);
  return diffusionLength - (diffusionLength + 1.f) * exp(-uClamped * phaseLog);
}

float Subsurface_backwardDwivediFraction(
    float distanceOpposite, float distanceFromEntryPlane, float diffusionLength)
{
  if (distanceOpposite <= 0.f || diffusionLength <= 0.f)
    return 0.f;
  const float clamped = clamp(distanceFromEntryPlane, 0.f, distanceOpposite);
  const float exponent = (distanceOpposite - 2.f * clamped) / diffusionLength;
  return 1.f / (1.f + exp(exponent));
}

// The walk ///////////////////////////////////////////////////////////////////

// _SssWalkState
struct SssWalkState
{
  float3 posRayOrg;
  float3 dirRay;
  float3 throughput;
  float3 extinction;
  float3 scattering;
  float3 alpha;
  float anisotropy;
  bool haveOppositeInterface;
  float distanceOpposite;
  float3 posEntry;
  float3 guideN;
  float diffusionLength;
  float phaseLog;
  float guidedFraction;
  float3 extinctionStar;
  float3 scatteringStar;
};

// _SssTraceResult
struct SssTraceResult
{
  bool foundSurface;
  float distance;
  float3 posHit;
  float3 NgWorldExt;
  float3 NgObjectExt;
  uint32 primID;
  float u;
  float v;
};

// _CleanCoefficient()
static inline float cleanCoefficient(float value)
{
  return (isfinite(value) && value > 0.f) ? value : 0.f;
}

// _InitPrecomputedCoefficients() of one channel
static inline void precomputedChannel(float absorption,
    float scattering,
    float &extinctionOutput,
    float &scatteringOutput,
    float &alphaOutput)
{
  const float a = cleanCoefficient(absorption);
  const float s = cleanCoefficient(scattering);
  scatteringOutput = s;
  extinctionOutput = a + s;
  alphaOutput = extinctionOutput > SSS_EXTINCTION_EPS
      ? clamp(s / extinctionOutput, 0.f, 0.999999f)
      : 0.f;
}

// _InitDwivediAndSimilarity(): false where the diffusion length is too close
// to 1 (phaseLog would explode)
static inline bool initDwivediAndSimilarity(SssWalkState &state)
{
  state.diffusionLength =
      Subsurface_diffusionLengthDwivedi(sssMax3(state.alpha));
  if (state.diffusionLength <= 1.f + 1.0e-6f)
    return false;
  state.phaseLog =
      log((state.diffusionLength + 1.f) / (state.diffusionLength - 1.f));
  // favour classic sampling for strong HG anisotropy, guided for isotropic
  state.guidedFraction = 1.f - max(0.5f, pow(abs(state.anisotropy), 0.125f));
  // similarity-reduced coefficients (Wrenninge, Villemin and Hery)
  state.scatteringStar = state.scattering * (1.f - state.anisotropy);
  state.extinctionStar =
      state.extinction - state.scattering + state.scatteringStar;
  return true;
}

// _BuildOrthonormalBasis()
static inline void buildOrthonormalBasis(float3 z, float3 &x, float3 &y)
{
  if (abs(z.z) < 0.999f)
    x = make_float3(-z.y, z.x, 0.f);
  else
    x = cppCross(make_float3(0.f, 1.f, 0.f), z);
  if (cppDot(x, x) <= 1.0e-12f)
    x = make_float3(1.f, 0.f, 0.f);
  else
    x = gfNormalize(x);
  y = cppCross(z, x);
  if (cppDot(y, y) <= 1.0e-12f)
    y = make_float3(0.f, 1.f, 0.f);
  else
    y = gfNormalize(y);
}

// _EvalTransmittance()
static inline float3 evalTransmittance(float3 extinction, float distance)
{
  return make_float3(exp(-extinction.x * distance),
      exp(-extinction.y * distance),
      exp(-extinction.z * distance));
}

// _MinPositiveComponent()
static inline float minPositiveComponent(float3 v)
{
  float result = SSS_FLT_MAX;
  if (v.x > SSS_EXTINCTION_EPS)
    result = min(result, v.x);
  if (v.y > SSS_EXTINCTION_EPS)
    result = min(result, v.y);
  if (v.z > SSS_EXTINCTION_EPS)
    result = min(result, v.z);
  return result;
}

// The query of one walk ray: its owner (Embree's instID, instPrimID and
// geomID of the owner's hits)
struct SssQueryContext
{
  RTCRayQueryContext context;
  uint32 instID;
  uint32 instPrimID;
  uint32 geomID;
};

// Embree's argument filter of the walk's rays: only the owner's hits count,
// so the walk sees its own surface alone, as Typhoon's trace of the owner's
// prototype scene does. Called for single rays (rtcIntersect1()), but written
// for any N, laid out as RTCRayN and RTCHitN.
static unmasked void sssOwnerFilter(
    const uniform RTCFilterFunctionNArguments *uniform args)
{
  const uniform SssQueryContext *uniform ctx =
      (const uniform SssQueryContext *uniform)args->context;
  const uniform unsigned int N = args->N;
  const uniform uint32 *uniform hit = (const uniform uint32 *uniform)args->hit;
  for (uniform unsigned int i = 0; i < N; i++) {
    // RTCHitN's geomID, instID[0] and instPrimID[0]
    if (args->valid[i] != 0
        && (hit[6 * N + i] != ctx->geomID || hit[7 * N + i] != ctx->instID
            || hit[(7 + RTC_MAX_INSTANCE_LEVEL_COUNT) * N + i]
                != ctx->instPrimID))
      args->valid[i] = 0;
  }
}

// _TraceSssBoundary(): the owner's first surface along the walk ray from
// 'org' along 'dir' in [tnear, tfar], other surfaces filtered out. Each lane
// traces alone with rtcIntersect1(), as Typhoon's walk does: Embree runs
// argument filters in its single-ray kernels, but its packet kernels for
// triangles and quads skip them unless the scene was built with
// RTC_SCENE_FLAG_FILTER_FUNCTION_IN_ARGUMENTS, which would cost every ray.
static inline void traceBoundary(uniform RTCScene scene,
    const SubsurfaceOwner &owner,
    float3 org,
    float3 dir,
    float tnear,
    float tfar,
    SssTraceResult &result)
{
  result.foundSurface = false;
  if (cppDot(dir, dir) <= 1.0e-20f)
    return;

  uniform RTCIntersectArguments args;
  rtcInitIntersectArguments(&args);
  args.flags = (uniform RTCRayQueryFlags)(RTC_RAY_QUERY_FLAG_INCOHERENT
      | RTC_RAY_QUERY_FLAG_INVOKE_ARGUMENT_FILTER);
  args.filter = sssOwnerFilter;

  bool found = false;
  float t = 0.f;
  float3 Ng = make_float3(0.f);
  uint32 primID = RTC_INVALID_GEOMETRY_ID;
  float u = 0.f;
  float v = 0.f;
  foreach_active (lane) {
    uniform SssQueryContext ctx;
    rtcInitRayQueryContext(&ctx.context);
    ctx.instID = extract(owner.instID, lane);
    ctx.instPrimID = extract(owner.instPrimID, lane);
    ctx.geomID = extract(owner.geomID, lane);
    args.context = &ctx.context;

    // rtcIntersect1() needs its RTCRayHit 16-byte aligned, which ISPC
    // doesn't promise for a uniform local (RTCRayHit is under 128 bytes)
    uniform uint8 storage[SSS_RAYHIT_STORAGE];
    uniform RTCRayHit &rh = *((uniform RTCRayHit * uniform)(
        ((uniform uint64)((void *uniform)storage) + 15) & ~((uniform uint64)15)));
    rh.ray.org_x = extract(org.x, lane);
    rh.ray.org_y = extract(org.y, lane);
    rh.ray.org_z = extract(org.z, lane);
    rh.ray.tnear = extract(tnear, lane);
    rh.ray.dir_x = extract(dir.x, lane);
    rh.ray.dir_y = extract(dir.y, lane);
    rh.ray.dir_z = extract(dir.z, lane);
    rh.ray.time = 0.f;
    rh.ray.tfar = extract(tfar, lane);
    rh.ray.mask = 0xffffffffu;
    rh.ray.id = 0;
    rh.ray.flags = 0;
    rh.hit.geomID = RTC_INVALID_GEOMETRY_ID;
    rh.hit.primID = RTC_INVALID_GEOMETRY_ID;
    rh.hit.instID[0] = RTC_INVALID_GEOMETRY_ID;
    rh.hit.instPrimID[0] = RTC_INVALID_GEOMETRY_ID;
    rtcIntersect1(scene, &rh, &args);

    // only the owner's hit (the filter keeps no other)
    if (rh.hit.geomID == ctx.geomID && rh.hit.instID[0] == ctx.instID
        && rh.hit.instPrimID[0] == ctx.instPrimID) {
      found = insert(found, lane, true);
      t = insert(t, lane, rh.ray.tfar);
      Ng = make_float3(insert(Ng.x, lane, rh.hit.Ng_x),
          insert(Ng.y, lane, rh.hit.Ng_y),
          insert(Ng.z, lane, rh.hit.Ng_z));
      primID = insert(primID, lane, rh.hit.primID);
      u = insert(u, lane, rh.hit.u);
      v = insert(v, lane, rh.hit.v);
    }
  }
  if (!found)
    return;

  result.foundSurface = true;
  result.distance = t;
  result.posHit = org + dir * result.distance;
  result.primID = primID;
  result.u = u;
  result.v = v;
  result.NgObjectExt = Ng;
  float3 n = sssTransformNormal(owner.normalToWorld, Ng);
  if (cppDot(n, n) > 1.0e-20f)
    n = gfNormalize(n);
  else
    n = -dir;
  result.NgWorldExt = n;
}

void Subsurface_randomWalk(uniform RTCScene scene,
    const varying SubsurfaceOwner &owner,
    const varying SubsurfaceWalkInput &input,
    const varying SampleDomain &domain,
    uniform float fixedBias,
    varying SubsurfaceWalkOutput &output)
{
  const uniform float kTwoPi = 2.f * SSS_PI;
  const uniform float kInvTwoPi = 1.f / (2.f * SSS_PI);

  output.success = false;
  output.posExit = make_float3(0.f);
  output.NgWorldExt = make_float3(0.f);
  output.NgObjectExt = make_float3(0.f);
  output.dirExit = make_float3(0.f);
  output.primID = RTC_INVALID_GEOMETRY_ID;
  output.u = 0.f;
  output.v = 0.f;
  output.throughput = make_float3(0.f);
  output.walkSteps = 0;

  SssWalkState state;
  float3 throughputCorrection = make_float3(1.f);
  if (input.usePrecomputed) {
    precomputedChannel(input.absorption.x,
        input.scattering.x,
        state.extinction.x,
        state.scattering.x,
        state.alpha.x);
    precomputedChannel(input.absorption.y,
        input.scattering.y,
        state.extinction.y,
        state.scattering.y,
        state.alpha.y);
    precomputedChannel(input.absorption.z,
        input.scattering.z,
        state.extinction.z,
        state.scattering.z,
        state.alpha.z);
  } else {
    // _InitChiangCoefficients(): where the remap's albedo was clamped up to
    // the minimum, the walk starts at raw / clamped
    float3 rawAlpha;
    Subsurface_chiangRemap(input.albedo,
        input.radius,
        input.anisotropy,
        state.extinction,
        state.alpha,
        rawAlpha);
    state.scattering = state.extinction * state.alpha;
    if (rawAlpha.x < SSS_MIN_ALPHA)
      throughputCorrection.x = rawAlpha.x / SSS_MIN_ALPHA;
    if (rawAlpha.y < SSS_MIN_ALPHA)
      throughputCorrection.y = rawAlpha.y / SSS_MIN_ALPHA;
    if (rawAlpha.z < SSS_MIN_ALPHA)
      throughputCorrection.z = rawAlpha.z / SSS_MIN_ALPHA;
  }
  state.throughput = throughputCorrection;
  state.posRayOrg = input.posEntry;
  state.dirRay = input.dirEntry;
  state.anisotropy = clamp(input.anisotropy, -0.99f, 0.99f);
  state.posEntry = input.posEntry;
  state.guideN = input.guideN;
  state.haveOppositeInterface = false;
  state.distanceOpposite = 0.f;

  if (!initDwivediAndSimilarity(state))
    return;

  // Dwivedi sampling guides directions around the material's entry normal
  const float3 guideAxis = state.guideN;
  // the opposite interface must be farther than the first ray's offset
  const float entryBias = fixedBias > 0.f ? fixedBias : input.entryBias;

  for (uniform int bounce = 0; bounce < SUBSURFACE_MAX_BOUNCES; bounce++) {
    output.walkSteps++;
    const SampleDomain bounceDomain =
        SampleDomain_chain(domain, SAMPLE_DOMAIN_SSS_BOUNCE, bounce);

    // after SSS_SIMILARITY_LEVEL bounces: isotropic, reduced scattering
    float3 extinctionEffective;
    float3 scatteringEffective;
    float anisotropyEffective;
    float guidedFractionEffective;
    if (bounce <= SSS_SIMILARITY_LEVEL) {
      extinctionEffective = state.extinction;
      scatteringEffective = state.scattering;
      anisotropyEffective = state.anisotropy;
      guidedFractionEffective = state.guidedFraction;
    } else {
      extinctionEffective = state.extinctionStar;
      scatteringEffective = state.scatteringStar;
      anisotropyEffective = 0.f;
      guidedFractionEffective = SSS_REDUCED_GUIDED_FRACTION;
    }

    // Chiang's channel selection, MIS with the balance heuristic
    float3 channelPdf;
    const int channel = Medium_channelMIS(state.throughput,
        state.alpha,
        SampleDomain_draw1D(
            SampleDomain_fork(bounceDomain, SAMPLE_DOMAIN_SSS_CHANNEL)),
        channelPdf);

    float sampleExtinction = sssComponent(extinctionEffective, channel);
    if (sampleExtinction <= SSS_EXTINCTION_EPS)
      return;

    // the direction: the entry direction first, then guided (Dwivedi around
    // guideAxis) or classic (Henyey-Greenstein) sampling
    float stretchingForward = 1.f;
    float pdfFactorForward = 0.f;
    float stretchingBackward = 1.f;
    float pdfFactorBackward = 0.f;
    float backwardFraction = 0.f;
    if (bounce > 0) {
      const bool guided = SampleDomain_draw1D(SampleDomain_fork(
                              bounceDomain, SAMPLE_DOMAIN_SSS_GUIDE_CHOICE))
          < guidedFractionEffective;
      bool guideBackward = false;
      if (state.haveOppositeInterface) {
        const float distanceFromEntryPlane =
            cppDot(state.posRayOrg - state.posEntry, -state.guideN);
        backwardFraction =
            Subsurface_backwardDwivediFraction(state.distanceOpposite,
                distanceFromEntryPlane,
                state.diffusionLength);
        if (guided) {
          guideBackward = SampleDomain_draw1D(SampleDomain_fork(
                              bounceDomain, SAMPLE_DOMAIN_SSS_BACKWARD_CHOICE))
              < backwardFraction;
        }
      }

      const float2 phaseSample = SampleDomain_draw2D(
          SampleDomain_fork(bounceDomain, SAMPLE_DOMAIN_SSS_PHASE_DIRECTION));
      const float u1 = phaseSample.x;
      const float u2 = phaseSample.y;

      float3 dirScattered;
      float cosThetaEntry = 0.f;
      float pdfHenyeyGreenstein = 1.f;
      if (guided) {
        // forward Dwivedi around guideAxis; backward mirrors it along the
        // entry normal, towards the opposite side
        float cosTheta = Subsurface_samplePhaseDwivedi(
            state.diffusionLength, state.phaseLog, u1);
        if (guideBackward)
          cosTheta = -cosTheta;
        cosThetaEntry = cosTheta;

        float3 axisX, axisY;
        buildOrthonormalBasis(guideAxis, axisX, axisY);
        const float sinTheta = sqrt(max(0.f, 1.f - cosTheta * cosTheta));
        const float phi = kTwoPi * u2;
        dirScattered = axisX * (sinTheta * cos(phi))
            + axisY * (sinTheta * sin(phi)) + guideAxis * cosTheta;
        if (cppDot(dirScattered, dirScattered) > 1.0e-20f)
          dirScattered = gfNormalize(dirScattered);
        else
          dirScattered = guideAxis;
        pdfHenyeyGreenstein = Medium_pdfHenyeyGreenstein(
            dirScattered, -state.dirRay, anisotropyEffective);
      } else {
        dirScattered = Medium_sampleHenyeyGreenstein(
            -state.dirRay, anisotropyEffective, u1, u2);
        cosThetaEntry = cppDot(dirScattered, guideAxis);
        pdfHenyeyGreenstein = Medium_pdfHenyeyGreenstein(
            dirScattered, -state.dirRay, anisotropyEffective);
      }

      state.dirRay = dirScattered;

      // the guided pdfs relative to the classic one: Dwivedi's over
      // Henyey-Greenstein's, with the azimuth Dwivedi samples uniformly
      const float pdfHenyeyGreensteinSafe = max(pdfHenyeyGreenstein, 1.0e-8f);
      pdfFactorForward = kInvTwoPi
          * Subsurface_evalPhaseDwivedi(
              state.diffusionLength, state.phaseLog, cosThetaEntry)
          / pdfHenyeyGreensteinSafe;
      pdfFactorBackward = kInvTwoPi
          * Subsurface_evalPhaseDwivedi(
              state.diffusionLength, state.phaseLog, -cosThetaEntry)
          / pdfHenyeyGreensteinSafe;

      stretchingForward = 1.f - cosThetaEntry / state.diffusionLength;
      stretchingBackward = 1.f + cosThetaEntry / state.diffusionLength;

      if (guided)
        sampleExtinction *=
            guideBackward ? stretchingBackward : stretchingForward;
    }

    // free flight with the (possibly stretched) extinction
    const float uFlight = clamp(
        SampleDomain_draw1D(
            SampleDomain_fork(bounceDomain, SAMPLE_DOMAIN_SSS_FREE_FLIGHT)),
        1.0e-6f,
        1.f - 1.0e-6f);
    const float distanceFreeFlight =
        -log(1.f - uFlight) / max(sampleExtinction, SSS_EXTINCTION_EPS);
    float distanceSegment = distanceFreeFlight;

    // the first ray also looks for the opposite interface, farther
    float distanceRayMaximum = distanceFreeFlight;
    if (bounce == 0) {
      const float minExtinction = minPositiveComponent(extinctionEffective);
      if (minExtinction < SSS_FLT_MAX)
        distanceRayMaximum = max(distanceFreeFlight, 10.f / minExtinction);
    }

    const float tnear = fixedBias > 0.f
        ? fixedBias
        : (bounce == 0 ? input.entryBias
                       : epsilonFrom(state.posRayOrg,
                             state.dirRay,
                             0.f,
                             TRANSPORT_EPSILON_MIN));
    SssTraceResult trace;
    traceBoundary(scene,
        owner,
        state.posRayOrg,
        state.dirRay,
        tnear,
        distanceRayMaximum,
        trace);

    bool hit = trace.foundSurface;
    if (bounce == 0) {
      if (trace.foundSurface) {
        const float distanceOpposite =
            cppDot(trace.posHit - state.posEntry, -state.guideN);
        if (distanceOpposite > entryBias) {
          state.haveOppositeInterface = true;
          state.distanceOpposite = distanceOpposite;
        }
      }
      // the extended ray only finds the opposite interface: the walk still
      // scatters where the free flight ends first
      hit = trace.foundSurface && trace.distance < distanceFreeFlight;
    }
    if (hit)
      distanceSegment = trace.distance;

    // the classic pdf and contribution: T(t) to the surface; scattering times
    // T(t) over extinction times T(t) to a collision
    const float3 transmittance =
        evalTransmittance(extinctionEffective, distanceSegment);
    float3 classicPdf;
    float3 sampleContrib;
    if (hit) {
      classicPdf = transmittance;
      sampleContrib = transmittance;
    } else {
      classicPdf = extinctionEffective * transmittance;
      sampleContrib = scatteringEffective * transmittance;
    }

    // MIS of classic and guided sampling, the guided pdf with the stretched
    // extinction it sampled the distance with
    float3 pdf = classicPdf;
    if (bounce > 0) {
      const float3 extinctionStretched =
          extinctionEffective * stretchingForward;
      const float3 transmittanceStretched =
          evalTransmittance(extinctionStretched, distanceSegment);
      float3 pdfGuidedForward = hit
          ? transmittanceStretched
          : extinctionStretched * transmittanceStretched;
      pdfGuidedForward = pdfGuidedForward * pdfFactorForward;

      float3 guidedPdf = pdfGuidedForward;
      if (state.haveOppositeInterface) {
        const float3 extinctionBackwardStretched =
            extinctionEffective * stretchingBackward;
        const float3 transmittanceBackwardStretched =
            evalTransmittance(extinctionBackwardStretched, distanceSegment);
        float3 pdfGuidedBackward = hit
            ? transmittanceBackwardStretched
            : extinctionBackwardStretched * transmittanceBackwardStretched;
        pdfGuidedBackward = pdfGuidedBackward * pdfFactorBackward;
        guidedPdf = pdfGuidedForward * (1.f - backwardFraction)
            + pdfGuidedBackward * backwardFraction;
      }
      pdf = classicPdf * (1.f - guidedFractionEffective)
          + guidedPdf * guidedFractionEffective;
    }

    const float denom = cppDot(channelPdf, pdf);
    if (!isfinite(denom) || denom <= 1.0e-20f)
      return;
    state.throughput = state.throughput * (sampleContrib * (1.f / denom));

    const float maxThroughput = sssMax3(state.throughput);
    if (!isfinite(maxThroughput) || maxThroughput < SSS_VOLUME_THROUGHPUT_EPS)
      return;

    if (hit) {
      // the exit, its normal faced along the ray, which leaves the medium
      float3 NgWorld = trace.NgWorldExt;
      float3 NgObject = trace.NgObjectExt;
      if (cppDot(NgWorld, NgWorld) > 1.0e-20f) {
        NgWorld = gfNormalize(NgWorld);
      } else {
        // TransformNormalToObject(): through the transpose of the transform
        NgWorld = -state.dirRay;
        float3 n;
        if (sssTryNormalize(sssTransposeTimes(owner.objectToWorld, NgWorld), n))
          NgObject = n;
        else if (sssTryNormalize(NgWorld, n))
          NgObject = n;
        else
          NgObject = make_float3(0.f, 0.f, 1.f);
      }
      if (cppDot(NgWorld, state.dirRay) < 0.f) {
        NgWorld = -NgWorld;
        NgObject = -NgObject;
      }
      if (cppDot(NgObject, NgObject) > 1.0e-20f)
        NgObject = gfNormalize(NgObject);

      output.success = true;
      output.posExit = trace.posHit;
      output.NgWorldExt = NgWorld;
      output.NgObjectExt = NgObject;
      output.dirExit = state.dirRay;
      output.primID = trace.primID;
      output.u = trace.u;
      output.v = trace.v;
      output.throughput = state.throughput;
      return;
    }

    // on to the collision
    state.posRayOrg = state.posRayOrg + state.dirRay * distanceSegment;
  }
  // out of bounces: no exit
}
