// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0
//
// The C++ reference here ports helide's TransferFunction1D::render()
// (ANARI-SDK, src/devices/helide/volume/), Copyright 2021-2026 The Khronos
// Group, licensed under Apache-2.0.

// Checks transferFunction1D volumes: ISPC ray marching against a C++ port of
// helide's TransferFunction1D::render() (jitter included), then the device's
// volume intersection through rendered depth, ids, colors and the
// 'hitVolume' mode, and group/world bounds.

#include "SpatialField_ispc.h"
#include "Volume_ispc.h"
#include "test_volume_ispc.h"
// anari
#define ANARI_LINALG_DEFINITIONS
#include <anari/anari_cpp/ext/linalg.h>
#include <anari/anari_cpp.hpp>
// std
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace {

using namespace anari::math;

// Reference (helide's TransferFunction1D over a FLOAT32 field) ///////////////

struct HelideVolume
{
  uint3 dims;
  float3 origin;
  float3 spacing;
  std::vector<float> voxels; // x fastest
  float2 valueRange{0.f, 1.f};
  float unitDistance{1.f};
  float4 color{1.f};
  float opacity{1.f};
  std::vector<float4> colors; // empty: 'color'
  std::vector<float> opacities; // empty: 'opacity'

  float stepSize() const
  {
    return linalg::minelem(spacing) / 2.f;
  }

  float sampleAt(float3 p) const
  {
    const float3 local = 1.f / spacing * (p - origin);
    if (local.x < 0.f || local.x > dims.x - 1.f || local.y < 0.f
        || local.y > dims.y - 1.f || local.z < 0.f || local.z > dims.z - 1.f)
      return NAN;
    const uint3 i0 = uint3(local);
    const uint3 i1 = linalg::clamp(i0 + 1u, uint3(0u), dims - 1u);
    const float3 f = local - float3(i0);
    auto v = [&](uint32_t x, uint32_t y, uint32_t z) {
      return voxels[x + size_t(dims.x) * (y + size_t(dims.y) * z)];
    };
    const float v00 =
        linalg::lerp(v(i0.x, i0.y, i0.z), v(i1.x, i0.y, i0.z), f.x);
    const float v01 =
        linalg::lerp(v(i0.x, i1.y, i0.z), v(i1.x, i1.y, i0.z), f.x);
    const float v10 =
        linalg::lerp(v(i0.x, i0.y, i1.z), v(i1.x, i0.y, i1.z), f.x);
    const float v11 =
        linalg::lerp(v(i0.x, i1.y, i1.z), v(i1.x, i1.y, i1.z), f.x);
    return linalg::lerp(
        linalg::lerp(v00, v01, f.y), linalg::lerp(v10, v11, f.y), f.z);
  }

  float normalized(float s) const
  {
    const float size = valueRange.y - valueRange.x;
    return std::clamp((s - valueRange.x) * (1.f / size), 0.f, 1.f);
  }

  // helium's Array::valueAtLinear(); at 1 it reads one past the end with
  // weight 0, which here reads the last value instead
  template <typename T>
  static T valueAtLinear(const std::vector<T> &data, float in)
  {
    const float lowf = in * (data.size() - 1.f);
    const size_t lower = size_t(std::floor(lowf));
    const size_t upper = std::min(lower + 1, data.size() - 1);
    const float frac = lowf - lower;
    return linalg::lerp(data[lower], data[upper], frac);
  }

  float4 colorOf(float s) const
  {
    return colors.empty() ? color : valueAtLinear(colors, normalized(s));
  }

  // helide multiplies a uniform 'opacity' by the uniform color's alpha once
  // more; halcyon applies the color's alpha once for every kind of 'opacity'
  float opacityOf(float s) const
  {
    return opacities.empty() ? opacity
                             : valueAtLinear(opacities, normalized(s));
  }

  // helide's jitter: the first draw of a std::mt19937 seeded from the entry t
  static float jitter(float tLower, float stepSize)
  {
    std::mt19937 rng;
    rng.seed(tLower * 10000);
    std::uniform_real_distribution<float> dist(0.f, stepSize);
    return dist(rng);
  }

  float4 render(
      float3 org, float3 dir, float tLower, float tUpper, float invSR) const
  {
    const float step = stepSize() * invSR;
    float t = tLower + jitter(tLower, step);
    float3 color(0.f);
    float opacity = 0.f;
    float transmittance = 1.f;
    while (opacity < 0.99f && tUpper - t >= 0.f) {
      const float s = sampleAt(org + dir * t);
      if (!std::isnan(s)) {
        const float4 co = colorOf(s);
        const float o = opacityOf(s) * co.w;
        const float stepTransmittance = std::pow(1.f - o, step / unitDistance);
        color += transmittance * (1.f - stepTransmittance)
            * float3(co.x, co.y, co.z);
        opacity += transmittance * (1.f - stepTransmittance);
        transmittance *= stepTransmittance;
      }
      t += step;
    }
    return float4(color, opacity);
  }
};

// ISPC object storage, as the device's registry slots hold it
struct IspcStorage
{
  explicit IspcStorage(size_t size) : storage(new uint64_t[(size + 7) / 8]) {}
  void *ie() const
  {
    return storage.get();
  }
  std::unique_ptr<uint64_t[]> storage;
};

// 'v' set up as ISPC field + volume, as the device's finalize() sets them up:
// the field's macrocell value ranges and the volume's macrocell majorants
// in buffers the object owns
struct IspcVolume
{
  IspcStorage field{ispc::sizeof_SpatialField()};
  IspcStorage volume{ispc::sizeof_Volume()};
  std::vector<float> ranges; // packed float2
  std::vector<float> majorants;
};

void setupIspc(const HelideVolume &v, IspcVolume &out)
{
  ispc::SpatialField_setDefaults(out.field.ie());
  ispc::SpatialField_setStructuredRegular(out.field.ie(),
      v.voxels.data(),
      ANARI_FLOAT32,
      &v.dims.x,
      &v.origin.x,
      &v.spacing.x);
  const uint64_t cells = ispc::SpatialField_numMacrocells(out.field.ie());
  out.ranges.resize(2 * cells);
  ispc::SpatialField_computeMacrocellRanges(
      out.field.ie(), out.ranges.data(), 0, cells);
  ispc::SpatialField_setMacrocellRanges(
      out.field.ie(), cells > 0 ? out.ranges.data() : nullptr);
  ispc::Volume_setDefaults(out.volume.ie());
  ispc::Volume_setTransferFunction1D(out.volume.ie(),
      out.field.ie(),
      v.valueRange.x,
      v.valueRange.y,
      v.unitDistance,
      &v.color.x,
      v.colors.empty() ? nullptr : &v.colors[0].x,
      v.colors.size(),
      v.opacity,
      v.opacities.empty() ? nullptr : v.opacities.data(),
      v.opacities.size(),
      0);
  out.majorants.resize(cells);
  ispc::Volume_computeMacrocellMajorants(
      out.volume.ie(), out.majorants.data(), 0, cells);
  ispc::Volume_setMacrocellMajorants(
      out.volume.ie(), cells > 0 ? out.majorants.data() : nullptr, cells);
}

bool close(float4 a, float4 b, float eps = 1e-4f)
{
  return linalg::maxelem(linalg::abs(a - b)) <= eps;
}

// Rays through the volume's box from outside, clipped to it like
// intersectVolumes() does, some ending early (a surface in the box)
bool testRender(const char *name, const HelideVolume &v, float invSR)
{
  IspcVolume ispcVolume;
  setupIspc(v, ispcVolume);
  void *volume = ispcVolume.volume.ie();

  const float3 lower = v.origin;
  const float3 upper = v.origin + (float3(v.dims) - 1.f) * v.spacing;
  const float3 center = 0.5f * (lower + upper);

  std::vector<float3> orgs, dirs;
  std::vector<float> tLowers, tUppers;
  for (int i = 0; i < 67; i++) {
    const float a = 0.37f * i, b = 0.11f * i;
    const float3 dir = linalg::normalize(float3(
        std::cos(a) * std::cos(b), std::sin(b), std::sin(a) * std::cos(b)));
    // aim near the center, entering from 10 units away
    const float3 target = center
        + 0.4f * (upper - lower)
            * float3(
                std::sin(1.3f * i), std::cos(0.7f * i), std::sin(0.3f * i));
    const float3 org = target - 10.f * dir;
    const float3 inv = 1.f / dir;
    const float3 t0 = (lower - org) * inv, t1 = (upper - org) * inv;
    const float tl = std::max(linalg::maxelem(linalg::min(t0, t1)), 0.f);
    float tu = linalg::minelem(linalg::max(t0, t1));
    if (i % 5 == 0)
      tu = tl + 0.5f * (tu - tl);
    orgs.push_back(org);
    dirs.push_back(dir);
    tLowers.push_back(tl);
    tUppers.push_back(tu);
  }

  const int n = int(orgs.size());
  std::vector<float4> results(n);
  ispc::test_render(volume,
      &orgs[0].x,
      &dirs[0].x,
      tLowers.data(),
      tUppers.data(),
      invSR,
      &results[0].x,
      n);

  int failures = 0;
  int visible = 0;
  for (int i = 0; i < n; i++) {
    const float4 expected =
        v.render(orgs[i], dirs[i], tLowers[i], tUppers[i], invSR);
    visible += expected.w > 0.01f;
    if (!close(results[i], expected) && failures++ < 5) {
      std::printf(
          "FAIL %s: ray %d got (%g, %g, %g, %g), expected (%g, %g, %g, %g)\n",
          name,
          i,
          results[i].x,
          results[i].y,
          results[i].z,
          results[i].w,
          expected.x,
          expected.y,
          expected.z,
          expected.w);
    }
  }
  if (visible < n / 2) {
    std::printf(
        "FAIL %s: only %d of %d rays accumulate opacity\n", name, visible, n);
    failures++;
  }
  if (failures == 0)
    std::printf("PASS %s\n", name);
  return failures == 0;
}

// the CTS's 3^3 radial distance field over [-1, 1]^3
HelideVolume radialVolume()
{
  HelideVolume v;
  v.dims = uint3(3u);
  v.origin = float3(-1.f);
  v.spacing = float3(1.f);
  for (int z = 0; z < 3; z++)
    for (int y = 0; y < 3; y++)
      for (int x = 0; x < 3; x++)
        v.voxels.push_back(linalg::length(float3(x - 1.f, y - 1.f, z - 1.f)));
  return v;
}

bool testRendering()
{
  bool pass = true;

  HelideVolume v = radialVolume();
  v.colors = {
      {0.25f, 0.9f, 0.2f, 1.f}, {0.9f, 0.1f, 0.f, 1.f}, {0.f, 0.f, 0.7f, 1.f}};
  v.opacities = {0.25f, 0.7f, 1.0f};
  v.opacity = 0.1f;
  pass &= testRender("color and opacity arrays", v, 1.f);
  pass &= testRender("sampling rate 4", v, 1.f / 4.f);

  HelideVolume thin = v;
  thin.unitDistance = 10.f;
  thin.valueRange = {0.25f, 0.75f};
  pass &= testRender("unit distance and value range", thin, 1.f);

  HelideVolume uniform = radialVolume();
  uniform.color = float4(1.f, 0.75f, 0.f, 0.5f);
  uniform.opacity = 0.3f;
  pass &= testRender("uniform color and opacity", uniform, 1.f);

  HelideVolume mixed = radialVolume();
  mixed.color = float4(0.2f, 0.4f, 0.6f, 0.8f);
  mixed.opacities = {0.f, 1.f};
  pass &= testRender("uniform color, opacity array", mixed, 1.f / 0.5f);

  // anisotropic spacing, off origin
  HelideVolume aniso = radialVolume();
  aniso.origin = float3(2.f, -3.f, 0.5f);
  aniso.spacing = float3(0.2f, 1.5f, 0.7f);
  aniso.colors = v.colors;
  aniso.opacities = {0.05f, 0.2f};
  pass &= testRender("anisotropic spacing", aniso, 1.f);

  return pass;
}

// The pathtracer's majorant (Volume::majorant): the extinction of the
// largest opacity times colour alpha the transfer function blends for the
// values the field takes, and no less than the extinction anywhere in the
// field
bool testMajorant()
{
  bool pass = true;
  // voxels 0.2 to 0.6 over 3^3 points of [0, 2]^3
  HelideVolume base;
  base.dims = uint3(3);
  base.origin = float3(0.f);
  base.spacing = float3(1.f);
  for (int i = 0; i < 27; i++)
    base.voxels.push_back(0.2f + 0.4f * float((i * 7) % 27) / 26.f);
  std::vector<float> ramp; // 0, 0.1, ..., 1
  for (int i = 0; i <= 10; i++)
    ramp.push_back(float(i) / 10.f);
  const auto extinction = [](float alpha, float unitDistance) {
    return -std::log(1.f - std::min(alpha, 0.999f)) / unitDistance;
  };

  struct Case
  {
    const char *name;
    HelideVolume v;
    float expected;
  };
  std::vector<Case> cases;
  {
    Case c{"opacity ramp over the field's values", base, extinction(0.6f, 1.f)};
    c.v.opacities = ramp;
    cases.push_back(c);
  }
  {
    // values map to 1 - s: [0.4, 0.8]
    Case c{"reversed valueRange", base, extinction(0.8f, 2.f)};
    c.v.opacities = ramp;
    c.v.valueRange = float2(1.f, 0.f);
    c.v.unitDistance = 2.f;
    cases.push_back(c);
  }
  {
    Case c{"uniform opacity and colour alpha", base, extinction(0.25f, 1.f)};
    c.v.opacity = 0.5f;
    c.v.color = float4(1.f, 1.f, 1.f, 0.5f);
    cases.push_back(c);
  }
  {
    Case c{"colour alpha ramp", base, extinction(0.6f * 0.7f, 1.f)};
    c.v.opacity = 0.7f;
    for (float a : ramp)
      c.v.colors.push_back(float4(1.f, 0.f, 0.f, a));
    cases.push_back(c);
  }
  {
    Case c{"unitDistance 0", base, 0.f};
    c.v.unitDistance = 0.f;
    cases.push_back(c);
  }
  {
    // no value known: all of the transfer function
    Case c{"a field of NaN", base, extinction(1.f, 1.f)};
    std::fill(c.v.voxels.begin(), c.v.voxels.end(), NAN);
    c.v.opacities = ramp;
    cases.push_back(c);
  }

  std::mt19937 rng(42);
  std::uniform_real_distribution<float> uniform(0.f, 2.f);
  std::vector<float3> points(4096);
  for (auto &p : points)
    p = float3(uniform(rng), uniform(rng), uniform(rng));
  for (const auto &c : cases) {
    IspcVolume ispcVolume;
    setupIspc(c.v, ispcVolume);
    void *volume = ispcVolume.volume.ie();
    float maxExtinction = 0.f;
    const float majorant = ispc::test_majorant(
        volume, &points[0].x, int(points.size()), &maxExtinction);
    const bool ok = std::abs(majorant - c.expected) <= 1e-5f * c.expected
        && maxExtinction <= majorant;
    std::printf("%s majorant: %s (%g, expected %g, largest sample %g)\n",
        ok ? "PASS" : "FAIL",
        c.name,
        majorant,
        c.expected,
        maxExtinction);
    pass &= ok;
  }
  return pass;
}

// The pathtracer's macrocells (Volume_majorantFrom()): rays walked cell by
// cell through a sparse field (one blob in a corner) of several cells per
// axis with anisotropic spacing. Each step must make progress, stay in one
// cell (no longer than its diagonal) and bound the extinction everywhere
// along it; most of the path length must lie in cells of majorant 0, and no
// cell's majorant may exceed the volume's.
bool testMacrocells()
{
  HelideVolume v;
  v.dims = uint3(41, 41, 41);
  v.origin = float3(-1.f, -2.f, 0.5f);
  v.spacing = float3(0.05f, 0.1f, 0.04f);
  for (uint32_t k = 0; k < v.dims.z; k++) {
    for (uint32_t j = 0; j < v.dims.y; j++) {
      for (uint32_t i = 0; i < v.dims.x; i++) {
        const float3 p = float3(float(i), float(j), float(k)) / 40.f;
        // a solid ball, so that neighbouring cells differ sharply
        v.voxels.push_back(linalg::length(p - float3(0.75f)) < 0.15f ? 1.f : 0.f);
      }
    }
  }
  v.opacities = {0.f, 0.5f, 0.95f};
  v.unitDistance = 0.1f;

  IspcVolume ispcVolume;
  setupIspc(v, ispcVolume);
  void *volume = ispcVolume.volume.ie();
  // 8 voxel intervals per cell
  const float3 cell = 8.f * v.spacing;
  const float cellDiagonal = linalg::length(cell);
  const float3 lower = v.origin;
  const float3 upper = v.origin + (float3(v.dims) - 1.f) * v.spacing;

  std::mt19937 rng(7);
  std::uniform_real_distribution<float> unit(0.f, 1.f);
  std::vector<float3> orgs, dirs;
  for (int i = 0; i < 2000; i++) {
    orgs.push_back(lower + (upper - lower) * float3(unit(rng), unit(rng), unit(rng)));
    const float z = 1.f - 2.f * unit(rng), phi = 6.2831853f * unit(rng);
    const float r = std::sqrt(std::max(0.f, 1.f - z * z));
    dirs.push_back(float3(r * std::cos(phi), r * std::sin(phi), z));
  }
  // axis-aligned rays from cell boundaries
  for (int i = 0; i < 64; i++) {
    float3 o = lower + (upper - lower) * float3(unit(rng), unit(rng), unit(rng));
    const int axis = i % 3;
    o[axis] = lower[axis] + cell[axis] * float(1 + i % 4);
    float3 d(0.f);
    d[axis] = i % 2 ? 1.f : -1.f;
    orgs.push_back(o);
    dirs.push_back(d);
  }

  const size_t n = orgs.size();
  std::vector<float> t(n, 0.f), tEnd(n);
  for (size_t i = 0; i < n; i++) {
    const float3 a = (lower - orgs[i]) / dirs[i];
    const float3 b = (upper - orgs[i]) / dirs[i];
    tEnd[i] = linalg::minelem(linalg::max(a, b));
  }

  bool progress = true, inOneCell = true, bounded = true;
  int maxSteps = 0;
  double emptyLength = 0.0, totalLength = 0.0;
  float worst = 0.f;
  for (int step = 0; step < 64; step++) {
    std::vector<size_t> active;
    for (size_t i = 0; i < n; i++)
      if (t[i] < tEnd[i])
        active.push_back(i);
    if (active.empty())
      break;
    maxSteps = step + 1;
    std::vector<float3> o, d;
    std::vector<float> ts;
    for (size_t i : active) {
      o.push_back(orgs[i]);
      d.push_back(dirs[i]);
      ts.push_back(t[i]);
    }
    const int m = int(active.size());
    std::vector<float> majorants(m), exits(m);
    ispc::test_macrocellMajorant(volume,
        &o[0].x,
        &d[0].x,
        ts.data(),
        majorants.data(),
        exits.data(),
        m);
    constexpr int SAMPLES = 8;
    std::vector<float3> points;
    for (int j = 0; j < m; j++) {
      const float e = std::min(exits[j], tEnd[active[j]]);
      for (int k = 0; k < SAMPLES; k++)
        points.push_back(o[j] + d[j] * (ts[j] + (e - ts[j]) * (k + 0.5f) / SAMPLES));
    }
    std::vector<float> sigma(points.size());
    ispc::test_extinction(volume, &points[0].x, sigma.data(), int(points.size()));
    for (int j = 0; j < m; j++) {
      const size_t i = active[j];
      const float e = std::min(exits[j], tEnd[i]);
      progress &= exits[j] > ts[j];
      inOneCell &= e - ts[j] <= cellDiagonal * 1.0001f;
      for (int k = 0; k < SAMPLES; k++) {
        const float s = sigma[size_t(j) * SAMPLES + k];
        bounded &= s <= majorants[j] * 1.0001f + 1e-6f;
        worst = std::max(worst, s - majorants[j]);
      }
      totalLength += e - ts[j];
      if (majorants[j] == 0.f)
        emptyLength += e - ts[j];
      t[i] = exits[j] > ts[j] ? e : tEnd[i];
    }
  }
  float unused;
  std::vector<float3> none = {float3(0.f)};
  const float volumeMajorant = ispc::test_majorant(volume, &none[0].x, 1, &unused);
  const float cellMax = *std::max_element(
      ispcVolume.majorants.begin(), ispcVolume.majorants.end());

  bool pass = true;
  auto report = [&](const char *name, bool ok, const std::string &detail) {
    std::printf("%s macrocells: %s%s\n", ok ? "PASS" : "FAIL", name, detail.c_str());
    pass &= ok;
  };
  report("125 cells", ispcVolume.majorants.size() == 125,
      " (" + std::to_string(ispcVolume.majorants.size()) + ")");
  report("each step makes progress within 16 steps", progress && maxSteps <= 16,
      " (" + std::to_string(maxSteps) + " steps)");
  report("each step stays in one cell", inOneCell, "");
  report("the majorant bounds the extinction along each step", bounded,
      " (largest excess " + std::to_string(worst) + ")");
  const double emptyFraction = emptyLength / totalLength;
  report("most of the length is in empty cells", emptyFraction > 0.8,
      " (" + std::to_string(emptyFraction) + ")");
  report("no cell's majorant exceeds the volume's",
      cellMax > 0.f && cellMax <= volumeMajorant,
      " (" + std::to_string(cellMax) + " vs " + std::to_string(volumeMajorant) + ")");
  return pass;
}

// helide's jitter, which depends only on the entry t: ported exactly
bool testJitter()
{
  std::vector<float> ts;
  for (int i = 0; i < 1000; i++)
    ts.push_back(0.0137f * i * i + 0.001f * i);
  ts.push_back(0.f);
  ts.push_back(429496.7f); // seed near 2^32
  const float step = 0.37f;

  std::vector<float> results(ts.size());
  ispc::test_jitter(ts.data(), step, results.data(), int(ts.size()));

  int failures = 0;
  for (size_t i = 0; i < ts.size(); i++) {
    const float expected = HelideVolume::jitter(ts[i], step);
    if (results[i] != expected && failures++ < 5)
      std::printf("FAIL jitter(%g): got %.9g, expected %.9g\n",
          ts[i],
          results[i],
          expected);
  }
  if (failures == 0)
    std::printf("PASS jitter matches std::mt19937\n");
  return failures == 0;
}

// Device //////////////////////////////////////////////////////////////////////

constexpr uint2 IMAGE_SIZE = {64, 64};
constexpr uint32_t NONE = ~0u;

// device errors (e.g. ISPC object size mismatches) fail the test
bool g_deviceError = false;
std::vector<std::string> g_warnings;

void statusFunc(const void *,
    ANARIDevice,
    ANARIObject,
    ANARIDataType,
    ANARIStatusSeverity severity,
    ANARIStatusCode,
    const char *message)
{
  if (severity <= ANARI_SEVERITY_ERROR)
    g_deviceError = true;
  if (severity <= ANARI_SEVERITY_WARNING) {
    std::fprintf(stderr, "[ANARI] %s\n", message);
    g_warnings.push_back(message);
  }
}

// A 2^3 FLOAT32 field of 'value' filling [lower, upper]
anari::SpatialField makeField(
    anari::Device d, float3 lower, float3 upper, float value = 0.5f)
{
  const std::vector<float> voxels(8, value);
  auto f = anari::newObject<anari::SpatialField>(d, "structuredRegular");
  anari::setParameterArray3D(d, f, "data", voxels.data(), 2, 2, 2);
  anari::setParameter(d, f, "origin", lower);
  anari::setParameter(d, f, "spacing", upper - lower);
  anari::commitParameters(d, f);
  return f;
}

// transferFunction1D volume with a uniform 'color' and 'opacity' over a box
anari::Volume makeVolume(anari::Device d,
    float3 lower,
    float3 upper,
    uint32_t id,
    float4 color = float4(1.f, 0.f, 0.f, 1.f),
    float opacity = 1.f)
{
  auto vol = anari::newObject<anari::Volume>(d, "transferFunction1D");
  anari::setAndReleaseParameter(d, vol, "value", makeField(d, lower, upper));
  anari::setParameter(d, vol, "color", color);
  anari::setParameter(d, vol, "opacity", opacity);
  anari::setParameter(d, vol, "id", id);
  anari::commitParameters(d, vol);
  return vol;
}

// A white matte quad in the z = 'z' plane over [lo, hi] in x and y, id 'id'
anari::Surface makeQuad(
    anari::Device d, float2 lo, float2 hi, float z, uint32_t id)
{
  const std::vector<float3> verts = {
      {lo.x, lo.y, z}, {hi.x, lo.y, z}, {hi.x, hi.y, z}, {lo.x, hi.y, z}};
  const std::vector<uint3> indices = {{0, 1, 2}, {0, 2, 3}};
  auto geom = anari::newObject<anari::Geometry>(d, "triangle");
  anari::setParameterArray1D(
      d, geom, "vertex.position", verts.data(), verts.size());
  anari::setParameterArray1D(
      d, geom, "primitive.index", indices.data(), indices.size());
  anari::commitParameters(d, geom);
  auto mat = anari::newObject<anari::Material>(d, "matte");
  anari::setParameter(d, mat, "color", float3(1.f));
  anari::commitParameters(d, mat);
  auto surface = anari::newObject<anari::Surface>(d);
  anari::setAndReleaseParameter(d, surface, "geometry", geom);
  anari::setAndReleaseParameter(d, surface, "material", mat);
  anari::setParameter(d, surface, "id", id);
  anari::commitParameters(d, surface);
  return surface;
}

struct Pixels
{
  std::vector<float4> color;
  std::vector<float> depth;
  std::vector<uint32_t> primId, objId, instId;
};

// Renders 'world' seen by an orthographic camera at z = 5 looking down -z at
// [-1, 1]^2, over a transparent black background, with 'mode' and
// 'volumeSamplingRate' (helide's 1 unless given; not set if nullopt)
Pixels render(anari::Device d,
    anari::World world,
    const char *mode = "default",
    std::optional<float> samplingRate = 1.f)
{
  auto camera = anari::newObject<anari::Camera>(d, "orthographic");
  anari::setParameter(d, camera, "position", float3(0.f, 0.f, 5.f));
  anari::setParameter(d, camera, "direction", float3(0.f, 0.f, -1.f));
  anari::setParameter(d, camera, "up", float3(0.f, 1.f, 0.f));
  anari::setParameter(d, camera, "height", 2.f);
  anari::commitParameters(d, camera);

  auto renderer = anari::newObject<anari::Renderer>(d, "preview");
  anari::setParameter(d, renderer, "background", float4(0.f));
  anari::setParameter(d, renderer, "mode", mode);
  if (samplingRate)
    anari::setParameter(d, renderer, "volumeSamplingRate", *samplingRate);
  anari::commitParameters(d, renderer);

  auto frame = anari::newObject<anari::Frame>(d);
  anari::setParameter(d, frame, "size", IMAGE_SIZE);
  anari::setParameter(d, frame, "channel.color", ANARI_FLOAT32_VEC4);
  anari::setParameter(d, frame, "channel.depth", ANARI_FLOAT32);
  anari::setParameter(d, frame, "channel.primitiveId", ANARI_UINT32);
  anari::setParameter(d, frame, "channel.objectId", ANARI_UINT32);
  anari::setParameter(d, frame, "channel.instanceId", ANARI_UINT32);
  anari::setAndReleaseParameter(d, frame, "camera", camera);
  anari::setAndReleaseParameter(d, frame, "renderer", renderer);
  anari::setParameter(d, frame, "world", world);
  anari::commitParameters(d, frame);
  anari::render(d, frame);
  anari::wait(d, frame);

  Pixels p;
  const size_t n = size_t(IMAGE_SIZE.x) * IMAGE_SIZE.y;
  auto copy = [&](const char *channel, auto &out) {
    using T = typename std::decay_t<decltype(out)>::value_type;
    auto fb = anari::map<T>(d, frame, channel);
    if (fb.data)
      out.assign(fb.data, fb.data + n);
    else
      out.assign(n, T{});
    anari::unmap(d, frame, channel);
  };
  copy("channel.color", p.color);
  copy("channel.depth", p.depth);
  copy("channel.primitiveId", p.primId);
  copy("channel.objectId", p.objId);
  copy("channel.instanceId", p.instId);
  anari::release(d, frame);
  return p;
}

size_t pixelIndex(float2 pos)
{
  const auto x = uint32_t((pos.x + 1.f) / 2.f * IMAGE_SIZE.x);
  const auto y = uint32_t((pos.y + 1.f) / 2.f * IMAGE_SIZE.y);
  return size_t(y) * IMAGE_SIZE.x + x;
}

struct Probe
{
  const char *name;
  float2 pos;
  float depth;
  uint32_t primId, objId, instId;
  float4 color; // default mode
  float4 hitVolumeColor; // 'hitVolume' mode
};

const float4 RED(1.f, 0.f, 0.f, 1.f);
const float4 GREEN(0.f, 1.f, 0.f, 1.f);
const float4 WHITE(1.f);
const float4 CLEAR(0.f);
const float4 BLACK(0.f, 0.f, 0.f, 1.f);
const float4 HALF_RED_OVER_WHITE(1.f, 0.5f, 0.5f, 1.f);

// World (id 30), seen from z = 5:
//  - an opaque red world-level volume (id 5) over x, y in [-0.9, -0.5],
//    z in [-1, 1]
//  - an opaque red volume (id 6) in a group instanced (id 9) with a
//    scale of 0.5 and a translation of (0.5, 0.5, 0): its box
//    [-0.4, 0.4]^2 x [-1, 1] lands on x, y in [0.3, 0.7], z in [-0.5, 0.5]
//  - a white quad (id 8) at z = 0 over x in [-0.3, 0.3], y in [-0.9, -0.1],
//    with a thin red volume (id 7, half opaque over its depth) in front of
//    its lower half (z in [0.6, 1]) and an opaque volume (id 10) behind it
//    (z in [-1, -0.5])
anari::World makeWorld(anari::Device d)
{
  auto groupVolume =
      makeVolume(d, float3(-0.4f, -0.4f, -1.f), float3(0.4f, 0.4f, 1.f), 6);
  auto group = anari::newObject<anari::Group>(d);
  anari::setParameterArray1D(d, group, "volume", &groupVolume, 1);
  anari::commitParameters(d, group);
  anari::release(d, groupVolume);

  auto inst = anari::newObject<anari::Instance>(d, "transform");
  const mat4 xfm =
      linalg::mul(linalg::translation_matrix(float3(0.5f, 0.5f, 0.f)),
          linalg::scaling_matrix(float3(0.5f)));
  anari::setParameter(d, inst, "transform", xfm);
  anari::setParameter(d, inst, "id", uint32_t(9));
  anari::setAndReleaseParameter(d, inst, "group", group);
  anari::commitParameters(d, inst);

  // the thin volume's step size is 0.2 (half its smallest spacing), so a
  // jittered march takes 2 samples over its depth 0.4: each passes
  // 1 - (1 - o)^0.2, and o = 1 - 0.5^2.5 makes the total opacity 0.5
  const float thinOpacity = 1.f - std::pow(0.5f, 2.5f);
  std::vector<anari::Volume> volumes = {
      makeVolume(d, float3(-0.9f, -0.9f, -1.f), float3(-0.5f, -0.5f, 1.f), 5),
      makeVolume(d,
          float3(-0.3f, -0.9f, 0.6f),
          float3(0.3f, -0.5f, 1.f),
          7,
          RED,
          thinOpacity),
      makeVolume(
          d, float3(-0.3f, -0.9f, -1.f), float3(0.3f, -0.1f, -0.5f), 10)};
  auto quad = makeQuad(d, float2(-0.3f, -0.9f), float2(0.3f, -0.1f), 0.f, 8);

  auto world = anari::newObject<anari::World>(d);
  anari::setParameterArray1D(d, world, "instance", &inst, 1);
  anari::setParameterArray1D(
      d, world, "volume", volumes.data(), volumes.size());
  anari::setParameterArray1D(d, world, "surface", &quad, 1);
  anari::setParameter(d, world, "id", uint32_t(30));
  anari::commitParameters(d, world);
  anari::release(d, inst);
  for (auto v : volumes)
    anari::release(d, v);
  anari::release(d, quad);
  return world;
}

const std::vector<Probe> PROBES = {
    {"miss", {0.9f, -0.9f}, FLT_MAX, NONE, NONE, NONE, CLEAR, CLEAR},
    {"world volume", {-0.7f, -0.7f}, 4.f, 0, 5, 30, RED, CLEAR},
    {"instanced volume", {0.5f, 0.5f}, 4.5f, 0, 6, 9, RED, CLEAR},
    // the thin volume is in front of the quad: its ids win, and it is
    // composited over the quad
    {"thin volume over quad",
        {0.1f, -0.75f},
        4.f,
        0,
        7,
        30,
        HALF_RED_OVER_WHITE,
        GREEN},
    // helide reports the volume behind the quad; halcyon clips volumes to
    // the surface hit and misses it
    // ('hitVolume' paints surfaces without a volume hit with the background)
    {"volume behind quad", {0.1f, -0.25f}, 5.f, 1, 8, 30, WHITE, BLACK},
};

bool testIntersection(anari::Device d, anari::World world)
{
  const Pixels p = render(d, world);
  const Pixels hv = render(d, world, "hitVolume");
  int failures = 0;
  for (const auto &probe : PROBES) {
    const size_t i = pixelIndex(probe.pos);
    auto fail = [&](const char *what) {
      std::printf(
          "FAIL %s: %s (depth %g, ids %u/%u/%u, color %g %g %g %g, "
          "hitVolume color %g %g %g %g)\n",
          probe.name,
          what,
          p.depth[i],
          p.primId[i],
          p.objId[i],
          p.instId[i],
          p.color[i].x,
          p.color[i].y,
          p.color[i].z,
          p.color[i].w,
          hv.color[i].x,
          hv.color[i].y,
          hv.color[i].z,
          hv.color[i].w);
      failures++;
    };
    if (std::fabs(p.depth[i] - probe.depth)
        > 1e-3f * std::max(1.f, probe.depth))
      fail("depth");
    if (p.primId[i] != probe.primId || p.objId[i] != probe.objId
        || p.instId[i] != probe.instId)
      fail("ids");
    if (!close(p.color[i], probe.color, 0.02f))
      fail("color");
    if (!close(hv.color[i], probe.hitVolumeColor, 1e-4f))
      fail("hitVolume color");
  }
  std::printf("%s volume intersection\n", failures == 0 ? "PASS" : "FAIL");
  return failures == 0;
}

// 'volumeSamplingRate' defaults to 0.5. A translucent volume 1 deep with
// x spacing 0.6 is marched with steps of 0.6 (at 0.5) or 0.3 (at helide's 1),
// which cover different parts of its depth, so the two differ in opacity.
bool testDefaultSamplingRate(anari::Device d)
{
  auto vol = makeVolume(d,
      float3(-0.3f, -1.f, -0.5f),
      float3(0.3f, 1.f, 0.5f),
      1,
      RED,
      0.3f);
  auto world = anari::newObject<anari::World>(d);
  anari::setParameterArray1D(d, world, "volume", &vol, 1);
  anari::commitParameters(d, world);

  const size_t i = pixelIndex(float2(0.f));
  const float4 byDefault = render(d, world, "default", std::nullopt).color[i];
  const float4 half = render(d, world, "default", 0.5f).color[i];
  const float4 one = render(d, world, "default", 1.f).color[i];
  const bool pass = byDefault == half && !close(half, one, 1e-3f);
  std::printf(
      "%s volumeSamplingRate default (opacity %g; rate 0.5: %g, rate 1: %g)\n",
      pass ? "PASS" : "FAIL",
      byDefault.w,
      half.w,
      one.w);

  anari::release(d, world);
  anari::release(d, vol);
  return pass;
}

struct box3
{
  float3 lower;
  float3 upper;
};

bool getBounds(anari::Device d, anari::Object o, box3 &b)
{
  return anariGetProperty(
      d, o, "bounds", ANARI_FLOAT32_BOX3, &b, sizeof(b), ANARI_WAIT);
}

bool sameBox(const box3 &a, const box3 &b)
{
  return linalg::maxelem(linalg::abs(a.lower - b.lower)) < 1e-5f
      && linalg::maxelem(linalg::abs(a.upper - b.upper)) < 1e-5f;
}

bool expectBounds(
    anari::Device d, anari::Object o, box3 expected, const char *name)
{
  box3 b;
  const bool pass = getBounds(d, o, b) && sameBox(b, expected);
  if (pass)
    std::printf("PASS bounds: %s\n", name);
  else {
    std::printf("FAIL bounds: %s: (%g, %g, %g)-(%g, %g, %g)\n",
        name,
        b.lower.x,
        b.lower.y,
        b.lower.z,
        b.upper.x,
        b.upper.y,
        b.upper.z);
  }
  return pass;
}

bool testBounds(anari::Device d)
{
  bool pass = true;

  auto vol = makeVolume(d, float3(1.f, 2.f, 3.f), float3(2.f, 4.f, 3.5f), 0);
  auto group = anari::newObject<anari::Group>(d);
  anari::setParameterArray1D(d, group, "volume", &vol, 1);
  anari::commitParameters(d, group);
  pass &= expectBounds(
      d, group, {{1.f, 2.f, 3.f}, {2.f, 4.f, 3.5f}}, "group volume");

  // world bounds transform the instanced volume
  auto inst = anari::newObject<anari::Instance>(d, "transform");
  const mat4 xfm =
      linalg::mul(linalg::translation_matrix(float3(10.f, 0.f, 0.f)),
          linalg::scaling_matrix(float3(2.f)));
  anari::setParameter(d, inst, "transform", xfm);
  anari::setParameter(d, inst, "group", group);
  anari::commitParameters(d, inst);
  auto world = anari::newObject<anari::World>(d);
  anari::setParameterArray1D(d, world, "instance", &inst, 1);
  anari::commitParameters(d, world);
  pass &= expectBounds(d,
      world,
      {{12.f, 4.f, 6.f}, {14.f, 8.f, 7.f}},
      "instanced volume in world");

  // plus a world-level volume
  auto worldVol = makeVolume(d, float3(-1.f), float3(0.f), 0);
  anari::setParameterArray1D(d, world, "volume", &worldVol, 1);
  anari::commitParameters(d, world);
  pass &= expectBounds(
      d, world, {{-1.f, -1.f, -1.f}, {14.f, 8.f, 7.f}}, "world volume");

  // invisible volumes don't count
  anari::setParameter(d, worldVol, "visible", false);
  anari::commitParameters(d, worldVol);
  pass &= expectBounds(
      d, world, {{12.f, 4.f, 6.f}, {14.f, 8.f, 7.f}}, "invisible world volume");

  // nor do invalid ones (field without data)
  auto field = anari::newObject<anari::SpatialField>(d, "structuredRegular");
  anari::commitParameters(d, field);
  anari::setParameter(d, vol, "value", field);
  anari::commitParameters(d, vol);
  anari::release(d, field);
  bool valid = true;
  anari::getProperty(d, vol, "valid", valid, ANARI_WAIT);
  pass &= !valid;
  std::printf(
      "%s volume with an invalid field is invalid\n", !valid ? "PASS" : "FAIL");
  box3 b;
  const bool empty = getBounds(d, group, b) && b.lower.x > b.upper.x;
  std::printf("%s bounds: invalid volume\n", empty ? "PASS" : "FAIL");
  pass &= empty;

  auto unknown = anari::newObject<anari::Volume>(d, "unknown");
  anari::commitParameters(d, unknown);
  anari::getProperty(d, unknown, "valid", valid, ANARI_WAIT);
  std::printf(
      "%s unknown volume subtype is invalid\n", !valid ? "PASS" : "FAIL");
  pass &= !valid;

  anari::release(d, unknown);
  anari::release(d, worldVol);
  anari::release(d, world);
  anari::release(d, inst);
  anari::release(d, group);
  anari::release(d, vol);
  return pass;
}

// An invisible volume isn't rendered, and one shown again is
bool testVisibility(anari::Device d)
{
  auto vol = makeVolume(d, float3(-1.f), float3(1.f), 3);
  auto world = anari::newObject<anari::World>(d);
  anari::setParameterArray1D(d, world, "volume", &vol, 1);
  anari::commitParameters(d, world);

  const size_t center = pixelIndex(float2(0.f));
  bool pass = render(d, world).objId[center] == 3;
  anari::setParameter(d, vol, "visible", false);
  anari::commitParameters(d, vol);
  pass &= render(d, world).objId[center] == NONE;
  anari::setParameter(d, vol, "visible", true);
  anari::commitParameters(d, vol);
  pass &= render(d, world).objId[center] == 3;

  std::printf("%s volume visibility\n", pass ? "PASS" : "FAIL");
  anari::release(d, world);
  anari::release(d, vol);
  return pass;
}

// A 'unitDistance' of 0 or less (or not finite) makes the volume invalid,
// with a warning, so neither renderer draws it; a valid one draws it again
bool testUnitDistance(anari::Device d)
{
  auto vol = makeVolume(d, float3(-1.f), float3(1.f), 3);
  auto world = anari::newObject<anari::World>(d);
  anari::setParameterArray1D(d, world, "volume", &vol, 1);
  anari::commitParameters(d, world);

  const size_t center = pixelIndex(float2(0.f));
  const auto warned = [&](size_t from) {
    for (size_t i = from; i < g_warnings.size(); i++) {
      if (g_warnings[i].find("'unitDistance'") != std::string::npos)
        return true;
    }
    return false;
  };
  bool pass = true;
  for (float u : {0.f, -1.f, NAN}) {
    const size_t from = g_warnings.size();
    anari::setParameter(d, vol, "unitDistance", u);
    anari::commitParameters(d, vol);
    pass &= render(d, world).objId[center] == NONE && warned(from);
  }
  anari::setParameter(d, vol, "unitDistance", 2.f);
  anari::commitParameters(d, vol);
  pass &= render(d, world).objId[center] == 3;

  std::printf("%s unitDistance of 0 or less makes the volume invalid\n",
      pass ? "PASS" : "FAIL");
  anari::release(d, world);
  anari::release(d, vol);
  return pass;
}

} // namespace

int main()
{
  bool pass = true;
  pass &= testJitter();
  pass &= testRendering();
  pass &= testMajorant();
  pass &= testMacrocells();

  auto library = anari::loadLibrary("halcyon", statusFunc);
  if (!library) {
    std::fprintf(stderr, "failed to load halcyon library\n");
    return 1;
  }
  auto d = anari::newDevice(library, "default");

  auto world = makeWorld(d);
  pass &= testIntersection(d, world);
  anari::release(d, world);
  pass &= testBounds(d);
  pass &= testVisibility(d);
  pass &= testUnitDistance(d);
  pass &= testDefaultSamplingRate(d);

  anari::release(d, d);
  anari::unloadLibrary(library);

  if (g_deviceError)
    std::printf("FAIL: device reported errors\n");

  return pass && !g_deviceError ? 0 : 1;
}
