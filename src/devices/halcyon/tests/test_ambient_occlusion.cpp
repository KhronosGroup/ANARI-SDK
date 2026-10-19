// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Checks ambient occlusion through the device: the 'preview' renderer's
// 'ambientOcclusion', 'ambientOcclusionRadius', 'ambientOcclusionEstimator',
// 'ambientOcclusionIntensity' and 'ambientOcclusionBlurPasses' parameters as
// introspection reports them; the
// 'ambientOcclusion' mode, which shows the occlusion factor as gray on the
// first visible surface (also behind a volume) and the background elsewhere;
// and the occlusion factor itself. The 'ambientOcclusion.unblurred' mode shows
// the factor before the blur.
//
// The layers scene's quadrants, seen by an orthographic camera, hold
// different visible hits: an opaque surface, one behind a mask cut-out,
// a blend surface over the background, a volume in front of a surface, and
// nothing. Its first visible surfaces lie in two planes facing the camera, 1
// apart, so with a smaller radius (also the one derived from its bounds) the
// factor is 1 everywhere, and a factor of 1 leaves the default mode's image
// as it is with 'ambientOcclusion' off. With a radius that reaches the layers
// in front of it, the surface behind the volume is occluded in both modes.
//
// The room scene (a floor, a wall standing on it and a sphere resting on it,
// seen by a perspective camera) and a lone plane facing the camera check the
// factor with an explicit radius: darker in the crease and under the sphere
// than on open floor, near 1 on the plane, the same in every render, and
// multiplying the default mode's color. The blur pass shows as a factor that
// varies less between neighbouring floor pixels than the unblurred one, as
// the blur of the unblurred factor over a window centred on each pixel, and
// as no halo: seen from behind the sphere, the open floor just outside its
// dark lower silhouette stays unoccluded; and a frame's earlier renders don't
// change the factor.
// A fully transparent volume whose box encloses the crease, or a mask
// cut-out in front of the whole room, leaves the factor unchanged; a blend
// layer just above the floor is the one occluded in the crease, and the floor
// seen through it isn't darkened. Both estimators ('g3d', the default, and
// 'sao') pass the room's probe, blur and lone-plane checks, and give the same
// factor when the room and the radius are scaled together; an unknown
// estimator warns and renders like 'g3d'. 'ambientOcclusionIntensity' raises
// the unblurred factor to its power, before the blur, and changing either
// parameter on an existing frame renders again.
//
// With more than one 'ambientOcclusionBlurPasses', the 'ambientOcclusion' mode
// shows the multi-pass (a-trous) blur of the unblurred factor, which the
// passes leave unchanged, and the default mode multiplies by it; values
// outside [1, 5] clamp silently. A box added to the room, seen from a low
// camera, shows the bands a single blur leaves on its face near the floor
// and that 3 passes remove; the probe and no-halo checks hold at 3 passes
// too, and the frame allocates its intermediate blur buffer only while it
// runs more than one pass.
//
// An 'ambientOcclusionRadius' of 0 derives the radius from the world's
// bounds: the room's factor is the one that radius gives when set, and
// follows the bounds when they change; a world with nothing in it renders
// without errors. An orthographic view of the room darkens the same probes.
// The hit buffer's normal is the first visible surface's own, also behind a
// volume and a cut-out in another instance, and it is the shading normal on
// the viewer's side of the geometry: a plane whose normals tilt off it
// darkens.
//
// The passes run only where their result is read: with 'ambientOcclusion' on,
// modes that don't read the factor, worlds without surfaces and intensity 0
// allocate no occlusion buffers (the DEBUG messages the frame reports when it
// allocates and frees them show this) and render the image they render with
// it off; changing the mode, the intensity or the world's contents on an
// existing frame starts and stops the passes.

// anari
#define ANARI_LINALG_DEFINITIONS
#include <anari/anari_cpp/ext/linalg.h>
#include <anari/anari_cpp.hpp>
// std
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace {

using namespace anari::math;

int g_errors = 0;
int g_warnings = 0;
// every warning's message
std::vector<std::string> g_warningMessages;
// DEBUG messages reporting that a frame allocated or freed its ambient
// occlusion buffers, which it has exactly while it runs the occlusion passes
int g_occlusionBuffersAllocated = 0;
int g_occlusionBuffersFreed = 0;
// DEBUG messages reporting that a frame allocated or freed its intermediate
// blur buffer, which it has exactly while it runs more than one blur pass
int g_blurBufferAllocated = 0;
int g_blurBufferFreed = 0;

void statusFunc(const void *,
    ANARIDevice,
    ANARIObject,
    ANARIDataType,
    ANARIStatusSeverity severity,
    ANARIStatusCode,
    const char *message)
{
  if (severity == ANARI_SEVERITY_FATAL_ERROR
      || severity == ANARI_SEVERITY_ERROR) {
    std::fprintf(stderr, "[ANARI ERROR] %s\n", message);
    g_errors++;
  } else if (severity == ANARI_SEVERITY_WARNING) {
    std::fprintf(stderr, "[ANARI WARNING] %s\n", message);
    g_warnings++;
    g_warningMessages.push_back(message);
  } else if (severity == ANARI_SEVERITY_DEBUG) {
    if (std::strstr(message, "allocating ambient occlusion buffers"))
      g_occlusionBuffersAllocated++;
    else if (std::strstr(message, "freeing ambient occlusion buffers"))
      g_occlusionBuffersFreed++;
    else if (std::strstr(
                 message, "allocating ambient occlusion intermediate blur"))
      g_blurBufferAllocated++;
    else if (std::strstr(
                 message, "freeing ambient occlusion intermediate blur"))
      g_blurBufferFreed++;
  }
}

constexpr uint2 IMAGE_SIZE = {32, 32};
// the camera looks down -z from this z; a layer at z is at depth CAMERA_Z - z
constexpr float CAMERA_Z = 5.f;
const float4 BACKGROUND = {0.1f, 0.2f, 0.3f, 1.f};
const float3 GRAY = {0.5f, 0.5f, 0.5f};
const uint32_t MISS_ID = ~0u;
// the depth of a missed ray (FLT_MAX)
const float MISS_DEPTH = 3.402823466e+38f;

// A matte quad over [x0, x1] x [y0, y1] at 'z'
struct Layer
{
  float2 lo;
  float2 hi;
  float z;
  const char *alphaMode;
  float opacity;
  uint32_t id;
};

anari::Surface makeSurface(anari::Device d, const Layer &l)
{
  const float3 positions[] = {{l.lo.x, l.lo.y, l.z},
      {l.hi.x, l.lo.y, l.z},
      {l.hi.x, l.hi.y, l.z},
      {l.lo.x, l.hi.y, l.z}};
  auto geom = anari::newObject<anari::Geometry>(d, "quad");
  anari::setParameterArray1D(d, geom, "vertex.position", positions, 4);
  anari::commitParameters(d, geom);

  auto mat = anari::newObject<anari::Material>(d, "matte");
  anari::setParameter(d, mat, "color", GRAY);
  anari::setParameter(d, mat, "alphaMode", l.alphaMode);
  anari::setParameter(d, mat, "opacity", l.opacity);
  anari::commitParameters(d, mat);

  auto surface = anari::newObject<anari::Surface>(d);
  anari::setAndReleaseParameter(d, surface, "geometry", geom);
  anari::setAndReleaseParameter(d, surface, "material", mat);
  anari::setParameter(d, surface, "id", l.id);
  anari::commitParameters(d, surface);
  return surface;
}

// A constant green volume of uniform 'opacity' over the box [lo, hi]
anari::Volume makeVolume(
    anari::Device d, float3 lo, float3 hi, float opacity, uint32_t id)
{
  const float voxels[8] = {1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f};
  auto field = anari::newObject<anari::SpatialField>(d, "structuredRegular");
  anari::setParameterArray3D(d, field, "data", voxels, 2, 2, 2);
  anari::setParameter(d, field, "origin", lo);
  anari::setParameter(d, field, "spacing", hi - lo);
  anari::commitParameters(d, field);

  auto volume = anari::newObject<anari::Volume>(d, "transferFunction1D");
  anari::setAndReleaseParameter(d, volume, "value", field);
  anari::setParameter(d, volume, "color", float3(0.f, 1.f, 0.f));
  anari::setParameter(d, volume, "opacity", opacity);
  anari::setParameter(d, volume, "id", id);
  anari::commitParameters(d, volume);
  return volume;
}

// A constant half-opaque green volume over [0, 1]^2 x [z0, z1]
anari::Volume makeVolume(anari::Device d, float z0, float z1, uint32_t id)
{
  return makeVolume(d, float3(0.f, 0.f, z0), float3(1.f, 1.f, z1), 0.5f, id);
}

const uint32_t VOLUME_ID = 7;

// Quadrants of the [-1, 1]^2 view: lower left an opaque surface; upper left
// the same surface behind a mask cut-out; lower right a blend surface over
// the background on its left half and nothing on its right half; upper right
// a volume in front of an opaque surface
anari::World makeWorld(anari::Device d)
{
  const std::vector<Layer> layers = {
      {{-1.f, -1.f}, {0.f, 1.f}, 0.f, "opaque", 1.f, 1},
      {{-1.f, 0.f}, {0.f, 1.f}, 1.f, "mask", 0.3f, 2},
      {{0.f, -1.f}, {0.5f, 0.f}, 0.f, "blend", 0.25f, 3},
      {{0.f, 0.f}, {1.f, 1.f}, -1.f, "opaque", 1.f, 4},
  };
  std::vector<anari::Surface> surfaces;
  for (const auto &l : layers)
    surfaces.push_back(makeSurface(d, l));
  auto volume = makeVolume(d, 0.f, 1.f, VOLUME_ID);

  auto world = anari::newObject<anari::World>(d);
  anari::setParameterArray1D(
      d, world, "surface", surfaces.data(), surfaces.size());
  anari::setParameterArray1D(d, world, "volume", &volume, 1);
  anari::commitParameters(d, world);
  for (auto s : surfaces)
    anari::release(d, s);
  anari::release(d, volume);
  return world;
}

// Orthographic camera looking down -z at the [-1, 1]^2 square
anari::Camera makeCamera(anari::Device d)
{
  auto camera = anari::newObject<anari::Camera>(d, "orthographic");
  anari::setParameter(d, camera, "position", float3(0.f, 0.f, CAMERA_Z));
  anari::setParameter(d, camera, "direction", float3(0.f, 0.f, -1.f));
  anari::setParameter(d, camera, "up", float3(0.f, 1.f, 0.f));
  anari::setParameter(d, camera, "height", 2.f);
  anari::setParameter(d, camera, "aspect", 1.f);
  anari::commitParameters(d, camera);
  return camera;
}

// Every channel of a rendered frame
struct Image
{
  uint2 size;
  std::vector<float4> color;
  std::vector<float> depth;
  std::vector<uint32_t> objectId;

  size_t at(uint2 px) const
  {
    return size_t(px.y) * size.x + px.x;
  }
};

// Renders 'frame', of 'size' with color, depth and objectId channels, and
// reads them back
Image renderImage(anari::Device d, anari::Frame frame, uint2 size)
{
  anari::render(d, frame);
  anari::wait(d, frame);

  const size_t n = size_t(size.x) * size.y;
  Image img;
  img.size = size;
  auto color = anari::map<float4>(d, frame, "channel.color");
  img.color.assign(color.data, color.data + n);
  anari::unmap(d, frame, "channel.color");
  auto depth = anari::map<float>(d, frame, "channel.depth");
  img.depth.assign(depth.data, depth.data + n);
  anari::unmap(d, frame, "channel.depth");
  auto ids = anari::map<uint32_t>(d, frame, "channel.objectId");
  img.objectId.assign(ids.data, ids.data + n);
  anari::unmap(d, frame, "channel.objectId");
  return img;
}

// A frame of 'size' with color, depth and objectId channels rendering
// 'world' seen by 'camera' with a 'preview' renderer that 'setup' configures
template <typename SETUP>
anari::Frame makeFrame(anari::Device d,
    anari::World world,
    anari::Camera camera,
    uint2 size,
    SETUP &&setup)
{
  auto renderer = anari::newObject<anari::Renderer>(d, "preview");
  anari::setParameter(d, renderer, "background", BACKGROUND);
  setup(renderer);
  anari::commitParameters(d, renderer);

  auto frame = anari::newObject<anari::Frame>(d);
  anari::setParameter(d, frame, "size", size);
  anari::setParameter(d, frame, "channel.color", ANARI_FLOAT32_VEC4);
  anari::setParameter(d, frame, "channel.depth", ANARI_FLOAT32);
  anari::setParameter(d, frame, "channel.objectId", ANARI_UINT32);
  anari::setParameter(d, frame, "world", world);
  anari::setParameter(d, frame, "camera", camera);
  anari::setAndReleaseParameter(d, frame, "renderer", renderer);
  anari::commitParameters(d, frame);
  return frame;
}

// Renders 'world' seen by 'camera' in a frame of 'size' with a 'default'
// renderer that 'setup' configures
template <typename SETUP>
Image render(anari::Device d,
    anari::World world,
    anari::Camera camera,
    uint2 size,
    SETUP &&setup)
{
  auto frame = makeFrame(d, world, camera, size, setup);
  Image img = renderImage(d, frame, size);
  anari::release(d, frame);
  return img;
}

// Renders the layers scene 'world' with the orthographic camera
template <typename SETUP>
Image render(anari::Device d, anari::World world, SETUP &&setup)
{
  auto camera = makeCamera(d);
  Image img = render(d, world, camera, IMAGE_SIZE, setup);
  anari::release(d, camera);
  return img;
}

Image renderMode(anari::Device d, anari::World world, const char *mode)
{
  return render(d, world, [&](anari::Renderer r) {
    anari::setParameter(d, r, "mode", mode);
  });
}

// Pixel covering world-space 'pos' on the view
uint2 pixelAt(float2 pos)
{
  return uint2(uint32_t((pos.x + 1.f) / 2.f * IMAGE_SIZE.x),
      uint32_t((pos.y + 1.f) / 2.f * IMAGE_SIZE.y));
}

bool near(float4 a, float4 b)
{
  const float4 e = linalg::abs(a - b);
  return e.x < 1e-5f && e.y < 1e-5f && e.z < 1e-5f && e.w < 1e-5f;
}

// Embree's hit distances differ in the last bits between its ISAs, so a
// surface's depth is compared within a tolerance (a miss's exactly)
bool nearDepth(float a, float b)
{
  return a == b || std::fabs(a - b) < 1e-5f * std::max(1.f, std::fabs(b));
}

bool report(const char *test, bool pass)
{
  std::printf("%s %s\n", pass ? "PASS" : "FAIL", test);
  return pass;
}

// Introspection lists the four parameters on the 'preview' renderer, with
// their defaults, 'g3d' and 'sao' as the estimator's values, and
// 'ambientOcclusion' and 'ambientOcclusion.unblurred' among the 'mode' values
bool testIntrospection(anari::Device d)
{
  bool pass = true;

  auto *enabled = (const bool *)anariGetParameterInfo(d,
      ANARI_RENDERER,
      "preview",
      "ambientOcclusion",
      ANARI_BOOL,
      "default",
      ANARI_BOOL);
  pass &= report(
      "ambientOcclusion declared, default true", enabled && *enabled == true);

  auto *radius = (const float *)anariGetParameterInfo(d,
      ANARI_RENDERER,
      "preview",
      "ambientOcclusionRadius",
      ANARI_FLOAT32,
      "default",
      ANARI_FLOAT32);
  pass &= report(
      "ambientOcclusionRadius declared, default 0", radius && *radius == 0.f);

  auto *estimator = (const char *)anariGetParameterInfo(d,
      ANARI_RENDERER,
      "preview",
      "ambientOcclusionEstimator",
      ANARI_STRING,
      "default",
      ANARI_STRING);
  pass &= report("ambientOcclusionEstimator declared, default g3d",
      estimator && std::strcmp(estimator, "g3d") == 0);
  auto *estimators = (const char *const *)anariGetParameterInfo(d,
      ANARI_RENDERER,
      "preview",
      "ambientOcclusionEstimator",
      ANARI_STRING,
      "value",
      ANARI_STRING_LIST);
  int estimatorCount = 0;
  bool g3dListed = false;
  bool saoListed = false;
  for (int i = 0; estimators && estimators[i]; i++) {
    estimatorCount++;
    g3dListed |= std::strcmp(estimators[i], "g3d") == 0;
    saoListed |= std::strcmp(estimators[i], "sao") == 0;
  }
  pass &= report("ambientOcclusionEstimator values g3d and sao",
      estimatorCount == 2 && g3dListed && saoListed);

  auto *intensity = (const float *)anariGetParameterInfo(d,
      ANARI_RENDERER,
      "preview",
      "ambientOcclusionIntensity",
      ANARI_FLOAT32,
      "default",
      ANARI_FLOAT32);
  pass &= report("ambientOcclusionIntensity declared, default 1",
      intensity && *intensity == 1.f);

  auto blurPassesInfo = [&](const char *info) {
    return (const int32_t *)anariGetParameterInfo(d,
        ANARI_RENDERER,
        "preview",
        "ambientOcclusionBlurPasses",
        ANARI_INT32,
        info,
        ANARI_INT32);
  };
  const int32_t *blurPasses = blurPassesInfo("default");
  const int32_t *minBlurPasses = blurPassesInfo("minimum");
  const int32_t *maxBlurPasses = blurPassesInfo("maximum");
  pass &= report("ambientOcclusionBlurPasses declared, default 1, range [1, 5]",
      blurPasses && *blurPasses == 1 && minBlurPasses && *minBlurPasses == 1
          && maxBlurPasses && *maxBlurPasses == 5);

  auto *modes = (const char *const *)anariGetParameterInfo(d,
      ANARI_RENDERER,
      "preview",
      "mode",
      ANARI_STRING,
      "value",
      ANARI_STRING_LIST);
  for (const char *mode : {"ambientOcclusion", "ambientOcclusion.unblurred"}) {
    bool listed = false;
    for (int i = 0; modes && modes[i]; i++)
      listed |= std::strcmp(modes[i], mode) == 0;
    pass &= report((std::string(mode) + " mode listed").c_str(), listed);
  }

  return pass;
}

struct Probe
{
  const char *name;
  float2 pos;
  bool surface; // whether the ray has a first visible surface
  float depth;
  uint32_t objectId;
};

// On the opaque surface behind the volume
const float2 BEHIND_VOLUME = {0.5f, 0.5f};

const std::vector<Probe> PROBES = {
    {"opaque surface", {-0.5f, -0.5f}, true, CAMERA_Z, 1},
    {"surface behind mask cut-out", {-0.5f, 0.5f}, true, CAMERA_Z, 1},
    {"blend surface", {0.25f, -0.5f}, true, CAMERA_Z, 3},
    {"miss", {0.75f, -0.5f}, false, MISS_DEPTH, MISS_ID},
    {"volume in front of surface",
        BEHIND_VOLUME,
        true,
        CAMERA_Z - 1.f,
        VOLUME_ID},
};

// The mode is white (an occlusion factor of 1) where the ray has a first
// visible surface, also behind a volume, and the background elsewhere; depth
// and objectId come from the visible hit (the volume, if it is in
// front), as in the default mode
bool testMode(anari::Device d, anari::World world)
{
  const Image img = renderMode(d, world, "ambientOcclusion");
  bool pass = true;
  for (const auto &p : PROBES) {
    const size_t i = img.at(pixelAt(p.pos));
    const float4 expected = p.surface ? float4(1.f) : BACKGROUND;
    const float4 c = img.color[i];
    const bool ok = near(c, expected) && nearDepth(img.depth[i], p.depth)
        && img.objectId[i] == p.objectId;
    if (!ok) {
      std::printf("  %s: color (%f, %f, %f, %f), depth %.9g, objectId %u\n",
          p.name,
          c.x,
          c.y,
          c.z,
          c.w,
          img.depth[i],
          img.objectId[i]);
    }
    pass &=
        report((std::string("ambientOcclusion mode, ") + p.name).c_str(), ok);
  }
  return pass;
}

bool sameImage(const Image &a, const Image &b)
{
  return a.color.size() == b.color.size()
      && std::memcmp(
             a.color.data(), b.color.data(), a.color.size() * sizeof(float4))
      == 0
      && a.depth == b.depth && a.objectId == b.objectId;
}

// The layers scene's first visible surfaces lie in the planes z = 0 and
// z = -1, which face the camera: a tap on a pixel's own plane lies in its
// tangent plane and adds nothing, and one on the other plane is at least 1
// away. So with an occlusion radius below 1 nothing occludes anything.
const float LAYERS_UNOCCLUDED_RADIUS = 0.5f;

// Where the occlusion factor is 1, 'ambientOcclusion' leaves the default
// mode's image exactly as it is with 'ambientOcclusion' off: blend layers,
// volumes, the background, depth and ids. Checked with radii at which the
// layers scene's factor is 1 on every surface pixel, as the
// 'ambientOcclusion' mode shows: LAYERS_UNOCCLUDED_RADIUS, the default radius
// (derived from the scene's bounds, about 0.1) and a negative one (clamped to
// 0 silently, so derived too)
bool testFactorOneLeavesDefaultMode(anari::Device d, anari::World world)
{
  const Image off = renderMode(d, world, "default");

  bool pass = true;
  struct Radius
  {
    const char *name;
    std::optional<float> value; // unset for the default radius
  };
  const Radius radii[] = {{"small", LAYERS_UNOCCLUDED_RADIUS},
      {"default", std::nullopt},
      {"negative", -1.f}};
  for (const auto &radius : radii) {
    const int warnings = g_warnings;
    auto setup = [&](const char *mode) {
      return [&, mode](anari::Renderer r) {
        anari::setParameter(d, r, "mode", mode);
        anari::setParameter(d, r, "ambientOcclusion", true);
        if (radius.value)
          anari::setParameter(d, r, "ambientOcclusionRadius", *radius.value);
      };
    };
    const Image factor = render(d, world, setup("ambientOcclusion"));
    const Image on = render(d, world, setup("default"));

    bool factorOne = true;
    for (size_t j = 0; j < factor.color.size(); j++) {
      const float4 c = factor.color[j];
      if (!near(c, BACKGROUND))
        factorOne &= c.x == 1.f && c.y == 1.f && c.z == 1.f && c.w == 1.f;
    }
    const std::string suffix = std::string(", ") + radius.name + " radius";
    pass &= report(("layers scene, factor 1 on every surface" + suffix).c_str(),
        factorOne);
    pass &=
        report(("factor 1 leaves the default mode unchanged" + suffix).c_str(),
            sameImage(off, on) && g_warnings == warnings);
  }
  return pass;
}

// An occlusion radius that reaches from the surface behind the volume to the
// layers in front of it
const float LAYERS_OCCLUSION_RADIUS = 1.5f;

// With an occlusion radius that reaches the layers in front of it, the
// surface behind the volume is occluded: the 'ambientOcclusion' mode shows its
// factor, below 1, and the default mode multiplies its color by it, with the
// volume composited in front as before. The volume is pure green, so the
// color's red and blue hold the surface's contribution alone.
bool testOccludedBehindVolume(anari::Device d, anari::World world)
{
  const Image factor = render(d, world, [&](anari::Renderer r) {
    anari::setParameter(d, r, "mode", "ambientOcclusion");
    anari::setParameter(
        d, r, "ambientOcclusionRadius", LAYERS_OCCLUSION_RADIUS);
  });
  const Image off = renderMode(d, world, "default");
  const Image on = render(d, world, [&](anari::Renderer r) {
    anari::setParameter(d, r, "ambientOcclusion", true);
    anari::setParameter(
        d, r, "ambientOcclusionRadius", LAYERS_OCCLUSION_RADIUS);
  });

  const size_t i = factor.at(pixelAt(BEHIND_VOLUME));
  const float f = factor.color[i].x;
  const float4 onColor = on.color[i];
  const float4 offColor = off.color[i];
  std::printf("  behind volume: factor %f, color (%f, %f, %f, %f) with "
              "occlusion, (%f, %f, %f, %f) without\n",
      f,
      onColor.x,
      onColor.y,
      onColor.z,
      onColor.w,
      offColor.x,
      offColor.y,
      offColor.z,
      offColor.w);

  bool pass = true;
  pass &= report("ambientOcclusion mode, surface behind volume occluded",
      f < 0.95f && near(factor.color[i], float4(f, f, f, 1.f)));
  pass &= report("default mode, surface behind volume multiplied by the factor",
      offColor.x > 0.05f && std::fabs(onColor.x - offColor.x * f) < 1e-5f
          && std::fabs(onColor.z - offColor.z * f) < 1e-5f
          && onColor.w == offColor.w);
  pass &= report("default mode, volume in front of occluded surface",
      onColor.y > onColor.x && nearDepth(on.depth[i], CAMERA_Z - 1.f)
          && on.objectId[i] == VOLUME_ID);
  return pass;
}

// Room scene //////////////////////////////////////////////////////////////////

constexpr uint2 ROOM_SIZE = {96, 96};
const uint32_t FLOOR_ID = 1;
const uint32_t WALL_ID = 2;
const uint32_t SPHERE_ID = 3;
// the sphere rests on the floor
const float3 SPHERE_CENTER = {0.8f, 0.5f, 0.6f};
const float SPHERE_RADIUS = 0.5f;
// the occlusion radius the room tests set
const float ROOM_OCCLUSION_RADIUS = 0.5f;

// A matte quad with 'id' on corners 'p', gray and opaque unless 'color',
// 'alphaMode' and 'opacity' say otherwise
anari::Surface makeQuad(anari::Device d,
    const float3 (&p)[4],
    uint32_t id,
    float3 color = GRAY,
    const char *alphaMode = "opaque",
    float opacity = 1.f,
    const float3 *normals = nullptr)
{
  auto geom = anari::newObject<anari::Geometry>(d, "quad");
  anari::setParameterArray1D(d, geom, "vertex.position", p, 4);
  if (normals)
    anari::setParameterArray1D(d, geom, "vertex.normal", normals, 4);
  anari::commitParameters(d, geom);

  auto mat = anari::newObject<anari::Material>(d, "matte");
  anari::setParameter(d, mat, "color", color);
  anari::setParameter(d, mat, "alphaMode", alphaMode);
  anari::setParameter(d, mat, "opacity", opacity);
  anari::commitParameters(d, mat);

  auto surface = anari::newObject<anari::Surface>(d);
  anari::setAndReleaseParameter(d, surface, "geometry", geom);
  anari::setAndReleaseParameter(d, surface, "material", mat);
  anari::setParameter(d, surface, "id", id);
  anari::commitParameters(d, surface);
  return surface;
}

anari::Surface makeSphere(anari::Device d, float3 center, float radius)
{
  auto geom = anari::newObject<anari::Geometry>(d, "sphere");
  anari::setParameterArray1D(d, geom, "vertex.position", &center, 1);
  anari::setParameter(d, geom, "radius", radius);
  anari::commitParameters(d, geom);

  auto mat = anari::newObject<anari::Material>(d, "matte");
  anari::setParameter(d, mat, "color", GRAY);
  anari::commitParameters(d, mat);

  auto surface = anari::newObject<anari::Surface>(d);
  anari::setAndReleaseParameter(d, surface, "geometry", geom);
  anari::setAndReleaseParameter(d, surface, "material", mat);
  anari::setParameter(d, surface, "id", SPHERE_ID);
  anari::commitParameters(d, surface);
  return surface;
}

// A world of 'surfaces' and, if not null, 'volume'; releases them
anari::World makeWorld(anari::Device d,
    std::vector<anari::Surface> surfaces,
    anari::Volume volume = nullptr)
{
  auto world = anari::newObject<anari::World>(d);
  anari::setParameterArray1D(
      d, world, "surface", surfaces.data(), surfaces.size());
  if (volume) {
    anari::setParameterArray1D(d, world, "volume", &volume, 1);
    anari::release(d, volume);
  }
  anari::commitParameters(d, world);
  for (auto s : surfaces)
    anari::release(d, s);
  return world;
}

// The room's surfaces: the floor (y = 0) and a wall standing on it
// (z = -1), with the sphere resting on the floor in front of the wall; the
// floor reaches z = 'floorEnd'. Every coordinate is multiplied by 'scale'.
std::vector<anari::Surface> roomSurfaces(
    anari::Device d, float floorEnd = 2.5f, float scale = 1.f)
{
  const float s = scale;
  const float3 floor[4] = {s * float3(-2.f, 0.f, -1.f),
      s * float3(2.f, 0.f, -1.f),
      s * float3(2.f, 0.f, floorEnd),
      s * float3(-2.f, 0.f, floorEnd)};
  const float3 wall[4] = {s * float3(-2.f, 0.f, -1.f),
      s * float3(2.f, 0.f, -1.f),
      s * float3(2.f, 2.f, -1.f),
      s * float3(-2.f, 2.f, -1.f)};
  return {makeQuad(d, floor, FLOOR_ID),
      makeQuad(d, wall, WALL_ID),
      makeSphere(d, s * SPHERE_CENTER, s * SPHERE_RADIUS)};
}

anari::World makeRoom(anari::Device d, float floorEnd = 2.5f)
{
  return makeWorld(d, roomSurfaces(d, floorEnd));
}

// A camera's position and axes: a perspective camera's (60 degree 'fovy',
// square image) with makeCamera(), an orthographic one's with
// makeOrthographicCamera()
struct View
{
  float3 position;
  float3 direction;
  float3 up;
};

const View ROOM_VIEW = {{0.f, 1.6f, 3.2f},
    linalg::normalize(float3(0.f, -1.4f, -3.5f)),
    {0.f, 1.f, 0.f}};

anari::Camera makeCamera(anari::Device d, const View &v)
{
  auto camera = anari::newObject<anari::Camera>(d, "perspective");
  anari::setParameter(d, camera, "position", v.position);
  anari::setParameter(d, camera, "direction", v.direction);
  anari::setParameter(d, camera, "up", v.up);
  anari::setParameter(d, camera, "aspect", 1.f);
  anari::commitParameters(d, camera);
  return camera;
}

// World-space 'p' in the camera space of 'v': along the image's horizontal
// and vertical axes and the view direction
float3 viewSpace(const View &v, float3 p)
{
  const float3 u = linalg::normalize(linalg::cross(v.direction, v.up));
  const float3 w = linalg::cross(u, v.direction);
  const float3 rel = p - v.position;
  return float3(
      linalg::dot(rel, u), linalg::dot(rel, w), linalg::dot(rel, v.direction));
}

// Pixel of a 'size' image at image-plane position 's' ((0.5, 0.5) is the
// centre, 1 the image's width and height)
uint2 pixelAt(uint2 size, float2 s)
{
  s += 0.5f;
  return uint2(uint32_t(s.x * size.x), uint32_t(s.y * size.y));
}

// Pixel of a 'size' image that the perspective camera 'v' sees world-space
// 'p' in
uint2 pixelAt(const View &v, uint2 size, float3 p)
{
  const float3 q = viewSpace(v, p);
  const float height = 2.f * std::tan(0.5f * float(M_PI) / 3.f); // fovy 60
  return pixelAt(size, q.xy() / q.z / height);
}

template <typename SETUP>
Image renderView(anari::Device d,
    anari::World world,
    const View &view,
    uint2 size,
    SETUP &&setup)
{
  auto camera = makeCamera(d, view);
  Image img = render(d, world, camera, size, setup);
  anari::release(d, camera);
  return img;
}

// The room in 'mode' (the 'ambientOcclusion' mode by default) with an
// 'ambientOcclusionRadius' of 'radius' and a renderer 'setup' also configures
template <typename SETUP>
Image renderRoomFactor(anari::Device d,
    anari::World room,
    float radius,
    const char *mode,
    SETUP &&setup)
{
  return renderView(d, room, ROOM_VIEW, ROOM_SIZE, [&](anari::Renderer r) {
    anari::setParameter(d, r, "mode", mode);
    anari::setParameter(d, r, "ambientOcclusionRadius", radius);
    setup(r);
  });
}

Image renderRoomFactor(anari::Device d,
    anari::World room,
    float radius = ROOM_OCCLUSION_RADIUS,
    const char *mode = "ambientOcclusion")
{
  return renderRoomFactor(d, room, radius, mode, [](anari::Renderer) {});
}

// 'world' seen by ROOM_VIEW in the default mode, with 'ambientOcclusion' on
// at ROOM_OCCLUSION_RADIUS if 'occlusion', else off
Image renderRoomDefault(anari::Device d, anari::World world, bool occlusion)
{
  return renderView(d, world, ROOM_VIEW, ROOM_SIZE, [&](anari::Renderer r) {
    anari::setParameter(d, r, "ambientOcclusion", occlusion);
    if (occlusion)
      anari::setParameter(
          d, r, "ambientOcclusionRadius", ROOM_OCCLUSION_RADIUS);
  });
}

// The estimators 'ambientOcclusionEstimator' accepts
const char *const ESTIMATORS[] = {"g3d", "sao"};

// A renderer setup selecting 'estimator' and, if given, 'intensity' as
// 'ambientOcclusionIntensity'
auto withEstimator(anari::Device d,
    const char *estimator,
    std::optional<float> intensity = std::nullopt)
{
  return [=](anari::Renderer r) {
    anari::setParameter(d, r, "ambientOcclusionEstimator", estimator);
    if (intensity)
      anari::setParameter(d, r, "ambientOcclusionIntensity", *intensity);
  };
}

// A renderer setup selecting 'passes' as 'ambientOcclusionBlurPasses' and
// 'estimator'
auto withBlurPasses(
    anari::Device d, int32_t passes, const char *estimator = "g3d")
{
  return [=](anari::Renderer r) {
    anari::setParameter(d, r, "ambientOcclusionBlurPasses", passes);
    anari::setParameter(d, r, "ambientOcclusionEstimator", estimator);
  };
}

// 'value' formatted with %g
std::string format(float value)
{
  char s[32];
  std::snprintf(s, sizeof(s), "%g", value);
  return s;
}

// 'test' with ", estimator 'estimator'" appended
std::string withName(const char *test, const char *estimator)
{
  return std::string(test) + ", estimator " + estimator;
}

// 'test' with ", 'passes' blur passes" appended unless 'passes' is 1
std::string withPasses(const std::string &test, int passes)
{
  return passes == 1 ? test
                     : test + ", " + std::to_string(passes) + " blur passes";
}

// Probe pixels on the room's floor
struct FloorProbe
{
  const char *name;
  float3 pos;
};

// 0.1 from the wall
const FloorProbe CREASE = {"crease", {-1.f, 0.f, -0.9f}};
// 0.35 in front of the sphere's contact point, where the sphere is 0.14
// above the floor
const FloorProbe UNDER_SPHERE = {"under sphere", {0.8f, 0.f, 0.95f}};
// farther than the occlusion radius from the wall and the sphere
const FloorProbe OPEN_FLOOR = {"open floor", {-1.2f, 0.f, 1.5f}};

// Index of 'pixel', the pixel of 'probe', checking that it sees the floor
bool floorPixel(
    const Image &img, const FloorProbe &probe, uint2 pixel, size_t &i)
{
  i = img.at(pixel);
  if (img.objectId[i] == FLOOR_ID)
    return true;
  std::printf("  %s: probe pixel sees object %u, not the floor\n",
      probe.name,
      img.objectId[i]);
  return false;
}

// Index of the pixel of 'probe' seen by ROOM_VIEW, checking that it sees the
// floor
bool floorPixel(const Image &img, const FloorProbe &probe, size_t &i)
{
  return floorPixel(img, probe, pixelAt(ROOM_VIEW, img.size, probe.pos), i);
}

// Pixel of 'img' that ROOM_VIEW sees 'p' in, as a function of 'p'
auto roomViewPixel(const Image &img)
{
  return [&img](float3 p) { return pixelAt(ROOM_VIEW, img.size, p); };
}

// The occlusion factors at the crease, under the sphere and on open floor of
// 'img', a render of the room in the 'ambientOcclusion' mode whose camera
// sees world-space 'p' in pixel 'pixel(p)'; false if a probe misses the floor
template <typename PIXEL>
bool roomFactors(const Image &img, PIXEL &&pixel, float3 &factors)
{
  size_t crease, under, open;
  if (!floorPixel(img, CREASE, pixel(CREASE.pos), crease)
      || !floorPixel(img, UNDER_SPHERE, pixel(UNDER_SPHERE.pos), under)
      || !floorPixel(img, OPEN_FLOOR, pixel(OPEN_FLOOR.pos), open))
    return false;
  factors = float3(img.color[crease].x, img.color[under].x, img.color[open].x);
  std::printf("  factor: crease %f, under sphere %f, open floor %f\n",
      factors.x,
      factors.y,
      factors.z);
  return true;
}

// With 'estimator' and 'passes' blur passes, the occlusion factor (the
// 'ambientOcclusion' mode's gray) in the crease and under the sphere is
// clearly below that of open floor, which is near 1
bool testRoomOccluded(
    anari::Device d, anari::World room, const char *estimator, int passes = 1)
{
  const Image img = renderRoomFactor(d,
      room,
      ROOM_OCCLUSION_RADIUS,
      "ambientOcclusion",
      withBlurPasses(d, passes, estimator));
  auto name = [&](const char *test) {
    return withPasses(withName(test, estimator), passes);
  };
  float3 f;
  if (!roomFactors(img, roomViewPixel(img), f))
    return report(name("room probes see the floor").c_str(), false);

  bool pass = true;
  pass &= report(
      name("ambientOcclusion mode, open floor near 1").c_str(), f.z > 0.95f);
  pass &= report(
      name("ambientOcclusion mode, crease darker than open floor").c_str(),
      f.x < f.z - 0.1f);
  pass &=
      report(name("ambientOcclusion mode, under sphere darker than open floor")
                 .c_str(),
          f.y < f.z - 0.1f);
  return pass;
}

// Mean difference between the factors of horizontally or vertically adjacent
// floor pixels of 'img' whose surroundings (the pixels within 2 of them, which
// the blur may read) are all floor, so silhouettes don't count
float floorVariation(const Image &img)
{
  auto openFloor = [&](int x, int y) {
    for (int dy = -2; dy <= 2; dy++) {
      for (int dx = -2; dx <= 2; dx++) {
        const int nx = x + dx;
        const int ny = y + dy;
        if (nx < 0 || ny < 0 || nx >= int(img.size.x) || ny >= int(img.size.y)
            || img.objectId[img.at(uint2(nx, ny))] != FLOOR_ID)
          return false;
      }
    }
    return true;
  };

  double sum = 0.0;
  size_t pairs = 0;
  for (int y = 0; y < int(img.size.y); y++) {
    for (int x = 0; x < int(img.size.x); x++) {
      if (!openFloor(x, y))
        continue;
      const float f = img.color[img.at(uint2(x, y))].x;
      if (openFloor(x + 1, y)) {
        sum += std::fabs(f - img.color[img.at(uint2(x + 1, y))].x);
        pairs++;
      }
      if (openFloor(x, y + 1)) {
        sum += std::fabs(f - img.color[img.at(uint2(x, y + 1))].x);
        pairs++;
      }
    }
  }
  return pairs ? float(sum / pairs) : 0.f;
}

// With 'estimator', the blur removes the noise of the occlusion pass's 4x4
// rotation pattern: neighbouring floor pixels vary by more than NOISE on
// average in the 'ambientOcclusion.unblurred' mode, which shows the factor
// before the blur, and by less than NOISE in the 'ambientOcclusion' mode.
// (The estimators differ in strength, so the noise they leave does too: about
// 0.015 for 'g3d' and 0.006 for 'sao' before the blur at the room's radius,
// and 0.004 and 0.002 after it.)
bool testRoomFloorSmooth(
    anari::Device d, anari::World room, const char *estimator)
{
  const float NOISE = 0.005f;
  const float blurred = floorVariation(renderRoomFactor(d,
      room,
      ROOM_OCCLUSION_RADIUS,
      "ambientOcclusion",
      withEstimator(d, estimator)));
  const float unblurred = floorVariation(renderRoomFactor(d,
      room,
      ROOM_OCCLUSION_RADIUS,
      "ambientOcclusion.unblurred",
      withEstimator(d, estimator)));
  std::printf("  floor variation %f, unblurred %f\n", blurred, unblurred);
  bool pass = report(
      withName("ambientOcclusion.unblurred mode, factor noisy on the floor",
          estimator)
          .c_str(),
      unblurred > NOISE);
  pass &= report(
      withName("ambientOcclusion mode, blurred factor smooth on the floor",
          estimator)
          .c_str(),
      blurred < NOISE);
  return pass;
}

// The factor the blur pass should give at pixel (x, y) of 'unblurred', a
// render in the 'ambientOcclusion.unblurred' mode with no volumes (so its
// depth channel holds the hit buffer's distances) and an occlusion radius of
// 'radius': the mean of the unblurred factors in the 5x5 window centred on
// the pixel, over the pixels with a surface, each weighted by 0.5 per axis
// in which it is 2 pixels away and by 1 - (d / radius)^2, 'd' being its
// distance from the pixel's, and left out at d >= radius
float centredBlur(const Image &unblurred, int x, int y, float radius)
{
  const float distance = unblurred.depth[unblurred.at(uint2(x, y))];
  auto spatial = [](int offset) { return std::abs(offset) == 2 ? 0.5f : 1.f; };
  float sum = 0.f;
  float weightSum = 0.f;
  for (int dy = -2; dy <= 2; dy++) {
    for (int dx = -2; dx <= 2; dx++) {
      const int nx = x + dx;
      const int ny = y + dy;
      if (nx < 0 || ny < 0 || nx >= int(unblurred.size.x)
          || ny >= int(unblurred.size.y))
        continue;
      const size_t j = unblurred.at(uint2(nx, ny));
      if (unblurred.depth[j] == MISS_DEPTH)
        continue;
      const float d = std::fabs(unblurred.depth[j] - distance) / radius;
      if (!(d < 1.f))
        continue;
      const float weight = spatial(dx) * spatial(dy) * (1.f - d * d);
      sum += weight * unblurred.color[j].x;
      weightSum += weight;
    }
  }
  return sum / weightSum;
}

// The blur is centred on the pixel, so the blurred factor lines up with the
// color image: every surface pixel of the room's 'ambientOcclusion' mode
// shows the centred 5x5 blur of the 'ambientOcclusion.unblurred' mode's
// factors (centredBlur()). A window off the pixel's centre shifts the dark
// crease and contact bands and fails. Checked with 'estimator' and, if
// given, 'intensity': the intensity exponent applies to the unblurred factor,
// before the blur.
bool testBlurCentred(anari::Device d,
    anari::World room,
    const char *estimator,
    std::optional<float> intensity = std::nullopt)
{
  const Image blurred = renderRoomFactor(d,
      room,
      ROOM_OCCLUSION_RADIUS,
      "ambientOcclusion",
      withEstimator(d, estimator, intensity));
  const Image unblurred = renderRoomFactor(d,
      room,
      ROOM_OCCLUSION_RADIUS,
      "ambientOcclusion.unblurred",
      withEstimator(d, estimator, intensity));

  float largest = 0.f;
  size_t surfacePixels = 0;
  for (int y = 0; y < int(blurred.size.y); y++) {
    for (int x = 0; x < int(blurred.size.x); x++) {
      const size_t i = blurred.at(uint2(x, y));
      if (unblurred.depth[i] == MISS_DEPTH)
        continue;
      surfacePixels++;
      const float expected =
          centredBlur(unblurred, x, y, ROOM_OCCLUSION_RADIUS);
      largest = std::max(largest, std::fabs(blurred.color[i].x - expected));
    }
  }
  std::printf(
      "  %zu surface pixels, largest difference from the centred blur %f\n",
      surfacePixels,
      largest);
  std::string name = withName(
      "ambientOcclusion mode, blur window centred on the pixel", estimator);
  if (intensity)
    name += ", intensity " + format(*intensity);
  return report(name.c_str(), surfacePixels > 0 && largest < 1e-5f);
}

// Multi-pass blur //////////////////////////////////////////////////////////

// Per-axis spatial weights of the first blur pass (see centredBlur()) and of
// every further pass (the B3-spline)
const float FIRST_BLUR_WEIGHTS[5] = {0.5f, 1.f, 1.f, 1.f, 0.5f};
const float ATROUS_BLUR_WEIGHTS[5] = {
    1.f / 16.f, 1.f / 4.f, 3.f / 8.f, 1.f / 4.f, 1.f / 16.f};

// One blur pass over 'factor', per-pixel factors of 'unblurred', a render in
// the 'ambientOcclusion.unblurred' mode with no volumes (so its depth channel
// holds the hit buffer's distances), with an occlusion radius of 'radius':
// each surface pixel gets the mean of the factors of the pixels with a
// surface at offsets -2..2 times 'stride' from it per axis, each weighted by
// 'weights' per axis and by 1 - (d / radius)^2, 'd' being its distance from
// the pixel's, and left out at d >= radius; other pixels get 1
std::vector<float> blurPass(const Image &unblurred,
    const std::vector<float> &factor,
    int stride,
    const float (&weights)[5],
    float radius)
{
  const int w = int(unblurred.size.x);
  const int h = int(unblurred.size.y);
  std::vector<float> blurred(factor.size(), 1.f);
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      const size_t i = unblurred.at(uint2(x, y));
      if (unblurred.depth[i] == MISS_DEPTH)
        continue;
      float sum = 0.f;
      float weightSum = 0.f;
      for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
          const int nx = x + dx * stride;
          const int ny = y + dy * stride;
          if (nx < 0 || ny < 0 || nx >= w || ny >= h)
            continue;
          const size_t j = unblurred.at(uint2(nx, ny));
          if (unblurred.depth[j] == MISS_DEPTH)
            continue;
          const float dist =
              std::fabs(unblurred.depth[j] - unblurred.depth[i]) / radius;
          if (!(dist < 1.f))
            continue;
          const float weight =
              weights[dx + 2] * weights[dy + 2] * (1.f - dist * dist);
          sum += weight * factor[j];
          weightSum += weight;
        }
      }
      blurred[i] = sum / weightSum;
    }
  }
  return blurred;
}

// The factor 'passes' blur passes give 'unblurred' (see blurPass()): the
// first with stride 1 and FIRST_BLUR_WEIGHTS, pass k after it with stride
// 2^(k - 1) and ATROUS_BLUR_WEIGHTS, each blurring the previous one's result
std::vector<float> multiPassBlur(
    const Image &unblurred, int passes, float radius)
{
  std::vector<float> factor(unblurred.color.size());
  for (size_t i = 0; i < factor.size(); i++)
    factor[i] = unblurred.color[i].x;
  factor = blurPass(unblurred, factor, 1, FIRST_BLUR_WEIGHTS, radius);
  for (int k = 2; k <= passes; k++)
    factor =
        blurPass(unblurred, factor, 1 << (k - 1), ATROUS_BLUR_WEIGHTS, radius);
  return factor;
}

// Largest difference between the factor of 'img', a render in the
// 'ambientOcclusion' mode, and 'expected' over the pixels 'unblurred' sees a
// surface in; 1 if there are none
float largestFactorDifference(const Image &img,
    const Image &unblurred,
    const std::vector<float> &expected)
{
  float largest = 0.f;
  size_t surfacePixels = 0;
  for (size_t i = 0; i < expected.size(); i++) {
    if (unblurred.depth[i] == MISS_DEPTH)
      continue;
    surfacePixels++;
    largest = std::max(largest, std::fabs(img.color[i].x - expected[i]));
  }
  return surfacePixels ? largest : 1.f;
}

// With 'passes' blur passes, the 'ambientOcclusion' mode shows the
// multi-pass blur (multiPassBlur()) of the factor the
// 'ambientOcclusion.unblurred' mode shows, which is the one a single pass
// gives; and the default mode with 'ambientOcclusion' multiplies the color of
// the room's surfaces by that factor
bool testMultiPassBlur(anari::Device d, anari::World room, int passes)
{
  auto setup = withBlurPasses(d, passes);
  const Image blurred = renderRoomFactor(
      d, room, ROOM_OCCLUSION_RADIUS, "ambientOcclusion", setup);
  const Image unblurred = renderRoomFactor(
      d, room, ROOM_OCCLUSION_RADIUS, "ambientOcclusion.unblurred", setup);
  const Image onePassUnblurred = renderRoomFactor(d,
      room,
      ROOM_OCCLUSION_RADIUS,
      "ambientOcclusion.unblurred",
      withBlurPasses(d, 1));
  const float largest = largestFactorDifference(blurred,
      unblurred,
      multiPassBlur(unblurred, passes, ROOM_OCCLUSION_RADIUS));
  std::printf("  %d passes, largest difference from the multi-pass blur %f\n",
      passes,
      largest);

  auto withOcclusion = [&](bool on) {
    return renderRoomFactor(
        d, room, ROOM_OCCLUSION_RADIUS, "default", [&](anari::Renderer r) {
          setup(r);
          anari::setParameter(d, r, "ambientOcclusion", on);
        });
  };
  const Image on = withOcclusion(true);
  const Image off = withOcclusion(false);
  bool multiplied = true;
  for (size_t i = 0; i < on.color.size(); i++) {
    if (unblurred.depth[i] == MISS_DEPTH)
      continue;
    multiplied &=
        near(on.color[i], float4(off.color[i].xyz() * blurred.color[i].x, 1.f));
  }

  auto name = [&](const char *test) { return withPasses(test, passes); };
  bool pass = report(
      name("ambientOcclusion mode, multi-pass blur of the unblurred factor")
          .c_str(),
      largest < 1e-5f);
  pass &= report(
      name("ambientOcclusion.unblurred mode unchanged by the passes").c_str(),
      sameImage(unblurred, onePassUnblurred));
  pass &=
      report(name("default mode multiplied by the last pass's factor").c_str(),
          multiplied);
  return pass;
}

const uint32_t BOX_ID = 4;
// A box standing on the room's floor, clear of the sphere and the wall
const float3 BOX_LO = {-1.8f, 0.f, 0.5f};
const float3 BOX_HI = {-1.2f, 0.6f, 1.1f};
// A camera low over the floor, looking at the wall past the box, so the
// box's face towards it (z = BOX_HI.z) is seen almost head-on
const View LOW_VIEW = {{0.f, 0.35f, 5.f},
    linalg::normalize(float3(0.f, -0.1f, -6.f)),
    {0.f, 1.f, 0.f}};
constexpr uint2 LOW_VIEW_SIZE = {480, 480};

// The room with the box, whose six faces are quads with BOX_ID
anari::World makeRoomWithBox(anari::Device d)
{
  auto surfaces = roomSurfaces(d);
  float3 c[8];
  for (int i = 0; i < 8; i++) {
    c[i] = float3(i & 1 ? BOX_HI.x : BOX_LO.x,
        i & 2 ? BOX_HI.y : BOX_LO.y,
        i & 4 ? BOX_HI.z : BOX_LO.z);
  }
  const int faces[6][4] = {{0, 1, 3, 2},
      {4, 5, 7, 6},
      {0, 1, 5, 4},
      {2, 3, 7, 6},
      {0, 2, 6, 4},
      {1, 3, 7, 5}};
  for (const auto &f : faces) {
    const float3 p[4] = {c[f[0]], c[f[1]], c[f[2]], c[f[3]]};
    surfaces.push_back(makeQuad(d, p, BOX_ID));
  }
  return makeWorld(d, surfaces);
}

// The largest rise of the factor, in the 'ambientOcclusion' mode with
// 'passes' blur passes, from one row to the next down the columns of the
// middle half of the box's face towards LOW_VIEW, from its top edge to the
// floor; 1 if the face covers too few pixels. Near the floor the true factor
// falls quickly with height, and a single blur can't cancel the 4x4 rotation
// pattern there, so it rises and falls every few rows (horizontal bands)
// instead of falling steadily.
float largestRiseTowardsFloor(anari::Device d, anari::World world, int passes)
{
  const Image img =
      renderView(d, world, LOW_VIEW, LOW_VIEW_SIZE, [&](anari::Renderer r) {
        anari::setParameter(d, r, "mode", "ambientOcclusion");
        anari::setParameter(
            d, r, "ambientOcclusionRadius", ROOM_OCCLUSION_RADIUS);
        withBlurPasses(d, passes)(r);
      });
  const float left = 0.75f * BOX_LO.x + 0.25f * BOX_HI.x;
  const float right = 0.25f * BOX_LO.x + 0.75f * BOX_HI.x;
  const uint2 topLeft =
      pixelAt(LOW_VIEW, img.size, float3(left, BOX_HI.y, BOX_HI.z));
  const uint2 topRight =
      pixelAt(LOW_VIEW, img.size, float3(right, BOX_HI.y, BOX_HI.z));
  const uint2 bottomLeft =
      pixelAt(LOW_VIEW, img.size, float3(left, BOX_LO.y, BOX_HI.z));

  float largest = 0.f;
  size_t facePixels = 0;
  for (int x = int(topLeft.x); x <= int(topRight.x); x++) {
    // rows count up the image, so towards the floor is down
    std::optional<float> above;
    for (int y = int(topLeft.y); y >= int(bottomLeft.y); y--) {
      const size_t i = img.at(uint2(x, y));
      if (img.objectId[i] != BOX_ID)
        continue;
      const float f = img.color[i].x;
      if (above)
        largest = std::max(largest, f - *above);
      above = f;
      facePixels++;
    }
  }
  std::printf(
      "  %d blur passes: %zu pixels of the box's face, largest rise towards "
      "the floor %f\n",
      passes,
      facePixels,
      largest);
  return facePixels >= 500 ? largest : 1.f;
}

// No bands near a floor contact: with 3 blur passes the factor falls steadily
// down the box's face towards the floor, rising by at most a small tolerance
// from one row to the next, where with 1 pass it rises noticeably
bool testNoBandsNearContact(anari::Device d)
{
  auto world = makeRoomWithBox(d);
  const float onePass = largestRiseTowardsFloor(d, world, 1);
  const float threePasses = largestRiseTowardsFloor(d, world, 3);
  anari::release(d, world);

  bool pass =
      report("1 blur pass, factor bands near a floor contact", onePass > 0.01f);
  pass &= report("3 blur passes, factor falls steadily towards a floor contact",
      threePasses < 0.001f);
  return pass;
}

// 'ambientOcclusionBlurPasses' is clamped to [1, 5] without a warning: 0
// renders like 1, which renders like leaving it unset, and 9 like 5, which
// differs from 1
bool testBlurPassesClamped(anari::Device d, anari::World room)
{
  const size_t warnings = g_warningMessages.size();
  auto factor = [&](int32_t passes) {
    return renderRoomFactor(d,
        room,
        ROOM_OCCLUSION_RADIUS,
        "ambientOcclusion",
        withBlurPasses(d, passes));
  };
  const Image unset = renderRoomFactor(d, room);
  const Image zero = factor(0);
  const Image one = factor(1);
  const Image five = factor(5);
  const Image nine = factor(9);

  bool pass = report("blur passes 1 renders like unset", sameImage(one, unset));
  pass &= report("blur passes 0 clamped to 1", sameImage(zero, one));
  pass &= report("blur passes 9 clamped to 5", sameImage(nine, five));
  pass &= report("blur passes 5 differ from 1", !sameImage(five, one));
  pass &= report("blur passes clamped without a warning",
      g_warningMessages.size() == warnings);
  return pass;
}

// Seen from between the wall and the sphere, the sphere's lower half, which
// the floor occludes, is silhouetted against open floor beyond it
const View SPHERE_BACK_VIEW = {{0.8f, 0.35f, -0.8f},
    linalg::normalize(float3(0.f, -0.15f, 1.f)),
    {0.f, 1.f, 0.f}};

// Unit direction of the camera ray of 'v' through the center of pixel 'px' of
// a 'size' image (the inverse of pixelAt())
float3 rayDirection(const View &v, uint2 size, uint2 px)
{
  const float3 u = linalg::normalize(linalg::cross(v.direction, v.up));
  const float3 w = linalg::cross(u, v.direction);
  const float halfHeight = std::tan(0.5f * float(M_PI) / 3.f); // fovy 60
  const float sx = (px.x + 0.5f) / size.x - 0.5f;
  const float sy = (px.y + 0.5f) / size.y - 0.5f;
  return linalg::normalize(v.direction + 2.f * halfHeight * (sx * u + sy * w));
}

// No halo: with 'passes' blur passes, floor pixels just outside the sphere's
// silhouette (a sphere pixel within 2 of them is nearer by more than the
// occlusion radius) whose floor point is farther than the occlusion radius
// from the sphere stay unoccluded, though sphere pixels next to them are dark
bool testNoHalo(anari::Device d, anari::World room, int passes = 1)
{
  const View &view = SPHERE_BACK_VIEW;
  const Image img =
      renderView(d, room, view, ROOM_SIZE, [&](anari::Renderer r) {
        anari::setParameter(d, r, "mode", "ambientOcclusion");
        anari::setParameter(
            d, r, "ambientOcclusionRadius", ROOM_OCCLUSION_RADIUS);
        withBlurPasses(d, passes)(r);
      });

  size_t probes = 0;
  float lowestProbe = 1.f;
  float darkestSphere = 1.f;
  for (int y = 0; y < int(img.size.y); y++) {
    for (int x = 0; x < int(img.size.x); x++) {
      const size_t i = img.at(uint2(x, y));
      if (img.objectId[i] != FLOOR_ID)
        continue;
      const float3 p = view.position
          + rayDirection(view, img.size, uint2(x, y)) * img.depth[i];
      if (linalg::length(p - SPHERE_CENTER) - SPHERE_RADIUS
          <= ROOM_OCCLUSION_RADIUS)
        continue;

      float darkestNear = 1.f;
      bool outside = false;
      for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
          const int nx = x + dx;
          const int ny = y + dy;
          if (nx < 0 || ny < 0 || nx >= int(img.size.x)
              || ny >= int(img.size.y))
            continue;
          const size_t j = img.at(uint2(nx, ny));
          if (img.objectId[j] == SPHERE_ID
              && img.depth[j] < img.depth[i] - ROOM_OCCLUSION_RADIUS) {
            outside = true;
            darkestNear = std::min(darkestNear, img.color[j].x);
          }
        }
      }
      if (!outside)
        continue;
      probes++;
      lowestProbe = std::min(lowestProbe, img.color[i].x);
      darkestSphere = std::min(darkestSphere, darkestNear);
    }
  }

  std::printf(
      "  %zu floor pixels outside the silhouette, lowest factor %f, darkest "
      "sphere pixel next to them %f\n",
      probes,
      lowestProbe,
      darkestSphere);
  bool pass =
      report(withPasses("no halo, floor pixels outside the sphere's silhouette",
                 passes)
                 .c_str(),
          probes >= 8 && darkestSphere < 0.8f);
  pass &= report(
      withPasses(
          "no halo, floor outside the silhouette stays unoccluded", passes)
          .c_str(),
      probes > 0 && lowestProbe > 0.95f);
  return pass;
}

// In the default mode, 'ambientOcclusion' multiplies the color of the room's
// opaque surfaces by the occlusion factor the 'ambientOcclusion' mode shows,
// so the crease and the floor under the sphere are darker than with it off.
// 'ambientOcclusion' is on by default: a renderer that doesn't set it renders
// the same image as one that sets it to true
bool testRoomDefaultMode(anari::Device d, anari::World room)
{
  const Image factor = renderRoomFactor(d, room);
  const Image off = renderRoomDefault(d, room, false);
  const Image on = renderRoomDefault(d, room, true);
  const Image unset =
      renderView(d, room, ROOM_VIEW, ROOM_SIZE, [&](anari::Renderer r) {
        anari::setParameter(
            d, r, "ambientOcclusionRadius", ROOM_OCCLUSION_RADIUS);
      });

  bool pass = report("default mode, ambientOcclusion on by default",
      sameImage(unset, on) && !sameImage(unset, off));
  for (const auto &probe : {CREASE, UNDER_SPHERE, OPEN_FLOOR}) {
    size_t i;
    if (!floorPixel(on, probe, i)) {
      pass &= report("room probes see the floor", false);
      continue;
    }
    const float f = factor.color[i].x;
    const float4 expected = float4(off.color[i].xyz() * f, 1.f);
    const bool ok = near(on.color[i], expected);
    if (!ok) {
      std::printf("  %s: color %f with occlusion, %f without, factor %f\n",
          probe.name,
          on.color[i].x,
          off.color[i].x,
          f);
    }
    pass &= report(
        (std::string("default mode multiplied by the factor, ") + probe.name)
            .c_str(),
        ok);
  }

  size_t crease, under, open;
  floorPixel(on, CREASE, crease);
  floorPixel(on, UNDER_SPHERE, under);
  floorPixel(on, OPEN_FLOOR, open);
  pass &= report("default mode, crease darker than open floor",
      on.color[crease].x < on.color[open].x - 0.05f);
  pass &= report("default mode, under sphere darker than open floor",
      on.color[under].x < on.color[open].x - 0.05f);
  pass &= report("default mode, crease darker with ambientOcclusion",
      on.color[crease].x < off.color[crease].x - 0.05f);
  pass &= report("default mode, open floor unchanged by ambientOcclusion",
      on.color[open].x > off.color[open].x - 0.01f);
  return pass;
}

const uint32_t CLEAR_VOLUME_ID = 9;
const uint32_t CUT_OUT_ID = 10;

// A volume whose box encloses the crease and the floor under the sphere but
// not the open floor, and whose transfer function is fully transparent,
// doesn't change the occlusion: the hit buffer holds the surfaces behind it,
// so the 'ambientOcclusion' mode shows the same factors as for the room
// without the volume, and the default mode with 'ambientOcclusion' renders
// the same colors
bool testClearVolume(anari::Device d, anari::World room)
{
  auto clearRoom = makeWorld(d,
      roomSurfaces(d),
      makeVolume(d,
          float3(-1.5f, -0.5f, -1.5f),
          float3(1.5f, 1.2f, 1.2f),
          0.f,
          CLEAR_VOLUME_ID));

  const Image with = renderRoomFactor(d, clearRoom);
  const Image without = renderRoomFactor(d, room);
  const Image defaultWith = renderRoomDefault(d, clearRoom, true);
  const Image defaultWithout = renderRoomDefault(d, room, true);
  anari::release(d, clearRoom);

  bool pass = true;
  // the probes inside the box see the volume first
  for (const auto &probe : {CREASE, UNDER_SPHERE}) {
    const size_t i = with.at(pixelAt(ROOM_VIEW, with.size, probe.pos));
    pass &= report(
        (std::string("clear volume in front of the ") + probe.name).c_str(),
        with.objectId[i] == CLEAR_VOLUME_ID);
  }

  float3 f;
  if (roomFactors(without, roomViewPixel(without), f)) {
    pass &= report("clear volume, crease darker than open floor",
        f.x < f.z - 0.1f);
  } else {
    pass &= report("room probes see the floor", false);
  }

  const size_t n = with.color.size();
  pass &= report("ambientOcclusion mode, factors unchanged by a clear volume",
      std::memcmp(with.color.data(), without.color.data(), n * sizeof(float4))
          == 0);
  pass &= report("default mode, occlusion unchanged by a clear volume",
      std::memcmp(defaultWith.color.data(),
          defaultWithout.color.data(),
          n * sizeof(float4))
          == 0);
  return pass;
}

// The factor doesn't depend on what a frame rendered before: the hit buffer
// keeps its last distance on pixels that no longer see a surface, and the
// passes skip such pixels. A frame that rendered the room and then a room
// whose floor ends through the occluded floor in front of the sphere gives
// the same factor as a new frame, also along the floor's new edge.
bool testNoStaleHits(anari::Device d, anari::World room)
{
  auto renderer = anari::newObject<anari::Renderer>(d, "preview");
  anari::setParameter(d, renderer, "background", BACKGROUND);
  anari::setParameter(d, renderer, "mode", "ambientOcclusion");
  anari::setParameter(
      d, renderer, "ambientOcclusionRadius", ROOM_OCCLUSION_RADIUS);
  anari::commitParameters(d, renderer);
  auto camera = makeCamera(d, ROOM_VIEW);

  auto frame = anari::newObject<anari::Frame>(d);
  anari::setParameter(d, frame, "size", ROOM_SIZE);
  anari::setParameter(d, frame, "channel.color", ANARI_FLOAT32_VEC4);
  anari::setParameter(d, frame, "world", room);
  anari::setAndReleaseParameter(d, frame, "camera", camera);
  anari::setAndReleaseParameter(d, frame, "renderer", renderer);
  anari::commitParameters(d, frame);

  anari::render(d, frame);
  anari::wait(d, frame);
  auto shortRoom = makeRoom(d, UNDER_SPHERE.pos.z - 0.05f);
  anari::setParameter(d, frame, "world", shortRoom);
  anari::commitParameters(d, frame);
  anari::render(d, frame);
  anari::wait(d, frame);

  const size_t n = size_t(ROOM_SIZE.x) * ROOM_SIZE.y;
  auto color = anari::map<float4>(d, frame, "channel.color");
  const std::vector<float4> reused(color.data, color.data + n);
  anari::unmap(d, frame, "channel.color");
  anari::release(d, frame);

  const Image fresh = renderRoomFactor(d, shortRoom);
  anari::release(d, shortRoom);

  return report("factor independent of the frame's earlier renders",
      std::memcmp(reused.data(), fresh.color.data(), n * sizeof(float4)) == 0);
}

// A frame of ROOM_SIZE with a color channel rendering 'room' seen by
// ROOM_VIEW with 'renderer', which the caller keeps and may commit again
anari::Frame makeRoomFrame(
    anari::Device d, anari::World room, anari::Renderer renderer)
{
  auto camera = makeCamera(d, ROOM_VIEW);
  auto frame = anari::newObject<anari::Frame>(d);
  anari::setParameter(d, frame, "size", ROOM_SIZE);
  anari::setParameter(d, frame, "channel.color", ANARI_FLOAT32_VEC4);
  anari::setParameter(d, frame, "world", room);
  anari::setAndReleaseParameter(d, frame, "camera", camera);
  anari::setParameter(d, frame, "renderer", renderer);
  anari::commitParameters(d, frame);
  return frame;
}

// Renders 'frame', made by makeRoomFrame(), and reads its color back
std::vector<float4> renderRoomColor(anari::Device d, anari::Frame frame)
{
  const size_t n = size_t(ROOM_SIZE.x) * ROOM_SIZE.y;
  anari::render(d, frame);
  anari::wait(d, frame);
  auto color = anari::map<float4>(d, frame, "channel.color");
  std::vector<float4> c(color.data, color.data + n);
  anari::unmap(d, frame, "channel.color");
  return c;
}

// Committing the renderer of an existing frame turns occlusion on and off:
// the crease darkens once the renderer asks for the 'ambientOcclusion' mode,
// and the default mode's image returns once it no longer does (with
// 'ambientOcclusion' off, so the default mode doesn't read the factor)
bool testToggleOnExistingFrame(anari::Device d, anari::World room)
{
  auto renderer = anari::newObject<anari::Renderer>(d, "preview");
  anari::setParameter(d, renderer, "background", BACKGROUND);
  anari::setParameter(d, renderer, "ambientOcclusion", false);
  anari::commitParameters(d, renderer);
  auto frame = makeRoomFrame(d, room, renderer);
  const size_t n = size_t(ROOM_SIZE.x) * ROOM_SIZE.y;
  auto renderColor = [&]() { return renderRoomColor(d, frame); };

  const auto before = renderColor();
  anari::setParameter(d, renderer, "mode", "ambientOcclusion");
  anari::setParameter(
      d, renderer, "ambientOcclusionRadius", ROOM_OCCLUSION_RADIUS);
  anari::commitParameters(d, renderer);
  const auto on = renderColor();
  anari::setParameter(d, renderer, "mode", "default");
  anari::commitParameters(d, renderer);
  const auto after = renderColor();

  anari::release(d, frame);
  anari::release(d, renderer);

  const size_t crease =
      size_t(pixelAt(ROOM_VIEW, ROOM_SIZE, CREASE.pos).y) * ROOM_SIZE.x
      + pixelAt(ROOM_VIEW, ROOM_SIZE, CREASE.pos).x;
  bool pass = report(
      "renderer commit turns occlusion on in a frame", on[crease].x < 0.9f);
  pass &= report("renderer commit turns occlusion off in a frame",
      std::memcmp(before.data(), after.data(), n * sizeof(float4)) == 0);
  return pass;
}

// With 'estimator', a lone plane facing the camera doesn't occlude itself:
// the factor is near 1 on every pixel that sees it, including those near its
// edges
bool testLonePlane(anari::Device d, const char *estimator)
{
  const float3 corners[4] = {
      {-1.f, -1.f, 0.f}, {1.f, -1.f, 0.f}, {1.f, 1.f, 0.f}, {-1.f, 1.f, 0.f}};
  auto world = makeWorld(d, {makeQuad(d, corners, FLOOR_ID)});
  const View view = {{0.f, 0.f, 3.f}, {0.f, 0.f, -1.f}, {0.f, 1.f, 0.f}};
  const Image img =
      renderView(d, world, view, ROOM_SIZE, [&](anari::Renderer r) {
        anari::setParameter(d, r, "mode", "ambientOcclusion");
        anari::setParameter(d, r, "ambientOcclusionRadius", 0.5f);
        withEstimator(d, estimator)(r);
      });
  anari::release(d, world);

  size_t onPlane = 0;
  float lowest = 1.f;
  for (size_t i = 0; i < img.color.size(); i++) {
    if (img.objectId[i] != FLOOR_ID)
      continue;
    onPlane++;
    lowest = std::min(lowest, img.color[i].x);
  }
  if (lowest < 0.99f)
    std::printf("  lowest factor on the plane %f\n", lowest);
  return report(withName("lone plane, factor near 1", estimator).c_str(),
      onPlane > 0 && lowest >= 0.99f);
}

// The hit buffer's normal is the shading normal, on the viewer's side of the
// geometry: a lone plane facing the camera with 'vertex.normal' along its
// geometric normal, or against it, has a factor near 1 like the plane without
// normals, while one whose normals tilt 45 degrees about y has the taps on
// the side they tilt away from above its tangent plane, and darkens
bool testShadingNormalPlane(anari::Device d)
{
  const float3 corners[4] = {
      {-1.f, -1.f, 0.f}, {1.f, -1.f, 0.f}, {1.f, 1.f, 0.f}, {-1.f, 1.f, 0.f}};
  // the lowest factor of the plane's pixels with 'vertex.normal' 'n'
  auto lowestFactor = [&](float3 n) {
    const float3 normals[4] = {n, n, n, n};
    auto world = makeWorld(
        d, {makeQuad(d, corners, FLOOR_ID, GRAY, "opaque", 1.f, normals)});
    const View view = {{0.f, 0.f, 3.f}, {0.f, 0.f, -1.f}, {0.f, 1.f, 0.f}};
    const Image img =
        renderView(d, world, view, ROOM_SIZE, [&](anari::Renderer r) {
          anari::setParameter(d, r, "mode", "ambientOcclusion.unblurred");
          anari::setParameter(d, r, "ambientOcclusionRadius", 0.5f);
        });
    anari::release(d, world);
    float lowest = 1.f;
    for (size_t i = 0; i < img.color.size(); i++) {
      if (img.objectId[i] == FLOOR_ID)
        lowest = std::min(lowest, img.color[i].x);
    }
    return lowest;
  };

  const float along = lowestFactor(float3(0.f, 0.f, 1.f));
  const float against = lowestFactor(float3(0.f, 0.f, -1.f));
  const float tilted = lowestFactor(float3(1.f, 0.f, 1.f));
  std::printf(
      "  lowest factor on the plane: normals along Ng %f, against Ng "
      "%f, tilted %f\n",
      along,
      against,
      tilted);
  bool pass =
      report("plane with normals along Ng, factor near 1", along >= 0.99f);
  pass &=
      report("plane with normals against Ng, factor near 1", against >= 0.99f);
  pass &= report("plane with tilted normals darkens", tilted < 0.9f);
  return pass;
}

// The hit buffer's normal is the first visible surface's own, also when a
// volume and then a mask cut-out lie in front of it: a plane facing the
// camera, placed by an instance that rotates a quad in its object's y = 0
// plane, behind a world-level mask cut-out behind a volume, has a factor near
// 1 like the lone plane. (With the cut-out's instance transform the normal
// would lie in the plane, and the plane would occlude itself.)
bool testNormalBehindVolumeAndCutOut(anari::Device d)
{
  const float3 corners[4] = {
      {-1.f, 0.f, -1.f}, {1.f, 0.f, -1.f}, {1.f, 0.f, 1.f}, {-1.f, 0.f, 1.f}};
  auto group = anari::newObject<anari::Group>(d);
  auto plane = makeQuad(d, corners, FLOOR_ID);
  anari::setParameterArray1D(d, group, "surface", &plane, 1);
  anari::commitParameters(d, group);
  anari::release(d, plane);

  // +90 degrees about x takes the object's y axis to z
  const mat4 xfm = linalg::rotation_matrix(
      linalg::rotation_quat(float3(1.f, 0.f, 0.f), 0.5f * float(M_PI)));
  auto inst = anari::newObject<anari::Instance>(d, "transform");
  anari::setAndReleaseParameter(d, inst, "group", group);
  anari::setParameterArray1D(d, inst, "transform", &xfm, 1);
  anari::commitParameters(d, inst);

  auto cutOut = makeSurface(
      d, Layer{{-1.f, -1.f}, {1.f, 1.f}, 0.5f, "mask", 0.3f, CUT_OUT_ID});
  auto volume = makeVolume(
      d, float3(-1.f, -1.f, 1.f), float3(1.f, 1.f, 1.5f), 0.5f, VOLUME_ID);

  auto world = anari::newObject<anari::World>(d);
  anari::setParameterArray1D(d, world, "instance", &inst, 1);
  anari::setParameterArray1D(d, world, "surface", &cutOut, 1);
  anari::setParameterArray1D(d, world, "volume", &volume, 1);
  anari::commitParameters(d, world);
  anari::release(d, inst);
  anari::release(d, cutOut);
  anari::release(d, volume);

  const View view = {{0.f, 0.f, 3.f}, {0.f, 0.f, -1.f}, {0.f, 1.f, 0.f}};
  const Image img =
      renderView(d, world, view, ROOM_SIZE, [&](anari::Renderer r) {
        anari::setParameter(d, r, "mode", "ambientOcclusion");
        anari::setParameter(d, r, "ambientOcclusionRadius", 0.5f);
      });
  anari::release(d, world);

  // the plane's pixels are the ones not showing the background
  size_t onPlane = 0;
  float lowest = 1.f;
  for (size_t i = 0; i < img.color.size(); i++) {
    if (near(img.color[i], BACKGROUND))
      continue;
    onPlane++;
    lowest = std::min(lowest, img.color[i].x);
  }
  if (lowest < 0.99f)
    std::printf("  lowest factor on the plane %f\n", lowest);
  const size_t centre = img.at(img.size / 2u);
  return report(
      "plane behind volume and cut-out in another instance, factor near 1",
      onPlane > 0 && lowest >= 0.99f && img.objectId[centre] == VOLUME_ID);
}

// The surface seen through a mask cut-out is the first visible surface there,
// so it receives occlusion: with a cut-out in front of the camera, covering
// the whole view, the room's 'ambientOcclusion' mode shows the factors it
// shows without it (dark in the crease), and so does the default mode with
// 'ambientOcclusion' on
bool testOccludedBehindCutOut(anari::Device d, anari::World room)
{
  // the camera, at z = 3.2, looks down -z: every ray crosses z = 2.8 inside
  // the cut-out
  const float3 corners[4] = {{-5.f, -5.f, 2.8f},
      {5.f, -5.f, 2.8f},
      {5.f, 5.f, 2.8f},
      {-5.f, 5.f, 2.8f}};
  auto surfaces = roomSurfaces(d);
  surfaces.push_back(makeQuad(d, corners, CUT_OUT_ID, GRAY, "mask", 0.3f));
  auto cutOutRoom = makeWorld(d, surfaces);

  const Image with = renderRoomFactor(d, cutOutRoom);
  const Image without = renderRoomFactor(d, room);
  const Image defaultWith = renderRoomDefault(d, cutOutRoom, true);
  const Image defaultWithout = renderRoomDefault(d, room, true);
  anari::release(d, cutOutRoom);

  float3 f;
  if (!roomFactors(with, roomViewPixel(with), f))
    return report("room probes see the floor through the cut-out", false);
  bool pass = report(
      "behind mask cut-out, crease darker than open floor", f.x < f.z - 0.1f);
  pass &= report("ambientOcclusion mode, factors unchanged by a mask cut-out",
      sameImage(with, without));
  pass &= report("default mode, occlusion unchanged by a mask cut-out",
      sameImage(defaultWith, defaultWithout));
  return pass;
}

const uint32_t BLEND_ID = 11;
const float3 RED = {0.8f, 0.f, 0.f};
const float3 BLUE = {0.f, 0.f, 0.8f};

// A blend surface in front of an occluded opaque surface is the first visible
// surface, so it is the one occluded and the surface behind it isn't
// darkened: a half-transparent red layer 0.02 above the room's floor, which
// is blue here, has a factor below 1 in the crease, and the default mode
// multiplies its red by that factor while the blue floor seen through it
// keeps its color
bool testOnlyBlendLayerOccluded(anari::Device d)
{
  const float3 floor[4] = {
      {-2.f, 0.f, -1.f}, {2.f, 0.f, -1.f}, {2.f, 0.f, 2.5f}, {-2.f, 0.f, 2.5f}};
  const float3 layer[4] = {{-2.f, 0.02f, -1.f},
      {2.f, 0.02f, -1.f},
      {2.f, 0.02f, 2.5f},
      {-2.f, 0.02f, 2.5f}};
  const float3 wall[4] = {
      {-2.f, 0.f, -1.f}, {2.f, 0.f, -1.f}, {2.f, 2.f, -1.f}, {-2.f, 2.f, -1.f}};
  auto world = makeWorld(d,
      {makeQuad(d, floor, FLOOR_ID, BLUE),
          makeQuad(d, layer, BLEND_ID, RED, "blend", 0.5f),
          makeQuad(d, wall, WALL_ID)});

  const Image factor = renderRoomFactor(d, world);
  const Image off = renderRoomDefault(d, world, false);
  const Image on = renderRoomDefault(d, world, true);
  anari::release(d, world);

  const size_t crease = factor.at(pixelAt(ROOM_VIEW, ROOM_SIZE, CREASE.pos));
  const size_t open = factor.at(pixelAt(ROOM_VIEW, ROOM_SIZE, OPEN_FLOOR.pos));
  const float f = factor.color[crease].x;
  const float4 onColor = on.color[crease];
  const float4 offColor = off.color[crease];
  std::printf(
      "  blend layer: factor %f in the crease, %f on open floor; "
      "color (%f, %f, %f, %f) with occlusion, (%f, %f, %f, %f) "
      "without\n",
      f,
      factor.color[open].x,
      onColor.x,
      onColor.y,
      onColor.z,
      onColor.w,
      offColor.x,
      offColor.y,
      offColor.z,
      offColor.w);

  bool pass = report("blend layer in the crease is the visible hit",
      factor.objectId[crease] == BLEND_ID && factor.objectId[open] == BLEND_ID);
  pass &= report("ambientOcclusion mode, blend layer in the crease occluded",
      f < factor.color[open].x - 0.1f);
  pass &= report("default mode, blend layer multiplied by its factor",
      offColor.x > 0.05f && std::fabs(onColor.x - offColor.x * f) < 1e-5f);
  pass &= report("default mode, surface behind the blend layer not darkened",
      offColor.z > 0.05f && onColor.z == offColor.z && onColor.w == offColor.w);
  return pass;
}

// Rendering the room again, also with other task grain sizes, gives the same
// occlusion factor at every pixel
bool testDeterministic(anari::Device d, anari::World room)
{
  const Image first = renderRoomFactor(d, room);
  const Image again = renderRoomFactor(d, room);
  bool pass = report("same factor rendered twice", sameImage(first, again));

  for (int grain : {1, 16}) {
    const Image regrained =
        renderView(d, room, ROOM_VIEW, ROOM_SIZE, [&](anari::Renderer r) {
          anari::setParameter(d, r, "mode", "ambientOcclusion");
          anari::setParameter(
              d, r, "ambientOcclusionRadius", ROOM_OCCLUSION_RADIUS);
          anari::setParameter(d, r, "taskGrainSizeWidth", grain);
          anari::setParameter(d, r, "taskGrainSizeHeight", grain);
        });
    pass &= report(
        ("same factor with task grain size " + std::to_string(grain)).c_str(),
        sameImage(first, regrained));
  }
  return pass;
}

// With 'ambientOcclusion' set to false the room renders as it did before
// occlusion existed: the default mode ignores 'ambientOcclusionRadius', and
// modes other than 'ambientOcclusion' ignore both parameters
bool testRoomUnchangedWhenOff(anari::Device d, anari::World room)
{
  const Image off =
      renderView(d, room, ROOM_VIEW, ROOM_SIZE, [&](anari::Renderer r) {
        anari::setParameter(d, r, "ambientOcclusion", false);
      });
  const Image offWithRadius =
      renderView(d, room, ROOM_VIEW, ROOM_SIZE, [&](anari::Renderer r) {
        anari::setParameter(d, r, "ambientOcclusion", false);
        anari::setParameter(
            d, r, "ambientOcclusionRadius", ROOM_OCCLUSION_RADIUS);
      });
  bool pass = report("room, default mode off ignores the radius",
      sameImage(off, offWithRadius));

  const Image ngOff =
      renderView(d, room, ROOM_VIEW, ROOM_SIZE, [&](anari::Renderer r) {
        anari::setParameter(d, r, "mode", "Ng");
        anari::setParameter(d, r, "ambientOcclusion", false);
      });
  const Image ngOn =
      renderView(d, room, ROOM_VIEW, ROOM_SIZE, [&](anari::Renderer r) {
        anari::setParameter(d, r, "mode", "Ng");
        anari::setParameter(d, r, "ambientOcclusion", true);
        anari::setParameter(
            d, r, "ambientOcclusionRadius", ROOM_OCCLUSION_RADIUS);
      });
  pass &=
      report("room, Ng mode ignores ambientOcclusion", sameImage(ngOff, ngOn));
  return pass;
}

// Estimator and intensity /////////////////////////////////////////////////////

// Without 'ambientOcclusionEstimator' and 'ambientOcclusionIntensity' the room
// renders as with 'g3d' and 1: in both ambient occlusion modes and in the
// default mode with 'ambientOcclusion' on
bool testDefaultEstimatorAndIntensity(anari::Device d, anari::World room)
{
  bool pass = true;
  for (const char *mode :
      {"ambientOcclusion", "ambientOcclusion.unblurred", "default"}) {
    auto on = [&](anari::Renderer r) {
      anari::setParameter(d, r, "ambientOcclusion", true);
    };
    const Image defaults =
        renderRoomFactor(d, room, ROOM_OCCLUSION_RADIUS, mode, on);
    const Image explicitG3d =
        renderRoomFactor(d, room, ROOM_OCCLUSION_RADIUS, mode, [&](auto r) {
          on(r);
          withEstimator(d, "g3d", 1.f)(r);
        });
    pass &=
        report((std::string("defaults are estimator g3d, intensity 1, ") + mode)
                   .c_str(),
            sameImage(defaults, explicitG3d));
  }
  return pass;
}

// With 'estimator', 'ambientOcclusionIntensity' raises the unblurred factor
// to its power at every surface pixel: 2 gives the square of the factor at
// 1 (darker), 0.5 its square root (lighter), and 0, or a negative value
// (clamped to 0 silently), gives 1 before and after the blur
bool testIntensity(anari::Device d, anari::World room, const char *estimator)
{
  auto unblurred = [&](float intensity) {
    return renderRoomFactor(d,
        room,
        ROOM_OCCLUSION_RADIUS,
        "ambientOcclusion.unblurred",
        withEstimator(d, estimator, intensity));
  };
  const Image one = unblurred(1.f);

  // largest difference, over the surface pixels, between 'img' and 'f'
  // applied to 'one'
  auto largestDifference = [&](const Image &img, auto &&f) {
    float largest = 0.f;
    for (size_t i = 0; i < one.color.size(); i++) {
      if (one.depth[i] != MISS_DEPTH)
        largest =
            std::max(largest, std::fabs(img.color[i].x - f(one.color[i].x)));
    }
    return largest;
  };
  float darkest = 1.f;
  for (size_t i = 0; i < one.color.size(); i++) {
    if (one.depth[i] != MISS_DEPTH)
      darkest = std::min(darkest, one.color[i].x);
  }
  std::printf("  darkest unblurred factor at intensity 1 %f\n", darkest);
  bool pass =
      report(withName("intensity 1, some pixels occluded", estimator).c_str(),
          darkest < 0.8f);

  const float squared =
      largestDifference(unblurred(2.f), [](float f) { return f * f; });
  const float root =
      largestDifference(unblurred(0.5f), [](float f) { return std::sqrt(f); });
  std::printf("  largest difference from the square %f, from the root %f\n",
      squared,
      root);
  pass &= report(
      withName("intensity 2 squares the unblurred factor", estimator).c_str(),
      squared < 1e-5f);
  pass &=
      report(withName("intensity 0.5 takes the unblurred factor's square root",
                 estimator)
                 .c_str(),
          root < 1e-5f);

  const int warnings = g_warnings;
  for (float intensity : {0.f, -1.f}) {
    for (const char *mode :
        {"ambientOcclusion", "ambientOcclusion.unblurred"}) {
      const Image img = renderRoomFactor(d,
          room,
          ROOM_OCCLUSION_RADIUS,
          mode,
          withEstimator(d, estimator, intensity));
      const float largest = largestDifference(img, [](float) { return 1.f; });
      pass &= report(withName(("intensity " + format(intensity)
                                  + " gives factor 1, " + mode)
                                  .c_str(),
                         estimator)
                         .c_str(),
          largest == 0.f);
    }
  }
  pass &=
      report(withName("negative intensity clamped silently", estimator).c_str(),
          g_warnings == warnings);
  return pass;
}

// An unknown 'ambientOcclusionEstimator' warns, naming the value, and renders
// like 'g3d'
bool testUnknownEstimator(anari::Device d, anari::World room)
{
  const Image g3d = renderRoomFactor(d,
      room,
      ROOM_OCCLUSION_RADIUS,
      "ambientOcclusion",
      withEstimator(d, "g3d"));
  const size_t warnings = g_warningMessages.size();
  const Image unknown = renderRoomFactor(d,
      room,
      ROOM_OCCLUSION_RADIUS,
      "ambientOcclusion",
      withEstimator(d, "noSuchEstimator"));
  bool named = false;
  for (size_t i = warnings; i < g_warningMessages.size(); i++)
    named |= g_warningMessages[i].find("noSuchEstimator") != std::string::npos;
  bool pass = report("unknown estimator warns, naming it", named);
  pass &= report("unknown estimator renders like g3d", sameImage(g3d, unknown));
  return pass;
}

// Committing 'ambientOcclusionIntensity' or 'ambientOcclusionEstimator' on
// the renderer of an existing frame renders it again: intensity 2 darkens
// the crease, 'sao' changes the factor, and going back to 'g3d' and 1 gives
// the first image again
bool testParametersRerender(anari::Device d, anari::World room)
{
  auto renderer = anari::newObject<anari::Renderer>(d, "preview");
  anari::setParameter(d, renderer, "background", BACKGROUND);
  anari::setParameter(d, renderer, "mode", "ambientOcclusion");
  anari::setParameter(
      d, renderer, "ambientOcclusionRadius", ROOM_OCCLUSION_RADIUS);
  anari::commitParameters(d, renderer);
  auto frame = makeRoomFrame(d, room, renderer);
  const size_t n = size_t(ROOM_SIZE.x) * ROOM_SIZE.y;
  auto renderColor = [&]() { return renderRoomColor(d, frame); };

  const auto first = renderColor();
  anari::setParameter(d, renderer, "ambientOcclusionIntensity", 2.f);
  anari::commitParameters(d, renderer);
  const auto darker = renderColor();
  anari::setParameter(d, renderer, "ambientOcclusionEstimator", "sao");
  anari::commitParameters(d, renderer);
  const auto sao = renderColor();
  anari::setParameter(d, renderer, "ambientOcclusionEstimator", "g3d");
  anari::setParameter(d, renderer, "ambientOcclusionIntensity", 1.f);
  anari::commitParameters(d, renderer);
  const auto back = renderColor();

  anari::release(d, frame);
  anari::release(d, renderer);

  const uint2 px = pixelAt(ROOM_VIEW, ROOM_SIZE, CREASE.pos);
  const size_t crease = size_t(px.y) * ROOM_SIZE.x + px.x;
  auto same = [&](const std::vector<float4> &a, const std::vector<float4> &b) {
    return std::memcmp(a.data(), b.data(), n * sizeof(float4)) == 0;
  };
  bool pass = report("intensity commit renders the frame again",
      darker[crease].x < first[crease].x - 0.05f);
  pass &=
      report("estimator commit renders the frame again", !same(sao, darker));
  pass &= report("going back to g3d and intensity 1 gives the first image",
      same(first, back));
  return pass;
}

// Derived radius and orthographic camera /////////////////////////////////////

// 3 percent of the diagonal of 'world's bounds: the occlusion radius an
// 'ambientOcclusionRadius' of 0 stands for
float derivedRadius(anari::Device d, anari::World world)
{
  float3 bounds[2] = {};
  if (!anariGetProperty(d,
          world,
          "bounds",
          ANARI_FLOAT32_BOX3,
          bounds,
          sizeof(bounds),
          ANARI_WAIT))
    return 0.f;
  return 0.03f * linalg::length(bounds[1] - bounds[0]);
}

// Largest difference between the factors ('ambientOcclusion' mode grays) of
// the same pixel of 'a' and 'b'
float maxFactorDifference(const Image &a, const Image &b)
{
  float d = 0.f;
  for (size_t i = 0; i < a.color.size() && i < b.color.size(); i++)
    d = std::max(d, std::fabs(a.color[i].x - b.color[i].x));
  return d;
}

// Factors that differ by less than this are the same up to rounding
const float SAME_FACTOR = 1e-4f;

// With an 'ambientOcclusionRadius' of 0 (the default) the occlusion radius is
// 3 percent of the world bounds' diagonal: the room's factor is the one that
// radius gives when set explicitly, and the crease is darker than open floor.
// (The under-sphere probe, 0.14 from the sphere, is barely inside this
// radius, so it isn't checked.)
bool testDerivedRadius(anari::Device d, anari::World room)
{
  const float radius = derivedRadius(d, room);
  const Image derived = renderRoomFactor(d, room, 0.f);
  const Image explicitRadius = renderRoomFactor(d, room, radius);
  const float difference = maxFactorDifference(derived, explicitRadius);
  std::printf("  derived radius %f, largest factor difference %f\n",
      radius,
      difference);

  bool pass = report("derived radius, factor as with that radius explicitly",
      radius > 0.f && difference < SAME_FACTOR);
  float3 f;
  if (!roomFactors(derived, roomViewPixel(derived), f))
    return report("room probes see the floor", false);
  pass &=
      report("derived radius, crease darker than open floor", f.x < f.z - 0.1f);
  return pass;
}

// With 'estimator', the occlusion factor doesn't depend on the scene's units:
// the room, the camera's position and the occlusion radius scaled together by
// 10 or 0.1 give the room's factor at every pixel, up to float rounding,
// before and after the blur
bool testScaleInvariant(
    anari::Device d, anari::World room, const char *estimator)
{
  const char *const modes[] = {
      "ambientOcclusion.unblurred", "ambientOcclusion"};
  Image base[2];
  for (int m = 0; m < 2; m++) {
    base[m] = renderRoomFactor(
        d, room, ROOM_OCCLUSION_RADIUS, modes[m], withEstimator(d, estimator));
  }

  bool pass = true;
  for (float scale : {10.f, 0.1f}) {
    auto scaledRoom = makeWorld(d, roomSurfaces(d, 2.5f, scale));
    View view = ROOM_VIEW;
    view.position = scale * ROOM_VIEW.position;
    for (int m = 0; m < 2; m++) {
      const Image img =
          renderView(d, scaledRoom, view, ROOM_SIZE, [&](anari::Renderer r) {
            anari::setParameter(d, r, "mode", modes[m]);
            anari::setParameter(
                d, r, "ambientOcclusionRadius", scale * ROOM_OCCLUSION_RADIUS);
            withEstimator(d, estimator)(r);
          });
      const float difference = maxFactorDifference(base[m], img);
      std::printf("  scale %g, %s: largest factor difference %f\n",
          scale,
          modes[m],
          difference);
      pass &=
          report(withName(("room scaled by " + format(scale)
                              + " with its radius, same factor, " + modes[m])
                              .c_str(),
                     estimator)
                     .c_str(),
              img.objectId == base[m].objectId && difference < SAME_FACTOR);
    }
    anari::release(d, scaledRoom);
  }
  return pass;
}

// The derived radius follows the world's bounds: once a floor hidden behind
// the wall extends the room's bounds, a frame that rendered the room renders
// the factor the new bounds give, which a new frame with that radius set
// explicitly also gives
bool testDerivedRadiusFollowsBounds(anari::Device d)
{
  auto surfaces = roomSurfaces(d);
  auto world = anari::newObject<anari::World>(d);
  anari::setParameterArray1D(
      d, world, "surface", surfaces.data(), surfaces.size());
  anari::commitParameters(d, world);
  const float before = derivedRadius(d, world);

  auto camera = makeCamera(d, ROOM_VIEW);
  auto frame = makeFrame(d, world, camera, ROOM_SIZE, [&](anari::Renderer r) {
    anari::setParameter(d, r, "mode", "ambientOcclusion");
  });
  const Image first = renderImage(d, frame, ROOM_SIZE);

  const float3 behindWall[4] = {{-2.f, 0.f, -9.f},
      {2.f, 0.f, -9.f},
      {2.f, 0.f, -1.5f},
      {-2.f, 0.f, -1.5f}};
  surfaces.push_back(makeQuad(d, behindWall, FLOOR_ID));
  anari::setParameterArray1D(
      d, world, "surface", surfaces.data(), surfaces.size());
  anari::commitParameters(d, world);
  const Image extended = renderImage(d, frame, ROOM_SIZE);
  anari::release(d, frame);
  anari::release(d, camera);

  const float after = derivedRadius(d, world);
  const Image expected = renderRoomFactor(d, world, after);
  for (auto s : surfaces)
    anari::release(d, s);
  anari::release(d, world);

  const float difference = maxFactorDifference(extended, expected);
  std::printf(
      "  derived radius %f, then %f; largest factor difference %f, %f from "
      "the first render\n",
      before,
      after,
      difference,
      maxFactorDifference(first, extended));
  bool pass = report("derived radius, grows with the bounds", after > before);
  pass &= report("derived radius, recomputed when the bounds change",
      difference < SAME_FACTOR
          && maxFactorDifference(first, extended) > SAME_FACTOR);
  return pass;
}

// A world with no surfaces or volumes has no bounds to derive a radius from:
// with ambient occlusion on it renders without errors, the background
// everywhere in the 'ambientOcclusion' mode (there is no surface for a factor
// other than 1), and the default mode as with ambient occlusion off
bool testEmptyWorld(anari::Device d)
{
  auto world = anari::newObject<anari::World>(d);
  anari::commitParameters(d, world);
  const int errors = g_errors;

  const Image mode =
      renderView(d, world, ROOM_VIEW, ROOM_SIZE, [&](anari::Renderer r) {
        anari::setParameter(d, r, "mode", "ambientOcclusion");
      });
  const Image off =
      renderView(d, world, ROOM_VIEW, ROOM_SIZE, [&](anari::Renderer) {});
  const Image on =
      renderView(d, world, ROOM_VIEW, ROOM_SIZE, [&](anari::Renderer r) {
        anari::setParameter(d, r, "ambientOcclusion", true);
      });
  anari::release(d, world);

  bool background = true;
  for (size_t i = 0; i < mode.color.size(); i++) {
    background &= near(mode.color[i], BACKGROUND) && mode.depth[i] == MISS_DEPTH
        && mode.objectId[i] == MISS_ID;
  }
  bool pass = report("empty world, no errors", g_errors == errors);
  pass &= report(
      "empty world, ambientOcclusion mode shows the background", background);
  pass &= report("empty world, default mode unchanged by ambientOcclusion",
      sameImage(off, on));
  return pass;
}

// Idle passes /////////////////////////////////////////////////////////////////

// A constant half-opaque green volume over the room's floor, in front of its
// wall
anari::Volume makeRoomVolume(anari::Device d)
{
  return makeVolume(
      d, float3(-1.f, 0.f, -0.5f), float3(1.f, 1.f, 1.f), 0.5f, 9);
}

// The frame runs the occlusion passes only where their result is read. With
// 'ambientOcclusion' on, it allocates no occlusion buffers (so runs no
// passes) in modes that don't read the factor, for a world without surfaces
// (empty, or volumes only) and at intensity 0, also in the two ambient
// occlusion modes; and each such image is the one rendered with
// 'ambientOcclusion' off. The default mode on the room allocates them.
bool testIdlePassesSkipped(anari::Device d, anari::World room)
{
  auto empty = anari::newObject<anari::World>(d);
  anari::commitParameters(d, empty);
  auto volumeOnly = anari::newObject<anari::World>(d);
  auto volume = makeRoomVolume(d);
  anari::setParameterArray1D(d, volumeOnly, "volume", &volume, 1);
  anari::release(d, volume);
  anari::commitParameters(d, volumeOnly);

  struct Case
  {
    const char *name;
    anari::World world;
    const char *mode;
    float intensity;
  };
  const Case cases[] = {
      {"Ng mode", room, "Ng", 1.f},
      {"primitiveId mode", room, "primitiveId", 1.f},
      {"empty world", empty, "default", 1.f},
      {"empty world, ambientOcclusion mode", empty, "ambientOcclusion", 1.f},
      {"volume-only world", volumeOnly, "default", 1.f},
      {"volume-only world, ambientOcclusion mode",
          volumeOnly,
          "ambientOcclusion",
          1.f},
      {"intensity 0", room, "default", 0.f},
      {"intensity 0, ambientOcclusion mode", room, "ambientOcclusion", 0.f},
      {"intensity 0, ambientOcclusion.unblurred mode",
          room,
          "ambientOcclusion.unblurred",
          0.f},
  };

  auto renderCase = [&](const Case &c, bool on) {
    return renderRoomFactor(
        d, c.world, ROOM_OCCLUSION_RADIUS, c.mode, [&](anari::Renderer r) {
          anari::setParameter(d, r, "ambientOcclusion", on);
          anari::setParameter(d, r, "ambientOcclusionIntensity", c.intensity);
        });
  };

  bool pass = true;
  for (const auto &c : cases) {
    const Image off = renderCase(c, false);
    const int allocated = g_occlusionBuffersAllocated;
    const Image on = renderCase(c, true);
    pass &= report((std::string("idle passes skipped, ") + c.name).c_str(),
        g_occlusionBuffersAllocated == allocated);
    pass &= report(
        (std::string("idle passes skipped, same image on and off, ") + c.name)
            .c_str(),
        sameImage(off, on));
  }

  anari::release(d, empty);
  anari::release(d, volumeOnly);

  for (const char *mode :
      {"default", "ambientOcclusion", "ambientOcclusion.unblurred"}) {
    const int allocated = g_occlusionBuffersAllocated;
    renderRoomFactor(d, room, ROOM_OCCLUSION_RADIUS, mode, [&](auto r) {
      anari::setParameter(d, r, "ambientOcclusion", true);
    });
    pass &= report(
        (std::string("passes run on the room, ") + mode + " mode").c_str(),
        g_occlusionBuffersAllocated == allocated + 1);
  }
  return pass;
}

// On an existing frame with 'ambientOcclusion' on, changing the mode, the
// intensity or the world's contents starts and stops the passes: the frame
// allocates its occlusion buffers when their result is read again and frees
// them when it no longer is, and each render gives the image a fresh frame
// gives
bool testIdlePassesToggle(anari::Device d)
{
  auto world = anari::newObject<anari::World>(d);
  auto surfaces = roomSurfaces(d);
  auto setSurfaces = [&]() {
    anari::setParameterArray1D(
        d, world, "surface", surfaces.data(), surfaces.size());
  };
  setSurfaces();
  anari::commitParameters(d, world);
  auto volume = makeRoomVolume(d);

  auto renderer = anari::newObject<anari::Renderer>(d, "preview");
  anari::setParameter(d, renderer, "background", BACKGROUND);
  anari::setParameter(d, renderer, "ambientOcclusion", true);
  anari::setParameter(
      d, renderer, "ambientOcclusionRadius", ROOM_OCCLUSION_RADIUS);
  anari::commitParameters(d, renderer);
  auto frame = makeRoomFrame(d, world, renderer);
  const size_t n = size_t(ROOM_SIZE.x) * ROOM_SIZE.y;

  // the color a fresh frame renders of 'world' as it is now in 'mode', with
  // 'ambientOcclusion' as given
  auto fresh = [&](const char *mode, bool on) {
    return renderRoomFactor(d,
        world,
        ROOM_OCCLUSION_RADIUS,
        mode,
        [&](anari::Renderer r) {
          anari::setParameter(d, r, "ambientOcclusion", on);
        })
        .color;
  };
  auto same = [&](const std::vector<float4> &a, const std::vector<float4> &b) {
    return a.size() == n && b.size() == n
        && std::memcmp(a.data(), b.data(), n * sizeof(float4)) == 0;
  };

  bool pass = true;
  // renders the frame after 'change', checking that the render allocated
  // (+1), freed (-1) or left (0) the occlusion buffers and gives 'expected'
  auto step =
      [&](const char *name, int buffers, auto &&change, auto &&expected) {
        change();
        const int allocated = g_occlusionBuffersAllocated;
        const int freed = g_occlusionBuffersFreed;
        const auto color = renderRoomColor(d, frame);
        const int events = (g_occlusionBuffersAllocated - allocated)
            - (g_occlusionBuffersFreed - freed);
        pass &= report(
            (std::string("toggle passes, ") + name + ", buffers").c_str(),
            events == buffers && g_occlusionBuffersAllocated - allocated <= 1
                && g_occlusionBuffersFreed - freed <= 1);
        pass &=
            report((std::string("toggle passes, ") + name + ", image").c_str(),
                same(color, expected()));
      };
  auto setMode = [&](const char *mode) {
    return [=]() {
      anari::setParameter(d, renderer, "mode", mode);
      anari::commitParameters(d, renderer);
    };
  };
  auto setIntensity = [&](float intensity) {
    return [=]() {
      anari::setParameter(d, renderer, "ambientOcclusionIntensity", intensity);
      anari::commitParameters(d, renderer);
    };
  };
  auto commitWorld = [&](auto &&change) {
    return [&, change]() {
      change();
      anari::commitParameters(d, world);
    };
  };
  const auto removeSurfaces =
      commitWorld([&]() { anari::unsetParameter(d, world, "surface"); });
  const auto addVolume = commitWorld(
      [&]() { anari::setParameterArray1D(d, world, "volume", &volume, 1); });
  const auto addSurfaces = commitWorld(setSurfaces);
  const auto removeVolume =
      commitWorld([&]() { anari::unsetParameter(d, world, "volume"); });
  auto nothing = []() {};
  auto occluded = [&]() { return fresh("default", true); };
  auto unoccluded = [&]() { return fresh("default", false); };
  auto ng = [&]() { return fresh("Ng", false); };

  step("default mode", 1, nothing, occluded);
  step("Ng mode", -1, setMode("Ng"), ng);
  step("back to the default mode", 1, setMode("default"), occluded);
  step("intensity 0", -1, setIntensity(0.f), unoccluded);
  step("intensity 1", 1, setIntensity(1.f), occluded);
  step("world emptied", -1, removeSurfaces, unoccluded);
  step("volume added", 0, addVolume, unoccluded);
  step("surfaces added", 1, addSurfaces, occluded);
  step("volume removed", 0, removeVolume, occluded);

  // the passes still darken the crease after all that
  const auto color = renderRoomColor(d, frame);
  const uint2 px = pixelAt(ROOM_VIEW, ROOM_SIZE, CREASE.pos);
  const size_t crease = size_t(px.y) * ROOM_SIZE.x + px.x;
  pass &= report("toggle passes, crease occluded at the end",
      color[crease].x < unoccluded()[crease].x - 0.05f);

  anari::release(d, frame);
  anari::release(d, renderer);
  anari::release(d, volume);
  for (auto s : surfaces)
    anari::release(d, s);
  anari::release(d, world);
  return pass;
}

// On an existing frame of the room, the intermediate blur buffer is allocated
// only while the occlusion passes run with more than one blur pass: changing
// 'ambientOcclusionBlurPasses' between 1 and more allocates and frees it, and
// so do starting and stopping the passes with the mode while more than one is
// set. Each render gives the image a fresh frame gives.
bool testBlurBufferToggle(anari::Device d, anari::World room)
{
  auto renderer = anari::newObject<anari::Renderer>(d, "preview");
  anari::setParameter(d, renderer, "background", BACKGROUND);
  anari::setParameter(d, renderer, "ambientOcclusion", true);
  anari::setParameter(
      d, renderer, "ambientOcclusionRadius", ROOM_OCCLUSION_RADIUS);
  anari::commitParameters(d, renderer);
  auto frame = makeRoomFrame(d, room, renderer);
  const size_t n = size_t(ROOM_SIZE.x) * ROOM_SIZE.y;

  const char *mode = "default";
  int32_t passes = 1;
  bool pass = true;
  // sets 'newMode' and 'newPasses' on the renderer and renders the frame,
  // checking that the render allocated (+1), freed (-1) or left (0) the
  // occlusion buffers and the intermediate blur buffer as 'buffers' and
  // 'blurBuffer' say
  auto step =
      [&](const char *newMode, int32_t newPasses, int buffers, int blurBuffer) {
        mode = newMode;
        passes = newPasses;
        anari::setParameter(d, renderer, "mode", mode);
        anari::setParameter(d, renderer, "ambientOcclusionBlurPasses", passes);
        anari::commitParameters(d, renderer);
        const int allocatedBefore = g_occlusionBuffersAllocated;
        const int freedBefore = g_occlusionBuffersFreed;
        const int blurAllocatedBefore = g_blurBufferAllocated;
        const int blurFreedBefore = g_blurBufferFreed;
        const auto color = renderRoomColor(d, frame);
        const int allocated = g_occlusionBuffersAllocated - allocatedBefore;
        const int freed = g_occlusionBuffersFreed - freedBefore;
        const int blurAllocated = g_blurBufferAllocated - blurAllocatedBefore;
        const int blurFreed = g_blurBufferFreed - blurFreedBefore;
        const auto fresh = renderRoomFactor(
            d, room, ROOM_OCCLUSION_RADIUS, mode, [&](anari::Renderer r) {
              anari::setParameter(d, r, "ambientOcclusion", true);
              anari::setParameter(d, r, "ambientOcclusionBlurPasses", passes);
            }).color;
        const std::string name = std::string("blur buffer, ") + mode
            + " mode, ambientOcclusionBlurPasses " + std::to_string(passes);
        pass &= report((name + ", buffers").c_str(),
            allocated - freed == buffers
                && blurAllocated - blurFreed == blurBuffer && allocated <= 1
                && freed <= 1 && blurAllocated <= 1 && blurFreed <= 1);
        pass &= report((name + ", image").c_str(),
            color.size() == n && fresh.size() == n
                && std::memcmp(color.data(), fresh.data(), n * sizeof(float4))
                    == 0);
      };

  step("default", 1, 1, 0);
  step("default", 3, 0, 1);
  step("default", 5, 0, 0);
  step("default", 1, 0, -1);
  step("ambientOcclusion", 2, 0, 1);
  step("Ng", 2, -1, -1);
  step("Ng", 4, 0, 0);
  step("ambientOcclusion.unblurred", 4, 1, 1);
  step("ambientOcclusion.unblurred", 1, 0, -1);

  anari::release(d, frame);
  anari::release(d, renderer);
  return pass;
}

// Height (and width) of the orthographic view along ROOM_VIEW's axis, which
// shows the whole room
const float ROOM_ORTHO_HEIGHT = 4.f;

anari::Camera makeOrthographicCamera(
    anari::Device d, const View &v, float height)
{
  auto camera = anari::newObject<anari::Camera>(d, "orthographic");
  anari::setParameter(d, camera, "position", v.position);
  anari::setParameter(d, camera, "direction", v.direction);
  anari::setParameter(d, camera, "up", v.up);
  anari::setParameter(d, camera, "height", height);
  anari::setParameter(d, camera, "aspect", 1.f);
  anari::commitParameters(d, camera);
  return camera;
}

// Pixel of a 'size' image that an orthographic camera with the position and
// axes of 'v' and 'height' sees world-space 'p' in
uint2 orthographicPixelAt(const View &v, float height, uint2 size, float3 p)
{
  return pixelAt(size, viewSpace(v, p).xy() / height);
}

// Seen by an orthographic camera, the crease and the floor under the sphere
// are darker than open floor with the room's radius, and the crease also with
// the derived one. Its rays are parallel, so the occlusion radius covers the
// same number of pixels at every distance: the same camera moved back along
// its axis gives the same factor.
bool testOrthographicRoom(anari::Device d, anari::World room)
{
  auto pixel = [&](float3 p) {
    return orthographicPixelAt(ROOM_VIEW, ROOM_ORTHO_HEIGHT, ROOM_SIZE, p);
  };
  auto renderFactor = [&](const View &view, float radius) {
    auto camera = makeOrthographicCamera(d, view, ROOM_ORTHO_HEIGHT);
    Image img = render(d, room, camera, ROOM_SIZE, [&](anari::Renderer r) {
      anari::setParameter(d, r, "mode", "ambientOcclusion");
      anari::setParameter(d, r, "ambientOcclusionRadius", radius);
    });
    anari::release(d, camera);
    return img;
  };
  View farView = ROOM_VIEW;
  farView.position = ROOM_VIEW.position - 10.f * ROOM_VIEW.direction;

  const Image img = renderFactor(ROOM_VIEW, ROOM_OCCLUSION_RADIUS);
  const Image far = renderFactor(farView, ROOM_OCCLUSION_RADIUS);
  const Image derived = renderFactor(ROOM_VIEW, 0.f);
  const float difference = maxFactorDifference(img, far);
  std::printf(
      "  largest factor difference from 10 farther back %f\n", difference);

  float3 f, fDerived;
  if (!roomFactors(img, pixel, f) || !roomFactors(derived, pixel, fDerived))
    return report("orthographic, room probes see the floor", false);
  bool pass = report("orthographic, open floor near 1", f.z > 0.95f);
  pass &=
      report("orthographic, crease darker than open floor", f.x < f.z - 0.1f);
  pass &= report(
      "orthographic, under sphere darker than open floor", f.y < f.z - 0.1f);
  pass &= report("orthographic, derived radius, crease darker than open floor",
      fDerived.x < fDerived.z - 0.1f);
  pass &= report("orthographic, factor independent of the camera's distance",
      img.objectId == far.objectId && difference < 1e-3f);
  return pass;
}

} // namespace

int main()
{
  auto library = anari::loadLibrary("halcyon", statusFunc);
  if (!library) {
    std::fprintf(stderr, "failed to load halcyon library\n");
    return 1;
  }
  auto d = anari::newDevice(library, "default");

  bool pass = testIntrospection(d);

  auto world = makeWorld(d);
  pass &= testMode(d, world);
  pass &= testFactorOneLeavesDefaultMode(d, world);
  pass &= testOccludedBehindVolume(d, world);
  anari::release(d, world);

  auto room = makeRoom(d);
  for (const char *estimator : ESTIMATORS) {
    pass &= testRoomOccluded(d, room, estimator);
    pass &= testRoomOccluded(d, room, estimator, 3);
    pass &= testRoomFloorSmooth(d, room, estimator);
    pass &= testBlurCentred(d, room, estimator);
    pass &= testBlurCentred(d, room, estimator, 2.f);
    pass &= testIntensity(d, room, estimator);
  }
  for (int passes : {2, 3, 5})
    pass &= testMultiPassBlur(d, room, passes);
  pass &= testBlurPassesClamped(d, room);
  pass &= testNoBandsNearContact(d);
  pass &= testRoomDefaultMode(d, room);
  pass &= testDefaultEstimatorAndIntensity(d, room);
  pass &= testUnknownEstimator(d, room);
  pass &= testParametersRerender(d, room);
  pass &= testNoHalo(d, room);
  pass &= testNoHalo(d, room, 3);
  pass &= testDeterministic(d, room);
  pass &= testRoomUnchangedWhenOff(d, room);
  pass &= testToggleOnExistingFrame(d, room);
  pass &= testNoStaleHits(d, room);
  pass &= testClearVolume(d, room);
  pass &= testOccludedBehindCutOut(d, room);
  pass &= testDerivedRadius(d, room);
  for (const char *estimator : ESTIMATORS)
    pass &= testScaleInvariant(d, room, estimator);
  pass &= testOrthographicRoom(d, room);
  pass &= testIdlePassesSkipped(d, room);
  pass &= testBlurBufferToggle(d, room);
  anari::release(d, room);
  pass &= testDerivedRadiusFollowsBounds(d);
  pass &= testEmptyWorld(d);
  pass &= testIdlePassesToggle(d);
  for (const char *estimator : ESTIMATORS)
    pass &= testLonePlane(d, estimator);
  pass &= testNormalBehindVolumeAndCutOut(d);
  pass &= testShadingNormalPlane(d);
  pass &= testOnlyBlendLayerOccluded(d);

  anari::release(d, d);
  anari::unloadLibrary(library);

  if (g_errors != 0) {
    std::printf("FAIL %d device errors\n", g_errors);
    pass = false;
  }
  return pass ? 0 : 1;
}
