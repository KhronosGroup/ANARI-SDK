// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// The 'preview' renderer's kernels (renderer/preview/Preview.h): the shading
// pass in every 'mode', the albedo and normal channels, and the ambient
// occlusion passes the frame runs before the shading pass when the renderer
// reads the occlusion factor (Frame::flushAndRender())

#include "shading/halcyon_camera.isph"
#include "shading/halcyon_frame.isph"

// A frame's ambient occlusion buffers, one element per pixel, all NULL
// unless the renderer reads the occlusion factor (PreviewFrameState in
// Preview.cpp, which owns them)
struct PreviewFrameState
{
  // The hit buffer: whether the pixel has a first visible surface (also
  // behind a volume), and if so its distance along the primary ray and its
  // world shading normal on the viewer's side of the geometry (packed float3)
  uniform uint8 *uniform hitSurface;
  uniform float *uniform hitDistance;
  uniform float *uniform hitNormal;
  // the occlusion factor from the occlusion pass, then from the last blur
  // pass; the shading pass reads the blurred one
  uniform float *uniform unblurredOcclusion;
  uniform float *uniform occlusion;
  // the occlusion radius of the occlusion and blur passes:
  // 'ambientOcclusionRadius', or one derived from the world's bounds; 0
  // leaves every factor 1
  uniform float occlusionRadius;
};

// Composites 'src' behind 'dst', which has accumulated opacity 'alpha'. As
// in helide, colors are not premultiplied by their own opacity.
inline float3 accumulateValue(float3 dst, float3 src, float alpha)
{
  return dst + (1.f - alpha) * src;
}

inline float accumulateValue(float dst, float src, float alpha)
{
  return dst + (1.f - alpha) * src;
}

// Eye-lit surface color, blending facing ratio falloff (with the shading
// normal) in by the renderer's 'eyeLightBlendRatio'
inline float3 shadeSurface(const uniform Renderer *uniform r,
    const RayHit &rh,
    const HitInfo &hitInfo,
    const uniform float4 invalidMaterialColor)
{
  const float3 N = worldShadingNormal(hitInfo, rh);
  const float falloff = clamp(abs(dot(-rh.ray.dir, N)), 0.f, 1.f);
  const float3 c =
      make_float3(Surface_getColor(rh, hitInfo, invalidMaterialColor));
  return lerp(c, c * falloff, r->preview.eyeLightBlendRatio)
      * r->preview.ambientRadiance;
}

inline float3 boolColor(bool pred)
{
  return pred ? make_float3(0.f, 1.f, 0.f) : make_float3(1.f, 0.f, 0.f);
}

// helide's 4-entry heatmap (blue, red, yellow, white) linearly interpolated at
// 'o' in [0, 1]
inline float3 opacityHeatmapColor(float o)
{
  const float f = clamp(o, 0.f, 1.f) * 3.f;
  if (f < 1.f)
    return lerp(make_float3(0.f, 0.f, 1.f), make_float3(1.f, 0.f, 0.f), f);
  else if (f < 2.f)
    return lerp(
        make_float3(1.f, 0.f, 0.f), make_float3(1.f, 1.f, 0.f), f - 1.f);
  else
    return lerp(
        make_float3(1.f, 1.f, 0.f), make_float3(1.f, 1.f, 1.f), f - 2.f);
}

// Surface opacity as a heatmap color, shaded like helide's 'opacityHeatmap'
// mode but with the world-space shading normal
inline float3 shadeOpacityHeatmap(
    const uniform Renderer *uniform r, const RayHit &rh, const HitInfo &hitInfo)
{
  const float3 N = worldShadingNormal(hitInfo, rh);
  const float falloff = abs(dot(-rh.ray.dir, N));
  const float3 c = opacityHeatmapColor(Surface_getOpacity(rh, hitInfo));
  return (0.8f * c * falloff + 0.2f * c) * r->preview.ambientRadiance;
}

inline float3 geometryAttribute(
    const RayHit &rh, const HitInfo &hitInfo, uniform Attribute attr)
{
  return make_float3(getGeometryAttribute(hitInfo, rh, attr));
}

// Color of a surface hit in the renderer's 'mode', before clamping; normal
// based modes use the world-space normal
inline float3 shadeGeometry(const uniform Renderer *uniform r,
    const RayHit &rh,
    const HitInfo &hitInfo,
    const uniform float4 invalidMaterialColor)
{
  switch (r->preview.mode) {
  case RENDER_MODE_PRIMITIVE_ID:
    return make_float3(makeRandomColor(hitInfo.primId));
  case RENDER_MODE_OBJECT_ID:
    return make_float3(makeRandomColor(hitInfo.objId));
  case RENDER_MODE_INSTANCE_ID:
    return make_float3(makeRandomColor(hitInfo.instId));
  case RENDER_MODE_EMBREE_PRIM_ID:
    return make_float3(makeRandomColor(rh.hit.primID));
  case RENDER_MODE_EMBREE_GEOM_ID:
    return make_float3(makeRandomColor(rh.hit.geomID));
  case RENDER_MODE_EMBREE_INST_ID:
    return make_float3(makeRandomColor(rh.hit.instID));
  case RENDER_MODE_NG:
    return normalize(worldNg(rh, hitInfo));
  case RENDER_MODE_NG_ABS:
    return normalize(abs(worldNg(rh, hitInfo)));
  case RENDER_MODE_NS:
    return worldShadingNormal(hitInfo, rh);
  case RENDER_MODE_NS_ABS:
    return abs(worldShadingNormal(hitInfo, rh));
  case RENDER_MODE_NS_FLIPPED: {
    // green: the interpolated normal as authored; red: negated into Ng's
    // hemisphere; blue: no interpolated normal (Ns is Ng)
    bool interpolated;
    const float3 N = unfacedObjectShadingNormal(hitInfo, rh, interpolated);
    if (!interpolated)
      return make_float3(0.f, 0.f, 1.f);
    return boolColor(!obtuseToNg(hitInfo, rh, N));
  }
  case RENDER_MODE_UVW:
    return make_float3(rh.hit.u, rh.hit.v, 1.f);
  case RENDER_MODE_BACKFACE:
    return boolColor(dot(worldNg(rh, hitInfo), rh.ray.dir) < 0.f);
  case RENDER_MODE_HIT_SURFACE:
    return boolColor(true);
  case RENDER_MODE_GEOMETRY_ATTRIBUTE_0:
    return geometryAttribute(rh, hitInfo, ATTRIBUTE_0);
  case RENDER_MODE_GEOMETRY_ATTRIBUTE_1:
    return geometryAttribute(rh, hitInfo, ATTRIBUTE_1);
  case RENDER_MODE_GEOMETRY_ATTRIBUTE_2:
    return geometryAttribute(rh, hitInfo, ATTRIBUTE_2);
  case RENDER_MODE_GEOMETRY_ATTRIBUTE_3:
    return geometryAttribute(rh, hitInfo, ATTRIBUTE_3);
  case RENDER_MODE_GEOMETRY_COLOR:
    return geometryAttribute(rh, hitInfo, ATTRIBUTE_COLOR);
  case RENDER_MODE_OPACITY_HEATMAP:
    return shadeOpacityHeatmap(r, rh, hitInfo);
  default:
    return shadeSurface(r, rh, hitInfo, invalidMaterialColor);
  }
}

// Ambient occlusion //////////////////////////////////////////////////////////
//
// Three passes run before the shading pass when the frame has occlusion
// buffers: the hit-buffer pass traces each pixel's primary ray and records its
// first visible surface, then the occlusion pass, a screen-space pass,
// estimates each pixel's occlusion factor from the hit buffer with the
// Alchemy/SAO estimator (McGuire et al., "Scalable Ambient Obscurance",
// HPG 2012) in the form the renderer's 'ambientOcclusionEstimator' selects
// (G3D's high-quality code or the paper's) and raises it to the power
// 'ambientOcclusionIntensity', and the blur passes, more screen-space passes,
// remove the noise of its 4x4 rotation pattern ('ambientOcclusionBlurPasses'
// of them, each blurring the previous one's result).

// Taps per pixel, on a spiral out to the occlusion radius
static const uniform int AO_TAPS = 16;
// Turns of the spiral: the SAO paper's choice for 16 taps
static const uniform float AO_SPIRAL_TURNS = 7.f;
// Occlusion bias as a fraction of the occlusion radius: a tap must lie this far
// above the pixel's tangent plane to occlude it, so a plane doesn't occlude
// itself
static const uniform float AO_BIAS = 0.01f;
// Added to a tap's squared distance, as a fraction of the squared occlusion
// radius, so taps very near the pixel don't dominate (SAO's epsilon)
static const uniform float AO_EPSILON = 0.01f;
// 'g3d' estimator: scale of the mean tap occlusion (G3D's 3 / taps,
// renormalised over the taps that land). As in G3D's SAO code, the factor is
// 1 - sqrt(scaled mean), so the square root also shapes its contrast.
static const uniform float AO_G3D_SCALE = 3.f;
// 'sao' estimator: the paper's scale of the tap sum, 5 sigma / (taps r^6),
// with its linear intensity sigma set to the occlusion radius r so the factor
// doesn't depend on the scene's units: 5 / (taps r^5), renormalised over the
// taps that land (the r^5 is folded into each tap)
static const uniform float AO_SAO_SCALE = 5.f;
// Rotation of the spiral per pixel, in 16ths of a turn, keyed by the pixel's
// position modulo 4 (a 4x4 Bayer pattern, so neighbours differ most)
static const uniform int AO_ROTATIONS[16] = {
    0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};

static const uniform float AO_TWO_PI = 6.283185307f;
// Half the side of the blur's square window, which is centred on the pixel
// and covers pixel offsets [-AO_BLUR_RADIUS, AO_BLUR_RADIUS] in x and y
static const uniform int AO_BLUR_RADIUS = 2;
// Spatial weight of a blur offset along one axis, from -AO_BLUR_RADIUS to
// AO_BLUR_RADIUS; a neighbour's is the product of its x and y offsets'. The
// window is one wider than the 4x4 rotation pattern (AO_ROTATIONS), so its
// outer offsets -2 and +2 fall on the same column (row) of the pattern; they
// weigh half each, so every rotation gets total weight 1 and the pattern
// cancels exactly. Changing the pattern's size means changing both constants.
static const uniform float AO_BLUR_WEIGHTS[2 * AO_BLUR_RADIUS + 1] = {
    0.5f, 1.f, 1.f, 1.f, 0.5f};
// Spatial weights of the blur passes after the first, per axis like
// AO_BLUR_WEIGHTS: the B3-spline of the a-trous transform. Pass k (k >= 2)
// multiplies the window's offsets by the stride 2^(k - 1), so each pass
// reaches twice as far as the one before and smooths the bands the first
// leaves where the factor changes quickly (near floor contacts).
static const uniform float AO_ATROUS_WEIGHTS[2 * AO_BLUR_RADIUS + 1] = {
    1.f / 16.f, 1.f / 4.f, 3.f / 8.f, 1.f / 4.f, 1.f / 16.f};

// World-space position of the hit buffer's surface at 'pixel', 'distance'
// along the pixel's primary ray
inline float3 hitBufferPosition(
    const uniform Frame *uniform frame, int2 pixel, float distance)
{
  const float2 screen =
      Camera_screenFromPixel(frame->camera, pixel, frame->invSize);
  const RayHit rh = Camera_makeRay(frame->camera, screen, frame->aspect);
  return rh.ray.org + rh.ray.dir * distance;
}

// Pixels per world unit along the frame's x and y: for a perspective camera at
// unit distance along its view direction, for an orthographic camera at any
// distance; an image region crop magnifies it
inline uniform float2 pixelScale(const uniform Frame *uniform frame)
{
  const uniform Camera *uniform c = frame->camera;
  const uniform float aspect = c->hasAspect ? c->aspect : frame->aspect;
  const uniform float regionWidth = abs(c->region.z - c->region.x);
  const uniform float regionHeight = abs(c->region.w - c->region.y);
  return make_float2(
      frame->size.x / (c->imagePlaneHeight * aspect * regionWidth),
      frame->size.y / (c->imagePlaneHeight * regionHeight));
}

// Occlusion factor of the hit buffer's surface at pixel (x, y), index
// 'pixelIndex', with the renderer's estimator and raised to its intensity: 1
// unless the camera is perspective or orthographic and the occlusion radius
// positive
inline float occlusionAt(const uniform Frame *uniform frame,
    const uniform PreviewFrameState *uniform ao,
    int x,
    int y,
    int pixelIndex)
{
  const uniform Camera *uniform c = frame->camera;
  const uniform Renderer *uniform r = frame->renderer;
  const uniform bool sao =
      r->preview.ambientOcclusionEstimator == AMBIENT_OCCLUSION_ESTIMATOR_SAO;
  const uniform float radius = ao->occlusionRadius;
  if (!(radius > 0.f)
      || (c->type != CAMERA_PERSPECTIVE && c->type != CAMERA_ORTHOGRAPHIC))
    return 1.f;
  const uniform float2 scale = pixelScale(frame);
  // a degenerate camera or region
  if (!(scale.x > 0.f && scale.x < HALCYON_RAY_TFAR && scale.y > 0.f
          && scale.y < HALCYON_RAY_TFAR))
    return 1.f;

#pragma ignore warning(perf)
  const float distance = ao->hitDistance[pixelIndex];
  const float3 P = hitBufferPosition(frame, make_int2(x, y), distance);
  const float3 n = load_float3(ao->hitNormal + 3 * pixelIndex);

  // the occlusion radius in pixels: at the surface's view depth for a
  // perspective camera, the same at every depth for an orthographic one
  float2 radiusPixels = scale * radius;
  if (c->type == CAMERA_PERSPECTIVE) {
    const float viewDepth = dot(P - c->pos, c->dir);
    if (!(viewDepth > 0.f))
      return 1.f;
    radiusPixels = radiusPixels / viewDepth;
  }

#pragma ignore warning(perf)
  const float rotation =
      AO_ROTATIONS[(y & 3) * 4 + (x & 3)] * (AO_TWO_PI / 16.f);
  const float2 center = make_float2(x + 0.5f, y + 0.5f);
  const uniform float invRadius = 1.f / radius;
  const uniform float invRadius2 = invRadius * invRadius;
  const uniform float bias = AO_BIAS * radius;
  const uniform float epsilon = AO_EPSILON * radius * radius;

  float sum = 0.f;
  int landed = 0;
  for (uniform int k = 0; k < AO_TAPS; k++) {
    const uniform float alpha = (k + 0.5f) / AO_TAPS;
    const float angle = alpha * (AO_SPIRAL_TURNS * AO_TWO_PI) + rotation;
    const float2 p =
        center + alpha * radiusPixels * make_float2(cos(angle), sin(angle));
    // taps outside the frame are dropped (NaN too)
    if (!(p.x >= 0.f && p.x < frame->size.x && p.y >= 0.f
            && p.y < frame->size.y))
      continue;
    const int2 tap = make_int2((int)p.x, (int)p.y);
    const int tapIndex = tap.y * frame->size.x + tap.x;
    // so are taps on pixels with no surface
#pragma ignore warning(perf)
    if (!ao->hitSurface[tapIndex])
      continue;
    landed++;

#pragma ignore warning(perf)
    const float3 v =
        hitBufferPosition(frame, tap, ao->hitDistance[tapIndex]) - P;
    const float vv = dot(v, v);
    if (sao) {
      // the paper's tap (r^2 - v.v)^3 max(v.n - b, 0) / (v.v + epsilon), 0
      // outside the radius, over r^5: each factor is taken relative to r so
      // it can't overflow or underflow
      const float vvRelative = vv * invRadius2;
      const float falloff = max(1.f - vvRelative, 0.f);
      sum += falloff * falloff * falloff
          * max((dot(v, n) - bias) * invRadius, 0.f)
          / (vvRelative + AO_EPSILON);
    } else {
      // G3D's falloff, cut to 0 at the radius
      const float falloff = max(1.f - vv * invRadius2, 0.f);
      sum += falloff * max((dot(v, n) - bias) * rsqrt(vv + epsilon), 0.f);
    }
  }

  if (landed == 0)
    return 1.f;
  const float factor = sao
      ? max(1.f - sum * (AO_SAO_SCALE / landed), 0.f)
      : max(1.f - sqrt(sum * (AO_G3D_SCALE / landed)), 0.f);
  // intensity 0 gives 1, also for a factor of 0
  const uniform float intensity = r->preview.ambientOcclusionIntensity;
  return intensity > 0.f ? pow(factor, intensity) : 1.f;
}

// Hit-buffer pass: records the first visible surface of the pixels in the
// SIMD span starting at (start_x, y), whatever volumes lie in front of it; a
// pixel with no first visible surface holds no surface
inline unmasked void renderHitBuffer(const uniform Frame *uniform frame,
    uniform PreviewFrameState *uniform ao,
    uniform int start_x,
    uniform int y)
{
  const int x = start_x + programIndex;
  if (x >= frame->size.x)
    return;

  const float2 screen =
      Camera_screenFromPixel(frame->camera, make_int2(x, y), frame->invSize);
  RayHit rh = Camera_makeRay(frame->camera, screen, frame->aspect);
  HitInfo surfaceInfo;
  float depth;
  HitInfo ids;
  const bool surface =
      traceFirstVisibleSurface(frame->world, rh, surfaceInfo, depth, ids);

  const int pixelIndex = y * frame->size.x + x;
#pragma ignore warning(perf)
  ao->hitSurface[pixelIndex] = surface ? 1 : 0;
  if (surface) {
    // the shading normal (in Ng's hemisphere), on the viewer's side of the
    // geometry: flipped where it points away from the geometric normal
    // facing the viewer
    float3 Ng = worldNg(rh, surfaceInfo);
    if (dot(Ng, rh.ray.dir) > 0.f)
      Ng = -Ng;
    float3 n = worldShadingNormal(surfaceInfo, rh);
    if (dot(n, Ng) < 0.f)
      n = -n;
#pragma ignore warning(perf)
    ao->hitDistance[pixelIndex] = rh.ray.tfar;
    store_float3(ao->hitNormal + 3 * pixelIndex, n);
  }
}

// Occlusion pass: the unblurred occlusion factor of the pixels in the SIMD
// span starting at (start_x, y), 1 where there is no surface; reads the whole
// hit buffer
inline unmasked void renderOcclusion(const uniform Frame *uniform frame,
    uniform PreviewFrameState *uniform ao,
    uniform int start_x,
    uniform int y)
{
  const int x = start_x + programIndex;
  if (x >= frame->size.x)
    return;

  const int pixelIndex = y * frame->size.x + x;
#pragma ignore warning(perf)
  const bool surface = ao->hitSurface[pixelIndex] != 0;
  const float factor = surface ? occlusionAt(frame, ao, x, y, pixelIndex) : 1.f;
#pragma ignore warning(perf)
  ao->unblurredOcclusion[pixelIndex] = factor;
}

// Blurred occlusion factor of the hit buffer's surface at pixel (x, y), index
// 'pixelIndex', in blur pass 'pass' (1 for the first) over 'source' (the
// unblurred factor for the first pass, the previous pass's result after it):
// the weighted mean of the factors in the 5x5 window centred on the pixel,
// whose offsets are multiplied by the pass's stride (1 for the first pass,
// 2^(pass - 1) after it), over the pixels with a surface whose hit distance
// is within the occlusion radius of the pixel's. A neighbour's weight is its
// spatial weight (AO_BLUR_WEIGHTS for the first pass, AO_ATROUS_WEIGHTS
// after it) times a depth weight that falls from 1 (the pixel's own
// distance) to 0 at the radius as 1 - (difference / radius)^2, so occlusion
// doesn't bleed across silhouettes and the pixel itself always counts.
inline float blurredOcclusionAt(const uniform Frame *uniform frame,
    const uniform PreviewFrameState *uniform ao,
    const uniform float *uniform source,
    uniform int pass,
    int x,
    int y,
    int pixelIndex)
{
  const uniform float radius = ao->occlusionRadius;
  // the occlusion pass leaves every factor 1
  if (!(radius > 0.f)) {
#pragma ignore warning(perf)
    return source[pixelIndex];
  }

  const uniform float *uniform weights =
      pass == 1 ? AO_BLUR_WEIGHTS : AO_ATROUS_WEIGHTS;
  const uniform int stride = 1 << (pass - 1);

#pragma ignore warning(perf)
  const float distance = ao->hitDistance[pixelIndex];
  const uniform float invRadius = 1.f / radius;

  float sum = 0.f;
  float weightSum = 0.f;
  for (uniform int dy = -AO_BLUR_RADIUS; dy <= AO_BLUR_RADIUS; dy++) {
    const int ny = y + dy * stride;
    if (ny < 0 || ny >= frame->size.y)
      continue;
    const uniform float weightY = weights[dy + AO_BLUR_RADIUS];
    for (uniform int dx = -AO_BLUR_RADIUS; dx <= AO_BLUR_RADIUS; dx++) {
      const int nx = x + dx * stride;
      if (nx < 0 || nx >= frame->size.x)
        continue;
      const int neighbourIndex = ny * frame->size.x + nx;
#pragma ignore warning(perf)
      if (!ao->hitSurface[neighbourIndex])
        continue;
#pragma ignore warning(perf)
      const float difference =
          abs(ao->hitDistance[neighbourIndex] - distance) * invRadius;
      if (!(difference < 1.f))
        continue;
      const float weight = weightY * weights[dx + AO_BLUR_RADIUS]
          * (1.f - difference * difference);
#pragma ignore warning(perf)
      sum += weight * source[neighbourIndex];
      weightSum += weight;
    }
  }
  // the pixel itself has a positive weight
  return sum / weightSum;
}

// Blur pass 'pass' (1 for the first; see blurredOcclusionAt()): the blurred
// occlusion factor of the pixels in the SIMD span starting at (start_x, y)
// into 'target', 1 where there is no surface; reads the whole of 'source'
// and the hit buffer
inline unmasked void renderOcclusionBlur(const uniform Frame *uniform frame,
    const uniform PreviewFrameState *uniform ao,
    uniform int start_x,
    uniform int y,
    uniform int pass,
    const uniform float *uniform source,
    uniform float *uniform target)
{
  const int x = start_x + programIndex;
  if (x >= frame->size.x)
    return;

  const int pixelIndex = y * frame->size.x + x;
#pragma ignore warning(perf)
  const bool surface = ao->hitSurface[pixelIndex] != 0;
  const float factor = surface
      ? blurredOcclusionAt(frame, ao, source, pass, x, y, pixelIndex)
      : 1.f;
#pragma ignore warning(perf)
  target[pixelIndex] = factor;
}

// The blurred occlusion factor of pixel 'i' from the last blur pass, 1
// (unoccluded) when the frame has no occlusion buffers
inline float occlusionFactor(const uniform PreviewFrameState *uniform ao, int i)
{
  if (!ao->occlusion)
    return 1.f;
#pragma ignore warning(perf)
  return ao->occlusion[i];
}

// The unblurred occlusion factor of pixel 'i' from the occlusion pass, 1
// (unoccluded) when the frame has no occlusion buffers
inline float unblurredOcclusionFactor(
    const uniform PreviewFrameState *uniform ao, int i)
{
  if (!ao->unblurredOcclusion)
    return 1.f;
#pragma ignore warning(perf)
  return ao->unblurredOcclusion[i];
}

// Color and opacity of a primary ray through 'screen' in the default mode,
// traced from 'rh', composited front to back as in helide: the volume in front
// of the surface hit (ray marched), then the surface, then what a miss shows
// (missColor(): the visible hdri lights, else the renderer's background).
// Unlike helide, where every surface is opaque, a surface is
// weighted by its opacity (Surface_getCompositeOpacity()), and the ray goes on
// through surfaces until it misses or the opacity reaches 0.99, marching the
// volume in front of each. Colors are not premultiplied (see
// accumulateValue()). 'depth' and 'ids' come from the visible hit: the
// first volume entered or surface with an opacity above 0. The first visible
// surface, also behind a volume, is darkened by 'occlusion' (the pixel's
// occlusion factor) before the volume in front of it is composited over it;
// nothing else is.
inline float4 renderDefault(const uniform Frame *uniform frame,
    float2 screen,
    float occlusion,
    RayHit &rh,
    float &depth,
    HitInfo &ids)
{
  const uniform Renderer *uniform r = frame->renderer;
  float3 color = make_float3(0.f);
  float opacity = 0.f;
  bool visible = false;
  bool surfaceSeen = false;
  depth = HALCYON_RAY_TFAR;

  while (true) {
    rtcIntersectV(frame->world->scene, (varying RTCRayHit * uniform) & rh);
    const HitInfo hitInfo = getHitInfo(frame->world, rh);
    // the miss ids unless a visible hit follows
    if (!visible)
      ids = hitInfo;

    // volumes are intersected between the previous surface hit and this one
    const VolumeHit vh = intersectVolumes(frame->world, rh);
    if (foundHit(vh)) {
      float3 volumeColor;
      float volumeOpacity;
      renderVolumeHit(
          vh, r->preview.invVolumeSamplingRate, volumeColor, volumeOpacity);
      color =
          accumulateValue(color, min(volumeColor, make_float3(1.f)), opacity);
      opacity =
          accumulateValue(opacity, clamp(volumeOpacity, 0.f, 1.f), opacity);
      if (!visible) {
        visible = true;
        depth = vh.tLower;
        setVolumeIds(ids, vh);
      }
    }

    if (!foundHit(hitInfo))
      break;

    const float surfaceOpacity = Surface_getCompositeOpacity(rh, hitInfo);
    if (surfaceOpacity > 0.f) {
      float3 surfaceColor =
          min(shadeSurface(r, rh, hitInfo, frame->invalidMaterialColor),
              make_float3(1.f));
      if (!visible) {
        visible = true;
        depth = rh.ray.tfar;
      }
      if (!surfaceSeen) {
        surfaceSeen = true;
        surfaceColor = surfaceColor * occlusion;
      }
      color = accumulateValue(color, surfaceColor * surfaceOpacity, opacity);
      opacity = accumulateValue(opacity, surfaceOpacity, opacity);
    }

    if (opacity >= 0.99f)
      break;
    continuePastHit(rh);
  }

  const float4 bg = missColor(r, frame->world, screen, rh.ray.dir);
  color = accumulateValue(color, make_float3(bg), opacity);
  opacity = accumulateValue(opacity, bg.w, opacity);

  return make_float4(color, opacity);
}

// Color and opacity of a primary ray through 'screen' in the
// 'ambientOcclusion' and 'ambientOcclusion.unblurred' modes, traced from 'rh':
// 'occlusion' (the pixel's blurred or unblurred occlusion factor) as opaque
// gray where there is a first visible surface, whatever volumes lie in front
// of it (volumes aren't drawn), else what a miss shows (missColor(): the
// visible hdri lights, else the background). 'depth' and
// 'ids' come from the visible hit, as in renderDefault().
inline float4 renderAmbientOcclusion(const uniform Frame *uniform frame,
    float2 screen,
    float occlusion,
    RayHit &rh,
    float &depth,
    HitInfo &ids)
{
  HitInfo surface;
  if (traceFirstVisibleSurface(frame->world, rh, surface, depth, ids))
    return make_float4(make_float3(occlusion), 1.f);
  return missColor(frame->renderer, frame->world, screen, rh.ray.dir);
}

// Color and opacity of a primary ray through 'screen' in a mode other than
// the default and the two ambient occlusion modes: the surface hit, opaque
// whatever its material's opacity (only 'opacityHeatmap' shows opacity), then
// what a miss shows (missColor(): the visible hdri lights, else the
// renderer's background, as in helide). Volumes don't render in these modes;
// 'hitVolume' only tests 'vh'.
inline float4 shadeRay(const uniform Renderer *uniform r,
    const uniform World *uniform world,
    float2 screen,
    const RayHit &rh,
    const HitInfo &hitInfo,
    const VolumeHit &vh,
    const uniform float4 invalidMaterialColor)
{
  const float4 bg = missColor(r, world, screen, rh.ray.dir);

  float3 color = make_float3(0.f);
  float opacity = 0.f;

  if (foundHit(hitInfo)) {
    // helide's 'hitVolume' only colors surfaces: green behind a volume, else
    // the background color
    const float3 surfaceColor = r->preview.mode == RENDER_MODE_HIT_VOLUME
        ? (foundHit(vh) ? boolColor(true) : make_float3(bg))
        : shadeGeometry(r, rh, hitInfo, invalidMaterialColor);
    const float3 geometryColor = min(surfaceColor, make_float3(1.f));
    color = accumulateValue(color, geometryColor, opacity);
    opacity = accumulateValue(opacity, 1.f, opacity);
  }

  color = accumulateValue(color, make_float3(bg), opacity);
  opacity = accumulateValue(opacity, bg.w, opacity);

  return make_float4(color, opacity);
}

inline unmasked void renderFrame(uniform Frame *uniform frame,
    const uniform PreviewFrameState *uniform ao,
    uniform int start_x,
    uniform int y)
{
  const int x = start_x + programIndex;
  if (x >= frame->size.x)
    return;

  // Create primary ray //

  const float2 screen =
      Camera_screenFromPixel(frame->camera, make_int2(x, y), frame->invSize);
  RayHit rh = Camera_makeRay(frame->camera, screen, frame->aspect);

  // The ray direction as a color, without tracing (helide's 'testFrame')
  if (frame->renderer->preview.mode == RENDER_MODE_TEST_FRAME) {
    const float4 color = make_float4(rh.ray.dir * 0.5f + 0.5f, 1.f);
    // an untraced ray has no hit (makeRay() invalidates its ids), so this
    // gives the miss ids
    const HitInfo miss = getHitInfo(frame->world, rh);
    writeOutputs(frame, y * frame->size.x + x, color, 0.f, miss);
    return;
  }

  // Shade //

  const int pixelIndex = y * frame->size.x + x;

  if (frame->renderer->preview.mode == RENDER_MODE_DEFAULT) {
    float depth;
    HitInfo ids;
    const float4 color = renderDefault(
        frame, screen, occlusionFactor(ao, pixelIndex), rh, depth, ids);
    writeOutputs(frame, pixelIndex, color, depth, ids);
    return;
  }

  if (frame->renderer->preview.mode == RENDER_MODE_AMBIENT_OCCLUSION
      || frame->renderer->preview.mode
          == RENDER_MODE_AMBIENT_OCCLUSION_UNBLURRED) {
    const float occlusion =
        frame->renderer->preview.mode == RENDER_MODE_AMBIENT_OCCLUSION
        ? occlusionFactor(ao, pixelIndex)
        : unblurredOcclusionFactor(ao, pixelIndex);
    float depth;
    HitInfo ids;
    const float4 color =
        renderAmbientOcclusion(frame, screen, occlusion, rh, depth, ids);
    writeOutputs(frame, pixelIndex, color, depth, ids);
    return;
  }

  rtcIntersectV(frame->world->scene, (varying RTCRayHit * uniform) & rh);

  HitInfo hitInfo = getHitInfo(frame->world, rh);

  // volumes are intersected up to the surface hit
  const VolumeHit vh = intersectVolumes(frame->world, rh);
  if (foundHit(vh))
    setVolumeIds(hitInfo, vh);

  const float4 color = shadeRay(frame->renderer,
      frame->world,
      screen,
      rh,
      hitInfo,
      vh,
      frame->invalidMaterialColor);
  const float depth = foundHit(vh) ? min(rh.ray.tfar, vh.tLower) : rh.ray.tfar;

  // Write results //

  writeOutputs(frame, pixelIndex, color, depth, hitInfo);
}

// The albedo and normal channels of pixel span (start_x, y), whatever the
// mode: the surface colour (Surface_getColor(), the invalid material colour
// without a valid material) and the world shading normal (as authored, not
// faced to the viewer) of the pixel center's first visible surface, whatever
// volumes lie in front of it; 0 in both where there is none
inline unmasked void renderAlbedoAndNormal(
    uniform Frame *uniform frame, uniform int start_x, uniform int y)
{
  const int x = start_x + programIndex;
  if (x >= frame->size.x)
    return;

  const float2 screen =
      Camera_screenFromPixel(frame->camera, make_int2(x, y), frame->invSize);
  RayHit rh = Camera_makeRay(frame->camera, screen, frame->aspect);
  HitInfo surface;
  float depth;
  HitInfo ids;
  float3 albedo = make_float3(0.f);
  float3 normal = make_float3(0.f);
  if (traceFirstVisibleSurface(frame->world, rh, surface, depth, ids)) {
    albedo =
        make_float3(Surface_getColor(rh, surface, frame->invalidMaterialColor));
    normal = worldShadingNormal(surface, rh);
  }
  const int pixelIndex = y * frame->size.x + x;
  writeAlbedo(frame, pixelIndex, albedo);
  writeNormal(frame, pixelIndex, normal);
}

// Exported interface /////////////////////////////////////////////////////////

export void Preview_setParameters(void *uniform _self,
    uniform float ambientRadiance,
    uniform float eyeLightBlendRatio,
    uniform float invVolumeSamplingRate,
    uniform RenderMode mode,
    uniform AmbientOcclusionEstimator ambientOcclusionEstimator,
    uniform float ambientOcclusionIntensity)
{
  uniform Renderer *uniform self = object_cast<Renderer>(_self);
  self->preview.ambientRadiance = ambientRadiance;
  self->preview.eyeLightBlendRatio = eyeLightBlendRatio;
  self->preview.invVolumeSamplingRate = invVolumeSamplingRate;
  self->preview.mode = mode;
  self->preview.ambientOcclusionEstimator = ambientOcclusionEstimator;
  self->preview.ambientOcclusionIntensity = ambientOcclusionIntensity;
}

export uniform ulong sizeof_PreviewFrameState()
{
  return sizeof(uniform PreviewFrameState);
}

// 'hitSurface' (1 byte per pixel), 'hitDistance' (1 float), 'hitNormal' (3
// floats), 'unblurredOcclusion' and 'occlusion' (1 float each) are all NULL,
// or all per-pixel buffers
export void PreviewFrameState_setBuffers(void *uniform _self,
    void *uniform hitSurface,
    float *uniform hitDistance,
    float *uniform hitNormal,
    float *uniform unblurredOcclusion,
    float *uniform occlusion)
{
  uniform PreviewFrameState *uniform self =
      (uniform PreviewFrameState * uniform) _self;
  self->hitSurface = (uniform uint8 * uniform) hitSurface;
  self->hitDistance = hitDistance;
  self->hitNormal = hitNormal;
  self->unblurredOcclusion = unblurredOcclusion;
  self->occlusion = occlusion;
}

// The occlusion radius of the occlusion and blur passes (see
// PreviewFrameState::occlusionRadius() in Preview.cpp)
export void PreviewFrameState_setOcclusionRadius(
    void *uniform _self, uniform float radius)
{
  uniform PreviewFrameState *uniform self =
      (uniform PreviewFrameState * uniform) _self;
  self->occlusionRadius = radius;
}

export void Preview_renderHitBuffer(void *uniform _frame,
    void *uniform _state,
    uniform int start_x,
    uniform int y)
{
  renderHitBuffer(object_cast<Frame>(_frame),
      (uniform PreviewFrameState * uniform) _state,
      start_x,
      y);
}

export void Preview_renderOcclusion(void *uniform _frame,
    void *uniform _state,
    uniform int start_x,
    uniform int y)
{
  renderOcclusion(object_cast<Frame>(_frame),
      (uniform PreviewFrameState * uniform) _state,
      start_x,
      y);
}

// Blur pass 'pass' (1 for the first) from 'source' into 'target', per-pixel
// buffers distinct from each other: the first pass blurs the unblurred
// factor, each later one the previous pass's result, and the last writes the
// state's 'occlusion'
export void Preview_renderOcclusionBlur(void *uniform _frame,
    void *uniform _state,
    uniform int start_x,
    uniform int y,
    uniform int pass,
    const uniform float *uniform source,
    uniform float *uniform target)
{
  renderOcclusionBlur(object_cast<Frame>(_frame),
      (uniform PreviewFrameState * uniform) _state,
      start_x,
      y,
      pass,
      source,
      target);
}

// The shading pass, and the albedo and normal channels where the frame has
// them; 'state' is the frame's PreviewFrameState
export void Preview_renderFrame(void *uniform _frame,
    void *uniform _state,
    uniform int start_x,
    uniform int y)
{
  uniform Frame *uniform frame = object_cast<Frame>(_frame);
  renderFrame(frame, (uniform PreviewFrameState * uniform) _state, start_x, y);
  if (frame->albedo || frame->normal)
    renderAlbedoAndNormal(frame, start_x, y);
}
