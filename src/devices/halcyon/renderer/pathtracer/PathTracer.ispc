// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: LicenseRef-TOST-1.0
//
// The path integrator below is ported from Typhoon (NVIDIA-Omniverse/OpenUSD,
// branch typhoon/main, commit 70c45e83b08a7b45588cd6cdbd2ae67f5147befb),
// pxr/imaging/plugin/hdEmbree/renderer/integrator/: pathIntegrator.cpp
// (_IntegratePath(), _AddPathRadiance(), _PopulateSssExitRayHit()),
// lighting.cpp (_AccumulateEnvironment(), _ComputeDirectLightingMIS(),
// _ComputeMediumDirectLighting()),
// visibility.cpp (_Visibility(), _FindNearestFiniteLightHit()),
// surfaceShading.cpp (_ComputeSmoothShadowOffsetOut()) and sss.cpp
// (_TraceSubsurface()) and volumeTransport.cpp (_TraceVolumeTransmission(),
// _UpdatePathMedium()), with their hero-wavelength branches
// (HeroWavelength.isph). Copyright 2018-2026 Pixar, licensed under the
// Tomorrow Open Source Technology License 1.0 (see THIRD-PARTY.md). Keep in
// step with those files when the pin in external/typhoon/PROVENANCE.md
// moves.

// The 'quality' renderer's shading pass (PathTracer.h): Typhoon's
// unidirectional path tracer. Every vertex gathers emission, then light from
// every light by next-event estimation (Typhoon's lightSamplesPerHit
// stratified samples each, with transparent shadows), weighed against BSDF
// sampling by multiple importance sampling (the power heuristic); the path
// goes on along its BSDF sample, which gathers the lights it finds with the
// other weight, until maxBounces, a miss or Russian roulette ends it.
// Surfaces shade through the material interface (PathMaterial.isph): C++
// MaterialXCpp graphs, and a native Lambertian for surfaces without a closure;
// transport policy (MIS weights, clamps,
// ray offsets) is in Transport.isph. Random numbers come from Typhoon's
// sample generator (Sampling.isph), from the domains Typhoon draws them from.
// transferFunction1D volumes scatter (VolumeTransport.isph, halcyon's own):
// a collision found by delta tracking is a path vertex like a surface's,
// and shadow rays and emitter-only segments take their transmittance by
// ratio tracking. A subsurface sample walks through the surface's medium
// (Subsurface.isph, Typhoon's random walk; sss.cpp's _TraceSubsurface() is
// traceSubsurface() below) and leaves through a white Lambertian. A path that
// crosses into a closure's interior medium (Medium.isph) is in it until it
// crosses out: segments there may scatter, with Typhoon's channel MIS, and
// their throughput and shadow rays take its transmittance (one medium at a
// time, as Typhoon's). A path that meets a dispersive closure follows one
// hero wavelength from there (HeroWavelength.isph). With 'enableCaustics'
// off (Typhoon's default), caustic-class lobes are pruned after a
// diffuse-like event, a caustic sample ends the path, and shadow rays pass
// straight through thick transmissive blockers; with it on, the contributions
// of caustic paths are clamped to 'causticsClampThreshold' too.

#include "renderer/pathtracer/AdaptiveSampling.isph"
#include "renderer/pathtracer/HeroWavelength.isph"
#include "renderer/pathtracer/Lights.isph"
#include "renderer/pathtracer/PathMaterial.isph"
#include "renderer/pathtracer/Sampling.isph"
#include "renderer/pathtracer/ShadingContext.isph"
#include "renderer/pathtracer/Subsurface.isph"
#include "renderer/pathtracer/Transport.isph"
#include "renderer/pathtracer/VolumeTransport.isph"
#include "shading/halcyon_camera.isph"
#include "shading/halcyon_frame.isph"

// Typhoon's Russian roulette caps the survival probability
#define PATH_RR_MAX_SURVIVAL 0.95f

// Shadow rays: Typhoon's limits of material hits evaluated and of hits
// traced, and its visibility below which a shadow ray is blocked
#define SHADOW_MAX_TRANSPARENT_HITS 16
#define SHADOW_MAX_INTERSECTIONS 256
#define SHADOW_VISIBILITY_THRESHOLD 1e-4f

// Objects ////////////////////////////////////////////////////////////////////

// Which object a hit is on, for the shadow rays' per-object rules (Typhoon's
// instance and prototype contexts): the instance's transform and the surface
struct PathObject
{
  uint instID;
  uint instPrimID;
  uint geomID;
};

inline PathObject PathObject_of(const RayHit &rh)
{
  PathObject o;
  o.instID = rh.hit.instID;
  o.instPrimID = rh.hit.instPrimID;
  o.geomID = rh.hit.geomID;
  return o;
}

inline bool PathObject_equal(const PathObject &a, const PathObject &b)
{
  return a.instID == b.instID && a.instPrimID == b.instPrimID
      && a.geomID == b.geomID;
}

// No object: a vertex in a volume
inline PathObject PathObject_none()
{
  PathObject o;
  o.instID = RTC_INVALID_GEOMETRY_ID;
  o.instPrimID = RTC_INVALID_GEOMETRY_ID;
  o.geomID = RTC_INVALID_GEOMETRY_ID;
  return o;
}

// Interior media ////////////////////////////////////////////////////////////

// The homogeneous medium a path or shadow ray is in (Typhoon's
// ty::MediumState): the interior medium of the closure whose surface it
// crossed into, until it crosses that object's surface again. One at a time,
// as in Typhoon: inside a medium, the surfaces of other objects neither enter
// theirs nor leave this one.
struct PathMediumState
{
  bool active;
  HomogeneousMedium medium;
  // The object whose surface leaves the medium (Typhoon: the prototype)
  PathObject owner;
};

inline void PathMediumState_clear(PathMediumState &m)
{
  m.active = false;
  Medium_clear(m.medium);
  m.owner = PathObject_none();
}

// Enters or leaves the medium where the path crosses the surface of 'object'
// along a direction of dot 'dirDotNg' with its exterior geometric normal, the
// material shaded there being 'state' (Typhoon's _UpdatePathMedium()): leaves
// the medium crossing its owner's surface outwards, enters the closure's
// interior medium crossing inwards while in none
inline void updatePathMedium(PathMediumState &m,
    const PathMaterialState &state,
    const PathObject &object,
    float dirDotNg)
{
  if (m.active && PathObject_equal(m.owner, object) && dirDotNg > 0.f) {
    PathMediumState_clear(m);
  } else if (!m.active && (state.flags & PATH_MATERIAL_HAS_INTERIOR_MEDIUM) != 0
      && dirDotNg < 0.f) {
    PathMedium interior;
    PathMaterial_medium(state, interior);
    m.medium = Medium_of(interior);
    m.active = true;
    m.owner = object;
  }
}

// Lights /////////////////////////////////////////////////////////////////////

// The nearest finite light (a sphere, quad or ring) the ray from 'org' along
// unit 'dir' meets before distance 'maxDist', in 'nearest' (Typhoon's
// _FindNearestFiniteLightHit() and _EvaluateLightGeometryHit()). With
// 'visibleOnly' (camera rays) only lights that are 'visible'
// (KHR_LIGHT_PRIMARY_VISIBILITY) count: Typhoon's camera rays see only
// lights visible in primary rays, which it puts into the Embree scene, and
// its other rays every finite light. Rays meet the shapes of visible lights
// from both sides, as Typhoon's Embree scene's, so a side of a quad or ring
// that doesn't emit is met as black; they pass through that side of the
// others.
inline bool nearestFiniteLight(const uniform World *uniform world,
    float3 org,
    float3 dir,
    float maxDist,
    bool visibleOnly,
    LightSample &nearest)
{
  bool found = false;
  float closest = maxDist;
  for (uniform uint32 i = 0; i < world->numLights; i++) {
    const uniform Light *uniform light = world->lights + i;
    if (!Light_isFinite(light))
      continue;
    if (visibleOnly && !light->visible)
      continue;
    LightSample ls = Light_eval(light, org, dir);
    float dist;
    if (!ls.valid && light->visible && Light_hit(light, org, dir, dist)) {
      // _EvaluateLightGeometryHit()'s black hit
      ls = LightSample_make(make_float3(0.f), dir, dist, 0.f, true);
    }
    if (ls.valid && ls.dist > 0.f && isfinite(ls.dist) && ls.dist < closest) {
      closest = ls.dist;
      nearest = ls;
      found = true;
    }
  }
  return found;
}

// Whether a 'visible' finite light is in the way of the ray from 'org' along
// unit 'dir' before distance 'maxDist': Typhoon's shadow rays are blocked by
// the light geometry it adds for lights visible in primary rays, from
// either side
inline bool visibleLightBlocks(
    const uniform World *uniform world, float3 org, float3 dir, float maxDist)
{
  for (uniform uint32 i = 0; i < world->numLights; i++) {
    const uniform Light *uniform light = world->lights + i;
    if (!light->visible)
      continue;
    float dist;
    if (Light_hit(light, org, dir, dist) && dist < maxDist)
      return true;
  }
  return false;
}

// The lights next-event estimation samples and rays leaving surfaces
// gather: the world's, then the ambient light (a constant dome that camera
// rays don't see) where it emits; light i's DirectLightSelect domain is
// chained with i, so the ambient light, last, leaves the world's lights'
// numbers as they are
inline uniform uint32 pathLightCount(const uniform World *uniform world,
    const uniform PathTracerParameters &params)
{
  return world->numLights + (params.ambientLight != NULL ? 1 : 0);
}

inline const uniform Light *uniform pathLight(
    const uniform World *uniform world,
    const uniform PathTracerParameters &params,
    uniform uint32 i)
{
  return i < world->numLights ? world->lights + i : params.ambientLight;
}

// Adds 'contribution' (path throughput included) to the path's 'radiance',
// clamped to the renderer's firefly threshold, and first to its caustics
// clamp threshold on a 'caustic' path (Typhoon's _AddPathRadiance(); a path
// is caustic once it took a caustic sample, which only enableCaustics allows)
inline void addPathRadiance(const uniform PathTracerParameters &params,
    float3 &radiance,
    float3 contribution,
    bool caustic)
{
  if (caustic) {
    contribution =
        Transport_clampFirefly(contribution, params.causticsClampThreshold);
  }
  radiance = radiance
      + Transport_clampFirefly(contribution, params.fireflyClampThreshold);
}

// The origin of a ray leaving the surface hit 'P', reached at 't' along
// 'inDir', along 'outDir': P moved off the surface by its epsilonFrom(),
// returned in 'bias' (the new ray's tnear), along 'Ng' faced towards outDir
inline float3 leaveSurface(
    float3 P, float3 Ng, float3 inDir, float t, float3 outDir, float &bias)
{
  bias = epsilonFrom(P, inDir, t, TRANSPORT_EPSILON_MIN);
  return Transport_offsetRayOrigin(P, Ng, outDir, bias);
}

// Native materials' inputs ///////////////////////////////////////////////////

// Native kinds' parameter 'p' at the hit of 'rh', as their graph's input
// resolves it (a valid sampler's value, else the attribute's, else the
// constant)
inline float4 materialParameterAt(
    const uniform MaterialParameter &p, const RayHit &rh, const HitInfo &hit)
{
  if (Sampler_isValid(p.sampler))
    return Sampler_sample(p.sampler, hit, rh);
  if (p.attribute != ATTRIBUTE_NONE)
    return getAttribute(hit, rh, p.attribute);
  return p.value;
}

// What native kinds read beyond the shading context at the hit of 'rh':
// native matte's and physicallyBased's colour, as their graphs would read
// it, otherwise the colour of the native Lambertian that lights surfaces
// without a closure (no valid material, or one that didn't compile); and
// native physicallyBased's other parameters, with its normal maps'
// ('normalMaps': shadow rays don't read them)
inline void nativeInputs(const uniform Frame *uniform frame,
    MaterialKind kind,
    const RayHit &rh,
    const HitInfo &hit,
    uniform bool normalMaps,
    PathShadeInput &input)
{
  if (MaterialKind_isNative(kind)) {
    input.baseColor =
        make_float3(Surface_getColor(rh, hit, frame->invalidMaterialColor));
  } else {
    input.baseColor = make_float3(frame->invalidMaterialColor);
  }
  if (kind != MATERIAL_KIND_PHYSICALLY_BASED)
    return;
#pragma ignore warning(perf)
  const uniform Material *varying material = hit.surface->material;
  foreach_unique (m in material) {
    const uniform PhysicallyBasedParameters *uniform p = m->physicallyBased;
    input.pbm = PathPbm_input(materialParameterAt(p->metallic, rh, hit),
        materialParameterAt(p->roughness, rh, hit),
        materialParameterAt(p->specular, rh, hit),
        materialParameterAt(p->specularColor, rh, hit),
        materialParameterAt(p->ior, rh, hit),
        materialParameterAt(p->emissive, rh, hit),
        materialParameterAt(p->clearcoat, rh, hit),
        materialParameterAt(p->clearcoatRoughness, rh, hit),
        materialParameterAt(p->sheenColor, rh, hit),
        materialParameterAt(p->sheenRoughness, rh, hit),
        materialParameterAt(p->transmission, rh, hit),
        materialParameterAt(p->thickness, rh, hit));
    // the normal maps' sampler nodes: (0, 0, 1, 1) while one is invalid
    if (normalMaps) {
      input.pbm.hasNormalMap = p->normal != NULL;
      input.pbm.normalMap = Sampler_isValid(p->normal)
          ? Sampler_sample(p->normal, hit, rh)
          : make_float4(0.f, 0.f, 1.f, 1.f);
      input.pbm.hasCoatNormalMap = p->clearcoatNormal != NULL;
      input.pbm.coatNormalMap = Sampler_isValid(p->clearcoatNormal)
          ? Sampler_sample(p->clearcoatNormal, hit, rh)
          : make_float4(0.f, 0.f, 1.f, 1.f);
    }
  }
}

// Visibility /////////////////////////////////////////////////////////////////

// The shadow-ray view of the material at the blocker hit 'rh' of a shadow
// ray: only C++ materials get the whole shading context (native kinds
// read the material's opacity, and native physicallyBased its parameters)
inline void blockerMaterial(const uniform Frame *uniform frame,
    const uniform PathMaterialContext *uniform context,
    const RayHit &rh,
    const HitInfo &hit,
    PathShadowHit &blocker)
{
#pragma ignore warning(perf)
  const uniform Material *varying material = hit.surface->material;
  PathShadeInput input;
  const MaterialKind kind = PathMaterial_kind(context, material, 0);
  if (kind == MATERIAL_KIND_CPP) {
    RayDifferential rd;
    rd.valid = false;
    SurfaceDifferentials differentials;
    ShadingContext_build(frame, rh, hit, rd, input, differentials);
    input.baseColor = make_float3(0.f);
  } else {
    // as ShadingContext_build() makes them
    input.Ng = normalize(worldNg(rh, hit));
    input.wo = normalize(-rh.ray.dir);
    input.baseColor = make_float3(0.f);
    // native physicallyBased's transmission, its colour and IOR, and its
    // thickness
    if (kind == MATERIAL_KIND_PHYSICALLY_BASED)
      nativeInputs(frame, kind, rh, hit, false, input);
  }
  input.baseOpacity = Surface_getCompositeOpacity(rh, hit);
  input.heroWavelengthNm = 0.f;
  input.flags = dot(input.Ng, input.wo) > 0.f ? PATH_SHADE_FRONT_FACING : 0;
  input.uPresence = 0.f;
  input.uWavelength = 0.f;
  input.u1 = 0.f;
  input.u2 = 0.f;
  input.uLobe = 0.f;
  PathMaterial_shadow(context, material, input, blocker);
}

// The fraction of light that reaches 'P' along the shadow ray towards unit
// 'dir' from distance 'maxDist' (Typhoon's _Visibility()): the product of what
// each surface on the way lets through, 1 - presence plus presence times its
// straight-through transmission (tinted by its transmission colour) for
// thin-walled surfaces and, with caustics off, thick ones; 1 - opacity for
// thick surfaces of the 'conservative' origin's own object 'origin'. Blocked
// by a visible finite light on the way (from either side), by surfaces
// without a material, and past SHADOW_MAX_INTERSECTIONS hits; after
// SHADOW_MAX_TRANSPARENT_HITS material hits what got through so far is kept.
// The ray starts at 'P' moved by 'bias' (the vertex's epsilonFrom()) off the
// surface along 'reference' (the vertex's exterior geometric normal), and goes
// on past each surface it passes moved off it by the epsilonFrom() of that hit;
// it ends once no more than that offset is left to go. Each stretch between
// surfaces multiplies the transmittance of the transferFunction1D volumes on
// it, by ratio tracking from 'domain' (the light sample's), and the
// Beer-Lambert transmittance of the homogeneous medium it is in: 'startMedium'
// (the vertex's) at first, left at its owner's surface, and where it is in
// none, entered at a volume-only boundary or (caustics off) a thick
// transmissive surface with an interior medium crossed inwards; the surface of
// the medium's owner lets max(1 - opacity, transmission) through, or straight
// through untinted.
inline float3 visibility(const uniform Frame *uniform frame,
    const uniform PathMaterialContext *uniform context,
    float3 P,
    float3 reference,
    float3 dir,
    float maxDist,
    float bias,
    bool conservative,
    const PathObject &origin,
    const PathMediumState &startMedium,
    const SampleDomain &domain)
{
  const uniform World *uniform world = frame->world;
  if (frame->renderer->pathtracer.disableShadows)
    return make_float3(1.f);

  float3 vis = make_float3(1.f);
  float3 org = Transport_offsetRayOrigin(P, reference, dir, bias);
  float remaining = maxDist;
  PathMediumState medium = startMedium;
  // the thick transparent object the ray is inside of, without a medium
  bool insideStraight = false;
  PathObject straightOwner;
  int materialHits = 0;
  int volumeStep = 0;

  for (uniform int i = 0; i < SHADOW_MAX_INTERSECTIONS; i++) {
    RayHit rh = makeRay(org, dir, bias, remaining);
    rtcIntersectV(world->scene, (varying RTCRayHit * uniform) & rh);
    const HitInfo hit = getHitInfo(world, rh);
    const bool found = foundHit(hit);
    if (visibleLightBlocks(world, org, dir, found ? rh.ray.tfar : remaining))
      return make_float3(0.f);
    if (world->numVolumeInstances > 0) {
      vis = vis
          * VolumeTransport_transmittance(world,
              org,
              dir,
              bias,
              found ? min(rh.ray.tfar, remaining) : remaining,
              domain,
              volumeStep);
      if (Transport_isNearlyBlack(vis, SHADOW_VISIBILITY_THRESHOLD))
        return make_float3(0.f);
    }
    if (!found) {
      if (medium.active && remaining > 0.f)
        vis = vis * Medium_evalBeerTransmittance(medium.medium, remaining);
      return vis;
    }

    const float hitDist = min(rh.ray.tfar, remaining);
    if (medium.active && hitDist > 0.f) {
      vis = vis * Medium_evalBeerTransmittance(medium.medium, hitDist);
      if (Transport_isNearlyBlack(vis, SHADOW_VISIBILITY_THRESHOLD))
        return make_float3(0.f);
    }
    if (++materialHits > SHADOW_MAX_TRANSPARENT_HITS)
      return vis;

    PathShadowHit blocker;
    blockerMaterial(frame, context, rh, hit, blocker);
    const float3 NgBlocker = normalize(worldNg(rh, hit));
    const PathObject object = PathObject_of(rh);
    const bool approximateThick = !frame->renderer->pathtracer.enableCaustics
        && !(conservative && PathObject_equal(object, origin));
    const bool exitsMedium =
        medium.active && PathObject_equal(object, medium.owner);
    const bool exitsStraight =
        insideStraight && PathObject_equal(object, straightOwner);
    const bool hasClosure = (blocker.flags & PATH_MATERIAL_HAS_CLOSURE) != 0;
    const bool thinWalled = (blocker.flags & PATH_MATERIAL_THIN_WALLED) != 0;
    const bool hasMedium =
        (blocker.flags & PATH_MATERIAL_HAS_INTERIOR_MEDIUM) != 0;
    const bool volumeOnly =
        hasClosure && (blocker.flags & PATH_MATERIAL_VOLUME_ONLY_BOUNDARY) != 0;

    float3 surfaceVis = make_float3(0.f);
    if (volumeOnly) {
      surfaceVis = make_float3(1.f);
    } else if (hasClosure) {
      if (thinWalled || approximateThick) {
        float3 transmission = make_float3(0.f);
        if (thinWalled
            || (approximateThick
                && (exitsMedium || exitsStraight
                    || blocker.transmission > 0.f))) {
          transmission = make_float3(blocker.straightTransmission);
          if (thinWalled || (!hasMedium && !exitsMedium && !exitsStraight)) {
            transmission =
                transmission * clamp(blocker.transmissionColor, 0.f, 1.f);
          }
          transmission = clamp(transmission, 0.f, 1.f);
        }
        const float presence = clamp(blocker.presence, 0.f, 1.f);
        surfaceVis = clamp(
            make_float3(1.f - presence) + transmission * presence, 0.f, 1.f);
      } else {
        float scalarVis = 1.f - clamp(blocker.opacity, 0.f, 1.f);
        if (exitsMedium)
          scalarVis = max(scalarVis, clamp(blocker.transmission, 0.f, 1.f));
        surfaceVis = make_float3(clamp(scalarVis, 0.f, 1.f));
      }
    }
    vis = vis * surfaceVis;
    if (Transport_isNearlyBlack(vis, SHADOW_VISIBILITY_THRESHOLD))
      return make_float3(0.f);

    org = leaveSurface(org + dir * hitDist, NgBlocker, dir, hitDist, dir, bias);
    remaining -= hitDist;
    if (remaining <= bias)
      return vis;

    if (exitsMedium) {
      PathMediumState_clear(medium);
    } else if (exitsStraight) {
      insideStraight = false;
    } else if (volumeOnly && !medium.active && dot(dir, NgBlocker) < 0.f) {
      medium.active = true;
      medium.medium = Medium_of(blocker.interiorMedium);
      medium.owner = object;
    } else if (approximateThick && !medium.active && hasClosure && !thinWalled
        && blocker.transmission > 0.f && dot(dir, NgBlocker) < 0.f) {
      if (hasMedium) {
        medium.active = true;
        medium.medium = Medium_of(blocker.interiorMedium);
        medium.owner = object;
      } else {
        insideStraight = true;
        straightOwner = object;
      }
    }
  }
  // Typhoon blocks rays through pathologically many layers
  return make_float3(0.f);
}

// Direct lighting ////////////////////////////////////////////////////////////

// The lift of a smooth-shaded triangle's hit off its facet, faced to the
// incident side (Typhoon's _ComputeSmoothShadowOffsetOut()): from the
// triangle's world-space corners and corner normals; zero for other subtypes
// and triangles without shading normals
inline float3 smoothShadowOffset(
    const RayHit &rh, const HitInfo &hit, float3 NgExt, bool frontFacing)
{
  TriangleCorners c;
  bool found = false;
#pragma ignore warning(perf)
  const uniform Geometry *varying geometry = hit.surface->geometry;
  foreach_unique (g in geometry) {
    if (g->getTriangleCorners != NULL)
      found = g->getTriangleCorners(g, rh.hit.primID, c);
  }
  if (!found)
    return make_float3(0.f);
  const float3 offset = Transport_smoothTriangleShadowOffsetFromCorners(
      c, objectToWorld(hit), normalToWorld(hit), rh.hit.u, rh.hit.v, NgExt);
  return frontFacing ? offset : -offset;
}

// Light reaching the vertex 'input' from every light (pathLight(), the ambient
// light's dome too), reflected by the BSDF of 'state' towards input.wo
// (Typhoon's _ComputeDirectLightingMIS()): the mean of lightSamplesPerHit
// stratified samples of each light from 'domain' (the vertex's DirectLighting
// domain), each weighed against BSDF sampling by the multi-sample power
// heuristic where 'includeMis' (and the light isn't a delta), times the shadow
// ray's visibility, clamped to the firefly threshold. Shadow rays leave from
// the hit lifted towards the smooth surface near the terminator of
// smooth-shaded triangles, moved off it by the hit's epsilonFrom(), in the
// path's 'medium', but out of it where they leave the medium's own surface
// outwards (Typhoon's keep it to the light), and at a surface hit from
// outside with the path in none, in the vertex's interior medium where they go
// inwards through a surface they pass straight through (Typhoon's start in
// none). hdri lights are sampled over the whole sphere, or for a
// reflection-only BSDF folded into the hemisphere around state.Ns. On a path
// of throughput 't' in hero-wavelength mode, each sample is the product of its
// spectral values (HeroWavelength_lightSample()), to be weighed with
// PathThroughput_weightDirect().
inline float3 directLighting(const uniform Frame *uniform frame,
    const uniform PathMaterialContext *uniform context,
    const RayHit &rh,
    const HitInfo &hit,
    const PathShadeInput &input,
    const PathMaterialState &state,
    const PathMediumState &medium,
    const PathThroughput &t,
    const SampleDomain &domain,
    bool includeMis)
{
  const uniform World *uniform world = frame->world;
  const uniform PathTracerParameters &params = frame->renderer->pathtracer;
  const uniform int sampleCount = params.lightSamplesPerHit;
  const bool front = (input.flags & PATH_SHADE_FRONT_FACING) != 0;
  const float3 NsrfOut = PathMaterial_faceNormal(input.Nsrf, front);
  const float3 NgOut = PathMaterial_faceNormal(input.Ng, front);
  // straight shadow rays can't stand in for refraction through the vertex's
  // own coupled thick dielectric
  const bool conservative = !params.enableCaustics
      && (state.flags & PATH_MATERIAL_COUPLED_THICK_DIELECTRIC) != 0;
  const PathObject origin = PathObject_of(rh);
  const float bias =
      epsilonFrom(input.P, rh.ray.dir, rh.ray.tfar, TRANSPORT_EPSILON_MIN);
  float3 offset = make_float3(0.f);
  bool offsetComputed = false;
  // a shadow ray that leaves the medium's own surface outwards is out of it
  // (Typhoon's keeps the medium to the light, as it never crosses the owner
  // again; a deliberate difference)
  const bool onOwner = medium.active && PathObject_equal(medium.owner, origin);
  // and with the path outside (in no medium, at a surface it hit from
  // outside: a vertex reached from inside without the medium keeps none, as
  // its continuations do), one that goes inwards through a thick surface
  // with an interior medium, which it passes straight through (caustics off,
  // and the vertex's own object isn't conservative: otherwise its own surface
  // blocks it), starts in that medium, as it would had it crossed the surface
  // there (Typhoon's starts in none; a deliberate difference)
  PathMediumState inwardMedium;
  PathMediumState_clear(inwardMedium);
  if (!medium.active && front && !params.enableCaustics && !conservative
      && (state.flags & PATH_MATERIAL_HAS_CLOSURE) != 0
      && (state.flags & PATH_MATERIAL_HAS_INTERIOR_MEDIUM) != 0
      && (state.flags & PATH_MATERIAL_THIN_WALLED) == 0) {
    PathMedium interior;
    PathMaterial_medium(state, interior);
    inwardMedium.active = true;
    inwardMedium.medium = Medium_of(interior);
    inwardMedium.owner = origin;
  }
  // a reflection-only BSDF samples hdri lights in the hemisphere it reflects
  // into (Typhoon's SamplingMode::ReflectionHemisphere)
  const bool reflectionOnly =
      (state.flags & PATH_MATERIAL_BSDF_REFLECTION_ONLY) != 0;

  float3 direct = make_float3(0.f);
  for (uniform uint32 i = 0; i < pathLightCount(world, params); i++) {
    const uniform Light *uniform light = pathLight(world, params, i);
    const SampleDomain lightDomain = SampleDomain_chain(
        domain, SAMPLE_DOMAIN_DIRECT_LIGHT_SELECT, (uniform int)i);
    float3 radianceLight = make_float3(0.f);
    for (uniform int s = 0; s < sampleCount; s++) {
      const SampleDomain sampleDomain = SampleDomain_split(
          lightDomain, SAMPLE_DOMAIN_DIRECT_LIGHT_SAMPLE, sampleCount, s);
      const float2 u = Transport_stratifyLightSample(
          sampleCount, s, SampleDomain_draw2D(sampleDomain));
      const LightSample ls =
          Light_sample(light, input.P, state.Ns, reflectionOnly, u);
      if (!ls.valid || length(ls.radiance) <= TRANSPORT_MIN_LUMINANCE_CUTOFF)
        continue;

      const float offsetWeight =
          Transport_shadowTerminatorOffsetWeight(NsrfOut, NgOut, ls.wi);
      if (offsetWeight > 0.f && !offsetComputed) {
        offset = smoothShadowOffset(rh, hit, input.Ng, front);
        offsetComputed = true;
      }
      const bool transmission = dot(NsrfOut, ls.wi) < 0.f;
      PathMediumState shadowMedium = medium;
      if (onOwner && dot(ls.wi, input.Ng) > 0.f)
        PathMediumState_clear(shadowMedium);
      else if (inwardMedium.active && dot(ls.wi, input.Ng) < 0.f)
        shadowMedium = inwardMedium;
      const float3 Pshadow =
          input.P + (transmission ? -offset : offset) * offsetWeight;
      const float3 vis = visibility(frame,
          context,
          Pshadow,
          input.Ng,
          ls.wi,
          ls.dist * TRANSPORT_SHADOW_DISTANCE_SCALE,
          bias,
          conservative,
          origin,
          shadowMedium,
          sampleDomain);
      if (Transport_isNearlyBlack(vis))
        continue;

      PathBsdfEval bsdf;
      PathMaterial_eval(state, ls.wi, bsdf);
      bsdf.f = Transport_sanitize(bsdf.f, false);
      float weight = 1.f;
      if (includeMis && !ls.delta) {
        const float lightPdf = ls.pdfInverse > 0.f ? 1.f / ls.pdfInverse : 0.f;
        weight = Transport_powerHeuristic(
            Transport_multiSampleMisLightPdf(lightPdf, sampleCount), bsdf.pdf);
      }
      float3 sampleRadiance;
      if (t.hero) {
        sampleRadiance = HeroWavelength_lightSample(ls.radiance,
            bsdf.f,
            vis,
            ls.pdfInverse,
            weight,
            t.wavelengthNm,
            t.wavelengthPdf);
      } else {
        sampleRadiance = ls.radiance * bsdf.f * vis * (ls.pdfInverse * weight);
      }
      radianceLight = radianceLight
          + Transport_clampFirefly(
              sampleRadiance, params.fireflyClampThreshold);
    }
    direct = direct + radianceLight / (uniform float)sampleCount;
  }
  return direct;
}

// Light reaching the volume collision 'P' from every light, scattered by the
// phase function of anisotropy 'g' towards 'wo' (Typhoon's
// _ComputeMediumDirectLighting(), and halcyon's for transferFunction1D
// volumes): as directLighting(), with the phase function in place of the
// BSDF, weighed against phase sampling where 'includeMis' (but for delta
// lights), shadow rays from 'P' itself in the path's 'medium'. Light i draws
// from 'domain' chained by 'selectKey' with i and split by 'sampleKey'
// (Typhoon's MediumDirectLightSelect and MediumDirectLightSample for
// homogeneous media; the surfaces' keys for grid volumes). In hero-wavelength
// mode (of the path's throughput 't') as directLighting().
inline float3 volumeDirectLighting(const uniform Frame *uniform frame,
    const uniform PathMaterialContext *uniform context,
    float3 P,
    float3 wo,
    float g,
    const PathMediumState &medium,
    const PathThroughput &t,
    const SampleDomain &domain,
    uniform int selectKey,
    uniform int sampleKey,
    bool includeMis)
{
  const uniform World *uniform world = frame->world;
  const uniform PathTracerParameters &params = frame->renderer->pathtracer;
  const uniform int sampleCount = params.lightSamplesPerHit;
  const PathObject none = PathObject_none();

  float3 direct = make_float3(0.f);
  for (uniform uint32 i = 0; i < pathLightCount(world, params); i++) {
    const uniform Light *uniform light = pathLight(world, params, i);
    const SampleDomain lightDomain =
        SampleDomain_chain(domain, selectKey, (uniform int)i);
    float3 radianceLight = make_float3(0.f);
    for (uniform int s = 0; s < sampleCount; s++) {
      const SampleDomain sampleDomain =
          SampleDomain_split(lightDomain, sampleKey, sampleCount, s);
      const float2 u = Transport_stratifyLightSample(
          sampleCount, s, SampleDomain_draw2D(sampleDomain));
      const LightSample ls = Light_sample(light, P, u);
      if (!ls.valid || length(ls.radiance) <= TRANSPORT_MIN_LUMINANCE_CUTOFF)
        continue;
      const float3 vis = visibility(frame,
          context,
          P,
          make_float3(0.f),
          ls.wi,
          ls.dist * TRANSPORT_SHADOW_DISTANCE_SCALE,
          0.f,
          false,
          none,
          medium,
          sampleDomain);
      if (Transport_isNearlyBlack(vis))
        continue;

      const float phase = VolumePhase_eval(wo, ls.wi, g);
      if (!(phase > 0.f))
        continue;
      float weight = 1.f;
      if (includeMis && !ls.delta && ls.pdfInverse > 0.f) {
        const float lightPdf =
            Transport_multiSampleMisLightPdf(1.f / ls.pdfInverse, sampleCount);
        if (lightPdf > 0.f)
          weight = Transport_powerHeuristic(lightPdf, phase);
      }
      float3 sampleRadiance;
      if (t.hero) {
        sampleRadiance = HeroWavelength_mediumLightSample(ls.radiance,
            vis,
            phase,
            ls.pdfInverse,
            weight,
            t.wavelengthNm,
            t.wavelengthPdf);
      } else {
        sampleRadiance = ls.radiance * vis * (phase * ls.pdfInverse * weight);
      }
      radianceLight = radianceLight
          + Transport_clampFirefly(
              sampleRadiance, params.fireflyClampThreshold);
    }
    direct = direct + radianceLight / (uniform float)sampleCount;
  }
  return direct;
}

// What a ray leaving a surface along unit 'dir' from 'org' gathers where it
// misses the scene (Typhoon's _AccumulateEnvironment() for indirect rays): the
// directional lights with a cone it points into and the hdri lights (also
// those camera rays don't see, and the ambient light's constant dome),
// weighed against light sampling by the power heuristic after a BSDF sample
// of pdf 'lastBsdfPdf' (0 after a specular event), with an hdri's pdf in the
// hemisphere around 'hemisphereN' where 'hemisphere' (the last vertex was a
// reflection-only surface, which sampled it there), each weighed by the
// throughput 't' and added to 'radiance' (on a 'caustic' path clamped as
// such)
inline void gatherEnvironment(const uniform World *uniform world,
    const uniform PathTracerParameters &params,
    float3 org,
    float3 dir,
    float lastBsdfPdf,
    float3 hemisphereN,
    bool hemisphere,
    const PathThroughput &t,
    bool caustic,
    float3 &radiance)
{
  for (uniform uint32 i = 0; i < pathLightCount(world, params); i++) {
    const uniform Light *uniform light = pathLight(world, params, i);
    if (light->type == LIGHT_HDRI) {
      // its radiance counts also where its pdf is 0 (weight 1)
      const LightSample ls =
          HdriLight_eval(light, dir, hemisphereN, hemisphere);
      const float3 L = ls.radiance
          * Transport_bsdfSampleMisWeight(
              lastBsdfPdf, ls.pdfInverse, params.lightSamplesPerHit);
      addPathRadiance(params, radiance, PathThroughput_weight(t, L), caustic);
      continue;
    }
    // a delta directional light (angularDiameter 0) is found by light
    // sampling only: Typhoon's gathers it within 1e-5 of its direction's
    // cosine (an angular radius of about 0.26 degrees), which shows it to
    // specular paths as a disc of its irradiance as radiance (a deliberate
    // difference)
    if (light->type != LIGHT_DIRECTIONAL || Light_isDelta(light))
      continue;
    const LightSample ls = Light_eval(light, org, dir);
    if (!ls.valid)
      continue;
    const float3 L = ls.radiance
        * Transport_bsdfSampleMisWeight(
            lastBsdfPdf, ls.pdfInverse, params.lightSamplesPerHit);
    addPathRadiance(params, radiance, PathThroughput_weight(t, L), caustic);
  }
}

// Paths //////////////////////////////////////////////////////////////////////

// The material interface's input at the surface hit of 'rh' (its shading
// context, from the ray differential 'rd'), with the colour of native kinds
// (the lane's PathMaterial_kind() 'kind') and the material's opacity
// 'baseOpacity' and random numbers from the vertex's domain 'bounceDomain'
// (Typhoon's), the path's hero wavelength 'heroWavelengthNm' (0 while it is
// RGB), and the differentials the path keeps for a specular continuation
inline void makeShadeInput(const uniform Frame *uniform frame,
    MaterialKind kind,
    const RayHit &rh,
    const HitInfo &hit,
    const RayDifferential &rd,
    float baseOpacity,
    float heroWavelengthNm,
    const SampleDomain &bounceDomain,
    PathShadeInput &input,
    SurfaceDifferentials &differentials)
{
  ShadingContext_build(frame, rh, hit, rd, input, differentials);
  nativeInputs(frame, kind, rh, hit, true, input);
  input.baseOpacity = baseOpacity;
  input.heroWavelengthNm = heroWavelengthNm;
  input.flags = dot(input.Ng, input.wo) > 0.f ? PATH_SHADE_FRONT_FACING : 0;
  // Typhoon draws presence and the hero wavelength only where it needs them;
  // drawing them always changes no other number, as domains are independent
  input.uPresence = SampleDomain_draw1D(
      SampleDomain_fork(bounceDomain, SAMPLE_DOMAIN_PRESENCE));
  input.uWavelength = SampleDomain_draw1D(
      SampleDomain_fork(bounceDomain, SAMPLE_DOMAIN_WAVELENGTH));
  const float3 u = SampleDomain_draw3D(
      SampleDomain_fork(bounceDomain, SAMPLE_DOMAIN_BSDF_SAMPLE));
  input.u1 = u.x;
  input.u2 = u.y;
  input.uLobe = u.z;
}

// Russian roulette at a vertex from the fork 'key' of 'bounceDomain'
// (RussianRoulette, or MediumRussianRoulette at a collision in a homogeneous
// medium), unbiased: whether the path survives, its 'throughput' then
// divided by its chance (Typhoon's: the largest component of the throughput
// as RGB, at most PATH_RR_MAX_SURVIVAL)
inline bool survivesRoulette(PathThroughput &throughput,
    const SampleDomain &bounceDomain,
    uniform int key = SAMPLE_DOMAIN_RUSSIAN_ROULETTE)
{
  const float3 rgb = PathThroughput_rgb(throughput);
  const float survival =
      min(max(rgb.x, max(rgb.y, rgb.z)), PATH_RR_MAX_SURVIVAL);
  if (survival <= 0.f
      || SampleDomain_draw1D(SampleDomain_fork(bounceDomain, key)) > survival)
    return false;
  PathThroughput_divide(throughput, survival);
  return true;
}

// Moves the ray differential's origins by 'dist' along 'dir', as the path's
// ray moves on through a surface it doesn't scatter at
inline void advanceDifferential(RayDifferential &rd, float3 dir, float dist)
{
  if (rd.valid) {
    rd.rxOrigin = rd.rxOrigin + dir * dist;
    rd.ryOrigin = rd.ryOrigin + dir * dist;
  }
}

// Moves the path's ray from 'org' past its surface hit at distance 'dist'
// along 'dir' by the hit's epsilonFrom(), which becomes the ray's 'bias', with
// its differential, for a null event (Typhoon's presence pass-through): not a
// scattering event, so nothing else changes
inline void advancePastHit(
    float3 &org, float3 dir, float dist, float &bias, RayDifferential &rd)
{
  bias = epsilonFrom(org + dir * dist, dir, dist, TRANSPORT_EPSILON_MIN);
  org = org + dir * (dist + bias);
  advanceDifferential(rd, dir, dist + bias);
}

// Subsurface scattering //////////////////////////////////////////////////////

// Follows the subsurface sample 'bsdfSample' of the vertex at the hit of 'rh'
// (Typhoon's _TraceSubsurface()): enters the surface along the material's
// entry direction (PathMaterial_subsurface(), from the vertex's
// SssEntryDirection domain) with the sample's weight, walks through its
// medium (Subsurface_randomWalk(), from its SssEntry domain) and returns in
// 'exitHit' the hit where the walk leaves, with the walk's weight in
// 'throughput'. False where the path ends: no entry direction below the
// surface, no exit, or a throughput nearly black.
inline bool traceSubsurface(const uniform Frame *uniform frame,
    const RayHit &rh,
    const HitInfo &hit,
    const PathShadeInput &input,
    const PathMaterialState &state,
    const PathBsdfSample &bsdfSample,
    const SampleDomain &bounceDomain,
    PathThroughput &throughput,
    RayHit &exitHit)
{
  const float2 u = SampleDomain_draw2D(
      SampleDomain_fork(bounceDomain, SAMPLE_DOMAIN_SSS_ENTRY_DIRECTION));
  PathSubsurface entry;
  PathMaterial_subsurface(state, bsdfSample, u.x, u.y, entry);
  if (!entry.valid)
    return false;
  PathThroughput_apply(throughput, Transport_sanitize(bsdfSample.f, true));
  if (Transport_isNearlyBlack(
          PathThroughput_rgb(throughput), TRANSPORT_MIN_LUMINANCE_CUTOFF))
    return false;

  SubsurfaceOwner owner;
  owner.instID = rh.hit.instID;
  owner.instPrimID = rh.hit.instPrimID;
  owner.geomID = rh.hit.geomID;
  owner.normalToWorld = normalToWorld(hit);
  owner.objectToWorld = objectToWorld(hit).l;

  const bool front = (input.flags & PATH_SHADE_FRONT_FACING) != 0;
  SubsurfaceWalkInput walk;
  walk.posEntry = input.P;
  walk.dirEntry = entry.entryDir;
  walk.guideN = PathMaterial_faceNormal(input.Nsrf, front);
  walk.albedo = entry.albedo;
  walk.radius = entry.radius;
  walk.anisotropy = entry.anisotropy;
  walk.usePrecomputed = entry.usePrecomputed;
  walk.absorption = entry.absorption;
  walk.scattering = entry.scattering;
  walk.entryBias =
      epsilonFrom(input.P, rh.ray.dir, rh.ray.tfar, TRANSPORT_EPSILON_MIN);

  SubsurfaceWalkOutput exit;
  Subsurface_randomWalk(frame->world->scene,
      owner,
      walk,
      SampleDomain_fork(bounceDomain, SAMPLE_DOMAIN_SSS_ENTRY),
      0.f,
      exit);
  if (!exit.success)
    return false;
  PathThroughput_apply(throughput, Transport_sanitize(exit.throughput, true));
  if (Transport_isNearlyBlack(
          PathThroughput_rgb(throughput), TRANSPORT_MIN_LUMINANCE_CUTOFF))
    return false;

  // the exit as a hit of a ray arriving from outside (Typhoon's
  // _PopulateSssExitRayHit()): at the exit point (t = 0), its normal faced
  // along the walk's direction
  exitHit = makeRay(exit.posExit, normalize(-exit.dirExit), 0.f, 0.f);
  exitHit.hit.Ng = exit.NgObjectExt;
  exitHit.hit.u = exit.u;
  exitHit.hit.v = exit.v;
  exitHit.hit.primID = exit.primID;
  exitHit.hit.geomID = owner.geomID;
  exitHit.hit.instID = owner.instID;
  exitHit.hit.instPrimID = owner.instPrimID;
  return true;
}

// What a path gives the albedo and normal channels: the albedo and the
// shading normal of its first non-specular hit (see renderFrame())
struct PathAlbedoNormal
{
  float3 albedo;
  float3 normal;
};

// One path through pixel (x, y) for sample 'sample' (counted from the last
// change), Typhoon's _IntegratePath(): its radiance, and its opacity (1 where
// the camera ray finds a surface or a light or scatters in a volume; where it
// misses, alpha + (1 - T) (1 - alpha) of the background's (or the visible
// hdris') alpha, T the mean of the throughput's channels, the transmittance of
// the media it crossed). With 'wantAlbedoNormal', also its albedo
// and normal channel values in 'channels': at the first surface it scatters at
// whose closure isn't specular-only (PATH_MATERIAL_SPECULAR_ONLY), the
// closure's albedo times the path's throughput (the specular weights, and
// Russian roulette's, of the specular-only surfaces before it) and the
// surface's smooth shading normal, as authored (exterior: not faced to the ray,
// no normal map); a surface without a closure gives the invalid material
// colour, a volume collision its colour (in a homogeneous medium its
// single-scattering albedo) and normal 0. Null events (mask cut-outs, rejected
// presence, volume-only boundaries) are looked past. A specular-only surface
// leaves its normal until a later surface replaces it, and the albedo 0 if the
// path ends first; 0 in both where the camera ray meets no surface.
inline float4 tracePath(const uniform Frame *uniform frame,
    const uniform PathMaterialContext *uniform context,
    int x,
    int y,
    int sample,
    uniform bool wantAlbedoNormal,
    PathAlbedoNormal &channels)
{
  const uniform World *uniform world = frame->world;
  const uniform Renderer *uniform r = frame->renderer;
  const uniform PathTracerParameters &params = r->pathtracer;

  // Typhoon's sampler of the pixel sample
  const SampleDomain root = SampleDomain_root(params.frameSeed, x, y, sample);
  const float2 jitter = SampleDomain_draw2D(
      SampleDomain_fork(root, SAMPLE_DOMAIN_CAMERA_JITTER));
  const float2 screen = Camera_screenFromPixel(
      frame->camera, make_int2(x, y), jitter, frame->invSize);
  const RayHit cameraRay = Camera_makeRay(frame->camera, screen, frame->aspect);
  RayDifferential rd =
      RayDifferential_camera(frame, make_int2(x, y), jitter, cameraRay);
  float3 org = cameraRay.ray.org;
  float3 dir = normalize(cameraRay.ray.dir);

  float3 radiance = make_float3(0.f);
  float opacity = 1.f;
  // RGB, or after the first dispersive closure spectral at its hero
  // wavelength
  PathThroughput throughput;
  PathThroughput_init(throughput);
  // whether the path took a caustic sample (only with enableCaustics): its
  // contributions are clamped to causticsClampThreshold too
  bool caustic = false;
  // the last scattering event's BSDF pdf, 0 after a specular one (MIS)
  float lastBsdfPdf = 0.f;
  // whether the last scattering event was a non-specular sample of a BSDF
  // that only reflects (after caustic pruning), which samples hdri lights in
  // the hemisphere
  // around its shading normal (Typhoon's lastLightSamplingMode and
  // lastLightSamplingNormal; a volume collision samples the whole sphere)
  bool lastHemisphere = false;
  float3 lastHemisphereN = make_float3(0.f);
  bool cameraSegment = true;
  bool diffuseLikeAncestor = false;
  // the offset of the ray's origin off the surface it leaves, epsilonFrom()
  // of the hit, and the ray's tnear (0 on the camera segment)
  float bias = 0.f;
  // until the path reaches its first non-specular hit
  bool albedoNormalPending = wantAlbedoNormal;
  // whether the next vertex is the exit of a subsurface walk, 'exitHit'
  bool subsurfaceExit = false;
  RayHit exitHit;
  // the homogeneous interior medium the path is in
  PathMediumState medium;
  PathMediumState_clear(medium);
  channels.albedo = make_float3(0.f);
  channels.normal = make_float3(0.f);

  // 'bounce' counts scattering events: vertices up to maxBounces scatter, and
  // the ray leaving the last one only gathers the lights it finds (Typhoon's
  // emitter-only segment). 'pathEvent' counts every surface the path meets,
  // null events and subsurface walks' exits included, and every volume or
  // medium collision, and selects the vertex's sample domain.
  int bounce = 0;
  for (int pathEvent = 0;; pathEvent++) {
    const bool emitterOnly = bounce > params.maxBounces;
    const SampleDomain bounceDomain =
        SampleDomain_chain(root, SAMPLE_DOMAIN_PATH_BOUNCE, pathEvent);

    // a subsurface walk's exit is the next hit, as the walk found it
    // (Typhoon's synthetic exit hit): no segment leads there, so no light or
    // volume is in front of it
    RayHit rh;
    if (subsurfaceExit) {
      rh = exitHit;
    } else {
      rh = makeRay(org, dir, cameraSegment ? 0.f : bias, HALCYON_RAY_TFAR);
      rtcIntersectV(world->scene, (varying RTCRayHit * uniform) & rh);
    }
    const HitInfo hit = getHitInfo(world, rh);
    const bool hitSurface = foundHit(hit);

    LightSample lightHit;
    bool lightFound = false;
    if (!subsurfaceExit) {
      lightFound = nearestFiniteLight(world,
          org,
          dir,
          hitSurface ? rh.ray.tfar : HALCYON_RAY_TFAR,
          cameraSegment,
          lightHit);
    }

    // the homogeneous medium the path is in (Typhoon's
    // _TraceVolumeTransmission()): a collision sampled by one channel's
    // extinction (channel MIS), or for an absorbing-only medium none; a
    // subsurface walk's exit is in none
    MediumTrack track;
    track.collision = floatbits(0x7f800000);
    const bool inMedium = medium.active;
    if (inMedium) {
      float uChannel = 0.f;
      float uFreeFlight = 0.f;
      if (!Medium_isAbsorbingOnly(medium.medium)) {
        uChannel = SampleDomain_draw1D(
            SampleDomain_fork(bounceDomain, SAMPLE_DOMAIN_MEDIUM_CHANNEL));
        uFreeFlight = SampleDomain_draw1D(
            SampleDomain_fork(bounceDomain, SAMPLE_DOMAIN_MEDIUM_FREE_FLIGHT));
      }
      Medium_beginTrack(medium.medium,
          PathThroughput_rgb(throughput),
          uChannel,
          uFreeFlight,
          track);
    }

    // the transferFunction1D volumes in front of the light or surface (and of
    // a medium collision, whose extinction adds to theirs): a collision there
    // is the path's next vertex (delta tracking), weighed by the medium's
    // probability of no collision before it; the emitter-only segment only
    // takes their transmittance (ratio tracking)
    if (world->numVolumeInstances > 0 && !subsurfaceExit) {
      const float tEnd =
          min(lightFound ? lightHit.dist
                         : (hitSurface ? rh.ray.tfar : HALCYON_RAY_TFAR),
              track.collision);
      float tCollision;
      float3 albedo;
      if (emitterOnly) {
        int step = 0;
        PathThroughput_apply(throughput,
            VolumeTransport_transmittance(
                world, org, dir, rh.ray.tnear, tEnd, bounceDomain, step));
      } else if (VolumeTransport_freeFlight(world,
                     org,
                     dir,
                     rh.ray.tnear,
                     tEnd,
                     bounceDomain,
                     tCollision,
                     albedo)) {
        if (inMedium) {
          PathThroughput_apply(throughput,
              Medium_transmittanceWeight(medium.medium, track, tCollision));
          if (Transport_isNearlyBlack(PathThroughput_rgb(throughput),
                  TRANSPORT_MIN_LUMINANCE_CUTOFF))
            break;
        }
        // a volume collision is a scattering event, the first non-specular
        // hit of the albedo and normal channels (of normal 0)
        const float3 P = org + dir * tCollision;
        if (albedoNormalPending) {
          channels.albedo = PathThroughput_rgb(throughput) * albedo;
          channels.normal = make_float3(0.f);
          albedoNormalPending = false;
        }
        PathThroughput_apply(throughput, albedo);
        addPathRadiance(params,
            radiance,
            PathThroughput_weightDirect(throughput,
                volumeDirectLighting(frame,
                    context,
                    P,
                    -dir,
                    0.f,
                    medium,
                    throughput,
                    SampleDomain_fork(
                        bounceDomain, SAMPLE_DOMAIN_DIRECT_LIGHTING),
                    SAMPLE_DOMAIN_DIRECT_LIGHT_SELECT,
                    SAMPLE_DOMAIN_DIRECT_LIGHT_SAMPLE,
                    true)),
            caustic);
        if (Transport_isNearlyBlack(PathThroughput_rgb(throughput), 0.f))
          break;

        // past maxBounces the phase sample only looks for a light
        float phasePdf;
        const float3 wi = VolumePhase_sample(-dir,
            0.f,
            SampleDomain_draw2D(
                SampleDomain_fork(bounceDomain, SAMPLE_DOMAIN_VOLUME_PHASE)),
            phasePdf);
        const bool emitterOnlySample = bounce >= params.maxBounces;
        lastBsdfPdf = phasePdf;
        lastHemisphere = false;
        diffuseLikeAncestor = true;
        cameraSegment = false;
        if (!emitterOnlySample && bounce >= params.minBouncesBeforeRR
            && !survivesRoulette(throughput, bounceDomain))
          break;
        rd.valid = false;
        org = P;
        dir = wi;
        bias = 0.f;
        bounce++;
        continue;
      }
    }

    if (inMedium) {
      // a collision before the surface and the light scatters there (on the
      // emitter-only segment and past maxBounces it only gathers light):
      // Typhoon's medium event, a bounce
      const float tLight = lightFound ? lightHit.dist : floatbits(0x7f800000);
      const float tSurface = hitSurface ? rh.ray.tfar : floatbits(0x7f800000);
      if (track.collision < min(tLight, tSurface)) {
        const float3 P = org + dir * track.collision;
        if (albedoNormalPending) {
          channels.albedo =
              PathThroughput_rgb(throughput) * Medium_albedo(medium.medium);
          channels.normal = make_float3(0.f);
          albedoNormalPending = false;
        }
        PathThroughput_apply(throughput,
            Medium_scatterWeight(medium.medium, track, track.collision));
        if (Transport_isNearlyBlack(
                PathThroughput_rgb(throughput), TRANSPORT_MIN_LUMINANCE_CUTOFF))
          break;
        addPathRadiance(params,
            radiance,
            PathThroughput_weightDirect(throughput,
                volumeDirectLighting(frame,
                    context,
                    P,
                    -dir,
                    medium.medium.anisotropy,
                    medium,
                    throughput,
                    SampleDomain_fork(
                        bounceDomain, SAMPLE_DOMAIN_MEDIUM_DIRECT_LIGHTING),
                    SAMPLE_DOMAIN_MEDIUM_DIRECT_LIGHT_SELECT,
                    SAMPLE_DOMAIN_MEDIUM_DIRECT_LIGHT_SAMPLE,
                    bounce < params.maxBounces)),
            caustic);
        if (bounce >= params.maxBounces)
          break;
        if (bounce >= params.minBouncesBeforeRR
            && !survivesRoulette(throughput,
                bounceDomain,
                SAMPLE_DOMAIN_MEDIUM_RUSSIAN_ROULETTE))
          break;
        float phasePdf;
        const float3 wi = VolumePhase_sample(-dir,
            medium.medium.anisotropy,
            SampleDomain_draw2D(
                SampleDomain_fork(bounceDomain, SAMPLE_DOMAIN_MEDIUM_PHASE)),
            phasePdf);
        if (!(phasePdf > 0.f) || !isfinite(phasePdf))
          break;
        rd.valid = false;
        org = P;
        dir = wi;
        bias = 0.f;
        lastBsdfPdf = phasePdf;
        lastHemisphere = false;
        diffuseLikeAncestor = true;
        cameraSegment = false;
        bounce++;
        continue;
      }
      // otherwise the light or surface is reached through the medium; a ray
      // that leaves the scene inside a medium ends the path (Typhoon's)
      if (!lightFound && !hitSurface)
        break;
      const float tReached = lightFound ? tLight : tSurface;
      if (!(tReached > 0.f))
        break;
      PathThroughput_apply(throughput,
          Medium_transmittanceWeight(medium.medium, track, tReached));
      if (Transport_isNearlyBlack(
              PathThroughput_rgb(throughput), TRANSPORT_MIN_LUMINANCE_CUTOFF))
        break;
    }

    // a finite light in front of the surface ends the path; a light found by
    // a BSDF sample is weighed against light sampling
    if (lightFound) {
      const float3 L = lightHit.radiance
          * Transport_bsdfSampleMisWeight(
              lastBsdfPdf, lightHit.pdfInverse, params.lightSamplesPerHit);
      addPathRadiance(
          params, radiance, PathThroughput_weight(throughput, L), caustic);
      break;
    }

    if (!hitSurface) {
      if (cameraSegment) {
        // the visible hdri lights, else the background, through the media
        // the camera ray crossed (volume-only boundaries; Typhoon shows it
        // unweighted, a deliberate difference): weighed
        // by the throughput, and showing the mean of its channels of what is
        // behind, as transferFunction1D volumes' passes (throughput 1) and
        // collisions (opacity 1) converge to
        const float4 miss = missColor(r, world, screen, dir);
        radiance =
            radiance + PathThroughput_weight(throughput, make_float3(miss));
        const float3 t = PathThroughput_rgb(throughput);
        const float transmittance = (t.x + t.y + t.z) / 3.f;
        opacity = clamp(
            miss.w + (1.f - transmittance) * (1.f - miss.w), 0.f, 1.f);
      } else {
        gatherEnvironment(world,
            params,
            org,
            dir,
            lastBsdfPdf,
            lastHemisphereN,
            lastHemisphere,
            throughput,
            caustic,
            radiance);
      }
      break;
    }
#pragma ignore warning(perf)
    const uniform Material *varying material = hit.surface->material;
    // a native material's opacity is its presence: a fully absent surface (a
    // mask cut-out) is a null event without shading it
    const float baseOpacity = Surface_getCompositeOpacity(rh, hit);
    // a walk's exit is shaded by the white Lambertian that replaces its
    // material (C++'s, whatever the material's kind)
    const uint32 exitFlag = subsurfaceExit ? PATH_SHADE_SUBSURFACE_EXIT : 0;
    subsurfaceExit = false;
    const MaterialKind kind = PathMaterial_kind(context, material, exitFlag);
    if (kind != MATERIAL_KIND_CPP && baseOpacity <= 0.f) {
      advancePastHit(org, dir, rh.ray.tfar, bias, rd);
      continue;
    }
    // the ray leaving the last scattering vertex only gathers lights;
    // Typhoon's light-only ray mask would see them through ordinary surfaces,
    // its documented intent (and this) has those end the path; mask cut-outs
    // are passed (native ones as above, C++ ones by a visibility-only
    // evaluation of their presence), partly present surfaces end it.
    // Volume-only boundaries are passed too, into or out of their medium,
    // whose transmittance the segment then takes, as through Typhoon's mask.
    if (emitterOnly) {
      PathShadowHit absent;
      absent.flags = 0;
      if (kind == MATERIAL_KIND_CPP)
        blockerMaterial(frame, context, rh, hit, absent);
      const bool hasClosure = (absent.flags & PATH_MATERIAL_HAS_CLOSURE) != 0;
      if (hasClosure
          && (absent.flags & PATH_MATERIAL_VOLUME_ONLY_BOUNDARY) != 0) {
        const float3 Ng = normalize(worldNg(rh, hit));
        const float dirDotNg = dot(dir, Ng);
        const PathObject object = PathObject_of(rh);
        if (medium.active && PathObject_equal(medium.owner, object)
            && dirDotNg > 0.f) {
          PathMediumState_clear(medium);
        } else if (!medium.active
            && (absent.flags & PATH_MATERIAL_HAS_INTERIOR_MEDIUM) != 0
            && dirDotNg < 0.f) {
          medium.active = true;
          medium.medium = Medium_of(absent.interiorMedium);
          medium.owner = object;
        }
        org = leaveSurface(
            org + dir * rh.ray.tfar, Ng, dir, rh.ray.tfar, dir, bias);
        advanceDifferential(rd, dir, rh.ray.tfar + bias);
        continue;
      }
      if (hasClosure && absent.presence <= 0.f) {
        advancePastHit(org, dir, rh.ray.tfar, bias, rd);
        continue;
      }
      break;
    }

    PathShadeInput input;
    SurfaceDifferentials differentials;
    makeShadeInput(frame,
        kind,
        rh,
        hit,
        rd,
        baseOpacity,
        PathThroughput_wavelength(throughput),
        bounceDomain,
        input,
        differentials);
    input.flags |= exitFlag;
    if (diffuseLikeAncestor && !params.enableCaustics)
      input.flags |= PATH_SHADE_PRUNE_CAUSTICS;
    if (albedoNormalPending)
      input.flags |= PATH_SHADE_ALBEDO;
    const bool front = (input.flags & PATH_SHADE_FRONT_FACING) != 0;

    PathMaterialState state;
    PathBsdfSample bsdfSample;
    PathMaterial_shade(context, material, input, state, bsdfSample);
    if (state.flags & PATH_MATERIAL_PRESENCE_SKIPPED) {
      advancePastHit(org, dir, rh.ray.tfar, bias, rd);
      continue;
    }
    const bool hasClosure = (state.flags & PATH_MATERIAL_HAS_CLOSURE) != 0;
    const bool hasBsdf = (state.flags & PATH_MATERIAL_HAS_BSDF) != 0;
    if (albedoNormalPending && !(state.flags & PATH_MATERIAL_VOLUME_ONLY_BOUNDARY)) {
      channels.normal = input.Nsrf;
      if (!hasClosure) {
        channels.albedo = PathThroughput_rgb(throughput) * input.baseColor;
        albedoNormalPending = false;
      } else if (!(state.flags & PATH_MATERIAL_SPECULAR_ONLY)) {
        channels.albedo = PathThroughput_rgb(throughput) * state.albedo;
        albedoNormalPending = false;
      }
    }
    // the first dispersive closure (of a BSDF that samples, after caustic
    // pruning) puts the path into hero-wavelength mode for the rest of it
    if (state.flags & PATH_MATERIAL_HERO_STARTED) {
      PathThroughput_startHero(
          throughput, state.heroWavelengthNm, state.heroWavelengthPdf);
    }
    const bool hasSample = (bsdfSample.flags & PATH_SAMPLE_VALID) != 0;
    const bool subsurface = (bsdfSample.flags & PATH_SAMPLE_SUBSURFACE) != 0;
    const bool specular = (bsdfSample.flags & PATH_SAMPLE_SPECULAR) != 0;
    // the surface's normal derivatives don't describe a normal the material
    // resolved itself (Typhoon's tolerance)
    if (length2(state.Ns - PathMaterial_faceNormal(input.Nsrf, front)) > 1e-12f)
      differentials.hasNormalDerivatives = false;

    // a subsurface sample enters the surface and walks to where it leaves;
    // entry, walk and exit are one bounce (the exit's), so the entry adds no
    // emission or direct light and isn't counted
    if (hasBsdf && hasSample && bounce < params.maxBounces && subsurface
        && (state.flags & PATH_MATERIAL_HAS_SUBSURFACE) != 0) {
      if (!traceSubsurface(frame,
              rh,
              hit,
              input,
              state,
              bsdfSample,
              bounceDomain,
              throughput,
              exitHit))
        break;
      org = exitHit.ray.org;
      dir = exitHit.ray.dir;
      rd.valid = false;
      lastBsdfPdf = 0.f;
      // the walk leaves the path in no medium (Typhoon's)
      PathMediumState_clear(medium);
      diffuseLikeAncestor = true;
      cameraSegment = false;
      subsurfaceExit = true;
      continue;
    }

    if (hasClosure) {
      addPathRadiance(params,
          radiance,
          PathThroughput_weight(throughput, state.emission),
          caustic);
    }

    // next-event estimation; a surface without a closure (no valid
    // material) is lit as a Lambertian of its colour (Typhoon: a diffuse
    // closure of its display colour; halcyon: the invalid material colour),
    // without MIS as its path goes no further
    if (hasBsdf || !hasClosure) {
      PathMaterialState lightState = state;
      if (!hasClosure) {
        PathShadeInput fallback = input;
        fallback.baseOpacity = 1.f;
        PathBsdfSample unused;
        lightState.kind = MATERIAL_KIND_LAMBERT;
        PathLambert_shade(fallback, lightState, unused);
      }
      const float3 direct = directLighting(frame,
          context,
          rh,
          hit,
          input,
          lightState,
          medium,
          throughput,
          SampleDomain_fork(bounceDomain, SAMPLE_DOMAIN_DIRECT_LIGHTING),
          hasClosure);
      addPathRadiance(params,
          radiance,
          PathThroughput_weightDirect(throughput, direct),
          caustic);
    }

    // a scattering-free volume boundary is crossed without a bounce, into
    // or out of its interior medium
    if (state.flags & PATH_MATERIAL_VOLUME_ONLY_BOUNDARY) {
      updatePathMedium(medium, state, PathObject_of(rh), dot(dir, input.Ng));
      org = leaveSurface(input.P, input.Ng, dir, rh.ray.tfar, dir, bias);
      advanceDifferential(rd, dir, rh.ray.tfar + bias);
      continue;
    }

    // past maxBounces a valid sample only looks for a light
    const bool emitterOnlySample =
        bounce >= params.maxBounces && hasBsdf && hasSample && !subsurface;
    if (bounce >= params.maxBounces && !emitterOnlySample)
      break;
    if (!hasBsdf || !hasSample || subsurface)
      break;

    const float3 wi = bsdfSample.wi;
    const float woDotNg = dot(input.wo, input.Ng);
    const float wiDotNg = dot(wi, input.Ng);
    const bool crossesBoundary =
        (woDotNg > 0.f && wiDotNg < 0.f) || (woDotNg < 0.f && wiDotNg > 0.f);
    // the sampled interface as absolute IORs, air outside
    float iorIn = 1.f;
    float iorOut = 1.f;
    if (crossesBoundary && bsdfSample.eta > 0.f && bsdfSample.eta != 1.f) {
      if (front)
        iorOut = 1.f / bsdfSample.eta;
      else
        iorIn = bsdfSample.eta;
    }
    // a specular or boundary-crossing sample after a diffuse-like event is a
    // caustic one: with caustics off, pruning keeps caustic lobes from being
    // sampled, and this catches what only shows after sampling; with them on,
    // the path's contributions are clamped as caustic from here. A
    // translucent sample (diffuse transmission) crosses without being one, as
    // pruning keeps its lobe and next-event estimation goes through it
    // (Typhoon's counts every crossing; a deliberate difference)
    const bool translucentSample = !specular
        && (bsdfSample.flags & PATH_SAMPLE_DIFFUSE_LIKE) != 0
        && (bsdfSample.flags & PATH_SAMPLE_TRANSMISSION) != 0;
    const bool causticSample = diffuseLikeAncestor
        && (specular || (crossesBoundary && !translucentSample));
    if (causticSample && !params.enableCaustics)
      break;
    caustic = caustic || causticSample;

    if (throughput.hero) {
      throughput.spectral = throughput.spectral
          * HeroWavelength_bsdfWeight(
              bsdfSample.f, bsdfSample.pdf, specular, throughput.wavelengthNm);
    } else {
      const float3 weight = Transport_sanitize(
          specular ? bsdfSample.f : bsdfSample.f / bsdfSample.pdf, true);
      throughput.rgb = throughput.rgb * weight;
    }

    lastBsdfPdf = specular ? 0.f : bsdfSample.pdf;
    // the hemisphere next-event estimation sampled the hdris in: the BSDF's
    // flag (after caustic pruning), so that their MIS weights sum to 1 where
    // pruning removes a transmission lobe. Typhoon takes the closure's
    // (pathIntegrator.cpp:694); a deliberate difference.
    lastHemisphere =
        !specular && (state.flags & PATH_MATERIAL_BSDF_REFLECTION_ONLY) != 0;
    lastHemisphereN = state.Ns;
    if (bsdfSample.flags & PATH_SAMPLE_DIFFUSE_LIKE)
      diffuseLikeAncestor = true;
    cameraSegment = false;
    // any boundary-crossing sample going inwards through a thick surface
    // enters its interior medium (one at a time, as Typhoon's), and any going
    // outwards through the medium's owner leaves it, whatever its eta.
    // Typhoon's enter and leave only on a refraction (eta other than 1), so a
    // translucent lobe's sample crossed the interior without the medium and,
    // leaving, kept it outside, where next-event estimation through the same
    // lobe takes it inside and leaves it outside: deliberate differences
    if (crossesBoundary && hasClosure) {
      const PathObject object = PathObject_of(rh);
      if (medium.active && PathObject_equal(medium.owner, object)
          && wiDotNg > 0.f) {
        PathMediumState_clear(medium);
      } else if ((state.flags & PATH_MATERIAL_THIN_WALLED) == 0) {
        updatePathMedium(medium, state, object, wiDotNg);
      }
    }

    if (!emitterOnlySample && bounce >= params.minBouncesBeforeRR
        && !survivesRoulette(throughput, bounceDomain))
      break;

    RayDifferential_propagate(rd,
        differentials,
        input.P,
        state.Ns,
        input.wo,
        wi,
        iorIn / iorOut,
        specular);
    // off the geometric surface to the sampled side
    org = leaveSurface(input.P, input.Ng, dir, rh.ray.tfar, wi, bias);
    dir = wi;
    bounce++;
  }

  return make_float4(max(radiance, make_float3(0.f)), opacity);
}

// The shading context value of 'mode' (not PATH_TRACER_MODE_DEFAULT) at the
// first visible surface of the ray through the center of pixel (x, y), as a
// color: a vector in rgb with alpha 1, (s, t, 0, 1) for the texture
// coordinates, the display opacity in alpha, (dudx, dvdx, dudy, dvdy) for the
// texture coordinates' derivatives; transparent black where the ray misses
inline float4 shadingContextValue(const uniform Frame *uniform frame,
    uniform PathTracerMode mode,
    int x,
    int y)
{
  const float2 offset = make_float2(0.5f);
  const float2 screen = Camera_screenFromPixel(
      frame->camera, make_int2(x, y), offset, frame->invSize);
  RayHit rh = Camera_makeRay(frame->camera, screen, frame->aspect);
  const RayDifferential rd =
      RayDifferential_camera(frame, make_int2(x, y), offset, rh);
  HitInfo hit;
  float depth;
  HitInfo ids;
  if (!traceFirstVisibleSurface(frame->world, rh, hit, depth, ids))
    return make_float4(0.f);

  PathShadeInput ctx;
  SurfaceDifferentials s;
  ShadingContext_build(frame, rh, hit, rd, ctx, s);
  switch (mode) {
  case PATH_TRACER_MODE_POSITION:
    return make_float4(ctx.objectP, 1.f);
  case PATH_TRACER_MODE_NORMAL:
    return make_float4(ctx.Nsrf, 1.f);
  case PATH_TRACER_MODE_TANGENT:
    return make_float4(ctx.tangent, 1.f);
  case PATH_TRACER_MODE_BITANGENT:
    return make_float4(ctx.bitangent, 1.f);
  case PATH_TRACER_MODE_TEXCOORD:
    return make_float4(ctx.texcoord.x, ctx.texcoord.y, 0.f, 1.f);
  case PATH_TRACER_MODE_DISPLAY_COLOR:
    return make_float4(ctx.displayColor, ctx.displayOpacity);
  case PATH_TRACER_MODE_DPDU:
    return make_float4(ctx.dPdu, 1.f);
  case PATH_TRACER_MODE_DPDV:
    return make_float4(ctx.dPdv, 1.f);
  case PATH_TRACER_MODE_DNDU:
    return make_float4(s.dndu, 1.f);
  case PATH_TRACER_MODE_DNDV:
    return make_float4(s.dndv, 1.f);
  case PATH_TRACER_MODE_DPDX:
    return make_float4(ctx.dPdx, 1.f);
  case PATH_TRACER_MODE_DPDY:
    return make_float4(ctx.dPdy, 1.f);
  case PATH_TRACER_MODE_TEXCOORD_DERIVATIVES:
    return make_float4(ctx.dudx, ctx.dvdx, ctx.dudy, ctx.dvdy);
  default:
    return make_float4(0.f);
  }
}

// A frame's refinement state for the 'quality' renderer (PathTracerFrameState
// in PathTracer.cpp, which owns its buffers): per pixel, the running mean
// colour, its Welford sum of squared deviations (packed float3), whether the
// pixel is converged, and the albedo and normal channels' running means
// (packed float3, NULL for channels the frame doesn't have)
struct PathTracerFrameState
{
  float4 *uniform accum;
  uniform float *uniform varianceM2;
  uniform uint8 *uniform converged;
  uniform float *uniform albedoAccum;
  uniform float *uniform normalAccum;
  // renders accumulated before this one, 0 after a change (store as float to
  // avoid constant conversions)
  uniform float accumId;
  // the samples every pixel not yet converged took before this render
  uniform int firstSample;
};

// Adds sample 'count' (counted from 1) 'value' to the running mean 'mean'
inline void accumulateFloat3(float3 &mean, float3 value, int count)
{
  mean = count > 1 ? mean + (value - mean) / (float)count : value;
}

// Shading pass: adds 'pixelSamples' paths to the running mean of every pixel
// in the SIMD span starting at (start_x, y) that isn't converged, and the
// paths' albedo and normal (tracePath()) to the running means of those
// channels, if the frame has them; then decides whether the pixel is
// converged (adaptive sampling, AdaptiveSampling.isph): a converged pixel
// takes no more samples, and keeps its channels, until a change. Writes the
// depth and id channels from the pixel center's visible hit, as the
// 'preview' renderer does (on the first render after a change only, as they
// don't change while renders accumulate).
inline unmasked void renderFrame(uniform Frame *uniform frame,
    uniform PathTracerFrameState *uniform state,
    uniform int start_x,
    uniform int y)
{
  const int x = start_x + programIndex;
  if (x >= frame->size.x)
    return;

  const int pixelIndex = y * frame->size.x + x;
  const uniform bool firstRender = state->accumId == 0.f;
  // a change starts the pixel's statistics over
  if (!firstRender && state->converged[pixelIndex])
    return;

  const uniform Renderer *uniform r = frame->renderer;
  const uniform int pixelSamples = r->pathtracer.pixelSamples;
  // every pixel not yet converged took the same samples before this render,
  // so sample indices are per pixel (as Typhoon's sample passes skip
  // converged pixels)
  const uniform int firstSample = state->firstSample;

  uniform PathMaterialContext context;
  context.useAdobeOpenPBR = false;
  context.nativeMaterials = r->pathtracer.nativeMaterials;
  context.luminance[0] = r->pathtracer.luminance[0];
  context.luminance[1] = r->pathtracer.luminance[1];
  context.luminance[2] = r->pathtracer.luminance[2];
  context.viewPosition[0] = frame->camera->pos.x;
  context.viewPosition[1] = frame->camera->pos.y;
  context.viewPosition[2] = frame->camera->pos.z;

  // the albedo and normal channels, from the same paths as the color (in
  // the debug modes, from paths traced for them alone)
  const uniform bool wantAlbedoNormal =
      state->albedoAccum || state->normalAccum;

  float4 debugColor = make_float4(0.f);
  const uniform PathTracerMode mode = r->pathtracer.mode;
  if (mode != PATH_TRACER_MODE_DEFAULT) {
    // the same value whatever the sample
    debugColor = shadingContextValue(frame, mode, x, y);
  }

  float4 mean = make_float4(0.f);
  float3 m2 = make_float3(0.f);
  float3 albedo = make_float3(0.f);
  float3 normal = make_float3(0.f);
  if (!firstRender) {
#pragma ignore warning(perf)
    mean = state->accum[pixelIndex];
    m2 = loadPackedFloat3(state->varianceM2, pixelIndex);
    if (state->albedoAccum)
      albedo = loadPackedFloat3(state->albedoAccum, pixelIndex);
    if (state->normalAccum)
      normal = loadPackedFloat3(state->normalAccum, pixelIndex);
  }

  // every sample goes into the running means one by one (Typhoon's
  // _UpdateVariance()), so N renders of one sample are one render of N
  for (uniform int s = 0; s < pixelSamples; s++) {
    const int count = firstSample + s + 1;
    float4 color = debugColor;
    if (mode == PATH_TRACER_MODE_DEFAULT || wantAlbedoNormal) {
      PathAlbedoNormal channels;
      const float4 radiance = tracePath(
          frame, &context, x, y, firstSample + s, wantAlbedoNormal, channels);
      if (mode == PATH_TRACER_MODE_DEFAULT)
        color = radiance;
      accumulateFloat3(albedo, channels.albedo, count);
      accumulateFloat3(normal, channels.normal, count);
    }
    AdaptiveSampling_addSample(mean, m2, color, count);
  }

#pragma ignore warning(perf)
  state->accum[pixelIndex] = mean;
  storePackedFloat3(state->varianceM2, pixelIndex, m2);
  // a render is a pass of 'pixelSamples' samples: pixels converge between
  // renders (as between Typhoon's one-sample passes)
#pragma ignore warning(perf)
  state->converged[pixelIndex] = AdaptiveSampling_converged(make_float3(mean),
                                     m2,
                                     firstSample + pixelSamples,
                                     r->pathtracer.adaptiveThreshold,
                                     r->pathtracer.minSamplesBeforeAdaptive)
      ? 1
      : 0;

  writeColor(frame, pixelIndex, mean);
  if (state->albedoAccum) {
    storePackedFloat3(state->albedoAccum, pixelIndex, albedo);
    writeAlbedo(frame, pixelIndex, albedo);
  }
  if (state->normalAccum) {
    storePackedFloat3(state->normalAccum, pixelIndex, normal);
    writeNormal(frame, pixelIndex, normal);
  }

  if (firstRender
      && (frame->depth || frame->primId || frame->objId || frame->instId)) {
    const float2 screen =
        Camera_screenFromPixel(frame->camera, make_int2(x, y), frame->invSize);
    RayHit rh = Camera_makeRay(frame->camera, screen, frame->aspect);
    HitInfo surface;
    float depth;
    HitInfo ids;
    traceFirstVisibleSurface(frame->world, rh, surface, depth, ids);
    writeDepthAndIds(frame, pixelIndex, depth, ids);
  }
}

// Exported interface /////////////////////////////////////////////////////////

export void PathTracer_setParameters(void *uniform _self,
    uniform PathTracerMode mode,
    uniform int pixelSamples,
    uniform int maxBounces,
    const void *uniform ambientLight,
    uniform int randomNumberSeed,
    uniform int lightSamplesPerHit,
    uniform int minBouncesBeforeRR,
    uniform float fireflyClampThreshold,
    uniform bool disableShadows,
    uniform bool enableCaustics,
    uniform float causticsClampThreshold,
    uniform bool nativeMaterials,
    const uniform float *uniform luminance /* float3 */,
    uniform int differentialSamples,
    uniform float adaptiveThreshold,
    uniform int minSamplesBeforeAdaptive)
{
  uniform Renderer *uniform self = object_cast<Renderer>(_self);
  self->pathtracer.mode = mode;
  self->pathtracer.lightSamplesPerHit = lightSamplesPerHit;
  self->pathtracer.minBouncesBeforeRR = minBouncesBeforeRR;
  self->pathtracer.fireflyClampThreshold = fireflyClampThreshold;
  self->pathtracer.disableShadows = disableShadows;
  self->pathtracer.enableCaustics = enableCaustics;
  self->pathtracer.causticsClampThreshold = causticsClampThreshold;
  self->pathtracer.nativeMaterials = nativeMaterials;
  for (uniform int i = 0; i < 3; i++)
    self->pathtracer.luminance[i] = luminance[i];
  self->pathtracer.pixelSamples = pixelSamples;
  self->pathtracer.maxBounces = maxBounces;
  self->pathtracer.adaptiveThreshold = adaptiveThreshold;
  self->pathtracer.minSamplesBeforeAdaptive = minSamplesBeforeAdaptive;
  // the footprint of one of 'differentialSamples' samples (Typhoon's
  // samplesToConvergence); Typhoon scales the differentials only above 1
  // sample, which 1 / sqrt(1) leaves as they are
  self->pathtracer.differentialScale =
      1.f / sqrt((uniform float)max(differentialSamples, 1));
  self->pathtracer.ambientLight = (const uniform Light *uniform)ambientLight;
  // ANARI has no scene frame (Typhoon's default seed): frame 0
  self->pathtracer.frameSeed = Sampling_resolveFrameSeed(randomNumberSeed, 0.f);
}

export uniform ulong sizeof_PathTracerFrameState()
{
  return sizeof(uniform PathTracerFrameState);
}

// 'accum' (float4 per pixel), 'varianceM2' (3 floats), 'converged' (1 byte)
// are all NULL, or all per-pixel buffers; 'albedoAccum' and 'normalAccum' (3
// floats per pixel) are NULL for channels the frame doesn't have
export void PathTracerFrameState_setBuffers(void *uniform _self,
    void *uniform accum,
    float *uniform varianceM2,
    void *uniform converged,
    float *uniform albedoAccum,
    float *uniform normalAccum)
{
  uniform PathTracerFrameState *uniform self =
      (uniform PathTracerFrameState * uniform) _self;
  self->accum = (uniform float4 * uniform) accum;
  self->varianceM2 = varianceM2;
  self->converged = (uniform uint8 * uniform) converged;
  self->albedoAccum = albedoAccum;
  self->normalAccum = normalAccum;
}

// 'id': renders since the last change; 'firstSample': the samples every
// pixel not yet converged took in them
export void PathTracerFrameState_setAccumulation(
    void *uniform _self, uniform long id, uniform int firstSample)
{
  uniform PathTracerFrameState *uniform self =
      (uniform PathTracerFrameState * uniform) _self;
  self->accumId = cast<uniform float>(id);
  self->firstSample = firstSample;
}

export void PathTracer_renderFrame(void *uniform _frame,
    void *uniform _state,
    uniform int start_x,
    uniform int y)
{
  uniform Frame *uniform frame = object_cast<Frame>(_frame);
  renderFrame(
      frame, (uniform PathTracerFrameState * uniform) _state, start_x, y);
}
