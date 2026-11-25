// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// The pathtracer's progressive refinement and adaptive sampling through the
// device (Typhoon's policy; AGENTS.md, "Adaptive sampling"): renders refine
// whatever the frame's 'accumulation' says; they stop at
// 'convergedSamplesPerPixel', or once every pixel has converged; a converged
// pixel stops taking samples while the others go on with the samples they
// would have taken without adaptive sampling; the frame's 'numSamples' and
// 'refinementProgress' properties report it; and a change starts over.

// anari
#define ANARI_LINALG_DEFINITIONS
#include <anari/anari_cpp/ext/linalg.h>
#include <anari/anari_cpp.hpp>
// std
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace {

using namespace anari::math;

// not a multiple of any ISPC program count, to exercise partial SIMD chunks
constexpr uint2 IMAGE_SIZE = {37, 37};

// device errors and warnings fail the test
int g_messages = 0;

void statusFunc(const void *,
    ANARIDevice,
    ANARIObject,
    ANARIDataType,
    ANARIStatusSeverity severity,
    ANARIStatusCode,
    const char *message)
{
  if (severity == ANARI_SEVERITY_FATAL_ERROR || severity == ANARI_SEVERITY_ERROR
      || severity == ANARI_SEVERITY_WARNING) {
    std::fprintf(stderr, "[ANARI] %s\n", message);
    g_messages++;
  }
}

bool report(const std::string &name, bool ok)
{
  std::printf("%s %s\n", ok ? "PASS" : "FAIL", name.c_str());
  return ok;
}

// Scene //////////////////////////////////////////////////////////////////////

anari::Surface makeQuad(
    anari::Device d, const std::vector<float3> &v, float3 color)
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
  anari::commitParameters(d, surface);
  return surface;
}

// A floor over x < 0.5 and a wall x = 0.5 rising from it, edge-on to the
// camera, under the ambient light: floor pixels near the wall are noisy
// (interreflection), the floor far from it less so, and the background on the
// right is constant
anari::World makeCornerWorld(anari::Device d)
{
  std::vector<anari::Surface> surfaces = {makeQuad(d,
                                              {{-1.f, -1.f, 0.f},
                                                  {0.5f, -1.f, 0.f},
                                                  {0.5f, 1.f, 0.f},
                                                  {-1.f, 1.f, 0.f}},
                                              float3(0.8f, 0.5f, 0.25f)),
      makeQuad(d,
          {{0.5f, -1.f, 0.f},
              {0.5f, 1.f, 0.f},
              {0.5f, 1.f, 2.f},
              {0.5f, -1.f, 2.f}},
          float3(0.9f))};
  auto world = anari::newObject<anari::World>(d);
  anari::setParameterArray1D(
      d, world, "surface", surfaces.data(), surfaces.size());
  anari::commitParameters(d, world);
  for (auto s : surfaces)
    anari::release(d, s);
  return world;
}

// Orthographic camera at z = 3 looking down -z at the [-1, 1]^2 square
anari::Camera makeCamera(anari::Device d)
{
  auto camera = anari::newObject<anari::Camera>(d, "orthographic");
  anari::setParameter(d, camera, "position", float3(0.f, 0.f, 3.f));
  anari::setParameter(d, camera, "direction", float3(0.f, 0.f, -1.f));
  anari::setParameter(d, camera, "up", float3(0.f, 1.f, 0.f));
  anari::setParameter(d, camera, "height", 2.f);
  anari::setParameter(d, camera, "aspect", 1.f);
  anari::commitParameters(d, camera);
  return camera;
}

// The pathtracer's sampling parameters; adaptive sampling is off unless a
// test sets 'minSamples' below 'converged'
struct Sampling
{
  int pixelSamples{1};
  int converged{256};
  float threshold{0.01f};
  int minSamples{100000};
};

// A frame of 'world' with the pathtracer (or 'subtype') lit by an ambient
// light, a float color channel, and 'accumulation' left unset unless given
struct Frame
{
  anari::Device d;
  anari::Frame frame;
  anari::Camera camera;
  anari::Renderer renderer;

  Frame(anari::Device d,
      anari::World world,
      const Sampling &s,
      const char *subtype = "quality",
      int accumulation = -1)
      : d(d)
  {
    renderer = anari::newObject<anari::Renderer>(d, subtype);
    // one render is the whole first render (no progressive resolution)
    anari::setParameter(d, renderer, "resolutionDivider", 0);
    anari::setParameter(d, renderer, "ambientRadiance", 1.f);
    anari::setParameter(
        d, renderer, "background", float4(0.1f, 0.2f, 0.3f, 1.f));
    if (std::strcmp(subtype, "quality") == 0) {
      anari::setParameter(d, renderer, "pixelSamples", s.pixelSamples);
      anari::setParameter(d, renderer, "convergedSamplesPerPixel", s.converged);
      anari::setParameter(d, renderer, "adaptiveThreshold", s.threshold);
      anari::setParameter(
          d, renderer, "minSamplesBeforeAdaptive", s.minSamples);
    }
    anari::commitParameters(d, renderer);

    camera = makeCamera(d);
    frame = anari::newObject<anari::Frame>(d);
    anari::setParameter(d, frame, "size", IMAGE_SIZE);
    anari::setParameter(d, frame, "channel.color", ANARI_FLOAT32_VEC4);
    if (accumulation >= 0)
      anari::setParameter(d, frame, "accumulation", accumulation != 0);
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

  // Renders 'n' times, returning the color channel after the last
  std::vector<float4> render(int n = 1)
  {
    for (int i = 0; i < n; i++) {
      anari::render(d, frame);
      anari::wait(d, frame);
    }
    return color();
  }

  std::vector<float4> color()
  {
    std::vector<float4> pixels;
    auto fb = anari::map<float4>(d, frame, "channel.color");
    if (fb.data && fb.width == IMAGE_SIZE.x && fb.height == IMAGE_SIZE.y)
      pixels.assign(fb.data, fb.data + fb.width * fb.height);
    anari::unmap(d, frame, "channel.color");
    return pixels;
  }

  // The frame's 'numSamples' (-1 if not reported)
  int numSamples(ANARIWaitMask wait = ANARI_WAIT)
  {
    int n = -1;
    if (!anari::getProperty(d, frame, "numSamples", n, wait))
      n = -1;
    return n;
  }

  // The frame's 'refinementProgress' (-1 if not reported)
  float progress()
  {
    float p = -1.f;
    if (!anari::getProperty(d, frame, "refinementProgress", p, ANARI_WAIT))
      p = -1.f;
    return p;
  }

  bool reports(int n, float p)
  {
    const int gotN = numSamples();
    const float gotP = progress();
    const bool ok = gotN == n && std::abs(gotP - p) <= 1e-6f;
    if (!ok) {
      std::printf(
          "  numSamples %d (expected %d), refinementProgress %g "
          "(expected %g)\n",
          gotN,
          n,
          gotP,
          p);
    }
    return ok;
  }
};

bool sameImage(const std::vector<float4> &a, const std::vector<float4> &b)
{
  return !a.empty() && a.size() == b.size()
      && std::memcmp(a.data(), b.data(), a.size() * sizeof(float4)) == 0;
}

bool samePixel(float4 a, float4 b)
{
  return std::memcmp(&a, &b, sizeof(float4)) == 0;
}

// Tests //////////////////////////////////////////////////////////////////////

bool testIntrospection(anari::Device d)
{
  bool pass = true;
  auto intDefault = [&](const char *name) {
    auto *v = (const int32_t *)anariGetParameterInfo(d,
        ANARI_RENDERER,
        "quality",
        name,
        ANARI_INT32,
        "default",
        ANARI_INT32);
    return v ? *v : -1;
  };
  pass &= report("convergedSamplesPerPixel declared, default 256",
      intDefault("convergedSamplesPerPixel") == 256);
  pass &= report("minSamplesBeforeAdaptive declared, default 64",
      intDefault("minSamplesBeforeAdaptive") == 64);
  auto *threshold = (const float *)anariGetParameterInfo(d,
      ANARI_RENDERER,
      "quality",
      "adaptiveThreshold",
      ANARI_FLOAT32,
      "default",
      ANARI_FLOAT32);
  pass &= report("adaptiveThreshold declared, default 0.01",
      threshold && *threshold == 0.01f);
  return pass;
}

// The pathtracer refines whatever 'accumulation' says, and reports
// 'numSamples' and 'refinementProgress'; the preview renderer neither refines
// nor reports them
bool testProperties(anari::Device d, anari::World world)
{
  bool pass = true;
  Sampling s;
  s.pixelSamples = 2;
  s.converged = 16;

  for (int accumulation : {-1, 0, 1}) {
    Frame f(d, world, s, "quality", accumulation);
    const std::string suffix = accumulation < 0 ? " (accumulation unset)"
        : accumulation                          ? " (accumulation true)"
                                                : " (accumulation false)";
    pass &= report("nothing reported before a render" + suffix,
        f.numSamples() == -1 && f.progress() == -1.f);
    const auto first = f.render();
    pass &= report("one render: 2 samples" + suffix, f.reports(2, 2.f / 16.f));
    const auto third = f.render(2);
    pass &= report("three renders: 6 samples, refined" + suffix,
        f.reports(6, 6.f / 16.f) && !sameImage(first, third));
  }

  Frame def(d, world, s, "default", 1);
  def.render(3);
  pass &= report("the preview renderer reports neither",
      def.numSamples() == -1 && def.progress() == -1.f);

  // an ANARI_WAIT query waits for the render queued before it
  Frame f(d, world, s);
  anari::render(d, f.frame);
  pass &=
      report("an ANARI_WAIT query sees the queued render", f.numSamples() == 2);
  anari::wait(d, f.frame);
  return pass;
}

// Renders stop at 'convergedSamplesPerPixel', reached at whole renders, and
// then leave the image as it is
bool testSampleLimit(anari::Device d, anari::World world)
{
  bool pass = true;

  Sampling s;
  s.converged = 4;
  Frame f(d, world, s);
  f.render(3);
  pass &= report("below the limit", f.reports(3, 0.75f));
  const auto atLimit = f.render();
  pass &= report("at the limit", f.reports(4, 1.f));
  pass &= report("renders past the limit leave the image",
      sameImage(f.render(3), atLimit) && f.reports(4, 1.f));

  Sampling once = s;
  once.pixelSamples = 4;
  Frame direct(d, world, once);
  pass &= report("4 renders of 1 sample == 1 render of 4 at the limit",
      sameImage(direct.render(), atLimit));

  // 3 + 3 samples: the second render goes past 4, and is the last
  Sampling three = s;
  three.pixelSamples = 3;
  Frame overshoot(d, world, three);
  overshoot.render();
  pass &= report("3 of 4 samples", overshoot.reports(3, 0.75f));
  const auto six = overshoot.render(3);
  Sampling sixOnce = s;
  sixOnce.pixelSamples = 6;
  Frame sixDirect(d, world, sixOnce);
  pass &= report("the limit ends at the render that reaches it",
      overshoot.reports(6, 1.f) && sameImage(six, sixDirect.render()));

  return pass;
}

// A threshold every pixel passes converges every pixel after
// 'minSamplesBeforeAdaptive' samples: the image is then the one renders
// without adaptive sampling give after that many, and renders stop
bool testAllConverge(anari::Device d, anari::World world)
{
  bool pass = true;

  Sampling s;
  s.threshold = 1e30f;
  s.minSamples = 3;
  Frame adaptive(d, world, s);
  adaptive.render(2);
  pass &= report("not converged before minSamplesBeforeAdaptive",
      adaptive.reports(2, 2.f / 256.f));
  adaptive.render();
  pass &= report("every pixel converged: numSamples is the limit",
      adaptive.reports(256, 1.f));
  const auto converged = adaptive.render(5);

  Frame plain(d, world, Sampling{});
  pass &= report("converged pixels take no more samples",
      sameImage(converged, plain.render(3)) && adaptive.reports(256, 1.f));

  // a negative minSamplesBeforeAdaptive turns adaptive sampling off (Typhoon
  // compares it as unsigned)
  Sampling off = s;
  off.minSamples = -1;
  Frame never(d, world, off);
  // any commit starts every frame over, so 'plain' starts over too
  const auto plain8 = plain.render(8);
  pass &= report("negative minSamplesBeforeAdaptive: no pixel converges",
      sameImage(never.render(8), plain8) && never.reports(8, 8.f / 256.f));

  // a camera change starts over
  anari::setParameter(d, adaptive.camera, "position", float3(0.f, 0.f, 4.f));
  anari::commitParameters(d, adaptive.camera);
  Frame fresh(d, world, Sampling{});
  pass &= report("a change starts over",
      sameImage(adaptive.render(), fresh.render())
          && adaptive.reports(1, 1.f / 256.f));
  return pass;
}

// With a threshold some pixels pass early and others don't, each pixel shows
// what renders without adaptive sampling show after the samples it took: at
// least 'minSamplesBeforeAdaptive', and all of them for pixels that didn't
// converge. So unconverged pixels keep the sample indices they'd have without
// adaptive sampling (Typhoon's per-pixel sample sequence).
bool testSomeConverge(anari::Device d, anari::World world)
{
  constexpr int N = 24;
  constexpr int MIN_SAMPLES = 4;
  bool pass = true;

  Frame plain(d, world, Sampling{});
  std::vector<std::vector<float4>> after(N + 1);
  for (int k = 1; k <= N; k++)
    after[k] = plain.render();

  Sampling s;
  s.threshold = 0.002f;
  s.minSamples = MIN_SAMPLES;
  Frame adaptive(d, world, s);
  const auto image = adaptive.render(N);
  pass &= report("not every pixel converged", adaptive.reports(N, N / 256.f));

  int early = 0;
  int full = 0;
  int unexplained = 0;
  for (size_t i = 0; i < image.size(); i++) {
    if (samePixel(image[i], after[N][i])) {
      full++;
      continue;
    }
    bool found = false;
    for (int k = MIN_SAMPLES; k < N && !found; k++)
      found = samePixel(image[i], after[k][i]);
    early += found;
    unexplained += !found;
  }
  std::printf(
      "  %d pixels stopped early, %d took all %d samples\n", early, full, N);
  pass &= report(
      "every pixel shows its first samples, at least "
      "minSamplesBeforeAdaptive",
      unexplained == 0);
  pass &= report("some pixels stop early, others don't", early > 0 && full > 0);
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
  auto world = makeCornerWorld(d);
  pass &= testProperties(d, world);
  pass &= testSampleLimit(d, world);
  pass &= testAllConverge(d, world);
  pass &= testSomeConverge(d, world);
  anari::release(d, world);

  anari::release(d, d);
  anari::unloadLibrary(library);

  pass &= report("no warnings or errors", g_messages == 0);
  return pass ? 0 : 1;
}
