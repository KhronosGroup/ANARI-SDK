// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0
//
// KHR_GEOMETRY_ISOSURFACE: structuredRegular fields with known level sets,
// extracted to triangle meshes at commit (README.md "Isosurfaces"). An
// orthographic camera looks down -Z over a 4x4 world-unit window, so a pixel
// is covered exactly where some level set lies under it; bounds queries read
// the extracted surface itself.

#include <anari/anari.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <thread>
#include <vector>

static int g_errors = 0;
static std::string g_lastExtraction;   // the device's extraction statistics

static void statusFunc(const void *, ANARIDevice, ANARIObject, ANARIDataType,
    ANARIStatusSeverity severity, ANARIStatusCode, const char *message)
{
  if (std::strstr(message, "isosurface extracted"))
    g_lastExtraction = message;
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

static std::string str(const std::vector<float> &v)
{
  std::string s = "(";
  for (size_t i = 0; i < v.size(); ++i)
    s += (i ? ", " : "") + std::to_string(v[i]);
  return s + ")";
}

static constexpr uint32_t W = 128, H = 128;

struct Image
{
  std::vector<float> rgba;
  std::vector<uint32_t> prim;
  static int px(float w) { return std::clamp(int((w + 2.f) / 4.f * W), 0, int(W) - 1); }
  const float *world(float wx, float wy) const
  {
    return &rgba[(size_t(px(wy)) * W + px(wx)) * 4];
  }
  uint32_t primAt(float wx, float wy) const
  {
    return prim[size_t(px(wy)) * W + px(wx)];
  }
  bool covered(float wx, float wy) const
  {
    const float *p = world(wx, wy);
    return p[0] + p[1] + p[2] > 0.02f;
  }
  // Covered pixel count, as world area (each pixel is (4/W)^2).
  float coveredArea() const
  {
    size_t n = 0;
    for (size_t i = 0; i < size_t(W) * H; ++i)
      n += rgba[i * 4] + rgba[i * 4 + 1] + rgba[i * 4 + 2] > 0.02f;
    return float(n) * 16.f / float(W * H);
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
    // Uniform ambient light: every covered pixel is lit whatever its facing.
    // HdEmbree samples the ambient dome stochastically (one hemisphere
    // direction per sample), so a single sample can land at a grazing angle
    // and read near black; 16 samples keep every covered pixel well lit.
    float ambient = 1.f;
    int32_t samples = 16;
    anariSetParameter(d, renderer, "ambientRadiance", ANARI_FLOAT32, &ambient);
    anariSetParameter(d, renderer, "pixelSamples", ANARI_INT32, &samples);
    anariCommitParameters(d, renderer);
    frame = anariNewFrame(d);
    uint32_t size[2] = {W, H};
    ANARIDataType fmt = ANARI_FLOAT32_VEC4, u32 = ANARI_UINT32;
    anariSetParameter(d, frame, "size", ANARI_UINT32_VEC2, size);
    anariSetParameter(d, frame, "channel.color", ANARI_DATA_TYPE, &fmt);
    anariSetParameter(d, frame, "channel.primitiveId", ANARI_DATA_TYPE, &u32);
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

  void setSurfaces(const std::vector<ANARISurface> &surfaces)
  {
    if (surfaces.empty()) {
      anariUnsetParameter(d, world, "surface");
    } else {
      ANARIArray1D a = anariNewArray1D(d, surfaces.data(), nullptr, nullptr,
          ANARI_SURFACE, surfaces.size());
      anariSetParameter(d, world, "surface", ANARI_ARRAY1D, &a);
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
    Image img;
    const float *p = static_cast<const float *>(
        anariMapFrame(d, frame, "channel.color", &w, &h, &type));
    if (p && w == W && h == H && type == ANARI_FLOAT32_VEC4)
      img.rgba.assign(p, p + size_t(W) * H * 4);
    else
      img.rgba.assign(size_t(W) * H * 4, -1.f);
    anariUnmapFrame(d, frame, "channel.color");
    const uint32_t *ids = static_cast<const uint32_t *>(
        anariMapFrame(d, frame, "channel.primitiveId", &w, &h, &type));
    if (ids && w == W && h == H && type == ANARI_UINT32)
      img.prim.assign(ids, ids + size_t(W) * H);
    else
      img.prim.assign(size_t(W) * H, 0xDEADBEEFu);
    anariUnmapFrame(d, frame, "channel.primitiveId");
    return img;
  }

  std::vector<float> bounds(ANARIObject o)
  {
    std::vector<float> b(6, 0.f);
    anariGetProperty(d, o, "bounds", ANARI_FLOAT32_BOX3, b.data(),
        6 * sizeof(float), ANARI_WAIT);
    return b;
  }
};

// A structuredRegular field of n^3 FLOAT32 samples spanning `size` per axis
// from `origin`, value f(x, y, z) at each vertex. The data array is kept so a
// test can edit it in place.
struct Field
{
  ANARISpatialField field{nullptr};
  ANARIArray3D data{nullptr};
  int n{0};
  float origin[3]{};
  float spacing{1.f};

  void fill(ANARIDevice d, const std::function<float(float, float, float)> &f)
  {
    float *v = static_cast<float *>(anariMapArray(d, data));
    for (int k = 0; k < n; ++k)
      for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i)
          v[(size_t(k) * n + j) * n + i] = f(origin[0] + i * spacing,
              origin[1] + j * spacing, origin[2] + k * spacing);
    anariUnmapArray(d, data);
  }
};

static Field makeField(ANARIDevice d, int n, const float origin[3], float size,
    const std::function<float(float, float, float)> &f)
{
  Field out;
  out.n = n;
  std::copy(origin, origin + 3, out.origin);
  out.spacing = size / float(n - 1);
  out.data =
      anariNewArray3D(d, nullptr, nullptr, nullptr, ANARI_FLOAT32, n, n, n);
  out.fill(d, f);
  out.field = anariNewSpatialField(d, "structuredRegular");
  anariSetParameter(d, out.field, "data", ANARI_ARRAY3D, &out.data);
  anariSetParameter(d, out.field, "origin", ANARI_FLOAT32_VEC3, origin);
  float sp[3] = {out.spacing, out.spacing, out.spacing};
  anariSetParameter(d, out.field, "spacing", ANARI_FLOAT32_VEC3, sp);
  anariCommitParameters(d, out.field);
  return out;
}

static void release(ANARIDevice d, Field &f)
{
  anariRelease(d, f.field);
  anariRelease(d, f.data);
}

static float radius(float x, float y, float z)
{
  return std::sqrt(x * x + y * y + z * z);
}

struct Iso
{
  ANARIGeometry geom{nullptr};
  ANARISurface surface{nullptr};
  ANARIMaterial material{nullptr};
};

static Iso makeIso(ANARIDevice d, ANARISpatialField field, float isovalue)
{
  Iso out;
  out.geom = anariNewGeometry(d, "isosurface");
  anariSetParameter(d, out.geom, "field", ANARI_SPATIAL_FIELD, &field);
  anariSetParameter(d, out.geom, "isovalue", ANARI_FLOAT32, &isovalue);
  anariCommitParameters(d, out.geom);
  out.material = anariNewMaterial(d, "matte");
  anariSetParameter(d, out.material, "color", ANARI_STRING, "color");
  anariCommitParameters(d, out.material);
  out.surface = anariNewSurface(d);
  anariSetParameter(d, out.surface, "geometry", ANARI_GEOMETRY, &out.geom);
  anariSetParameter(d, out.surface, "material", ANARI_MATERIAL, &out.material);
  anariCommitParameters(d, out.surface);
  return out;
}

static void release(ANARIDevice d, Iso &iso)
{
  anariRelease(d, iso.surface);
  anariRelease(d, iso.material);
  anariRelease(d, iso.geom);
}

static void setColor(ANARIDevice d, ANARIGeometry g, float r, float gr, float b)
{
  float c[4] = {r, gr, b, 1.f};
  anariSetParameter(d, g, "color", ANARI_FLOAT32_VEC4, c);
}

int main(int argc, char **argv)
{
  const char *delegate = argc > 1 ? argv[1] : "storm";
  ANARILibrary lib = anariLoadLibrary("hydra", statusFunc, nullptr);
  if (!lib) {
    std::printf("failed to load hydra library\n");
    return 2;
  }
  ANARIDevice d = anariNewDevice(lib, delegate);
  if (!d) {
    std::printf("failed to create device '%s'\n", delegate);
    return 2;
  }

  // --- extension claim and subtype gating ---------------------------------
  bool claimed = false;
  const char *const *ext = nullptr;
  anariGetProperty(d, d, "extension", ANARI_STRING_LIST, &ext, sizeof(ext),
      ANARI_WAIT);
  for (; ext && *ext; ++ext)
    claimed |= std::strcmp(*ext, "ANARI_KHR_GEOMETRY_ISOSURFACE") == 0;
  bool listed = false;
  for (const char **g = anariGetObjectSubtypes(d, ANARI_GEOMETRY); g && *g; ++g)
    listed |= std::strcmp(*g, "isosurface") == 0;
  const bool isEmbree = std::strcmp(delegate, "embree") == 0;
  check(claimed == !isEmbree && listed == !isEmbree,
      "KHR_GEOMETRY_ISOSURFACE claimed and listed iff fields are offered");
  if (!isEmbree) {
    auto reflects = [&](const char *name, ANARIDataType type) {
      const auto *p = static_cast<const ANARIParameter *>(anariGetObjectInfo(
          d, ANARI_GEOMETRY, "isosurface", "parameter", ANARI_PARAMETER_LIST));
      for (; p && p->name; ++p)
        if (p->type == type && std::strcmp(p->name, name) == 0)
          return true;
      return false;
    };
    check(reflects("field", ANARI_SPATIAL_FIELD)
            && reflects("isovalue", ANARI_FLOAT32)
            && reflects("isovalue", ANARI_ARRAY1D)
            && reflects("primitive.color", ANARI_ARRAY1D)
            && reflects("primitive.id", ANARI_ARRAY1D),
        "isosurface parameters are reflected (incl. bridge primitive.id)");
  }

  {
    Scene scene(d);
    const float origin[3] = {-1.5f, -1.5f, -1.5f};

    // --- sphere: f = |p|, isovalue r -> a sphere of radius r ----------------
    std::printf("sphere level sets\n");
    Field sphere = makeField(d, 49, origin, 3.f, radius);
    Iso iso = makeIso(d, sphere.field, 1.f);
    setColor(d, iso.geom, 1.f, 1.f, 1.f);
    anariCommitParameters(d, iso.geom);
    scene.setSurfaces({iso.surface});
    {
      Image img = scene.render();
      check(img.covered(0.f, 0.f) && img.covered(0.9f, 0.f)
              && img.covered(0.f, -0.9f),
          "inside the r = 1 disc is covered");
      check(!img.covered(1.1f, 0.f) && !img.covered(0.f, 1.1f)
              && !img.covered(0.8f, 0.8f),
          "outside the r = 1 disc is background");
      const float area = img.coveredArea();
      check(std::fabs(area - float(M_PI)) < 0.1f,
          "silhouette area ~ pi: " + std::to_string(area));
      std::vector<float> b = scene.bounds(scene.world);
      float err = 0.f;
      for (int k = 0; k < 3; ++k)
        err = std::max({err, std::fabs(b[k] + 1.f), std::fabs(b[k + 3] - 1.f)});
      std::printf("    sphere bounds %s, max error %g (spacing %g)\n",
          str(b).c_str(), err, sphere.spacing);
      check(err < 0.25f * sphere.spacing * sphere.spacing / 1.f + 1e-3f,
          "world bounds are the r = 1 sphere to within h^2/4");
    }


    // --- edits: isovalue, field data in place, field object, field params ---
    std::printf("edits\n");
    auto extent = [&](ANARIObject o) {
      std::vector<float> b = scene.bounds(o);
      return std::max(std::fabs(b[0]), std::fabs(b[3]));
    };
    {
      float r = 0.5f;
      anariSetParameter(d, iso.geom, "isovalue", ANARI_FLOAT32, &r);
      anariCommitParameters(d, iso.geom);
      Image img = scene.render();
      check(img.covered(0.4f, 0.f) && !img.covered(0.6f, 0.f),
          "scalar isovalue edit re-extracts (r = 0.5)");
      check(std::fabs(extent(scene.world) - 0.5f) < 1e-3f,
          "bounds follow the isovalue edit");
      r = 1.f;
      anariSetParameter(d, iso.geom, "isovalue", ANARI_FLOAT32, &r);
      anariCommitParameters(d, iso.geom);
    }
    {
      // In-place field edit: f = 2|p| puts isovalue 1 at r = 0.5.
      sphere.fill(d, [](float x, float y, float z) { return 2.f * radius(x, y, z); });
      Image img = scene.render();
      check(img.covered(0.4f, 0.f) && !img.covered(0.6f, 0.f),
          "in-place field data edit re-extracts");
      sphere.fill(d, radius);
      img = scene.render();
      check(img.covered(0.9f, 0.f) && !img.covered(1.1f, 0.f),
          "restoring the field data restores the surface");
    }
    {
      // Field parameter edit: shifting the origin by +0.5 in x moves the
      // level set with it.
      const float shifted[3] = {-1.f, -1.5f, -1.5f};
      anariSetParameter(d, sphere.field, "origin", ANARI_FLOAT32_VEC3, shifted);
      anariCommitParameters(d, sphere.field);
      std::vector<float> b = scene.bounds(scene.world);
      check(std::fabs(b[0] + 0.5f) < 1e-3f && std::fabs(b[3] - 1.5f) < 1e-3f,
          "field origin edit moves the surface: " + str(b));
      anariSetParameter(d, sphere.field, "origin", ANARI_FLOAT32_VEC3, origin);
      anariCommitParameters(d, sphere.field);
    }
    {
      // A different field object: f = |p| / 0.75 -> r = 0.75 at isovalue 1.
      Field smaller = makeField(d, 49, origin, 3.f,
          [](float x, float y, float z) { return radius(x, y, z) / 0.75f; });
      anariSetParameter(d, iso.geom, "field", ANARI_SPATIAL_FIELD, &smaller.field);
      anariCommitParameters(d, iso.geom);
      check(std::fabs(extent(scene.world) - 0.75f) < 1e-3f,
          "replacing the field object re-extracts");
      anariSetParameter(d, iso.geom, "field", ANARI_SPATIAL_FIELD, &sphere.field);
      anariCommitParameters(d, iso.geom);
      release(d, smaller);
      check(std::fabs(extent(scene.world) - 1.f) < 1e-3f,
          "rebinding the original field restores the surface");
    }

    // --- empty surfaces -----------------------------------------------------
    std::printf("empty surfaces\n");
    {
      float r = 5.f;   // above every sample: no crossing
      anariSetParameter(d, iso.geom, "isovalue", ANARI_FLOAT32, &r);
      anariCommitParameters(d, iso.geom);
      Image img = scene.render();
      check(img.coveredArea() == 0.f, "an isovalue outside the field range renders nothing");
      std::vector<float> b = scene.bounds(scene.world);
      check(b[0] > b[3], "an empty isosurface contributes no bounds");
      r = 1.f;
      anariSetParameter(d, iso.geom, "isovalue", ANARI_FLOAT32, &r);
      anariCommitParameters(d, iso.geom);
      img = scene.render();
      check(img.covered(0.f, 0.f), "a crossing isovalue brings the surface back");
    }

    // --- transforms ---------------------------------------------------------
    std::printf("transforms\n");
    {
      scene.setSurfaces({});
      ANARIGroup group = anariNewGroup(d);
      ANARIArray1D surfaces =
          anariNewArray1D(d, &iso.surface, nullptr, nullptr, ANARI_SURFACE, 1);
      anariSetParameter(d, group, "surface", ANARI_ARRAY1D, &surfaces);
      anariCommitParameters(d, group);
      ANARIInstance inst = anariNewInstance(d, "transform");
      // Scale 0.5 then translate +0.5 in x: the unit sphere -> r 0.5 at x 0.5.
      const float m[16] = {0.5f, 0, 0, 0, 0, 0.5f, 0, 0, 0, 0, 0.5f, 0,
          0.5f, 0, 0, 1};
      anariSetParameter(d, inst, "transform", ANARI_FLOAT32_MAT4, m);
      anariSetParameter(d, inst, "group", ANARI_GROUP, &group);
      anariCommitParameters(d, inst);
      scene.setInstances({inst});
      Image img = scene.render();
      check(img.covered(0.5f, 0.f) && img.covered(0.9f, 0.f)
              && !img.covered(1.1f, 0.f) && !img.covered(-0.1f, 0.f),
          "an instance transform moves and scales the surface");
      std::vector<float> w = scene.bounds(scene.world);
      std::vector<float> g = scene.bounds(group);
      check(std::fabs(w[0]) < 1e-3f && std::fabs(w[3] - 1.f) < 1e-3f
              && std::fabs(w[1] + 0.5f) < 1e-3f && std::fabs(w[4] - 0.5f) < 1e-3f,
          "world bounds are transformed: " + str(w));
      check(std::fabs(g[0] + 1.f) < 1e-3f && std::fabs(g[3] - 1.f) < 1e-3f,
          "group bounds stay in object space: " + str(g));
      scene.setInstances({});
      anariRelease(d, inst);
      anariRelease(d, surfaces);
      anariRelease(d, group);
    }

    release(d, iso);
    release(d, sphere);
    scene.setSurfaces({});

    // --- multiple isovalues, per-isovalue attributes and primitive IDs -------
    // f = x + z: level set c is the plane z = c - x, tilted toward the
    // camera. Over the [-1, 1]^3 domain, isovalue -0.5 alone lies under
    // x < -0.5; isovalue 0.5 is nearer the camera wherever it exists (x > -0.5).
    std::printf("multiple isovalues\n");
    {
      const float o2[3] = {-1.f, -1.f, -1.f};
      Field planes = makeField(d, 21, o2, 2.f,
          [](float x, float, float z) { return x + z; });
      Iso multi = makeIso(d, planes.field, 0.f);
      const float values[2] = {-0.5f, 0.5f};
      ANARIArray1D isoArray =
          anariNewArray1D(d, nullptr, nullptr, nullptr, ANARI_FLOAT32, 2);
      std::memcpy(anariMapArray(d, isoArray), values, sizeof(values));
      anariUnmapArray(d, isoArray);
      anariSetParameter(d, multi.geom, "isovalue", ANARI_ARRAY1D, &isoArray);
      const float colors[6] = {1, 0, 0, 0, 1, 0};
      ANARIArray1D colorArray =
          anariNewArray1D(d, colors, nullptr, nullptr, ANARI_FLOAT32_VEC3, 2);
      anariSetParameter(d, multi.geom, "primitive.color", ANARI_ARRAY1D, &colorArray);
      anariCommitParameters(d, multi.geom);
      scene.setSurfaces({multi.surface});

      Image img = scene.render();
      check(img.covered(-0.8f, 0.f) && img.covered(0.8f, 0.f),
          "both level sets render");
      if (!isEmbree) {
        const float *l = img.world(-0.8f, 0.f), *r = img.world(0.8f, 0.f);
        check(l[0] > 2.f * l[1] && r[1] > 2.f * r[0],
            "primitive.color is per isovalue: left red, right green");
      }
      check(img.primAt(-0.8f, 0.f) == 0 && img.primAt(0.8f, 0.f) == 1,
          "primitiveId is the isovalue index: " + std::to_string(img.primAt(-0.8f, 0.f))
              + ", " + std::to_string(img.primAt(0.8f, 0.f)));
      std::vector<float> b = scene.bounds(scene.world);
      // Plane z = c - x clipped to the domain: isovalue -0.5 spans
      // x in [-1, 0.5], isovalue 0.5 x in [-0.5, 1]; z spans [-1, 1].
      check(std::fabs(b[0] + 1.f) < 1e-4f && std::fabs(b[3] - 1.f) < 1e-4f
              && std::fabs(b[2] + 1.f) < 1e-4f && std::fabs(b[5] - 1.f) < 1e-4f,
          "linear-field planes are exact to the domain: " + str(b));

      const uint32_t ids[2] = {7, 9};
      ANARIArray1D idArray =
          anariNewArray1D(d, ids, nullptr, nullptr, ANARI_UINT32, 2);
      anariSetParameter(d, multi.geom, "primitive.id", ANARI_ARRAY1D, &idArray);
      anariCommitParameters(d, multi.geom);
      img = scene.render();
      check(img.primAt(-0.8f, 0.f) == 7 && img.primAt(0.8f, 0.f) == 9,
          "primitive.id maps isovalue indices to user IDs");
      anariUnsetParameter(d, multi.geom, "primitive.id");
      anariCommitParameters(d, multi.geom);
      anariRelease(d, idArray);

      if (!isEmbree) {
        // primitive.attribute0 per isovalue, read by the material; then the
        // constant attribute0 once the array is removed.
        const float attrs[8] = {0, 0, 1, 1, 1, 0, 0, 1};
        ANARIArray1D attrArray =
            anariNewArray1D(d, attrs, nullptr, nullptr, ANARI_FLOAT32_VEC4, 2);
        anariSetParameter(d, multi.geom, "primitive.attribute0", ANARI_ARRAY1D, &attrArray);
        anariCommitParameters(d, multi.geom);
        anariSetParameter(d, multi.material, "color", ANARI_STRING, "attribute0");
        anariCommitParameters(d, multi.material);
        img = scene.render();
        const float *l = img.world(-0.8f, 0.f), *r = img.world(0.8f, 0.f);
        check(l[2] > 2.f * l[0] && r[0] > 2.f * r[2],
            "primitive.attribute0 is per isovalue: left blue, right red");
        anariUnsetParameter(d, multi.geom, "primitive.attribute0");
        const float yellow[4] = {1, 1, 0, 1};
        anariSetParameter(d, multi.geom, "attribute0", ANARI_FLOAT32_VEC4, yellow);
        anariCommitParameters(d, multi.geom);
        img = scene.render();
        l = img.world(-0.8f, 0.f);
        r = img.world(0.8f, 0.f);
        check(l[0] > 2.f * l[2] && l[1] > 2.f * l[2] && r[0] > 2.f * r[2]
                && r[1] > 2.f * r[2],
            "constant attribute0 covers every isovalue");
        anariUnsetParameter(d, multi.geom, "attribute0");
        anariCommitParameters(d, multi.geom);
        anariSetParameter(d, multi.material, "color", ANARI_STRING, "color");
        anariCommitParameters(d, multi.material);
        anariRelease(d, attrArray);
      }

      // In-place isovalue array edit: drop the second plane out of range.
      float *v = static_cast<float *>(anariMapArray(d, isoArray));
      v[1] = 10.f;
      anariUnmapArray(d, isoArray);
      img = scene.render();
      check(img.primAt(0.f, 0.f) == 0 && !img.covered(0.8f, 0.f),
          "in-place isovalue array edit re-extracts (only isovalue 0 left)");
      b = scene.bounds(scene.world);
      check(std::fabs(b[3] - 0.5f) < 1e-4f,
          "bounds follow the remaining plane: " + str(b));

      scene.setSurfaces({});
      anariRelease(d, colorArray);
      anariRelease(d, isoArray);
      release(d, multi);
      release(d, planes);
    }

    // --- approximation error --------------------------------------------------
    // The mesh reproduces the level set of the field's piecewise-linear
    // (Kuhn tetrahedral) interpolant, whose distance from the true sphere
    // shrinks as h^2. Measured on the rendered surface itself: under the
    // orthographic camera a pixel centre fixes the surface point's x and y,
    // and an emissive transform sampler writes its object-space z (as
    // z - 0.75, near zero where the color buffer is most precise) into the
    // float color channel, with a zero-irradiance light so nothing else
    // contributes. Storm only (embree has no samplers).
    if (!isEmbree) {
      std::printf("approximation error (f = |p|, isovalue 1)\n");
      ANARILight dark = anariNewLight(d, "directional");
      const float zero = 0.f;
      anariSetParameter(d, dark, "irradiance", ANARI_FLOAT32, &zero);
      anariCommitParameters(d, dark);
      ANARIArray1D lights =
          anariNewArray1D(d, &dark, nullptr, nullptr, ANARI_LIGHT, 1);
      anariSetParameter(d, scene.world, "light", ANARI_ARRAY1D, &lights);
      anariCommitParameters(d, scene.world);
      float noAmbient = 0.f;
      anariSetParameter(d, scene.renderer, "ambientRadiance", ANARI_FLOAT32, &noAmbient);
      anariCommitParameters(d, scene.renderer);

      ANARISampler xf = anariNewSampler(d, "transform");
      anariSetParameter(d, xf, "inAttribute", ANARI_STRING, "objectPosition");
      // Column-major: red = z - 0.75, green = blue = 0.
      const float zToRed[16] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0};
      const float offset[4] = {-0.75f, 0.f, 0.f, 0.f};
      anariSetParameter(d, xf, "outTransform", ANARI_FLOAT32_MAT4, zToRed);
      anariSetParameter(d, xf, "outOffset", ANARI_FLOAT32_VEC4, offset);
      anariCommitParameters(d, xf);
      ANARIMaterial emissive = anariNewMaterial(d, "physicallyBased");
      const float black[3] = {0.f, 0.f, 0.f};
      anariSetParameter(d, emissive, "baseColor", ANARI_FLOAT32_VEC3, black);
      anariSetParameter(d, emissive, "emissive", ANARI_SAMPLER, &xf);
      anariCommitParameters(d, emissive);

      float previous = 0.f;
      for (int n : {17, 33, 65}) {
        Field f = makeField(d, n, origin, 3.f, radius);
        Iso s = makeIso(d, f.field, 1.f);
        anariSetParameter(d, s.surface, "material", ANARI_MATERIAL, &emissive);
        anariCommitParameters(d, s.surface);
        scene.setSurfaces({s.surface});
        Image img = scene.render();
        float maxErr = 0.f, sumErr = 0.f;
        size_t count = 0;
        for (uint32_t y = 0; y < H; ++y)
          for (uint32_t x = 0; x < W; ++x) {
            const float wx = (x + 0.5f) / W * 4.f - 2.f;
            const float wy = (y + 0.5f) / H * 4.f - 2.f;
            // The front cap z >= 0.6, clear of the silhouette.
            if (wx * wx + wy * wy > 0.64f)
              continue;
            const float z = img.world(wx, wy)[0] + 0.75f;
            const float e = std::fabs(radius(wx, wy, z) - 1.f);
            maxErr = std::max(maxErr, e);
            sumErr += e;
            ++count;
          }
        const float h = f.spacing;
        std::printf("    n = %3d  h = %.4f  max |r - 1| = %.5f (%.3f h^2)  "
                    "mean = %.5f  over %zu px\n",
            n, h, maxErr, maxErr / (h * h), count ? sumErr / count : 0.f, count);
        // Linear interpolation of |p| along an edge of length l errs by at
        // most l^2 / (8 r); the longest Kuhn edge is the cell diagonal
        // sqrt(3) h, so the level set sits within 3 h^2 / 8 of the sphere.
        check(count > 1000 && maxErr < 0.375f * h * h + 2e-4f,
            "n = " + std::to_string(n)
                + ": surface within 3 h^2 / 8 (+2e-4 readback) of the sphere");
        if (previous > 0.f && n <= 33)
          check(maxErr < 0.5f * previous, "error falls at least 2x per halving of h");
        previous = maxErr;
        scene.setSurfaces({});
        release(d, s);
        release(d, f);
      }

      anariRelease(d, emissive);
      anariRelease(d, xf);
      anariUnsetParameter(d, scene.world, "light");
      anariCommitParameters(d, scene.world);
      anariRelease(d, lights);
      anariRelease(d, dark);
    }

    // --- in-place isovalue edits during an extraction -------------------------
    // Vela's isosurface slider edits a shared isovalue array in place (map,
    // write, unmap) while frames are in flight. The commit buffer flushes on the
    // device worker, so an unmap can land while the worker is still extracting
    // the previous value; that last edit must still reach the device.
    std::printf("isovalue edits during extraction\n");
    {
      const int n = 128;
      Field f = makeField(d, n, origin, 3.f, radius);
      ANARIArray1D values =
          anariNewArray1D(d, nullptr, nullptr, nullptr, ANARI_FLOAT32, 1);
      auto setValue = [&](float v) {
        *static_cast<float *>(anariMapArray(d, values)) = v;
        anariUnmapArray(d, values);
      };
      setValue(0.5f);
      Iso s = makeIso(d, f.field, 0.f);
      anariSetParameter(d, s.geom, "isovalue", ANARI_ARRAY1D, &values);
      anariCommitParameters(d, s.geom);
      scene.setSurfaces({s.surface});
      scene.bounds(scene.world);
      {
        // Control: the same edits with the extraction finished in between.
        setValue(1.f);
        anariRenderFrame(d, scene.frame);
        anariFrameReady(d, scene.frame, ANARI_WAIT);
        setValue(0.75f);
        std::vector<float> b = scene.bounds(scene.world);
        check(std::fabs(b[3] - 0.75f) < 0.01f,
            "an unmap after the extraction reaches the device: " + str(b));
        setValue(0.5f);
        scene.bounds(scene.world);
      }
      for (int delayMs : {5, 10, 20, 30}) {
        setValue(1.f);
        anariRenderFrame(d, scene.frame);   // the worker starts extracting r = 1
        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
        setValue(0.75f);                    // lands mid-extraction
        anariFrameReady(d, scene.frame, ANARI_WAIT);
        std::vector<float> b = scene.bounds(scene.world);
        check(std::fabs(b[3] - 0.75f) < 0.01f,
            "an unmap " + std::to_string(delayMs)
                + " ms into an extraction reaches the device: " + str(b));
        setValue(0.5f);   // reset between trials
        scene.bounds(scene.world);
      }
      scene.setSurfaces({});
      release(d, s);
      anariRelease(d, values);
      release(d, f);
    }

    // --- extraction cost ------------------------------------------------------
    // Stage 1 extracts on the device worker; a bounds query waits for it.
    std::printf("extraction cost\n");
    {
      const int n = 128;
      Field f = makeField(d, n, origin, 3.f, radius);
      Iso s = makeIso(d, f.field, 0.f);
      scene.setSurfaces({s.surface});
      scene.bounds(scene.world);
      float r = 1.f;
      anariSetParameter(d, s.geom, "isovalue", ANARI_FLOAT32, &r);
      anariCommitParameters(d, s.geom);
      const auto t0 = std::chrono::steady_clock::now();
      std::vector<float> b = scene.bounds(scene.world);
      const double ms = std::chrono::duration<double, std::milli>(
          std::chrono::steady_clock::now() - t0).count();
      std::printf("    %d^3 field, one sphere: extraction + bounds %.1f ms\n", n, ms);
      std::printf("    device: %s\n", g_lastExtraction.c_str());
      check(std::fabs(b[3] - 1.f) < 1e-3f, "large-field extraction completes");
      scene.setSurfaces({});
      release(d, s);
      release(d, f);
    }
  }

  anariRelease(d, d);
  anariUnloadLibrary(lib);
  std::printf("\n%s (%d issues)\n",
      g_errors == 0 ? "ALL CHECKS PASSED" : "CHECKS FAILED", g_errors);
  return g_errors == 0 ? 0 : 1;
}
