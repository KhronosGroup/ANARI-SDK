// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// The pathtracer's 'sampleLimit', VisRTX's name for the sample limit, which
// hdAnari reads and forwards (AGENTS.md, "Adaptive sampling"): it is declared
// with 'convergedSamplesPerPixel's default (256); set, it is the limit, else
// 'convergedSamplesPerPixel' is, and a 'convergedSamplesPerPixel' that loses
// warns once per pair of values unless it is at its default (hdAnari forwards
// both, at their defaults unless the user changed them); 0 renders until
// every pixel converged. An hdAnari-style loop (render until 'numSamples' reaches the
// limit hdAnari reads) ends whether 'sampleLimit' is set or not, and when
// every pixel converges early.

// anari
#define ANARI_LINALG_DEFINITIONS
#include <anari/anari_cpp/ext/linalg.h>
#include <anari/anari_cpp.hpp>
// std
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

using namespace anari::math;

// not a multiple of any ISPC program count, to exercise partial SIMD chunks
constexpr uint2 IMAGE_SIZE = {37, 37};

// device errors fail the test; warnings are recorded, in order
int g_errors = 0;
std::vector<std::string> g_warnings;

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
    std::fprintf(stderr, "[ANARI] %s\n", message);
    g_errors++;
  } else if (severity == ANARI_SEVERITY_WARNING) {
    std::fprintf(stderr, "[ANARI warning] %s\n", message);
    g_warnings.push_back(message);
  }
}

bool report(const std::string &name, bool ok)
{
  std::printf("%s %s\n", ok ? "PASS" : "FAIL", name.c_str());
  return ok;
}

// The warnings since 'from' that name 'sampleLimit'
int sampleLimitWarnings(size_t from)
{
  return int(std::count_if(
      g_warnings.begin() + from, g_warnings.end(), [](const std::string &w) {
        return w.find("sampleLimit") != std::string::npos;
      }));
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
// camera, under the ambient light (test_adaptive_sampling's scene): floor
// pixels near the wall are noisy, the background on the right is constant
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

// The pathtracer's parameters a test sets; negative limits are left unset
struct Settings
{
  int pixelSamples{1};
  int sampleLimit{-1};
  int converged{-1};
  // adaptive sampling is off unless a test sets 'minSamples'
  float threshold{0.01f};
  int minSamples{100000};
  const char *mode{"default"};
};

// A frame of 'world' with the pathtracer lit by an ambient light and a float
// color channel
struct Frame
{
  anari::Device d;
  anari::Frame frame;
  anari::Camera camera;
  anari::Renderer renderer;

  Frame(anari::Device d, anari::World world, const Settings &s) : d(d)
  {
    renderer = anari::newObject<anari::Renderer>(d, "quality");
    anari::setParameter(d, renderer, "ambientRadiance", 1.f);
    anari::setParameter(
        d, renderer, "background", float4(0.1f, 0.2f, 0.3f, 1.f));
    anari::setParameter(d, renderer, "mode", s.mode);
    anari::setParameter(d, renderer, "pixelSamples", s.pixelSamples);
    if (s.sampleLimit >= 0)
      anari::setParameter(d, renderer, "sampleLimit", s.sampleLimit);
    if (s.converged >= 0)
      anari::setParameter(d, renderer, "convergedSamplesPerPixel", s.converged);
    anari::setParameter(d, renderer, "adaptiveThreshold", s.threshold);
    anari::setParameter(d, renderer, "minSamplesBeforeAdaptive", s.minSamples);
    anari::commitParameters(d, renderer);

    camera = makeCamera(d);
    frame = anari::newObject<anari::Frame>(d);
    anari::setParameter(d, frame, "size", IMAGE_SIZE);
    anari::setParameter(d, frame, "channel.color", ANARI_FLOAT32_VEC4);
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
    std::vector<float4> pixels;
    auto fb = anari::map<float4>(d, frame, "channel.color");
    if (fb.data && fb.width == IMAGE_SIZE.x && fb.height == IMAGE_SIZE.y)
      pixels.assign(fb.data, fb.data + fb.width * fb.height);
    anari::unmap(d, frame, "channel.color");
    return pixels;
  }

  // The frame's 'numSamples' (-1 if not reported)
  int numSamples()
  {
    int n = -1;
    if (!anari::getProperty(d, frame, "numSamples", n, ANARI_WAIT))
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

  // hdAnari's loop (HdAnariRenderPass::_UpdateProgress()): renders until
  // 'numSamples' reaches 'limit', the render setting 'sampleLimit' (the
  // renderer's declared default unless the app set it). Returns the renders
  // it took, or -1 if it didn't end within 'maxRenders'.
  int hdAnariLoop(int limit, int maxRenders = 1000)
  {
    for (int i = 1; i <= maxRenders; i++) {
      anari::render(d, frame);
      anari::wait(d, frame);
      if (numSamples() >= limit)
        return i;
    }
    return -1;
  }
};

bool sameImage(const std::vector<float4> &a, const std::vector<float4> &b)
{
  return !a.empty() && a.size() == b.size()
      && std::memcmp(a.data(), b.data(), a.size() * sizeof(float4)) == 0;
}

// Tests //////////////////////////////////////////////////////////////////////

// The limit hdAnari reads when the app sets none: 'sampleLimit's declared
// default, which must be 'convergedSamplesPerPixel's
int declaredSampleLimit(anari::Device d)
{
  auto *v = (const int32_t *)anariGetParameterInfo(d,
      ANARI_RENDERER,
      "quality",
      "sampleLimit",
      ANARI_INT32,
      "default",
      ANARI_INT32);
  return v ? *v : -1;
}

bool testIntrospection(anari::Device d)
{
  bool pass = report("sampleLimit declared on pathtracer, default 256",
      declaredSampleLimit(d) == 256);
  auto *minimum = (const int32_t *)anariGetParameterInfo(d,
      ANARI_RENDERER,
      "quality",
      "sampleLimit",
      ANARI_INT32,
      "minimum",
      ANARI_INT32);
  pass &= report("sampleLimit minimum 0", minimum && *minimum == 0);
  return pass;
}

// hdAnari's loop ends at the limit: unset (the declared default), set, and
// set over a different 'convergedSamplesPerPixel', which it wins
bool testLimit(anari::Device d, anari::World world)
{
  bool pass = true;

  Settings unset;
  unset.pixelSamples = 32;
  Frame byDefault(d, world, unset);
  pass &= report("unset: hdAnari's loop ends at the declared default",
      byDefault.hdAnariLoop(declaredSampleLimit(d)) == 8
          && byDefault.reports(256, 1.f));

  Settings set;
  set.sampleLimit = 6;
  set.pixelSamples = 2;
  Frame limited(d, world, set);
  const int renders = limited.hdAnariLoop(6);
  const auto atLimit = limited.render(3);
  pass &= report("set: hdAnari's loop ends at sampleLimit",
      renders == 3 && limited.reports(6, 1.f));

  Settings asConverged = set;
  asConverged.sampleLimit = -1;
  asConverged.converged = 6;
  Frame same(d, world, asConverged);
  pass &= report("sampleLimit stops renders as convergedSamplesPerPixel does",
      sameImage(atLimit, same.render(5)) && same.reports(6, 1.f));

  const size_t before = g_warnings.size();
  Settings both = set;
  both.converged = 64;
  Frame wins(d, world, both);
  pass &= report("set over convergedSamplesPerPixel: sampleLimit wins",
      wins.hdAnariLoop(6) == 3 && wins.reports(6, 1.f)
          && sameImage(wins.render(), atLimit));
  pass &= report("different values warn", sampleLimitWarnings(before) == 1);

  // committing the same values again (hdAnari commits every setting when
  // one changes) doesn't warn again; changing one of them does
  const size_t again = g_warnings.size();
  anari::setParameter(d, wins.renderer, "pixelSamples", 2);
  anari::commitParameters(d, wins.renderer);
  wins.render();
  pass &= report("warns once", sampleLimitWarnings(again) == 0);
  anari::setParameter(d, wins.renderer, "sampleLimit", 4);
  anari::commitParameters(d, wins.renderer);
  wins.render();
  pass &= report(
      "new different values warn again", sampleLimitWarnings(again) == 1);

  const size_t equal = g_warnings.size();
  Settings agree = set;
  agree.converged = 6;
  Frame agreeing(d, world, agree);
  pass &= report("both changed and equal: their limit",
      agreeing.hdAnariLoop(6) == 3 && agreeing.reports(6, 1.f)
          && sameImage(agreeing.render(), atLimit));
  pass &= report("equal values don't warn", sampleLimitWarnings(equal) == 0);
  return pass;
}

// hdAnari forwards both limits, at their declared defaults unless the user
// changed them in usdview, and waits for 'numSamples' to reach 'sampleLimit':
// a set 'sampleLimit' decides, so hdAnari's loop always ends, and a changed
// 'convergedSamplesPerPixel' it overrides warns
bool testSampleLimitDecides(anari::Device d, anari::World world)
{
  bool pass = true;
  const size_t before = g_warnings.size();

  Settings both;
  both.sampleLimit = 256;
  both.converged = 256;
  both.pixelSamples = 32;
  Frame byDefault(d, world, both);
  pass &= report("both at their default: the default",
      byDefault.hdAnariLoop(declaredSampleLimit(d)) == 8
          && byDefault.reports(256, 1.f));

  Settings onlyLimit = both;
  onlyLimit.sampleLimit = 6;
  onlyLimit.pixelSamples = 2;
  Frame limited(d, world, onlyLimit);
  pass &= report("only sampleLimit changed: sampleLimit",
      limited.hdAnariLoop(6) == 3 && limited.reports(6, 1.f));
  pass &= report("only sampleLimit changed doesn't warn",
      sampleLimitWarnings(before) == 0);

  // usdview's 'convergedSamplesPerPixel' changed alone: hdAnari still reads
  // 'sampleLimit' as 256, and its loop ends there
  const size_t onlyConvergedWarnings = g_warnings.size();
  Settings onlyConverged = both;
  onlyConverged.converged = 6;
  Frame converged(d, world, onlyConverged);
  pass &= report("only convergedSamplesPerPixel changed: sampleLimit decides, "
                 "and hdAnari's loop ends",
      converged.hdAnariLoop(declaredSampleLimit(d)) == 8
          && converged.reports(256, 1.f));
  pass &= report("only convergedSamplesPerPixel changed warns",
      sampleLimitWarnings(onlyConvergedWarnings) == 1);

  // an explicit 'sampleLimit' 256 is set, not unset
  Settings explicit256 = both;
  explicit256.converged = 512;
  Frame at256(d, world, explicit256);
  pass &= report("sampleLimit 256 over convergedSamplesPerPixel 512: 256",
      at256.hdAnariLoop(256) == 8 && at256.reports(256, 1.f));
  return pass;
}

// Every pixel converging early: 'numSamples' reports the limit, so hdAnari's
// loop ends, whether the limit is 'sampleLimit's default or set
bool testEarlyConvergence(anari::Device d, anari::World world)
{
  bool pass = true;
  Settings s;
  s.threshold = 1e30f;
  s.minSamples = 3;
  Frame byDefault(d, world, s);
  pass &= report("early convergence, unset: hdAnari's loop ends",
      byDefault.hdAnariLoop(declaredSampleLimit(d)) == 3
          && byDefault.reports(256, 1.f));

  s.sampleLimit = 16;
  Frame set(d, world, s);
  pass &= report("early convergence, set: hdAnari's loop ends",
      set.hdAnariLoop(16) == 3 && set.reports(16, 1.f));
  return pass;
}

// 'sampleLimit' 0: renders refine until every pixel converged, reporting the
// samples taken and the fraction of converged pixels; hdAnari's loop never
// ends on 'numSamples' there, so this loop renders until the progress is 1
bool testUnbounded(anari::Device d, anari::World world)
{
  bool pass = true;

  Settings s;
  s.sampleLimit = 0;
  s.threshold = 1e30f;
  s.minSamples = 3;
  Frame all(d, world, s);
  all.render(2);
  pass &= report("0: no pixel converged yet", all.reports(2, 0.f));
  const auto converged = all.render();
  pass &= report("0: every pixel converged", all.reports(3, 1.f));
  pass &= report("0: then renders stop",
      sameImage(all.render(4), converged) && all.reports(3, 1.f));

  // past any default limit, with only some pixels converged
  Settings some;
  some.sampleLimit = 0;
  some.pixelSamples = 100;
  // only the absolute test: the background converges, the floor doesn't
  some.threshold = 0.f;
  some.minSamples = 4;
  Frame partial(d, world, some);
  partial.render(3);
  const int n = partial.numSamples();
  const float p = partial.progress();
  std::printf("  %d samples, %g of the pixels converged\n", n, p);
  pass &= report("0: renders go past convergedSamplesPerPixel",
      n == 300 && p > 0.f && p < 1.f);
  // a pixel count's fraction
  const float pixels = float(IMAGE_SIZE.x * IMAGE_SIZE.y);
  pass &= report("0: progress is the fraction of converged pixels",
      std::abs(p * pixels - std::round(p * pixels)) < 1e-3f);
  return pass;
}

// The differentials' scale follows the limit: 'sampleLimit' when it is set
// and above 0, else 'convergedSamplesPerPixel' (256 if unset), as the 'dPdx'
// mode shows
bool testDifferentials(anari::Device d, anari::World world)
{
  bool pass = true;
  auto image = [&](int sampleLimit, int converged) {
    Settings s;
    s.mode = "dPdx";
    s.sampleLimit = sampleLimit;
    s.converged = converged;
    Frame f(d, world, s);
    return f.render();
  };
  const auto at256 = image(-1, -1);
  const auto at16 = image(-1, 16);
  pass &= report("dPdx differs between limits", !sameImage(at256, at16));
  pass &= report("sampleLimit sets the differentials' scale",
      sameImage(image(16, -1), at16));
  pass &= report(
      "sampleLimit wins for the differentials", sameImage(image(16, 64), at16));
  pass &= report("sampleLimit 256 over convergedSamplesPerPixel 16: 256's scale",
      sameImage(image(256, 16), at256));
  pass &= report(
      "0: convergedSamplesPerPixel's scale", sameImage(image(0, 16), at16));
  pass &= report("0: convergedSamplesPerPixel's default scale",
      sameImage(image(0, -1), at256));
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
  pass &= testLimit(d, world);
  pass &= testSampleLimitDecides(d, world);
  pass &= testEarlyConvergence(d, world);
  pass &= testUnbounded(d, world);
  pass &= testDifferentials(d, world);
  anari::release(d, world);

  anari::release(d, d);
  anari::unloadLibrary(library);

  pass &= report("no errors", g_errors == 0);
  return pass ? 0 : 1;
}
