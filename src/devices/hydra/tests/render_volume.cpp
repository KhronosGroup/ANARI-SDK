// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0
//
// structuredRegular spatial fields classified by transferFunction1D volumes
// (SPEC §7). An orthographic camera looks down -Z through volumes whose
// classification is known in closed form: with no lighting (emission-
// absorption), a ray crossing object-space thickness L of constant color c and
// per-unitDistance opacity a reads back c * (1 - (1 - a)^(L / unitDistance))
// over a black background. The generated shader clips every ray-march step to
// the field domain and composites it exactly, so these hold to float noise.

#include <anari/anari.h>
#ifdef ANARI_HYDRA_HAS_NANOVDB
#include <nanovdb/tools/CreateNanoGrid.h>
#include <nanovdb/tools/GridBuilder.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

static int g_errors = 0;

static void statusFunc(const void *, ANARIDevice, ANARIObject, ANARIDataType,
    ANARIStatusSeverity severity, ANARIStatusCode, const char *message)
{
  if (severity <= ANARI_SEVERITY_WARNING)
    std::printf("[%s] %s\n",
        severity <= ANARI_SEVERITY_ERROR ? "ERR" : "WARN", message);
  if (severity <= ANARI_SEVERITY_ERROR)
    ++g_errors;
}

static void check(bool ok, const std::string &what)
{
  std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
  if (!ok)
    ++g_errors;
}

static bool near(float a, float b, float tol)
{
  return std::fabs(a - b) <= tol;
}

static constexpr uint32_t W = 64, H = 64;

struct Image
{
  std::vector<float> rgba;
  const float *at(int x, int y) const { return &rgba[(size_t(y) * W + x) * 4]; }
  // Pixel whose centre sits at world (wx, wy) under the ortho camera below.
  const float *world(float wx, float wy) const
  {
    const int x = int((wx + 2.f) / 4.f * W);
    const int y = int((wy + 2.f) / 4.f * H);
    return at(std::clamp(x, 0, int(W) - 1), std::clamp(y, 0, int(H) - 1));
  }
};

struct Scene
{
  ANARIDevice d{nullptr};
  ANARIWorld world{nullptr};
  ANARICamera camera{nullptr};
  ANARIRenderer renderer{nullptr};
  ANARIFrame frame{nullptr};

  explicit Scene(ANARIDevice dev) : d(dev)
  {
    world = anariNewWorld(d);
    anariCommitParameters(d, world);
    // Orthographic, looking down -Z, 4x4 world units centred on the origin.
    camera = anariNewCamera(d, "orthographic");
    float pos[3] = {0.f, 0.f, 10.f}, dir[3] = {0.f, 0.f, -1.f},
          up[3] = {0.f, 1.f, 0.f}, height = 4.f, aspect = 1.f;
    anariSetParameter(d, camera, "position", ANARI_FLOAT32_VEC3, pos);
    anariSetParameter(d, camera, "direction", ANARI_FLOAT32_VEC3, dir);
    anariSetParameter(d, camera, "up", ANARI_FLOAT32_VEC3, up);
    anariSetParameter(d, camera, "height", ANARI_FLOAT32, &height);
    anariSetParameter(d, camera, "aspect", ANARI_FLOAT32, &aspect);
    anariCommitParameters(d, camera);
    renderer = anariNewRenderer(d, "default");
    float bg[4] = {0.f, 0.f, 0.f, 1.f};
    anariSetParameter(d, renderer, "background", ANARI_FLOAT32_VEC4, bg);
    anariCommitParameters(d, renderer);
    frame = anariNewFrame(d);
    uint32_t size[2] = {W, H};
    ANARIDataType fmt = ANARI_FLOAT32_VEC4;
    anariSetParameter(d, frame, "size", ANARI_UINT32_VEC2, size);
    anariSetParameter(d, frame, "channel.color", ANARI_DATA_TYPE, &fmt);
    anariSetParameter(d, frame, "world", ANARI_WORLD, &world);
    anariSetParameter(d, frame, "camera", ANARI_CAMERA, &camera);
    anariSetParameter(d, frame, "renderer", ANARI_RENDERER, &renderer);
    anariCommitParameters(d, frame);
  }

  ~Scene()
  {
    anariRelease(d, frame);
    anariRelease(d, renderer);
    anariRelease(d, camera);
    anariRelease(d, world);
  }

  void setVolumes(const std::vector<ANARIVolume> &volumes)
  {
    if (volumes.empty()) {
      anariUnsetParameter(d, world, "volume");
    } else {
      ANARIArray1D a = anariNewArray1D(d, volumes.data(), nullptr, nullptr,
          ANARI_VOLUME, volumes.size());
      anariSetParameter(d, world, "volume", ANARI_ARRAY1D, &a);
      anariRelease(d, a);
    }
    anariCommitParameters(d, world);
  }

  void setInstances(const std::vector<ANARIInstance> &instances)
  {
    if (instances.empty()) {
      anariUnsetParameter(d, world, "instance");
    } else {
      ANARIArray1D a = anariNewArray1D(d, instances.data(), nullptr, nullptr,
          ANARI_INSTANCE, instances.size());
      anariSetParameter(d, world, "instance", ANARI_ARRAY1D, &a);
      anariRelease(d, a);
    }
    anariCommitParameters(d, world);
  }

  Image render()
  {
    anariRenderFrame(d, frame);
    anariFrameReady(d, frame, ANARI_WAIT);
    uint32_t w = 0, h = 0;
    ANARIDataType type = ANARI_UNKNOWN;
    const float *p = static_cast<const float *>(
        anariMapFrame(d, frame, "channel.color", &w, &h, &type));
    Image img;
    if (p && w == W && h == H && type == ANARI_FLOAT32_VEC4)
      img.rgba.assign(p, p + size_t(W) * H * 4);
    else
      img.rgba.assign(size_t(W) * H * 4, -1.f);
    anariUnmapFrame(d, frame, "channel.color");
    return img;
  }

  std::vector<float> bounds(ANARIObject o)
  {
    std::vector<float> b(6, 0.f);
    anariGetProperty(
        d, o, "bounds", ANARI_FLOAT32_BOX3, b.data(), 6 * sizeof(float),
        ANARI_WAIT);
    return b;
  }
};

// A structuredRegular field of n^3 FLOAT32 samples spanning `size` world units
// per axis from `origin`, with value f(x, y, z) at each vertex. The data array
// is returned so a test can edit it in place.
struct Field
{
  ANARISpatialField field{nullptr};
  ANARIArray3D data{nullptr};
  int n{0};
};

static Field makeField(ANARIDevice d, int n, const float origin[3],
    float size, const std::function<float(float, float, float)> &f)
{
  Field out;
  out.n = n;
  out.data = anariNewArray3D(
      d, nullptr, nullptr, nullptr, ANARI_FLOAT32, n, n, n);
  const float spacing = size / float(n - 1);
  float *v = static_cast<float *>(anariMapArray(d, out.data));
  for (int k = 0; k < n; ++k)
    for (int j = 0; j < n; ++j)
      for (int i = 0; i < n; ++i)
        v[(size_t(k) * n + j) * n + i] = f(origin[0] + i * spacing,
            origin[1] + j * spacing, origin[2] + k * spacing);
  anariUnmapArray(d, out.data);
  out.field = anariNewSpatialField(d, "structuredRegular");
  anariSetParameter(d, out.field, "data", ANARI_ARRAY3D, &out.data);
  anariSetParameter(d, out.field, "origin", ANARI_FLOAT32_VEC3, origin);
  float sp[3] = {spacing, spacing, spacing};
  anariSetParameter(d, out.field, "spacing", ANARI_FLOAT32_VEC3, sp);
  anariCommitParameters(d, out.field);
  return out;
}

static void release(ANARIDevice d, Field &f)
{
  anariRelease(d, f.field);
  anariRelease(d, f.data);
}

// A transferFunction1D volume on `field` with a constant color and opacity.
static ANARIVolume makeVolume(ANARIDevice d, ANARISpatialField field,
    const float color[3], float opacity, float unitDistance = 1.f)
{
  ANARIVolume v = anariNewVolume(d, "transferFunction1D");
  anariSetParameter(d, v, "value", ANARI_SPATIAL_FIELD, &field);
  anariSetParameter(d, v, "color", ANARI_FLOAT32_VEC3, color);
  anariSetParameter(d, v, "opacity", ANARI_FLOAT32, &opacity);
  anariSetParameter(d, v, "unitDistance", ANARI_FLOAT32, &unitDistance);
  anariCommitParameters(d, v);
  return v;
}

// Emission-absorption closed form for a constant classification.
static float expected(float c, float a, float thickness, float unit = 1.f)
{
  return c * (1.f - std::pow(1.f - a, thickness / unit));
}

// The shader clips each ray-march step to the field domain and composites
// it exactly, so a constant classification is exact up to float noise; a
// varying one carries the midpoint rule's O(step^2) error.
static constexpr float kTol = 0.01f;

static bool rgbNear(const float *p, float r, float g, float b, float tol = kTol)
{
  return near(p[0], r, tol) && near(p[1], g, tol) && near(p[2], b, tol);
}

static std::string rgbStr(const float *p)
{
  char buf[96];
  std::snprintf(buf, sizeof(buf), "(%.3f, %.3f, %.3f, %.3f)", p[0], p[1], p[2],
      p[3]);
  return buf;
}

int main(int argc, char **argv)
{
  const char *subtype = argc > 1 ? argv[1] : "storm";
  ANARILibrary lib = anariLoadLibrary("hydra", statusFunc, nullptr);
  if (!lib)
    return 2;
  ANARIDevice d = anariNewDevice(lib, subtype);
  if (!d)
    return 2;

  // --- capability truth (SPEC §4.8) ---
  bool hasField = false, hasTf = false, hasNano = false;
  const char **ext = nullptr;
  anariGetProperty(d, d, "extension", ANARI_STRING_LIST, &ext, sizeof(ext),
      ANARI_WAIT);
  for (; ext && *ext; ++ext) {
    hasField |= !std::strcmp(*ext, "ANARI_KHR_SPATIAL_FIELD_STRUCTURED_REGULAR");
    hasNano |= !std::strcmp(*ext, "ANARI_KHR_SPATIAL_FIELD_NANOVDB");
    hasTf |= !std::strcmp(*ext, "ANARI_KHR_VOLUME_TRANSFER_FUNCTION1D");
  }
  const char **volSubtypes = anariGetObjectSubtypes(d, ANARI_VOLUME);
  const char **fieldSubtypes = anariGetObjectSubtypes(d, ANARI_SPATIAL_FIELD);
  const bool listsTf = volSubtypes && volSubtypes[0]
      && !std::strcmp(volSubtypes[0], "transferFunction1D");
  const bool listsField = fieldSubtypes && fieldSubtypes[0]
      && !std::strcmp(fieldSubtypes[0], "structuredRegular");
  if (std::strcmp(subtype, "storm") != 0) {
    // HdEmbree has no volume rprim: nothing volume-related is claimed.
    check(!hasField && !hasTf && !hasNano && !listsTf && !listsField,
        std::string(subtype) + " claims no volume extensions or subtypes");
    anariRelease(d, d);
    anariUnloadLibrary(lib);
    return g_errors ? 1 : 0;
  }
  check(hasField && hasTf && listsTf && listsField,
      "storm claims structuredRegular + transferFunction1D");

  {
    Scene scene(d);
    const float red[3] = {1.f, 0.f, 0.f};
    const float origin[3] = {-1.f, -1.f, -1.f};

    // --- constant classification vs the closed form ---
    Field constant = makeField(d, 17, origin, 2.f,
        [](float, float, float) { return 0.5f; });
    ANARIVolume vol = makeVolume(d, constant.field, red, 0.5f);
    scene.setVolumes({vol});
    Image img = scene.render();
    const float *c = img.world(0.f, 0.f);
    std::printf("    centre %s, corner %s\n", rgbStr(c).c_str(),
        rgbStr(img.world(1.8f, 1.8f)).c_str());
    check(rgbNear(c, expected(1.f, 0.5f, 2.f), 0.f, 0.f),
        "constant field: red * (1 - 0.5^2) through 2 units");
    check(rgbNear(img.world(1.8f, 1.8f), 0.f, 0.f, 0.f),
        "outside the field domain stays background");

    // --- opaque surfaces cut the ray march at their depth ---
    // A blue quad (unlit readback Q) at mid-depth leaves one unit of volume in
    // front: 0.5 * red + 0.5 * Q; behind the volume, all two units:
    // 0.75 * red + 0.25 * Q. Storm ends the march at floor(length / step)
    // steps, so up to 1.5 steps (optical depth 1.5 * 0.0625 * ln 2 here) in
    // front of a surface inside the volume go unintegrated.
    const float surfaceTol = 0.03f;
    for (float z : {0.f, -1.5f}) {
      float verts[12] = {-0.5f, -0.5f, z, 0.5f, -0.5f, z, 0.5f, 0.5f, z,
          -0.5f, 0.5f, z};
      ANARIArray1D pos =
          anariNewArray1D(d, verts, nullptr, nullptr, ANARI_FLOAT32_VEC3, 4);
      ANARIGeometry quad = anariNewGeometry(d, "quad");
      anariSetParameter(d, quad, "vertex.position", ANARI_ARRAY1D, &pos);
      anariCommitParameters(d, quad);
      ANARIMaterial mat = anariNewMaterial(d, "matte");
      const float blue[3] = {0.f, 0.f, 1.f};
      anariSetParameter(d, mat, "color", ANARI_FLOAT32_VEC3, blue);
      anariCommitParameters(d, mat);
      ANARISurface surf = anariNewSurface(d);
      anariSetParameter(d, surf, "geometry", ANARI_GEOMETRY, &quad);
      anariSetParameter(d, surf, "material", ANARI_MATERIAL, &mat);
      anariCommitParameters(d, surf);
      ANARIArray1D surfs =
          anariNewArray1D(d, &surf, nullptr, nullptr, ANARI_SURFACE, 1);
      anariSetParameter(d, scene.world, "surface", ANARI_ARRAY1D, &surfs);
      scene.setVolumes({});
      const Image alone = scene.render();
      scene.setVolumes({vol});
      img = scene.render();
      const float *q = alone.world(0.f, 0.f);
      const float front = z == 0.f ? 0.5f : 0.75f;
      const float *got = img.world(0.f, 0.f);
      check(q[2] > 0.5f
              && rgbNear(got, front + (1.f - front) * q[0],
                  (1.f - front) * q[1], (1.f - front) * q[2], surfaceTol),
          std::string(z == 0.f ? "quad inside" : "quad behind")
              + " the volume composites at its depth: " + rgbStr(got));
      anariUnsetParameter(d, scene.world, "surface");
      anariCommitParameters(d, scene.world);
      for (ANARIObject o : {ANARIObject(surfs), ANARIObject(surf),
               ANARIObject(mat), ANARIObject(quad), ANARIObject(pos)})
        anariRelease(d, o);
    }

    // --- unitDistance / opacity edits recommit the classification ---
    float unit = 2.f;
    anariSetParameter(d, vol, "unitDistance", ANARI_FLOAT32, &unit);
    anariCommitParameters(d, vol);
    img = scene.render();
    check(rgbNear(img.world(0.f, 0.f), expected(1.f, 0.5f, 2.f, 2.f), 0.f, 0.f),
        "unitDistance 2 halves the optical depth: " + rgbStr(img.world(0.f, 0.f)));
    unit = 1.f;
    float opacity = 0.2f;
    anariSetParameter(d, vol, "unitDistance", ANARI_FLOAT32, &unit);
    anariSetParameter(d, vol, "opacity", ANARI_FLOAT32, &opacity);
    anariCommitParameters(d, vol);
    img = scene.render();
    check(rgbNear(img.world(0.f, 0.f), expected(1.f, 0.2f, 2.f), 0.f, 0.f),
        "opacity edit: " + rgbStr(img.world(0.f, 0.f)));

    // --- valueRange + color array: t = (s - lo) / (hi - lo) ---
    // Color ramps blue -> red over the range; the field holds 0.5 everywhere.
    float ramp[6] = {0.f, 0.f, 1.f, 1.f, 0.f, 0.f};
    ANARIArray1D colors =
        anariNewArray1D(d, ramp, nullptr, nullptr, ANARI_FLOAT32_VEC3, 2);
    anariSetParameter(d, vol, "color", ANARI_ARRAY1D, &colors);
    opacity = 0.5f;
    anariSetParameter(d, vol, "opacity", ANARI_FLOAT32, &opacity);
    float range[2] = {0.f, 2.f};   // t = 0.25
    anariSetParameter(d, vol, "valueRange", ANARI_FLOAT32_BOX1, range);
    anariCommitParameters(d, vol);
    const float k = expected(1.f, 0.5f, 2.f);
    img = scene.render();
    check(rgbNear(img.world(0.f, 0.f), 0.25f * k, 0.f, 0.75f * k),
        "valueRange [0,2] reads the ramp at t=0.25: "
            + rgbStr(img.world(0.f, 0.f)));
    range[0] = 0.25f;
    range[1] = 0.75f;   // t = 0.5
    anariSetParameter(d, vol, "valueRange", ANARI_FLOAT32_BOX1, range);
    anariCommitParameters(d, vol);
    img = scene.render();
    check(rgbNear(img.world(0.f, 0.f), 0.5f * k, 0.f, 0.5f * k),
        "valueRange [0.25,0.75] reads t=0.5: " + rgbStr(img.world(0.f, 0.f)));
    range[0] = 1.f;
    range[1] = 3.f;   // below the range: clamps to t = 0
    anariSetParameter(d, vol, "valueRange", ANARI_FLOAT32_BOX1, range);
    anariCommitParameters(d, vol);
    img = scene.render();
    check(rgbNear(img.world(0.f, 0.f), 0.f, 0.f, k),
        "values below valueRange clamp to the first entry: "
            + rgbStr(img.world(0.f, 0.f)));

    // In-place color array edit (observed array) re-classifies.
    float *mc = static_cast<float *>(anariMapArray(d, colors));
    mc[0] = 0.f;
    mc[1] = 1.f;
    mc[2] = 0.f;
    anariUnmapArray(d, colors);
    img = scene.render();
    check(rgbNear(img.world(0.f, 0.f), 0.f, k, 0.f),
        "in-place color array edit: " + rgbStr(img.world(0.f, 0.f)));

    // --- classification edits have a bounded shader cost (019-5) ---
    // Interactive transfer-function editing commits a new classification per
    // drag. Each distinct state used to generate a new shader node, which
    // Sdr never frees; hydra.shaderNodeCount (the process's Sdr registry
    // size) must stay flat however many edits are made.
    {
      uint64_t before = 0, after = 0;
      const bool hasCount = anariGetProperty(d, d, "hydra.shaderNodeCount",
          ANARI_UINT64, &before, sizeof(before), ANARI_WAIT);
      check(hasCount && before > 0,
          "hydra.shaderNodeCount is reported: " + std::to_string(before));
      constexpr int kEdits = 16;
      float last = 0.f;
      for (int e = 1; e <= kEdits; ++e) {
        // A distinct ramp per edit: blue -> (e/kEdits) red over valueRange.
        last = float(e) / float(kEdits);
        float edit[6] = {0.f, 0.f, 1.f, last, 0.f, 0.f};
        ANARIArray1D a =
            anariNewArray1D(d, edit, nullptr, nullptr, ANARI_FLOAT32_VEC3, 2);
        anariSetParameter(d, vol, "color", ANARI_ARRAY1D, &a);
        anariRelease(d, a);
        float r[2] = {0.f, 1.f + 0.01f * float(e)};
        anariSetParameter(d, vol, "valueRange", ANARI_FLOAT32_BOX1, r);
        float ud = 1.f + 0.001f * float(e);
        anariSetParameter(d, vol, "unitDistance", ANARI_FLOAT32, &ud);
        anariCommitParameters(d, vol);
        img = scene.render();
      }
      anariGetProperty(d, d, "hydra.shaderNodeCount", ANARI_UINT64, &after,
          sizeof(after), ANARI_WAIT);
      check(after <= before + 1,
          std::to_string(kEdits) + " classification edits add at most one "
              "shader node: " + std::to_string(before) + " -> "
              + std::to_string(after));
      // The last edit is live: t = 0.5 / 1.16, unitDistance 1.016.
      const float t = 0.5f / 1.16f;
      const float kk = expected(1.f, 0.5f, 2.f, 1.016f);
      check(rgbNear(img.world(0.f, 0.f), t * last * kk, 0.f, (1.f - t) * kk),
          "the last classification edit renders: "
              + rgbStr(img.world(0.f, 0.f)));
      // Restore the ramp the next section starts from.
      anariSetParameter(d, vol, "color", ANARI_ARRAY1D, &colors);
      anariSetParameter(d, vol, "valueRange", ANARI_FLOAT32_BOX1, range);
      float one = 1.f;
      anariSetParameter(d, vol, "unitDistance", ANARI_FLOAT32, &one);
      anariCommitParameters(d, vol);
    }

    // --- opacity array and color alpha: final alpha = color.a * opacity ---
    float rgba[8] = {1.f, 1.f, 1.f, 0.5f, 1.f, 1.f, 1.f, 0.5f};
    ANARIArray1D colors4 =
        anariNewArray1D(d, rgba, nullptr, nullptr, ANARI_FLOAT32_VEC4, 2);
    float ops[3] = {0.f, 0.8f, 1.f};
    ANARIArray1D opacities =
        anariNewArray1D(d, ops, nullptr, nullptr, ANARI_FLOAT32, 3);
    anariSetParameter(d, vol, "color", ANARI_ARRAY1D, &colors4);
    anariSetParameter(d, vol, "opacity", ANARI_ARRAY1D, &opacities);
    range[0] = 0.f;
    range[1] = 1.f;   // t = 0.5 -> opacity 0.8, alpha 0.5 * 0.8
    anariSetParameter(d, vol, "valueRange", ANARI_FLOAT32_BOX1, range);
    anariCommitParameters(d, vol);
    img = scene.render();
    const float w = expected(1.f, 0.4f, 2.f);
    check(rgbNear(img.world(0.f, 0.f), w, w, w),
        "opacity array x color alpha: " + rgbStr(img.world(0.f, 0.f)));
    anariRelease(d, colors);
    anariRelease(d, colors4);
    anariRelease(d, opacities);

    // --- analytic field: classification tracks s(x) column by column ---
    // s = (x + 1) / 2 over the domain; opacity ramps 0 -> 1 with s, so the
    // column at x has per-unit opacity a(x) = s and thickness 2.
    Field linear = makeField(d, 33, origin, 2.f,
        [](float x, float, float) { return 0.5f * (x + 1.f); });
    float ramp01[2] = {0.f, 1.f};
    ANARIArray1D rampOps =
        anariNewArray1D(d, ramp01, nullptr, nullptr, ANARI_FLOAT32, 2);
    ANARIVolume analytic = makeVolume(d, linear.field, red, 1.f);
    anariSetParameter(d, analytic, "opacity", ANARI_ARRAY1D, &rampOps);
    anariCommitParameters(d, analytic);
    scene.setVolumes({analytic});
    img = scene.render();
    float worst = 0.f;
    for (float x : {-0.8f, -0.5f, -0.2f, 0.f, 0.2f, 0.5f, 0.7f}) {
      // Sample the pixel centre's x exactly.
      const int px = int((x + 2.f) / 4.f * W);
      const float cx = (px + 0.5f) * 4.f / W - 2.f;
      const float s = 0.5f * (cx + 1.f);
      const float e = expected(1.f, s, 2.f);
      const float got = img.at(px, H / 2)[0];
      worst = std::max(worst, std::fabs(got - e));
      std::printf("    x=%.3f s=%.3f expected %.4f got %.4f\n", cx, s, e, got);
    }
    check(worst <= kTol, "analytic linear field matches closed form per column");

    // --- nonlinear analytic field: reconstruction + ray-march error ---
    // s = 0.8 * ((z + 1) / 2)^2 * (0.5 + 0.5 sin(pi x)) varies along the ray
    // (quadratic) and across columns (sine), so neither trilinear
    // reconstruction nor the per-step midpoint rule is exact; the column's
    // optical depth is integrated here numerically from the analytic field.
    const float pi = 3.14159265f;
    auto curved = [pi](float x, float, float z) {
      const float u = 0.5f * (z + 1.f);
      return 0.8f * u * u * (0.5f + 0.5f * std::sin(pi * x));
    };
    Field nonlinear = makeField(d, 33, origin, 2.f, curved);
    anariSetParameter(d, analytic, "value", ANARI_SPATIAL_FIELD, &nonlinear.field);
    anariCommitParameters(d, analytic);
    img = scene.render();
    worst = 0.f;
    for (float x : {-0.7f, -0.3f, 0.f, 0.25f, 0.5f, 0.8f}) {
      const int px = int((x + 2.f) / 4.f * W);
      const float cx = (px + 0.5f) * 4.f / W - 2.f;
      double depth = 0.0;
      const int steps = 4000;
      for (int i = 0; i < steps; ++i) {
        const float z = -1.f + 2.f * (i + 0.5f) / steps;
        depth += -std::log(1.0 - curved(cx, 0.f, z)) * 2.0 / steps;
      }
      const float e = float(1.0 - std::exp(-depth));
      const float got = img.at(px, H / 2)[0];
      worst = std::max(worst, std::fabs(got - e));
      std::printf("    x=%.3f expected %.4f got %.4f\n", cx, e, got);
    }
    check(worst <= kTol,
        "nonlinear analytic field within the documented error ("
            + std::to_string(worst) + ")");
    anariRelease(d, rampOps);
    release(d, nonlinear);

    // --- anisotropic, non-cubic grid: index order and per-axis spacing ---
    // dims (3, 9, 5), spacing (1, 0.25, 0.5) from (-1, -1, -1) spans
    // [-1, 1]^3; values vary with x only (0, 0.5, 1), so the color ramp reads
    // t = (x + 1) / 2 per column. A transposed index order would vary it
    // along y or z instead.
    {
      float aniso[5][9][3];
      for (auto &slab : aniso)
        for (auto &row : slab)
          for (int i = 0; i < 3; ++i)
            row[i] = 0.5f * i;
      ANARIArray3D adata = anariNewArray3D(
          d, aniso, nullptr, nullptr, ANARI_FLOAT32, 3, 9, 5);
      ANARISpatialField af = anariNewSpatialField(d, "structuredRegular");
      anariSetParameter(d, af, "data", ANARI_ARRAY3D, &adata);
      anariSetParameter(d, af, "origin", ANARI_FLOAT32_VEC3, origin);
      float asp[3] = {1.f, 0.25f, 0.5f};
      anariSetParameter(d, af, "spacing", ANARI_FLOAT32_VEC3, asp);
      anariCommitParameters(d, af);
      float blueToRed[6] = {0.f, 0.f, 1.f, 1.f, 0.f, 0.f};
      ANARIArray1D btr =
          anariNewArray1D(d, blueToRed, nullptr, nullptr, ANARI_FLOAT32_VEC3, 2);
      ANARIVolume av = makeVolume(d, af, red, 0.5f);
      anariSetParameter(d, av, "color", ANARI_ARRAY1D, &btr);
      anariCommitParameters(d, av);
      scene.setVolumes({av});
      auto columnOk = [&](const Image &im, float x, float t, float thick) {
        const int px = int((x + 2.f) / 4.f * W);
        const float kk = expected(1.f, 0.5f, thick);
        const float *p = im.at(px, H / 2 + 5);
        return rgbNear(p, t * kk, 0.f, (1.f - t) * kk);
      };
      auto pixelT = [&](float x, float lo, float span) {
        const int px = int((x + 2.f) / 4.f * W);
        const float cx = (px + 0.5f) * 4.f / W - 2.f;
        return (cx - lo) / span;
      };
      img = scene.render();
      check(columnOk(img, -0.5f, pixelT(-0.5f, -1.f, 2.f), 2.f)
              && columnOk(img, 0.5f, pixelT(0.5f, -1.f, 2.f), 2.f),
          "anisotropic non-cubic grid reads x-varying values: "
              + rgbStr(img.world(-0.5f, 0.2f)) + " "
              + rgbStr(img.world(0.5f, 0.2f)));
      // Negative x spacing from x = 1 mirrors the ramp.
      float mirrorOrigin[3] = {1.f, -1.f, -1.f};
      float msp[3] = {-1.f, 0.25f, 0.5f};
      anariSetParameter(d, af, "origin", ANARI_FLOAT32_VEC3, mirrorOrigin);
      anariSetParameter(d, af, "spacing", ANARI_FLOAT32_VEC3, msp);
      anariCommitParameters(d, af);
      img = scene.render();
      check(columnOk(img, -0.5f, 1.f - pixelT(-0.5f, -1.f, 2.f), 2.f)
              && columnOk(img, 0.5f, 1.f - pixelT(0.5f, -1.f, 2.f), 2.f),
          "negative spacing mirrors the grid: " + rgbStr(img.world(-0.5f, 0.2f)));
      // Spacing edit: x spacing 0.5 from x = -1 shrinks the domain to
      // [-1, 0] and steepens the ramp.
      float esp[3] = {0.5f, 0.25f, 0.5f};
      anariSetParameter(d, af, "origin", ANARI_FLOAT32_VEC3, origin);
      anariSetParameter(d, af, "spacing", ANARI_FLOAT32_VEC3, esp);
      anariCommitParameters(d, af);
      img = scene.render();
      check(columnOk(img, -0.5f, pixelT(-0.5f, -1.f, 1.f), 2.f)
              && rgbNear(img.world(0.5f, 0.2f), 0.f, 0.f, 0.f),
          "spacing edit rescales the domain: " + rgbStr(img.world(-0.5f, 0.2f)));
      scene.setVolumes({});
      anariRelease(d, av);
      anariRelease(d, btr);
      anariRelease(d, af);
      anariRelease(d, adata);
    }

    // --- spacing / origin: coverage follows the domain, not the texture box ---
    // A 5^3 grid at origin (0.25, -1.25, -1) with spacing 0.25 covers
    // x in [0.25, 1.25], y in [-1.25, -0.25].
    const float origin2[3] = {0.25f, -1.25f, -1.f};
    Field small = makeField(d, 5, origin2, 1.f,
        [](float, float, float) { return 0.5f; });
    ANARIVolume smallVol = makeVolume(d, small.field, red, 0.9f);
    scene.setVolumes({smallVol});
    img = scene.render();
    check(img.world(0.75f, -0.75f)[0] > 0.3f, "shifted domain is covered");
    check(img.world(0.1f, -0.75f)[0] < 0.01f && img.world(1.4f, -0.75f)[0] < 0.01f
            && img.world(0.75f, -0.1f)[0] < 0.01f
            && img.world(0.75f, -1.4f)[0] < 0.01f
            && img.world(-0.75f, 0.75f)[0] < 0.01f,
        "coverage ends at the domain on every side");
    // Thickness is 1 unit along z.
    check(rgbNear(img.world(0.75f, -0.75f), expected(1.f, 0.9f, 1.f), 0.f, 0.f),
        "spacing sets the thickness: " + rgbStr(img.world(0.75f, -0.75f)));
    {
      std::vector<float> b = scene.bounds(scene.world);
      check(near(b[0], 0.25f, 1e-4f) && near(b[1], -1.25f, 1e-4f)
              && near(b[2], -1.f, 1e-4f) && near(b[3], 1.25f, 1e-4f)
              && near(b[4], -0.25f, 1e-4f) && near(b[5], 0.f, 1e-4f),
          "world bounds are the field domain");
    }
    // Origin edit moves the volume.
    const float origin3[3] = {-1.25f, 0.25f, -1.f};
    anariSetParameter(d, small.field, "origin", ANARI_FLOAT32_VEC3, origin3);
    anariCommitParameters(d, small.field);
    img = scene.render();
    check(img.world(-0.75f, 0.75f)[0] > 0.3f
            && img.world(0.75f, -0.75f)[0] < 0.01f,
        "origin edit moves the coverage");

    // --- in-place field data edit re-uploads the texture ---
    float *vals = static_cast<float *>(anariMapArray(d, small.data));
    for (int i = 0; i < 125; ++i)
      vals[i] = 0.f;
    anariUnmapArray(d, small.data);
    ANARIArray1D stepOps =
        anariNewArray1D(d, ramp01, nullptr, nullptr, ANARI_FLOAT32, 2);
    anariSetParameter(d, smallVol, "opacity", ANARI_ARRAY1D, &stepOps);
    anariCommitParameters(d, smallVol);
    img = scene.render();
    check(img.world(-0.75f, 0.75f)[0] < 0.01f,
        "field values edited to opacity 0 render empty: "
            + rgbStr(img.world(-0.75f, 0.75f)));
    vals = static_cast<float *>(anariMapArray(d, small.data));
    for (int i = 0; i < 125; ++i)
      vals[i] = 1.f;
    anariUnmapArray(d, small.data);
    img = scene.render();
    check(img.world(-0.75f, 0.75f)[0] > 0.5f,
        "field data edited back to 1 renders opaque again: "
            + rgbStr(img.world(-0.75f, 0.75f)));
    anariRelease(d, stepOps);

    // --- multiple volumes: two colors, one shared field ---
    const float green[3] = {0.f, 1.f, 0.f};
    anariSetParameter(d, small.field, "origin", ANARI_FLOAT32_VEC3, origin2);
    anariCommitParameters(d, small.field);
    ANARIVolume redVol = makeVolume(d, small.field, red, 0.5f);
    Field other = makeField(d, 5, origin3, 1.f,
        [](float, float, float) { return 0.5f; });
    ANARIVolume greenVol = makeVolume(d, other.field, green, 0.5f);
    ANARIVolume sharing = makeVolume(d, small.field, green, 0.5f);
    scene.setVolumes({redVol, greenVol});
    img = scene.render();
    const float one = expected(1.f, 0.5f, 1.f);
    check(rgbNear(img.world(0.75f, -0.75f), one, 0.f, 0.f)
            && rgbNear(img.world(-0.75f, 0.75f), 0.f, one, 0.f),
        "two volumes render independently: "
            + rgbStr(img.world(0.75f, -0.75f)) + " "
            + rgbStr(img.world(-0.75f, 0.75f)));
    // Two volumes on one field overlap exactly. Storm composites whole
    // volumes one over the other rather than interleaving their samples, so
    // only the total is ANARI's: 1 - 0.5 * 0.5 of the light, split unevenly.
    scene.setVolumes({redVol, sharing});
    img = scene.render();
    const float *both = img.world(0.75f, -0.75f);
    check(both[0] > 0.2f && both[1] > 0.2f
            && near(both[0] + both[1], 0.75f, kTol),
        "two volumes sharing a field both contribute: " + rgbStr(both));
    scene.setVolumes({});
    img = scene.render();
    check(rgbNear(img.world(0.75f, -0.75f), 0.f, 0.f, 0.f),
        "removing the volumes restores the background");

    // --- instanced volumes: transform, and object-space unitDistance ---
    // The 1-unit-thick slab, scaled 2x about the origin and moved: it covers
    // twice the area, yet a ray crosses the same object-space thickness, so
    // the color matches the untransformed slab (ANARI unitDistance is
    // object space).
    ANARIGroup group = anariNewGroup(d);
    ANARIArray1D gv =
        anariNewArray1D(d, &redVol, nullptr, nullptr, ANARI_VOLUME, 1);
    anariSetParameter(d, group, "volume", ANARI_ARRAY1D, &gv);
    anariCommitParameters(d, group);
    ANARIInstance inst = anariNewInstance(d, "transform");
    anariSetParameter(d, inst, "group", ANARI_GROUP, &group);
    // x' = 2x - 1.5, y' = 2y + 1.5: domain x [0.25,1.25] -> [-1, 1],
    // y [-1.25,-0.25] -> [-1, 1].
    float xf[16] = {2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 2, 0, -1.5f, 1.5f, 0, 1};
    anariSetParameter(d, inst, "transform", ANARI_FLOAT32_MAT4, xf);
    anariCommitParameters(d, inst);
    scene.setInstances({inst});
    img = scene.render();
    check(rgbNear(img.world(0.f, 0.f), one, 0.f, 0.f)
            && rgbNear(img.world(0.9f, -0.9f), one, 0.f, 0.f)
            && rgbNear(img.world(-0.9f, 0.9f), one, 0.f, 0.f),
        "scaled instance keeps object-space optical depth: "
            + rgbStr(img.world(0.f, 0.f)));
    check(img.world(1.2f, 0.f)[0] < 0.01f && img.world(0.f, 1.2f)[0] < 0.01f,
        "instance transform places the coverage");
    {
      std::vector<float> b = scene.bounds(scene.world);
      check(near(b[0], -1.f, 1e-4f) && near(b[1], -1.f, 1e-4f)
              && near(b[2], -2.f, 1e-4f) && near(b[3], 1.f, 1e-4f)
              && near(b[4], 1.f, 1e-4f) && near(b[5], 0.f, 1e-4f),
          "world bounds include the transformed volume");
      std::vector<float> g = scene.bounds(group);
      check(near(g[0], 0.25f, 1e-4f) && near(g[3], 1.25f, 1e-4f),
          "group bounds are the object-space domain");
    }
    // Two instances of one group (a second, translated copy).
    ANARIInstance inst2 = anariNewInstance(d, "transform");
    anariSetParameter(d, inst2, "group", ANARI_GROUP, &group);
    float xf2[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -2.f, 0.f, 0, 1};
    anariSetParameter(d, inst2, "transform", ANARI_FLOAT32_MAT4, xf2);
    anariCommitParameters(d, inst2);
    scene.setInstances({inst, inst2});
    img = scene.render();
    check(rgbNear(img.world(0.f, 0.f), one, 0.f, 0.f)
            && rgbNear(img.world(-1.25f, -0.75f), one, 0.f, 0.f),
        "two instances of a volume group both render: "
            + rgbStr(img.world(-1.25f, -0.75f)));

    // A 45-degree rotation about y: the ray at x crosses the 1-unit cube's
    // diamond cross-section along a chord of sqrt(2) - 2|x| object units
    // (the step clipping runs in object space along the actual ray).
    const float cs = std::sqrt(0.5f);
    // Rotate about the slab's centre (0.75, -0.75, -0.5), then move it to
    // the origin.
    float rot[16] = {cs, 0, -cs, 0, 0, 1, 0, 0, cs, 0, cs, 0, 0, 0, 0, 1};
    const float cx = 0.75f, cy = -0.75f, cz = -0.5f;
    rot[12] = -(rot[0] * cx + rot[4] * cy + rot[8] * cz);
    rot[13] = -(rot[1] * cx + rot[5] * cy + rot[9] * cz);
    rot[14] = -(rot[2] * cx + rot[6] * cy + rot[10] * cz);
    anariSetParameter(d, inst, "transform", ANARI_FLOAT32_MAT4, rot);
    anariCommitParameters(d, inst);
    scene.setInstances({inst});
    img = scene.render();
    const float pixelX = (int(W / 2) + 0.5f) * 4.f / W - 2.f;
    check(rgbNear(img.world(0.f, 0.f),
              expected(1.f, 0.5f, std::sqrt(2.f) - 2.f * pixelX), 0.f, 0.f),
        "rotated instance: diagonal chord through the cube: "
            + rgbStr(img.world(0.f, 0.f)));
    scene.setInstances({});

    // --- perspective rays diverge; the centre ray still crosses 2 units ---
    ANARIVolume persp = makeVolume(d, constant.field, red, 0.5f);
    scene.setVolumes({persp});
    ANARICamera pc = anariNewCamera(d, "perspective");
    float ppos[3] = {0.f, 0.f, 6.f}, pdir[3] = {0.f, 0.f, -1.f},
          pup[3] = {0.f, 1.f, 0.f};
    anariSetParameter(d, pc, "position", ANARI_FLOAT32_VEC3, ppos);
    anariSetParameter(d, pc, "direction", ANARI_FLOAT32_VEC3, pdir);
    anariSetParameter(d, pc, "up", ANARI_FLOAT32_VEC3, pup);
    anariCommitParameters(d, pc);
    anariSetParameter(d, scene.frame, "camera", ANARI_CAMERA, &pc);
    anariCommitParameters(d, scene.frame);
    img = scene.render();
    const float *pcentre = img.rgba.data() + ((size_t(H / 2) * W + W / 2) * 4);
    // The pixel centre is half a pixel off-axis: still within the slab face.
    check(rgbNear(pcentre, expected(1.f, 0.5f, 2.f), 0.f, 0.f),
        "perspective centre ray: " + rgbStr(pcentre));
    anariSetParameter(d, scene.frame, "camera", ANARI_CAMERA, &scene.camera);
    anariCommitParameters(d, scene.frame);
    anariRelease(d, pc);

    // --- fixed-point data decodes normalized: UFIXED8 128 -> 128/255 ---
    uint8_t bytes[8];
    std::memset(bytes, 128, sizeof(bytes));
    ANARIArray3D u8 = anariNewArray3D(
        d, bytes, nullptr, nullptr, ANARI_UFIXED8, 2, 2, 2);
    ANARISpatialField u8Field = anariNewSpatialField(d, "structuredRegular");
    anariSetParameter(d, u8Field, "data", ANARI_ARRAY3D, &u8);
    anariSetParameter(d, u8Field, "origin", ANARI_FLOAT32_VEC3, origin);
    float twoSpacing[3] = {2.f, 2.f, 2.f};
    anariSetParameter(d, u8Field, "spacing", ANARI_FLOAT32_VEC3, twoSpacing);
    anariCommitParameters(d, u8Field);
    anariSetParameter(d, persp, "value", ANARI_SPATIAL_FIELD, &u8Field);
    float blueRed[6] = {0.f, 0.f, 1.f, 1.f, 0.f, 0.f};
    ANARIArray1D br =
        anariNewArray1D(d, blueRed, nullptr, nullptr, ANARI_FLOAT32_VEC3, 2);
    anariSetParameter(d, persp, "color", ANARI_ARRAY1D, &br);
    anariCommitParameters(d, persp);
    img = scene.render();
    const float tu8 = 128.f / 255.f;
    check(rgbNear(img.world(0.f, 0.f), tu8 * k, 0.f, (1.f - tu8) * k),
        "UFIXED8 field reads as 128/255: " + rgbStr(img.world(0.f, 0.f)));
    anariRelease(d, br);
    anariRelease(d, u8Field);
    anariRelease(d, u8);

    // --- visible=false hides the volume ---
    bool hidden = false;
    anariSetParameter(d, persp, "visible", ANARI_BOOL, &hidden);
    anariCommitParameters(d, persp);
    img = scene.render();
    check(rgbNear(img.world(0.f, 0.f), 0.f, 0.f, 0.f),
        "visible=false hides the volume");
    scene.setVolumes({});
    anariRelease(d, persp);

#ifdef ANARI_HYDRA_HAS_NANOVDB
    // --- nanovdb: densified onto the structuredRegular path ---
    // Active voxels at index [0,7]^3 hold 0.5 over a 0 background; with
    // voxel size 0.25 and translation (-1,-1,-1) the worldBBox (index
    // [0, 8]) is [-1, 1]^3. The densified grid carries index 8's background
    // layer, so it must render exactly like the equivalent 9^3
    // structuredRegular field, and read bounds equal to the worldBBox.
    check(hasNano, "storm claims KHR_SPATIAL_FIELD_NANOVDB");
    for (bool fp16 : {false, true}) {
      nanovdb::tools::build::Grid<float> src(0.f, "density");
      auto acc = src.getAccessor();
      for (int k = 0; k < 8; ++k)
        for (int j = 0; j < 8; ++j)
          for (int i = 0; i < 8; ++i)
            acc.setValue(nanovdb::Coord(i, j, k), 0.5f);
      src.setTransform(0.25, nanovdb::Vec3d(-1.0));
      auto handle = fp16
          ? nanovdb::tools::createNanoGrid<decltype(src), nanovdb::Fp16>(src)
          : nanovdb::tools::createNanoGrid(src);
      ANARIArray1D blob = anariNewArray1D(d, nullptr, nullptr, nullptr,
          ANARI_UINT8, handle.bufferSize());
      std::memcpy(anariMapArray(d, blob), handle.data(), handle.bufferSize());
      anariUnmapArray(d, blob);
      ANARISpatialField nano = anariNewSpatialField(d, "nanovdb");
      anariSetParameter(d, nano, "data", ANARI_ARRAY1D, &blob);
      anariCommitParameters(d, nano);

      Field dense = makeField(d, 9, origin, 2.f, [](float x, float y, float z) {
        return x < 0.9f && y < 0.9f && z < 0.9f ? 0.5f : 0.f;
      });
      float ops01[2] = {0.f, 1.f};
      ANARIArray1D opArr =
          anariNewArray1D(d, ops01, nullptr, nullptr, ANARI_FLOAT32, 2);
      ANARIVolume nv = makeVolume(d, nano, red, 1.f);
      anariSetParameter(d, nv, "opacity", ANARI_ARRAY1D, &opArr);
      anariCommitParameters(d, nv);
      ANARIVolume dv = makeVolume(d, dense.field, red, 1.f);
      anariSetParameter(d, dv, "opacity", ANARI_ARRAY1D, &opArr);
      anariCommitParameters(d, dv);

      scene.setVolumes({nv});
      const Image nanoImg = scene.render();
      const std::vector<float> nb = scene.bounds(scene.world);
      scene.setVolumes({dv});
      const Image denseImg = scene.render();
      float worstDiff = 0.f;
      for (size_t i = 0; i < nanoImg.rgba.size(); ++i)
        worstDiff = std::max(
            worstDiff, std::fabs(nanoImg.rgba[i] - denseImg.rgba[i]));
      const std::string label = fp16 ? "nanovdb Fp16" : "nanovdb Float";
      // Fp16 quantizes 0.5 exactly; allow float noise either way.
      check(worstDiff < 2e-3f,
          label + " renders like the equivalent structuredRegular field (max "
              + std::to_string(worstDiff) + ")");
      check(near(nb[0], -1.f, 1e-5f) && near(nb[2], -1.f, 1e-5f)
              && near(nb[3], 1.f, 1e-5f) && near(nb[5], 1.f, 1e-5f),
          label + " bounds are the grid's worldBBox");
      // Column through the active interior: s = 0.5 for z in [-1, 0.75],
      // then linear to 0 at z = 1; opacity = s per unit.
      double depth = 0.0;
      const int steps = 20000;
      for (int i = 0; i < steps; ++i) {
        const double z = -1.0 + 2.0 * (i + 0.5) / steps;
        const double sz = z <= 0.75 ? 0.5 : 0.5 * (1.0 - z) / 0.25;
        depth += -std::log(1.0 - sz) * 2.0 / steps;
      }
      const float e = float(1.0 - std::exp(-depth));
      check(rgbNear(nanoImg.world(-0.3f, -0.3f), e, 0.f, 0.f),
          label + " column matches the analytic field: "
              + rgbStr(nanoImg.world(-0.3f, -0.3f)));

      scene.setVolumes({});
      anariRelease(d, nv);
      anariRelease(d, dv);
      anariRelease(d, opArr);
      anariRelease(d, nano);
      anariRelease(d, blob);
      release(d, dense);
    }
#endif

    for (ANARIVolume v : {vol, analytic, smallVol, redVol, greenVol, sharing})
      anariRelease(d, v);
    anariRelease(d, inst);
    anariRelease(d, inst2);
    anariRelease(d, gv);
    anariRelease(d, group);
    release(d, constant);
    release(d, linear);
    release(d, small);
    release(d, other);
  }

  anariRelease(d, d);
  anariUnloadLibrary(lib);
  std::printf("%s\n", g_errors ? "FAILED" : "PASSED");
  return g_errors ? 1 : 0;
}
