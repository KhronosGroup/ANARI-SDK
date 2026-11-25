// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Renders a scene with known surface, volume, primitive and instance IDs and
// checks the values mapped from the frame's primitiveId/objectId/instanceId
// channels. Further scenes check the C++ side that fills the ISPC ids: a
// surface 'id' changed after a render, UINT64 'primitive.id' values above
// UINT32_MAX, 'primitive.id' shorter than the primitive count (per geometry
// subtype), and instance 'id' arrays that are too short or not UINT32. Those
// warn and fall back; the warnings are asserted.

// anari
#define ANARI_LINALG_DEFINITIONS
#include <anari/anari_cpp/ext/linalg.h>
#include <anari/anari_cpp.hpp>
// std
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <initializer_list>
#include <mutex>
#include <string>
#include <vector>

namespace {

using namespace anari::math;

constexpr uint32_t NONE = ~0u;
constexpr uint2 IMAGE_SIZE = {32, 32};

// Device messages (reported on the render worker too); errors fail the test,
// warnings are checked where expected
struct Messages
{
  void add(ANARIStatusSeverity severity, const char *message)
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (severity <= ANARI_SEVERITY_ERROR)
      errors.push_back(message);
    else if (severity == ANARI_SEVERITY_WARNING)
      warnings.push_back(message);
  }

  size_t numErrors()
  {
    std::lock_guard<std::mutex> lock(mutex);
    return errors.size();
  }

  size_t numWarnings()
  {
    std::lock_guard<std::mutex> lock(mutex);
    return warnings.size();
  }

  // whether a warning since the first 'from' ones contains 'text'
  bool warnedSince(size_t from, const char *text)
  {
    std::lock_guard<std::mutex> lock(mutex);
    return std::any_of(warnings.begin() + from,
        warnings.end(),
        [&](const std::string &w) { return w.find(text) != w.npos; });
  }

 private:
  std::mutex mutex;
  std::vector<std::string> errors;
  std::vector<std::string> warnings;
} g_messages;

bool check(bool ok, const std::string &name)
{
  std::printf("%s %s\n", ok ? "PASS" : "FAIL", name.c_str());
  return ok;
}

// Two triangles covering a square of half-width 0.3 around 'center':
// triangle 0 is the lower-right half, triangle 1 the upper-left half
anari::Geometry makeQuad(anari::Device d, float3 center)
{
  const std::vector<float3> verts = {center + float3(-0.3f, -0.3f, 0.f),
      center + float3(0.3f, -0.3f, 0.f),
      center + float3(0.3f, 0.3f, 0.f),
      center + float3(-0.3f, 0.3f, 0.f)};
  const std::vector<uint3> indices = {{0, 1, 2}, {0, 2, 3}};

  auto geom = anari::newObject<anari::Geometry>(d, "triangle");
  anari::setParameterArray1D(
      d, geom, "vertex.position", verts.data(), verts.size());
  anari::setParameterArray1D(
      d, geom, "primitive.index", indices.data(), indices.size());
  return geom;
}

// Commits 'geom' and wraps it in a surface with the given 'id'
anari::Surface makeSurface(anari::Device d, anari::Geometry geom, uint32_t id)
{
  anari::commitParameters(d, geom);

  auto mat = anari::newObject<anari::Material>(d, "matte");
  anari::setParameter(d, mat, "color", float3(0.8f));
  anari::commitParameters(d, mat);

  auto surface = anari::newObject<anari::Surface>(d);
  anari::setAndReleaseParameter(d, surface, "geometry", geom);
  anari::setAndReleaseParameter(d, surface, "material", mat);
  anari::setParameter(d, surface, "id", id);
  anari::commitParameters(d, surface);
  return surface;
}

// transferFunction1D volume with the given 'id' over the box [lower, upper]
// (a 2^3 field)
anari::Volume makeVolume(
    anari::Device d, float3 lower, float3 upper, uint32_t id)
{
  const std::vector<float> voxels(8, 0.5f);
  auto field = anari::newObject<anari::SpatialField>(d, "structuredRegular");
  anari::setParameterArray3D(d, field, "data", voxels.data(), 2, 2, 2);
  anari::setParameter(d, field, "origin", lower);
  anari::setParameter(d, field, "spacing", upper - lower);
  anari::commitParameters(d, field);

  auto vol = anari::newObject<anari::Volume>(d, "transferFunction1D");
  anari::setAndReleaseParameter(d, vol, "value", field);
  anari::setParameter(d, vol, "id", id);
  anari::commitParameters(d, vol);
  return vol;
}

// World with:
//  - an instance transform array placing a quad (surface id 7, UINT32
//    'primitive.id' {100, 101}) top-left and top-right, instance ids {20, 21},
//    with a small volume (id 41) in front of each quad's center
//  - a world-level quad at the bottom (surface id 8, UINT64 'primitive.id'
//    {200, 201}); the world's id 30 is the instance id world-level
//    surfaces and volumes report
//  - a world-level volume (id 40) on the left between the quads, and one
//    (id 42) behind the bottom quad, which it hides
// Volume hits report primitive id 0, the volume's id and its instance's id.
anari::World makeWorld(anari::Device d)
{
  auto instGeom = makeQuad(d, float3(0.f));
  const std::vector<uint32_t> instPrimIds = {100, 101};
  anari::setParameterArray1D(
      d, instGeom, "primitive.id", instPrimIds.data(), instPrimIds.size());
  auto instSurface = makeSurface(d, instGeom, 7);

  auto instVolume =
      makeVolume(d, float3(-0.1f, -0.1f, 0.05f), float3(0.1f, 0.1f, 0.2f), 41);

  auto group = anari::newObject<anari::Group>(d);
  anari::setParameterArray1D(d, group, "surface", &instSurface, 1);
  anari::setParameterArray1D(d, group, "volume", &instVolume, 1);
  anari::commitParameters(d, group);
  anari::release(d, instSurface);
  anari::release(d, instVolume);

  const std::vector<mat4> xfms = {
      linalg::translation_matrix(float3(-0.5f, 0.5f, 0.f)),
      linalg::translation_matrix(float3(0.5f, 0.5f, 0.f))};
  const std::vector<uint32_t> instIds = {20, 21};

  auto inst = anari::newObject<anari::Instance>(d, "transform");
  anari::setAndReleaseParameter(d, inst, "group", group);
  anari::setParameterArray1D(d, inst, "transform", xfms.data(), xfms.size());
  anari::setParameterArray1D(d, inst, "id", instIds.data(), instIds.size());
  anari::commitParameters(d, inst);

  auto worldGeom = makeQuad(d, float3(0.f, -0.5f, 0.f));
  const std::vector<uint64_t> worldPrimIds = {200, 201};
  anari::setParameterArray1D(
      d, worldGeom, "primitive.id", worldPrimIds.data(), worldPrimIds.size());
  auto worldSurface = makeSurface(d, worldGeom, 8);

  const std::vector<anari::Volume> worldVolumes = {
      makeVolume(
          d, float3(-0.9f, -0.15f, -0.1f), float3(-0.5f, 0.15f, 0.1f), 40),
      makeVolume(
          d, float3(-0.1f, -0.6f, -0.5f), float3(0.1f, -0.4f, -0.2f), 42)};

  auto world = anari::newObject<anari::World>(d);
  anari::setParameterArray1D(d, world, "instance", &inst, 1);
  anari::setParameterArray1D(d, world, "surface", &worldSurface, 1);
  anari::setParameterArray1D(
      d, world, "volume", worldVolumes.data(), worldVolumes.size());
  anari::setParameter(d, world, "id", uint32_t(30));
  anari::commitParameters(d, world);
  anari::release(d, inst);
  anari::release(d, worldSurface);
  for (auto v : worldVolumes)
    anari::release(d, v);
  return world;
}

struct Probe
{
  const char *name;
  float2 pos; // world-space xy the camera sees at z = 0
  uint32_t primId;
  uint32_t objId;
  uint32_t instId;
};

const std::vector<Probe> PROBES = {
    {"miss", {0.9f, 0.9f}, NONE, NONE, NONE},
    {"instance 0, tri 0", {-0.35f, 0.35f}, 100, 7, 20},
    {"instance 0, tri 1", {-0.65f, 0.65f}, 101, 7, 20},
    {"instance 1, tri 0", {0.65f, 0.35f}, 100, 7, 21},
    {"instance 1, tri 1", {0.35f, 0.65f}, 101, 7, 21},
    {"world surface, tri 0", {0.15f, -0.65f}, 200, 8, 30},
    {"world surface, tri 1", {-0.15f, -0.35f}, 201, 8, 30},
    {"instance 0, volume", {-0.5f, 0.5f}, 0, 41, 20},
    {"instance 1, volume", {0.5f, 0.5f}, 0, 41, 21},
    {"world volume", {-0.7f, 0.f}, 0, 40, 30},
    // helide reports the volume behind the surface; halcyon clips volumes to
    // the surface hit
    {"world surface over a volume", {0.05f, -0.55f}, 200, 8, 30},
};

// Orthographic camera looking down -z at the [-1, 1]^2 square
anari::Camera makeCamera(anari::Device d)
{
  auto camera = anari::newObject<anari::Camera>(d, "orthographic");
  anari::setParameter(d, camera, "position", float3(0.f, 0.f, 1.f));
  anari::setParameter(d, camera, "direction", float3(0.f, 0.f, -1.f));
  anari::setParameter(d, camera, "up", float3(0.f, 1.f, 0.f));
  anari::setParameter(d, camera, "height", 2.f);
  anari::setParameter(d, camera, "aspect", 1.f);
  anari::commitParameters(d, camera);
  return camera;
}

// Renders 'world' into a new frame with a color channel plus the given
// id channels enabled as ANARI_UINT32
anari::Frame renderFrame(anari::Device d,
    anari::World world,
    std::initializer_list<const char *> idChannels)
{
  auto renderer = anari::newObject<anari::Renderer>(d, "preview");
  anari::commitParameters(d, renderer);

  auto frame = anari::newObject<anari::Frame>(d);
  anari::setParameter(d, frame, "size", IMAGE_SIZE);
  anari::setParameter(d, frame, "channel.color", ANARI_UFIXED8_RGBA_SRGB);
  for (const char *channel : idChannels)
    anari::setParameter(d, frame, channel, ANARI_UINT32);
  anari::setAndReleaseParameter(d, frame, "camera", makeCamera(d));
  anari::setAndReleaseParameter(d, frame, "renderer", renderer);
  anari::setParameter(d, frame, "world", world);
  anari::commitParameters(d, frame);

  anari::render(d, frame);
  anari::wait(d, frame);
  return frame;
}

size_t pixelIndex(float2 pos)
{
  const auto x = uint32_t((pos.x + 1.f) / 2.f * IMAGE_SIZE.x);
  const auto y = uint32_t((pos.y + 1.f) / 2.f * IMAGE_SIZE.y);
  return size_t(y) * IMAGE_SIZE.x + x;
}

// Maps 'channel' and returns its pixels, or an empty vector (after reporting
// why) if it isn't a full-size UINT32 channel
std::vector<uint32_t> mapIds(
    anari::Device d, anari::Frame frame, const char *channel)
{
  auto fb = anari::map<uint32_t>(d, frame, channel);
  std::vector<uint32_t> ids;
  if (!fb.data || fb.pixelType != ANARI_UINT32 || fb.width != IMAGE_SIZE.x
      || fb.height != IMAGE_SIZE.y) {
    std::printf("FAIL %s: mapped %p, %s, %ux%u\n",
        channel,
        (const void *)fb.data,
        anari::toString(fb.pixelType),
        fb.width,
        fb.height);
  } else {
    ids.assign(fb.data, fb.data + fb.width * fb.height);
  }
  anari::unmap(d, frame, channel);
  return ids;
}

bool checkIds(const char *channel,
    const std::vector<uint32_t> &ids,
    uint32_t Probe::*expected)
{
  if (ids.empty())
    return false;

  bool pass = true;
  for (auto &p : PROBES) {
    const uint32_t got = ids[pixelIndex(p.pos)];
    if (got != p.*expected) {
      std::printf("FAIL %s at %s: got %u, expected %u\n",
          channel,
          p.name,
          got,
          p.*expected);
      pass = false;
    }
  }
  std::printf("%s %s\n", pass ? "PASS" : "FAIL", channel);
  return pass;
}

bool testIdChannels(anari::Device d, anari::World world)
{
  auto frame = renderFrame(d,
      world,
      {"channel.primitiveId", "channel.objectId", "channel.instanceId"});

  bool pass = true;
  pass &= checkIds("channel.primitiveId",
      mapIds(d, frame, "channel.primitiveId"),
      &Probe::primId);
  pass &= checkIds(
      "channel.objectId", mapIds(d, frame, "channel.objectId"), &Probe::objId);
  pass &= checkIds("channel.instanceId",
      mapIds(d, frame, "channel.instanceId"),
      &Probe::instId);

  anari::release(d, frame);
  return pass;
}

// Channels that weren't requested must not be mappable
bool testDisabledChannels(anari::Device d, anari::World world)
{
  auto frame = renderFrame(d, world, {"channel.objectId"});

  bool pass = true;
  for (const char *channel : {"channel.primitiveId", "channel.instanceId"}) {
    auto fb = anari::map<uint32_t>(d, frame, channel);
    if (fb.data || fb.pixelType != ANARI_UNKNOWN) {
      std::printf("FAIL %s: mappable without being requested\n", channel);
      pass = false;
    }
    anari::unmap(d, frame, channel);
  }
  pass &= checkIds(
      "channel.objectId", mapIds(d, frame, "channel.objectId"), &Probe::objId);

  // Disabling a channel again unmaps it
  anari::setParameter(d, frame, "channel.objectId", ANARI_UNKNOWN);
  anari::commitParameters(d, frame);
  anari::render(d, frame);
  anari::wait(d, frame);
  auto fb = anari::map<uint32_t>(d, frame, "channel.objectId");
  if (fb.data) {
    std::printf("FAIL channel.objectId: still mappable after disabling\n");
    pass = false;
  }
  anari::unmap(d, frame, "channel.objectId");

  std::printf("%s disabled channels\n", pass ? "PASS" : "FAIL");
  anari::release(d, frame);
  return pass;
}

// Surface and world ids of the scenes below the base world
constexpr uint32_t SURFACE_ID = 7;
constexpr uint32_t WORLD_ID = 30;

// The primitive, object and instance ids of one pixel
struct PixelIds
{
  uint32_t primId;
  uint32_t objId;
  uint32_t instId;
};

// Renders 'world' with all three id channels and returns the ids at each of
// 'positions' (all ~0u if a channel can't be mapped)
std::vector<PixelIds> renderIds(
    anari::Device d, anari::World world, const std::vector<float2> &positions)
{
  auto frame = renderFrame(d,
      world,
      {"channel.primitiveId", "channel.objectId", "channel.instanceId"});
  const auto primIds = mapIds(d, frame, "channel.primitiveId");
  const auto objIds = mapIds(d, frame, "channel.objectId");
  const auto instIds = mapIds(d, frame, "channel.instanceId");
  anari::release(d, frame);

  std::vector<PixelIds> retval(positions.size(), {NONE, NONE, NONE});
  if (primIds.empty() || objIds.empty() || instIds.empty())
    return retval;
  for (size_t i = 0; i < positions.size(); i++) {
    const size_t p = pixelIndex(positions[i]);
    retval[i] = {primIds[p], objIds[p], instIds[p]};
  }
  return retval;
}

bool checkPixel(const std::string &name, PixelIds got, PixelIds expected)
{
  const bool ok = got.primId == expected.primId && got.objId == expected.objId
      && got.instId == expected.instId;
  if (!ok) {
    std::printf("FAIL %s: got ids (%u, %u, %u), expected (%u, %u, %u)\n",
        name.c_str(),
        got.primId,
        got.objId,
        got.instId,
        expected.primId,
        expected.objId,
        expected.instId);
  }
  return check(ok, name);
}

// World holding 'surface' directly, reporting instance id WORLD_ID
anari::World makeSurfaceWorld(anari::Device d, anari::Surface surface)
{
  auto world = anari::newObject<anari::World>(d);
  anari::setParameterArray1D(d, world, "surface", &surface, 1);
  anari::setParameter(d, world, "id", WORLD_ID);
  anari::commitParameters(d, world);
  return world;
}

// Surface 'id' set, changed and unset after renders reaches objectId
bool testSurfaceIdChanges(anari::Device d)
{
  auto surface = makeSurface(d, makeQuad(d, float3(0.f)), SURFACE_ID);
  auto world = makeSurfaceWorld(d, surface);
  const std::vector<float2> center = {{0.15f, -0.15f}};

  bool pass = checkPixel("surface id set",
      renderIds(d, world, center)[0],
      {0, SURFACE_ID, WORLD_ID});

  anari::setParameter(d, surface, "id", uint32_t(9));
  anari::commitParameters(d, surface);
  pass &= checkPixel("surface id changed to 9",
      renderIds(d, world, center)[0],
      {0, 9, WORLD_ID});

  anari::unsetParameter(d, surface, "id");
  anari::commitParameters(d, surface);
  pass &= checkPixel(
      "surface id unset", renderIds(d, world, center)[0], {0, NONE, WORLD_ID});

  anari::release(d, surface);
  anari::release(d, world);
  return pass;
}

// UINT64 'primitive.id' values above UINT32_MAX keep their low 32 bits, with
// a warning
bool testUint64PrimitiveIdTruncation(anari::Device d)
{
  auto geom = makeQuad(d, float3(0.f));
  const std::vector<uint64_t> ids = {(uint64_t(1) << 32) | 5, 6};
  anari::setParameterArray1D(d, geom, "primitive.id", ids.data(), ids.size());
  auto surface = makeSurface(d, geom, SURFACE_ID);
  auto world = makeSurfaceWorld(d, surface);
  anari::release(d, surface);

  const size_t warnings = g_messages.numWarnings();
  const auto got = renderIds(d, world, {{0.15f, -0.15f}, {-0.15f, 0.15f}});
  bool pass = checkPixel("UINT64 primitive.id above UINT32_MAX: truncated",
      got[0],
      {5, SURFACE_ID, WORLD_ID});
  pass &= checkPixel(
      "UINT64 primitive.id in range", got[1], {6, SURFACE_ID, WORLD_ID});
  pass &= check(g_messages.warnedSince(warnings,
                    "'primitive.id' values above UINT32_MAX are truncated"),
      "UINT64 primitive.id above UINT32_MAX: warning");

  anari::release(d, world);
  return pass;
}

// A geometry subtype with two primitives, one centred at (-0.5, 0) and one
// at (0.5, 0)
struct SubtypeScene
{
  const char *subtype;
  std::function<void(anari::Device, anari::Geometry)> setParameters;
  // in test names after the subtype (the curve's basis), if set
  const char *variant = nullptr;
};

// Two vertical cubic curve segments of 'basis' at x = -0.5 and x = 0.5, each
// over four collinear control points spanning y in [-0.3, 0.3]
void setCubicSegments(anari::Device d, anari::Geometry g, const char *basis)
{
  std::vector<float3> v;
  for (float x : {-0.5f, 0.5f}) {
    for (float y : {-0.9f, -0.3f, 0.3f, 0.9f})
      v.push_back({x, y, 0.f});
  }
  anari::setParameter(d, g, "basis", basis);
  anari::setParameterArray1D(d, g, "vertex.position", v.data(), v.size());
  const std::vector<uint32_t> index = {0, 4};
  anari::setParameterArray1D(
      d, g, "primitive.index", index.data(), index.size());
  anari::setParameter(d, g, "radius", 0.1f);
}

const std::vector<SubtypeScene> &subtypeScenes()
{
  // two vertical segments at x = -0.5 and x = 0.5
  static const std::vector<float3> SEGMENTS = {{-0.5f, -0.3f, 0.f},
      {-0.5f, 0.3f, 0.f},
      {0.5f, -0.3f, 0.f},
      {0.5f, 0.3f, 0.f}};
  static const std::vector<SubtypeScene> retval = {
      {"triangle",
          [](anari::Device d, anari::Geometry g) {
            const std::vector<float3> v = {{-0.8f, -0.3f, 0.f},
                {-0.2f, -0.3f, 0.f},
                {-0.5f, 0.3f, 0.f},
                {0.2f, -0.3f, 0.f},
                {0.8f, -0.3f, 0.f},
                {0.5f, 0.3f, 0.f}};
            anari::setParameterArray1D(
                d, g, "vertex.position", v.data(), v.size());
          }},
      {"quad",
          [](anari::Device d, anari::Geometry g) {
            std::vector<float3> v;
            for (float x : {-0.5f, 0.5f}) {
              v.push_back({x - 0.2f, -0.2f, 0.f});
              v.push_back({x + 0.2f, -0.2f, 0.f});
              v.push_back({x + 0.2f, 0.2f, 0.f});
              v.push_back({x - 0.2f, 0.2f, 0.f});
            }
            anari::setParameterArray1D(
                d, g, "vertex.position", v.data(), v.size());
          }},
      {"sphere",
          [](anari::Device d, anari::Geometry g) {
            const std::vector<float3> v = {{-0.5f, 0.f, 0.f}, {0.5f, 0.f, 0.f}};
            anari::setParameterArray1D(
                d, g, "vertex.position", v.data(), v.size());
            anari::setParameter(d, g, "radius", 0.2f);
          }},
      {"curve",
          [](anari::Device d, anari::Geometry g) {
            anari::setParameterArray1D(
                d, g, "vertex.position", SEGMENTS.data(), SEGMENTS.size());
            const std::vector<uint32_t> index = {0, 2};
            anari::setParameterArray1D(
                d, g, "primitive.index", index.data(), index.size());
            anari::setParameter(d, g, "radius", 0.1f);
          }},
      {"curve",
          [](anari::Device d, anari::Geometry g) {
            setCubicSegments(d, g, "bspline");
          },
          "bspline"},
      {"curve",
          [](anari::Device d, anari::Geometry g) {
            setCubicSegments(d, g, "catmullRom");
          },
          "catmullRom"},
      {"cylinder",
          [](anari::Device d, anari::Geometry g) {
            anari::setParameterArray1D(
                d, g, "vertex.position", SEGMENTS.data(), SEGMENTS.size());
            anari::setParameter(d, g, "radius", 0.1f);
          }},
      {"cone",
          [](anari::Device d, anari::Geometry g) {
            anari::setParameterArray1D(
                d, g, "vertex.position", SEGMENTS.data(), SEGMENTS.size());
            const std::vector<float> radii(4, 0.1f);
            anari::setParameterArray1D(
                d, g, "vertex.radius", radii.data(), radii.size());
          }},
  };
  return retval;
}

// 'primitive.id' shorter than the primitive count: the primitives past its
// end report their primitive index, with a warning
bool testShortPrimitiveId(anari::Device d, const SubtypeScene &scene)
{
  const std::string name = std::string(scene.subtype)
      + (scene.variant ? std::string(" ") + scene.variant : std::string())
      + " with a short primitive.id";

  auto geom = anari::newObject<anari::Geometry>(d, scene.subtype);
  scene.setParameters(d, geom);
  const uint32_t id = 100;
  anari::setParameterArray1D(d, geom, "primitive.id", &id, 1);
  auto surface = makeSurface(d, geom, SURFACE_ID);
  auto world = makeSurfaceWorld(d, surface);
  anari::release(d, surface);

  const size_t warnings = g_messages.numWarnings();
  const auto got = renderIds(d, world, {{-0.5f, 0.f}, {0.5f, 0.f}});
  bool pass = checkPixel(
      name + ": primitive 0 id", got[0], {100, SURFACE_ID, WORLD_ID});
  pass &= checkPixel(
      name + ": primitive 1 index", got[1], {1, SURFACE_ID, WORLD_ID});
  pass &=
      check(g_messages.warnedSince(warnings, "'primitive.id' has 1 elements"),
          name + ": warning");

  anari::release(d, world);
  return pass;
}

// An instance of a quad (surface SURFACE_ID) at (-0.5, 0) and (0.5, 0), whose
// 'id' array is set by 'setIds'; returns the ids at both quads' centres
std::vector<PixelIds> renderInstanceIds(
    anari::Device d, const std::function<void(anari::Instance)> &setIds)
{
  auto geom = makeQuad(d, float3(0.f));
  auto surface = makeSurface(d, geom, SURFACE_ID);
  auto group = anari::newObject<anari::Group>(d);
  anari::setParameterArray1D(d, group, "surface", &surface, 1);
  anari::commitParameters(d, group);
  anari::release(d, surface);

  const std::vector<mat4> xfms = {
      linalg::translation_matrix(float3(-0.5f, 0.f, 0.f)),
      linalg::translation_matrix(float3(0.5f, 0.f, 0.f))};
  auto inst = anari::newObject<anari::Instance>(d, "transform");
  anari::setAndReleaseParameter(d, inst, "group", group);
  anari::setParameterArray1D(d, inst, "transform", xfms.data(), xfms.size());
  setIds(inst);
  anari::commitParameters(d, inst);

  auto world = anari::newObject<anari::World>(d);
  anari::setParameterArray1D(d, world, "instance", &inst, 1);
  anari::commitParameters(d, world);
  anari::release(d, inst);

  auto retval = renderIds(d, world, {{-0.35f, -0.15f}, {0.65f, -0.15f}});
  anari::release(d, world);
  return retval;
}

// Instance 'id' arrays shorter than 'transform' or not UINT32 are dropped
// with a warning; every transform then reports the uniform 'id', which is
// unset (the array took its name), so ~0u. The base world covers a valid
// array, so ~0u here isn't what a broken id path would give anyway.
bool testInstanceIdArrays(anari::Device d)
{
  struct Case
  {
    const char *name;
    std::function<void(anari::Instance)> setIds;
    const char *warning;
  };
  const std::vector<Case> cases = {
      {"short instance id array",
          [&](anari::Instance inst) {
            const uint32_t id = 20;
            anari::setParameterArray1D(d, inst, "id", &id, 1);
          },
          "'id' array has 1 elements, but 'transform'"},
      {"UINT64 instance id array",
          [&](anari::Instance inst) {
            const std::vector<uint64_t> ids = {20, 21};
            anari::setParameterArray1D(d, inst, "id", ids.data(), ids.size());
          },
          "'id' array elements are ANARI_UINT64"},
  };

  bool pass = true;
  for (const auto &c : cases) {
    const std::string name = c.name;
    const size_t warnings = g_messages.numWarnings();
    const auto got = renderInstanceIds(d, c.setIds);
    pass &= checkPixel(name + ": instance 0", got[0], {0, SURFACE_ID, NONE});
    pass &= checkPixel(name + ": instance 1", got[1], {0, SURFACE_ID, NONE});
    pass &=
        check(g_messages.warnedSince(warnings, c.warning), name + ": warning");
  }
  return pass;
}

void statusFunc(const void *,
    ANARIDevice,
    ANARIObject,
    ANARIDataType,
    ANARIStatusSeverity severity,
    ANARIStatusCode,
    const char *message)
{
  if (severity <= ANARI_SEVERITY_WARNING) {
    std::fprintf(stderr,
        "[ANARI] %s: %s\n",
        severity <= ANARI_SEVERITY_ERROR ? "ERROR" : "WARNING",
        message);
  }
  g_messages.add(severity, message);
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

  auto world = makeWorld(d);
  bool pass = true;
  pass &= testIdChannels(d, world);
  pass &= testDisabledChannels(d, world);
  anari::release(d, world);
  pass &= check(g_messages.numWarnings() == 0, "valid ids: no warnings");

  pass &= testSurfaceIdChanges(d);
  pass &= testUint64PrimitiveIdTruncation(d);
  for (const auto &scene : subtypeScenes())
    pass &= testShortPrimitiveId(d, scene);
  pass &= testInstanceIdArrays(d);

  anari::release(d, d);
  anari::unloadLibrary(library);

  const size_t errors = g_messages.numErrors();
  if (errors != 0)
    std::printf("FAIL: device reported errors\n");

  return pass && errors == 0 ? 0 : 1;
}
