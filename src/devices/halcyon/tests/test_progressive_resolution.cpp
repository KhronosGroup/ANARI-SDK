// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// The 'quality' renderer's progressive resolution ('resolutionDivider'): the
// first render after a change spread over renders of ever finer grids of
// pixels. Each pass traces the samples the first render would, so after the
// pass of stride s every pixel shows (bitwise) what a frame without
// progressive resolution renders first at the top left pixel of its s x s
// block, in every channel; after the last pass the frame is that frame, and
// accumulates as it does. Also: the number of passes 'resolutionDivider'
// gives, the properties during the passes, and changes starting over.

// anari
#define ANARI_LINALG_DEFINITIONS
#include <anari/anari_cpp/ext/linalg.h>
#include <anari/anari_cpp.hpp>
// std
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace {

using namespace anari::math;

// not a multiple of any ISPC program count or of the strides, to exercise
// partial SIMD chunks and blocks
constexpr uint2 IMAGE_SIZE = {37, 29};

int g_errors = 0;

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
  }
}

bool report(const std::string &name, bool ok)
{
  std::printf("%s %s\n", ok ? "PASS" : "FAIL", name.c_str());
  return ok;
}

// Scene /////////////////////////////////////////////////////////////////////

anari::Surface makeQuad(
    anari::Device d, const std::vector<float3> &v, uint32_t id, float3 color)
{
  const std::vector<uint3> indices = {{0, 1, 2}, {0, 2, 3}};
  auto geom = anari::newObject<anari::Geometry>(d, "triangle");
  anari::setParameterArray1D(d, geom, "vertex.position", v.data(), v.size());
  anari::setParameterArray1D(
      d, geom, "primitive.index", indices.data(), indices.size());
  anari::commitParameters(d, geom);

  auto mat = anari::newObject<anari::Material>(d, "matte");
  anari::setParameter(d, mat, "color", color);
  anari::commitParameters(d, mat);

  auto surface = anari::newObject<anari::Surface>(d);
  anari::setAndReleaseParameter(d, surface, "geometry", geom);
  anari::setAndReleaseParameter(d, surface, "material", mat);
  anari::setParameter(d, surface, "id", id);
  anari::commitParameters(d, surface);
  return surface;
}

// A floor over x < 0.5 and a wall x = 0.5 rising from it, edge-on to the
// camera, of different ids and colors: noisy floor pixels, and edges in
// every channel
anari::World makeWorld(anari::Device d)
{
  std::vector<anari::Surface> surfaces = {makeQuad(d,
                                              {{-1.f, -1.f, 0.f},
                                                  {0.5f, -1.f, 0.f},
                                                  {0.5f, 1.f, 0.f},
                                                  {-1.f, 1.f, 0.f}},
                                              1,
                                              {0.8f, 0.5f, 0.25f}),
      makeQuad(d,
          {{0.5f, -1.f, 0.f},
              {0.5f, 1.f, 0.f},
              {0.5f, 1.f, 2.f},
              {0.5f, -1.f, 2.f}},
          2,
          float3(0.9f))};
  auto world = anari::newObject<anari::World>(d);
  anari::setParameterArray1D(
      d, world, "surface", surfaces.data(), surfaces.size());
  anari::commitParameters(d, world);
  for (auto s : surfaces)
    anari::release(d, s);
  return world;
}

// A perspective camera looking down at the scene at an angle, so that the
// wall shows
anari::Camera makeCamera(anari::Device d)
{
  auto camera = anari::newObject<anari::Camera>(d, "perspective");
  anari::setParameter(d, camera, "position", float3(-0.5f, -2.f, 2.5f));
  anari::setParameter(
      d, camera, "direction", linalg::normalize(float3(0.4f, 2.f, -2.5f)));
  anari::setParameter(d, camera, "up", float3(0.f, 0.f, 1.f));
  anari::setParameter(d, camera, "aspect", float(IMAGE_SIZE.x) / IMAGE_SIZE.y);
  anari::commitParameters(d, camera);
  return camera;
}

// Every channel of a frame, as raw bytes
struct Channels
{
  std::vector<uint8_t> color, depth, primId, objId, albedo, normal;
};

// The bytes of pixel 'i' of each channel of 'a' equal those of pixel 'j' of
// 'b'
bool samePixel(const Channels &a, size_t i, const Channels &b, size_t j)
{
  // a channel that failed to map (counted in g_errors) has no pixels
  if (a.color.empty() || a.depth.empty() || a.primId.empty() || a.objId.empty()
      || a.albedo.empty() || a.normal.empty() || b.color.empty()
      || b.depth.empty() || b.primId.empty() || b.objId.empty()
      || b.albedo.empty() || b.normal.empty())
    return false;
  const auto same = [&](const std::vector<uint8_t> &x,
                        const std::vector<uint8_t> &y,
                        size_t bytes) {
    return std::memcmp(x.data() + i * bytes, y.data() + j * bytes, bytes) == 0;
  };
  return same(a.color, b.color, 16) && same(a.depth, b.depth, 4)
      && same(a.primId, b.primId, 4) && same(a.objId, b.objId, 4)
      && same(a.albedo, b.albedo, 12) && same(a.normal, b.normal, 12);
}

// Every pixel of 'image' shows what pixel (x - x % stride, y - y % stride)
// of 'first' does, in every channel
bool blocksOf(const Channels &image, const Channels &first, int stride)
{
  for (uint32_t y = 0; y < IMAGE_SIZE.y; y++) {
    for (uint32_t x = 0; x < IMAGE_SIZE.x; x++) {
      const size_t i = size_t(y) * IMAGE_SIZE.x + x;
      const size_t j = size_t(y - y % stride) * IMAGE_SIZE.x + (x - x % stride);
      if (!samePixel(image, i, first, j))
        return false;
    }
  }
  return true;
}

bool sameImage(const Channels &a, const Channels &b)
{
  return blocksOf(a, b, 1);
}

// A frame with every channel, through makeCamera(), with a 'quality'
// renderer of 'pixelSamples' and 'resolutionDivider' (unset if negative)
struct Frame
{
  anari::Device d;
  anari::Frame frame;
  anari::Camera camera;
  anari::Renderer renderer;

  Frame(anari::Device d,
      anari::World world,
      int resolutionDivider,
      int pixelSamples = 1)
      : d(d)
  {
    renderer = anari::newObject<anari::Renderer>(d, "quality");
    anari::setParameter(d, renderer, "ambientRadiance", 1.f);
    anari::setParameter(d, renderer, "pixelSamples", pixelSamples);
    if (resolutionDivider >= 0)
      anari::setParameter(d, renderer, "resolutionDivider", resolutionDivider);
    anari::commitParameters(d, renderer);

    camera = makeCamera(d);
    frame = anari::newObject<anari::Frame>(d);
    anari::setParameter(d, frame, "size", IMAGE_SIZE);
    anari::setParameter(d, frame, "channel.color", ANARI_FLOAT32_VEC4);
    anari::setParameter(d, frame, "channel.depth", ANARI_FLOAT32);
    anari::setParameter(d, frame, "channel.primitiveId", ANARI_UINT32);
    anari::setParameter(d, frame, "channel.objectId", ANARI_UINT32);
    anari::setParameter(d, frame, "channel.albedo", ANARI_FLOAT32_VEC3);
    anari::setParameter(d, frame, "channel.normal", ANARI_FLOAT32_VEC3);
    anari::setParameter(d, frame, "camera", camera);
    anari::setParameter(d, frame, "renderer", renderer);
    anari::setParameter(d, frame, "world", world);
    anari::commitParameters(d, frame);
  }

  ~Frame()
  {
    anari::release(d, frame);
    anari::release(d, camera);
    anari::release(d, renderer);
  }

  // Renders 'n' times, returning every channel after the last
  Channels render(int n = 1)
  {
    for (int i = 0; i < n; i++) {
      anari::render(d, frame);
      anari::wait(d, frame);
    }
    Channels c;
    c.color = channel("channel.color", 16);
    c.depth = channel("channel.depth", 4);
    c.primId = channel("channel.primitiveId", 4);
    c.objId = channel("channel.objectId", 4);
    c.albedo = channel("channel.albedo", 12);
    c.normal = channel("channel.normal", 12);
    return c;
  }

  std::vector<uint8_t> channel(const char *name, size_t pixelBytes)
  {
    std::vector<uint8_t> bytes;
    uint32_t width = 0, height = 0;
    ANARIDataType type = ANARI_UNKNOWN;
    auto *data =
        (const uint8_t *)anariMapFrame(d, frame, name, &width, &height, &type);
    if (data && width == IMAGE_SIZE.x && height == IMAGE_SIZE.y)
      bytes.assign(data, data + size_t(width) * height * pixelBytes);
    else {
      std::printf("FAIL map %s\n", name);
      g_errors++;
    }
    anariUnmapFrame(d, frame, name);
    return bytes;
  }

  // 'numSamples' as of the last render, -1 if not reported
  int numSamples()
  {
    int n = -1;
    if (!anari::getProperty(d, frame, "numSamples", n, ANARI_WAIT))
      return -1;
    return n;
  }

  float refinementProgress()
  {
    float p = -1.f;
    anari::getProperty(d, frame, "refinementProgress", p, ANARI_WAIT);
    return p;
  }

  // Moves the camera, which changes the image
  void moveCamera(float x)
  {
    anari::setParameter(d, camera, "position", float3(x, -2.f, 2.5f));
    anari::commitParameters(d, camera);
  }
};

// Tests /////////////////////////////////////////////////////////////////////

// 'resolutionDivider' is declared on 'quality', default 4
bool testIntrospection(anari::Device d)
{
  auto *value = (const int32_t *)anariGetParameterInfo(d,
      ANARI_RENDERER,
      "quality",
      "resolutionDivider",
      ANARI_INT32,
      "default",
      ANARI_INT32);
  return report("resolutionDivider declared, default 4", value && *value == 4);
}

// The passes' strides for 'resolutionDivider' on IMAGE_SIZE (longer side
// 37): the divider rounded down to a power of two and to at most 37, halved
// each render down to 1
std::vector<int> expectedStrides(int resolutionDivider)
{
  int stride = 1;
  while (2 * stride <= std::min(resolutionDivider, 37))
    stride *= 2;
  std::vector<int> strides;
  for (; stride >= 1; stride /= 2)
    strides.push_back(stride);
  return strides;
}

// The passes for 'resolutionDivider' on IMAGE_SIZE, by hand
size_t expectedPasses(int resolutionDivider)
{
  switch (resolutionDivider) {
  case 2: // strides 2 (19 grid points along 37), 1
    return 2;
  case 6: // rounded down to 4 (10 grid points), 2, 1
    return 3;
  case 8: // 8 (5 grid points), 4, 2, 1
    return 4;
  case 1000: // capped to 37, rounded down to 32 (2 grid points), ..., 1
    return 6;
  default: // 1: every pixel at once
    return 1;
  }
}

// Each pass shows the first render at its resolution, in every channel;
// after the last, the frame is a frame without progressive resolution, and
// accumulates as it does. 'numSamples' and 'refinementProgress' are 0 during
// the passes.
bool testPasses(anari::Device d, anari::World world)
{
  bool pass = true;
  for (int pixelSamples : {1, 3}) {
    for (int resolutionDivider : {1, 2, 6, 8, 1000}) {
      const std::string what = "resolutionDivider "
          + std::to_string(resolutionDivider) + ", pixelSamples "
          + std::to_string(pixelSamples);
      const auto strides = expectedStrides(resolutionDivider);

      Frame reference(d, world, 0, pixelSamples);
      const Channels first = reference.render();
      const Channels third = reference.render(2);

      Frame f(d, world, resolutionDivider, pixelSamples);
      bool blocks = true;
      bool noSamples = true;
      for (size_t k = 0; k + 1 < strides.size(); k++) {
        const Channels image = f.render();
        // (and not the first render itself: the scene has detail at every
        // stride)
        blocks &=
            blocksOf(image, first, strides[k]) && !sameImage(image, first);
        noSamples &= f.numSamples() == 0 && f.refinementProgress() == 0.f;
      }
      pass &= report(std::to_string(strides.size()) + " passes, " + what,
          strides.size() == expectedPasses(resolutionDivider));
      pass &= report(
          "coarse passes show the first render's blocks, " + what, blocks);
      pass &=
          report("no samples reported during the passes, " + what, noSamples);
      pass &= report("the last pass completes the first render, " + what,
          sameImage(f.render(), first) && f.numSamples() == pixelSamples);
      pass &= report("renders accumulate after the passes, " + what,
          sameImage(f.render(2), third));
    }
  }
  return pass;
}

// 'resolutionDivider' 1 renders as 0, the first render at once; unset, as
// the default, 4
bool testDefaults(anari::Device d, anari::World world)
{
  bool pass = true;
  Frame zero(d, world, 0);
  Frame one(d, world, 1);
  pass &= report("resolutionDivider 1 renders the first render at once",
      sameImage(one.render(), zero.render()));

  Frame unset(d, world, -1);
  Frame four(d, world, 4);
  bool same = true;
  for (int i = 0; i < 4; i++)
    same &= sameImage(unset.render(), four.render());
  pass &= report("resolutionDivider unset renders as 4", same);
  return pass;
}

// A change starts the passes over, from the coarsest grid: after the passes
// and accumulation, and during the passes
bool testChange(anari::Device d, anari::World world)
{
  bool pass = true;

  // the image of a moved camera, its first coarse pass
  Frame moved(d, world, 8);
  moved.moveCamera(-0.4f);
  const Channels movedCoarse = moved.render();
  const Channels movedFirst = moved.render(3);

  Frame f(d, world, 8);
  f.render(6);
  f.moveCamera(-0.4f);
  pass &= report("a change after the passes starts them over",
      sameImage(f.render(), movedCoarse) && f.numSamples() == 0);
  pass &= report("and they complete the moved camera's first render",
      sameImage(f.render(3), movedFirst));

  Frame g(d, world, 8);
  g.render(2);
  g.moveCamera(-0.4f);
  pass &= report("a change during the passes starts them over",
      sameImage(g.render(), movedCoarse) && sameImage(g.render(3), movedFirst));

  // the renderer's 'resolutionDivider' changed: a change, with the new
  // value's passes
  Frame h(d, world, 8);
  Frame fine(d, world, 2);
  h.render(5);
  anari::setParameter(d, h.renderer, "resolutionDivider", 2);
  anari::commitParameters(d, h.renderer);
  pass &= report("a resolutionDivider change starts over with its passes",
      sameImage(h.render(), fine.render()) && h.numSamples() == 0);

  return pass;
}

} // namespace

int main()
{
  auto library = anari::loadLibrary("halcyon", statusFunc);
  if (!library) {
    std::fprintf(stderr, "could not load the halcyon library\n");
    return 1;
  }
  auto d = anari::newDevice(library, "default");

  bool pass = testIntrospection(d);
  auto world = makeWorld(d);
  pass &= testPasses(d, world);
  pass &= testDefaults(d, world);
  pass &= testChange(d, world);
  anari::release(d, world);

  anari::release(d, d);
  anari::unloadLibrary(library);

  pass &= report("no errors reported", g_errors == 0);
  return pass ? 0 : 1;
}
