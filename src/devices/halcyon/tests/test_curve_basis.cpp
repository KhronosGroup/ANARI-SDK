// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// The curve geometry's 'basis' (HALCYON_GEOMETRY_CURVE_BASIS): B-spline and
// Catmull-Rom round curves in the z = 0 plane, seen from above through an
// orthographic camera.
//
// - Shape: a strand is the union of the discs of radius r(u) around P(u),
//   both evaluated with the basis (Embree's formulas) over 4 control points
//   per segment; pixels well inside it are hit, pixels well outside missed.
// - Attributes: 'vertex.color' holds each control point's position and
//   radius, so the colour of a hit pixel, interpolated with the basis at the
//   hit's u, is a point of the curve within its radius of the pixel. A
//   Catmull-Rom strand passes through its inner control points.
// - Indexing: n vertices without 'primitive.index' are n - 3 segments;
//   'primitive.index' i is the segment over vertices i to i + 3, and the
//   segment's index selects its 'primitive.*' attributes and ids. Indices
//   past n - 4 clamp to the last segment with a warning.
// - Fewer than 4 vertices make a cubic curve invalid with a warning; an
//   unknown basis warns and draws linear segments; 'linear' and no 'basis'
//   render the same image; a basis committed again changes the curve.

// anari
#define ANARI_LINALG_DEFINITIONS
#include <anari/anari_cpp/ext/linalg.h>
#include <anari/anari_cpp.hpp>
// std
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace {

using namespace anari::math;

constexpr uint2 IMAGE_SIZE = {64, 64};
// the camera sees [-1, 1]^2
constexpr float PIXEL = 2.f / 64.f;
constexpr uint32_t SURFACE_ID = 3;

struct Messages
{
  std::vector<std::string> warnings;
  int errors = 0;

  bool warnedSince(size_t from, const char *text) const
  {
    for (size_t i = from; i < warnings.size(); i++) {
      if (warnings[i].find(text) != std::string::npos)
        return true;
    }
    return false;
  }
} g_messages;

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
    g_messages.errors++;
  } else if (severity == ANARI_SEVERITY_WARNING) {
    std::fprintf(stderr, "[ANARI WARNING] %s\n", message);
    g_messages.warnings.push_back(message);
  }
}

bool report(const std::string &name, bool ok, const std::string &detail = {})
{
  std::printf("%s %s%s%s\n",
      ok ? "PASS" : "FAIL",
      name.c_str(),
      detail.empty() ? "" : ": ",
      detail.c_str());
  return ok;
}

std::string str(float v)
{
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.6g", v);
  return buf;
}

// Bases (Embree's BSplineBasis and CatmullRomBasis) //////////////////////////

// The weights of the 4 control points of a segment at u in [0, 1]
float4 weights(const std::string &basis, float u)
{
  const float t = u, s = 1.f - u;
  if (basis == "bspline") {
    return float4(s * s * s,
               (4.f * (s * s * s) + t * t * t) + (12.f * (s * t * s) + 6.f * (t * s * t)),
               (4.f * (t * t * t) + s * s * s) + (12.f * (t * s * t) + 6.f * (s * t * s)),
               t * t * t)
        / 6.f;
  }
  return 0.5f
      * float4(-t * s * s,
          2.f + t * t * (3.f * t - 5.f),
          2.f + s * s * (3.f * s - 5.f),
          -s * t * t);
}

// A strand: control points and radii, the segments' first vertices
struct Strand
{
  std::string basis;
  std::vector<float3> points;
  std::vector<float> radii;
  std::vector<uint32_t> starts;

  float3 position(uint32_t segment, float u) const
  {
    const float4 w = weights(basis, u);
    const uint32_t i = starts[segment];
    return w.x * points[i] + w.y * points[i + 1] + w.z * points[i + 2]
        + w.w * points[i + 3];
  }

  float radius(uint32_t segment, float u) const
  {
    const float4 w = weights(basis, u);
    const uint32_t i = starts[segment];
    return w.x * radii[i] + w.y * radii[i + 1] + w.z * radii[i + 2]
        + w.w * radii[i + 3];
  }

  // min over the curve of |c - P(u)| - r(u): negative inside the strand
  float signedDistance(float2 c) const
  {
    float d = INFINITY;
    for (uint32_t s = 0; s < starts.size(); s++) {
      for (int k = 0; k <= 2000; k++) {
        const float u = k / 2000.f;
        const float3 p = position(s, u);
        d = std::min(d, length(c - float2(p.x, p.y)) - radius(s, u));
      }
    }
    return d;
  }

  // the distance from 'c' to the strand's end points
  float distanceToEnds(float2 c) const
  {
    const float3 a = position(0, 0.f);
    const float3 b = position(uint32_t(starts.size() - 1), 1.f);
    return std::min(
        length(c - float2(a.x, a.y)), length(c - float2(b.x, b.y)));
  }
};

// Scene //////////////////////////////////////////////////////////////////////

using SetParams = std::function<void(anari::Device, anari::Geometry)>;

// A curve of 'strand's points and radii, vertex colours its position mapped
// to [0, 1] ((p + 2) / 4) and 4 r; 'basis' unset where 'strand.basis' is
// empty; 'primitive.index' if 'indexed'
anari::Geometry makeCurve(anari::Device d,
    const Strand &strand,
    bool indexed,
    const SetParams &set = {})
{
  auto geom = anari::newObject<anari::Geometry>(d, "curve");
  if (!strand.basis.empty())
    anari::setParameter(d, geom, "basis", strand.basis);
  anari::setParameterArray1D(d,
      geom,
      "vertex.position",
      strand.points.data(),
      strand.points.size());
  anari::setParameterArray1D(
      d, geom, "vertex.radius", strand.radii.data(), strand.radii.size());
  std::vector<float3> colors;
  for (size_t i = 0; i < strand.points.size(); i++) {
    const float3 p = strand.points[i];
    colors.push_back(float3((p.x + 2.f) / 4.f, (p.y + 2.f) / 4.f, 4.f * strand.radii[i]));
  }
  anari::setParameterArray1D(
      d, geom, "vertex.color", colors.data(), colors.size());
  if (indexed) {
    anari::setParameterArray1D(d,
        geom,
        "primitive.index",
        strand.starts.data(),
        strand.starts.size());
  }
  if (set)
    set(d, geom);
  anari::commitParameters(d, geom);
  return geom;
}

struct Image
{
  std::vector<float4> color;
  std::vector<float> depth;
  std::vector<uint32_t> primitiveId;

  bool operator==(const Image &o) const
  {
    return color.size() == o.color.size()
        && std::equal(color.begin(),
            color.end(),
            o.color.begin(),
            [](float4 a, float4 b) {
              return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
            })
        && depth == o.depth && primitiveId == o.primitiveId;
  }

  bool hit(size_t i) const
  {
    return depth[i] < 1e30f;
  }
};

template <typename T>
std::vector<T> readChannel(anari::Device d, anari::Frame frame, const char *channel)
{
  std::vector<T> values;
  auto fb = anari::map<T>(d, frame, channel);
  if (fb.data && fb.width == IMAGE_SIZE.x && fb.height == IMAGE_SIZE.y)
    values.assign(fb.data, fb.data + size_t(IMAGE_SIZE.x) * IMAGE_SIZE.y);
  anari::unmap(d, frame, channel);
  return values;
}

// A frame of 'geom' (not released) seen from above in the 'default'
// renderer's 'mode', to render once or again
anari::Frame makeFrame(anari::Device d, anari::Geometry geom, const char *mode)
{
  auto material = anari::newObject<anari::Material>(d, "matte");
  anari::commitParameters(d, material);
  auto surface = anari::newObject<anari::Surface>(d);
  anari::setParameter(d, surface, "geometry", geom);
  anari::setAndReleaseParameter(d, surface, "material", material);
  anari::setParameter(d, surface, "id", SURFACE_ID);
  anari::commitParameters(d, surface);
  auto world = anari::newObject<anari::World>(d);
  anari::setParameterArray1D(d, world, "surface", &surface, 1);
  anari::commitParameters(d, world);
  anari::release(d, surface);

  auto camera = anari::newObject<anari::Camera>(d, "orthographic");
  anari::setParameter(d, camera, "position", float3(0.f, 0.f, 3.f));
  anari::setParameter(d, camera, "direction", float3(0.f, 0.f, -1.f));
  anari::setParameter(d, camera, "up", float3(0.f, 1.f, 0.f));
  anari::setParameter(d, camera, "height", 2.f);
  anari::setParameter(d, camera, "aspect", 1.f);
  anari::commitParameters(d, camera);

  auto renderer = anari::newObject<anari::Renderer>(d, "preview");
  anari::setParameter(d, renderer, "mode", mode);
  anari::setParameter(d, renderer, "background", float4(0.f));
  anari::commitParameters(d, renderer);

  auto frame = anari::newObject<anari::Frame>(d);
  anari::setParameter(d, frame, "size", IMAGE_SIZE);
  anari::setParameter(d, frame, "channel.color", ANARI_FLOAT32_VEC4);
  anari::setParameter(d, frame, "channel.depth", ANARI_FLOAT32);
  anari::setParameter(d, frame, "channel.primitiveId", ANARI_UINT32);
  anari::setAndReleaseParameter(d, frame, "camera", camera);
  anari::setAndReleaseParameter(d, frame, "renderer", renderer);
  anari::setAndReleaseParameter(d, frame, "world", world);
  anari::commitParameters(d, frame);
  return frame;
}

Image renderFrame(anari::Device d, anari::Frame frame)
{
  anari::render(d, frame);
  anari::wait(d, frame);

  Image image;
  image.color = readChannel<float4>(d, frame, "channel.color");
  image.depth = readChannel<float>(d, frame, "channel.depth");
  image.primitiveId = readChannel<uint32_t>(d, frame, "channel.primitiveId");
  return image;
}

// Renders 'geom' (not released) from above in a new frame
Image render(anari::Device d, anari::Geometry geom, const char *mode)
{
  auto frame = makeFrame(d, geom, mode);
  Image image = renderFrame(d, frame);
  anari::release(d, frame);
  return image;
}

// The centre of pixel i
float2 pixelCenter(size_t i)
{
  const size_t x = i % IMAGE_SIZE.x, y = i / IMAGE_SIZE.x;
  return float2(-1.f + (x + 0.5f) * PIXEL, -1.f + (y + 0.5f) * PIXEL);
}

// The pixel containing 'p'
size_t pixelAt(float2 p)
{
  const size_t x = size_t((p.x + 1.f) / PIXEL);
  const size_t y = size_t((p.y + 1.f) / PIXEL);
  return y * IMAGE_SIZE.x + x;
}

// The test strand: 5 control points, so 2 segments, radii growing along it
Strand testStrand(const std::string &basis)
{
  Strand s;
  s.basis = basis;
  s.points = {{-0.9f, -0.5f, 0.f},
      {-0.5f, 0.5f, 0.f},
      {0.f, -0.4f, 0.f},
      {0.5f, 0.6f, 0.f},
      {0.9f, -0.3f, 0.f}};
  s.radii = {0.04f, 0.05f, 0.06f, 0.07f, 0.08f};
  s.starts = {0, 1};
  return s;
}

// Tests //////////////////////////////////////////////////////////////////////

// Pixels well inside the strand are hit, well outside missed (away from its
// ends, whose caps aren't checked); every hit's colour is a point of the
// curve within its radius of the pixel
bool testShape(anari::Device d, const std::string &basis)
{
  const Strand strand = testStrand(basis);
  auto geom = makeCurve(d, strand, false);
  const Image image = render(d, geom, "geometry.color");
  anari::release(d, geom);
  if (image.depth.empty())
    return report(basis + " strand renders", false);

  const float margin = 0.25f * PIXEL;
  int wrongHits = 0, wrongMisses = 0, offCurve = 0, hits = 0;
  float worst = 0.f;
  for (size_t i = 0; i < image.depth.size(); i++) {
    const float2 c = pixelCenter(i);
    const float s = strand.signedDistance(c);
    if (strand.distanceToEnds(c) < 0.2f)
      continue;
    if (image.hit(i)) {
      hits++;
      if (s > margin)
        wrongHits++;
      // the attribute: P(u) and r(u) at the hit's u
      const float4 col = image.color[i];
      const float2 p(4.f * col.x - 2.f, 4.f * col.y - 2.f);
      const float r = col.z / 4.f;
      const float e = length(p - c) - r;
      worst = std::max(worst, e);
      if (e > 0.002f)
        offCurve++;
    } else if (s < -margin) {
      wrongMisses++;
    }
  }
  bool pass = report(basis + " strand: the shape of the basis",
      hits > 50 && wrongHits == 0 && wrongMisses == 0,
      std::to_string(hits) + " hits, " + std::to_string(wrongHits)
          + " outside, " + std::to_string(wrongMisses) + " missed inside");
  pass &= report(basis + " strand: attributes and radius along the basis",
      offCurve == 0,
      std::to_string(offCurve) + " hits off the curve, worst "
          + str(worst));

  if (basis == "catmullRom") {
    // through the control points between its segments, whose colour is
    // theirs (the strand's ends, p1 and p(n - 2), are caps)
    bool through = true;
    for (size_t k = 2; k + 2 < strand.points.size(); k++) {
      const float2 p(strand.points[k].x, strand.points[k].y);
      const size_t i = pixelAt(p);
      const float4 col = image.color[i];
      through &= image.hit(i)
          && length(float2(4.f * col.x - 2.f, 4.f * col.y - 2.f) - p)
              <= strand.radii[k] + PIXEL;
    }
    pass &= report("catmullRom strand passes through its control points",
        through);
  }
  return pass;
}

// Implicit indexing (n - 3 segments) and explicit 'primitive.index' (the
// segment over vertices i to i + 3), each segment with its 'primitive.id'
// and 'primitive.attribute0' at its midpoint
bool testIndexing(anari::Device d, const std::string &basis)
{
  bool pass = true;
  // implicit: the test strand's two segments
  {
    const Strand strand = testStrand(basis);
    const std::vector<uint32_t> ids = {7, 8};
    auto geom = makeCurve(d, strand, false, [&](anari::Device d, anari::Geometry g) {
      anari::setParameterArray1D(d, g, "primitive.id", ids.data(), ids.size());
    });
    const Image image = render(d, geom, "geometry.color");
    anari::release(d, geom);
    bool ok = !image.depth.empty();
    for (uint32_t s = 0; ok && s < 2; s++) {
      const float3 p = strand.position(s, 0.5f);
      const size_t i = pixelAt(float2(p.x, p.y));
      ok &= image.hit(i) && image.primitiveId[i] == ids[s];
    }
    pass &= report(basis + " implicit indexing: n - 3 segments", ok);
  }

  // explicit: three segments of 8 vertices on three rows, listed out of
  // order; vertices 5 to 7 start no segment
  {
    Strand strand;
    strand.basis = basis;
    for (float y : {0.6f, 0.f, -0.6f}) {
      for (float x : {-1.2f, -0.4f, 0.4f, 1.2f})
        strand.points.push_back(float3(x, y, 0.f));
    }
    // rows at 0.6 (vertices 0-3), 0 (4-7), -0.6 (8-11)
    strand.radii.assign(strand.points.size(), 0.1f);
    strand.starts = {8, 0, 4};
    const std::vector<uint32_t> ids = {100, 101, 102};
    const std::vector<float3> attributes = {
        {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, {0.f, 0.f, 1.f}};
    auto geom = makeCurve(d, strand, true, [&](anari::Device d, anari::Geometry g) {
      anari::setParameterArray1D(d, g, "primitive.id", ids.data(), ids.size());
      anari::setParameterArray1D(d,
          g,
          "primitive.attribute0",
          attributes.data(),
          attributes.size());
    });
    const Image image = render(d, geom, "geometry.attribute0");
    anari::release(d, geom);
    bool ok = !image.depth.empty();
    for (uint32_t s = 0; ok && s < 3; s++) {
      const float3 p = strand.position(s, 0.5f);
      const size_t i = pixelAt(float2(p.x, p.y));
      const float4 c = image.color[i];
      ok &= image.hit(i) && image.primitiveId[i] == ids[s]
          && float3(c.x, c.y, c.z) == attributes[s];
    }
    pass &= report(
        basis + " primitive.index: segments over i to i + 3, per-segment attributes and ids",
        ok);
  }
  return pass;
}

// 'primitive.index' past n - 4 uses the last segment, with a warning;
// fewer than 4 vertices make the curve invalid, with a warning
bool testClampAndShort(anari::Device d, const std::string &basis)
{
  bool pass = true;
  Strand strand = testStrand(basis);
  strand.starts = {0, 1};
  auto geom = makeCurve(d, strand, true);
  const Image reference = render(d, geom, "geometry.color");
  anari::release(d, geom);

  size_t from = g_messages.warnings.size();
  strand.starts = {0, 2};
  geom = makeCurve(d, strand, true);
  const Image clamped = render(d, geom, "geometry.color");
  anari::release(d, geom);
  pass &= report(basis + " primitive.index past the last segment: clamped",
      !clamped.depth.empty() && clamped == reference);
  pass &= report(basis + " primitive.index past the last segment: warning",
      g_messages.warnedSince(from, "past the"));

  from = g_messages.warnings.size();
  Strand shortStrand = testStrand(basis);
  shortStrand.points.resize(3);
  shortStrand.radii.resize(3);
  geom = makeCurve(d, shortStrand, false);
  const Image image = render(d, geom, "geometry.color");
  anari::release(d, geom);
  bool none = !image.depth.empty();
  for (size_t i = 0; i < image.depth.size(); i++)
    none &= !image.hit(i);
  pass &= report(basis + " with 3 vertices: invalid, not drawn", none);
  pass &= report(basis + " with 3 vertices: warning",
      g_messages.warnedSince(from, "at least 4 vertices"));
  return pass;
}

// 'linear' and no 'basis' render the same; an unknown basis warns and draws
// linear segments; a basis committed again changes the curve
bool testLinearAndRecommit(anari::Device d)
{
  bool pass = true;
  Strand strand = testStrand("");
  strand.starts = {0, 1, 2, 3};
  auto geom = makeCurve(d, strand, false);
  const Image unset = render(d, geom, "geometry.color");
  anari::release(d, geom);

  strand.basis = "linear";
  geom = makeCurve(d, strand, false);
  const Image linear = render(d, geom, "geometry.color");
  pass &= report("basis 'linear' renders as no basis",
      !unset.depth.empty() && linear == unset);

  // the same geometry committed as a B-spline, then linear again, in a frame
  // that rendered it before (its group's Embree scene must attach the new
  // Embree geometry)
  auto frame = makeFrame(d, geom, "geometry.color");
  renderFrame(d, frame);
  anari::setParameter(d, geom, "basis", "bspline");
  anari::commitParameters(d, geom);
  const Image recommitted = renderFrame(d, frame);
  auto fresh = makeCurve(d, testStrand("bspline"), false);
  const Image bspline = render(d, fresh, "geometry.color");
  anari::release(d, fresh);
  pass &= report("basis committed again: renders the new basis",
      !(recommitted == linear) && recommitted == bspline);
  anari::setParameter(d, geom, "basis", "linear");
  anari::commitParameters(d, geom);
  pass &= report("basis committed back to 'linear': renders linear",
      renderFrame(d, frame) == linear);
  anari::release(d, frame);
  anari::release(d, geom);

  const size_t from = g_messages.warnings.size();
  strand.basis = "hermite";
  geom = makeCurve(d, strand, false);
  const Image unknown = render(d, geom, "geometry.color");
  anari::release(d, geom);
  pass &= report("unknown basis: linear segments", unknown == linear);
  pass &= report(
      "unknown basis: warning", g_messages.warnedSince(from, "hermite"));
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

  bool pass = true;
  for (const char *basis : {"bspline", "catmullRom"}) {
    pass &= testShape(d, basis);
    pass &= testIndexing(d, basis);
  }
  const size_t warnings = g_messages.warnings.size();
  for (const char *basis : {"bspline", "catmullRom"})
    pass &= testClampAndShort(d, basis);
  pass &= testLinearAndRecommit(d);

  anari::release(d, d);
  anari::unloadLibrary(library);

  // only the warnings the tests provoke: 2 per basis, and the unknown one
  pass &= report("no errors, no other warnings",
      g_messages.errors == 0 && warnings == 0
          && g_messages.warnings.size() == 5,
      std::to_string(g_messages.warnings.size()) + " warnings");
  return pass ? 0 : 1;
}
