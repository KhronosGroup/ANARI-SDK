// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0
//
// Exercises the complete ANARI instance input forms (SPEC §4.3/§6.8):
//  - `transform` as FLOAT32_MAT4 or the compact FLOAT32_MAT3x4;
//  - `transform` as an ARRAY1D of MAT4 (KHR_INSTANCE_TRANSFORM_ARRAY), with a
//    parallel `id` UINT32 array feeding the instanceId channel;
//  - per-instance `color`/`attribute0..3` as FLOAT32 / VEC2 / VEC3 / VEC4
//    (ANARI component defaults (0,0,0,1) fill the missing lanes) and as
//    per-transform ARRAY1Ds of any of those element types.
//
// The sharp checks are byte-identical frames between scenes that the ANARI
// contract says are equivalent: MAT4 vs MAT3x4, N scalar instances vs one
// array instance with N transforms (surfaces, and group lights), and across
// several groups. Edits then mutate the array in place and swap the array for
// one of another length to prove the flattened instancer follows.

#include <anari/anari.h>
#include <anari/frontend/type_utility.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

static int g_errors = 0;

static void statusFunc(const void *,
    ANARIDevice,
    ANARIObject,
    ANARIDataType,
    ANARIStatusSeverity severity,
    ANARIStatusCode,
    const char *message)
{
  if (severity <= ANARI_SEVERITY_WARNING)
    std::printf("[%s] %s\n",
        severity <= ANARI_SEVERITY_ERROR ? "ERR" : "WARN",
        message);
  if (severity <= ANARI_SEVERITY_ERROR)
    ++g_errors;
}

static void check(bool ok, const std::string &what)
{
  std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
  if (!ok)
    ++g_errors;
}

// Column-major 4x4 translation.
static std::vector<float> mat4T(float tx, float ty = 0.f)
{
  return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, tx, ty, 0, 1};
}

// Column-major 3x4 (four float3 columns, the last being the translation).
static std::vector<float> mat3x4T(float tx, float ty = 0.f)
{
  return {1, 0, 0, 0, 1, 0, 0, 0, 1, tx, ty, 0};
}

struct Frame
{
  uint32_t w{0}, h{0};
  std::vector<uint32_t> color; // RGBA8 sRGB
  std::vector<uint32_t> instanceId;

  const uint8_t *px(uint32_t x, uint32_t y) const
  {
    return (const uint8_t *)&color[y * w + x];
  }
  uint32_t id(uint32_t x, uint32_t y) const
  {
    return instanceId[y * w + x];
  }
  int coverage() const
  {
    int n = 0;
    for (uint32_t p : color) {
      const uint8_t *c = (const uint8_t *)&p;
      if (c[0] > 40 || c[1] > 40 || c[2] > 40)
        ++n;
    }
    return n;
  }
  // 0=r,1=g,2=b dominant by 30+ over the other two at (x,y); -1 if none.
  int dominant(uint32_t x, uint32_t y) const
  {
    const uint8_t *c = px(x, y);
    for (int k = 0; k < 3; ++k)
      if (c[k] > c[(k + 1) % 3] + 30 && c[k] > c[(k + 2) % 3] + 30)
        return k;
    return -1;
  }
};

static size_t mismatches(const Frame &a, const Frame &b)
{
  if (a.color.size() != b.color.size())
    return ~size_t(0);
  size_t n = 0;
  for (size_t i = 0; i < a.color.size(); ++i)
    n += a.color[i] != b.color[i];
  return n;
}

// A live scene: one world, a camera, a frame. Arrays are app-owned (zero-copy),
// so `array()` copies the caller's data into storage that outlives the scene.
struct Scene
{
  ANARIDevice dev{nullptr};
  ANARIWorld world{nullptr};
  ANARIFrame frame{nullptr};
  ANARICamera cam{nullptr};
  ANARIRenderer ren{nullptr};
  std::vector<ANARIObject> owned; // released in reverse order
  std::deque<std::vector<uint8_t>> blobs;

  template <typename T>
  T keep(T o)
  {
    owned.push_back((ANARIObject)o);
    return o;
  }

  ANARIArray1D array(const void *data, ANARIDataType type, uint64_t n)
  {
    const size_t bytes = size_t(anariSizeOf(type)) * n;
    blobs.emplace_back((const uint8_t *)data, (const uint8_t *)data + bytes);
    return keep(
        anariNewArray1D(dev, blobs.back().data(), nullptr, nullptr, type, n));
  }

  // Quad prototype spanning local [-0.5,0.5]^2; matte color bound to `attr`.
  ANARIGroup quadGroup(const char *attr = "color", float half = 0.5f)
  {
    float verts[12] = {
        -half, -half, 0.f, half, -half, 0.f, half, half, 0.f, -half, half, 0.f};
    ANARIArray1D pos = array(verts, ANARI_FLOAT32_VEC3, 4);
    ANARIGeometry geom = keep(anariNewGeometry(dev, "quad"));
    anariSetParameter(dev, geom, "vertex.position", ANARI_ARRAY1D, &pos);
    anariCommitParameters(dev, geom);
    ANARIMaterial mat = keep(anariNewMaterial(dev, "matte"));
    anariSetParameter(dev, mat, "color", ANARI_STRING, attr);
    anariCommitParameters(dev, mat);
    ANARISurface surf = keep(anariNewSurface(dev));
    anariSetParameter(dev, surf, "geometry", ANARI_GEOMETRY, &geom);
    anariSetParameter(dev, surf, "material", ANARI_MATERIAL, &mat);
    anariCommitParameters(dev, surf);
    ANARIGroup group = keep(anariNewGroup(dev));
    ANARIArray1D surfArray = array(&surf, ANARI_SURFACE, 1);
    anariSetParameter(dev, group, "surface", ANARI_ARRAY1D, &surfArray);
    anariCommitParameters(dev, group);
    return group;
  }

  ANARIInstance instance(ANARIGroup group)
  {
    ANARIInstance inst = keep(anariNewInstance(dev, "transform"));
    anariSetParameter(dev, inst, "group", ANARI_GROUP, &group);
    return inst;
  }

  void setInstances(const std::vector<ANARIInstance> &instances)
  {
    ANARIArray1D instArray =
        array(instances.data(), ANARI_INSTANCE, instances.size());
    anariSetParameter(dev, world, "instance", ANARI_ARRAY1D, &instArray);
    anariCommitParameters(dev, world);
  }

  explicit Scene(ANARIDevice d) : dev(d)
  {
    world = keep(anariNewWorld(dev));
    cam = keep(anariNewCamera(dev, "perspective"));
    float eye[3] = {0.f, 0.f, 3.f};
    float dir[3] = {0.f, 0.f, -1.f};
    float up[3] = {0.f, 1.f, 0.f};
    float fovy = 0.7f;
    anariSetParameter(dev, cam, "position", ANARI_FLOAT32_VEC3, eye);
    anariSetParameter(dev, cam, "direction", ANARI_FLOAT32_VEC3, dir);
    anariSetParameter(dev, cam, "up", ANARI_FLOAT32_VEC3, up);
    anariSetParameter(dev, cam, "fovy", ANARI_FLOAT32, &fovy);
    anariCommitParameters(dev, cam);
    ren = keep(anariNewRenderer(dev, "default"));
    anariCommitParameters(dev, ren);
    frame = keep(anariNewFrame(dev));
    uint32_t size[2] = {128, 128};
    ANARIDataType colorFmt = ANARI_UFIXED8_RGBA_SRGB;
    ANARIDataType u32 = ANARI_UINT32;
    anariSetParameter(dev, frame, "size", ANARI_UINT32_VEC2, size);
    anariSetParameter(dev, frame, "channel.color", ANARI_DATA_TYPE, &colorFmt);
    anariSetParameter(dev, frame, "channel.instanceId", ANARI_DATA_TYPE, &u32);
    anariSetParameter(dev, frame, "world", ANARI_WORLD, &world);
    anariSetParameter(dev, frame, "camera", ANARI_CAMERA, &cam);
    anariSetParameter(dev, frame, "renderer", ANARI_RENDERER, &ren);
    anariCommitParameters(dev, frame);
  }

  ~Scene()
  {
    for (auto it = owned.rbegin(); it != owned.rend(); ++it)
      anariRelease(dev, *it);
  }

  Frame render()
  {
    anariRenderFrame(dev, frame);
    anariFrameReady(dev, frame, ANARI_WAIT);
    Frame f;
    ANARIDataType pt = ANARI_UNKNOWN;
    const uint32_t *px = (const uint32_t *)anariMapFrame(
        dev, frame, "channel.color", &f.w, &f.h, &pt);
    if (px)
      f.color.assign(px, px + size_t(f.w) * f.h);
    anariUnmapFrame(dev, frame, "channel.color");
    uint32_t iw = 0, ih = 0;
    const uint32_t *ids = (const uint32_t *)anariMapFrame(
        dev, frame, "channel.instanceId", &iw, &ih, &pt);
    if (ids && iw == f.w && ih == f.h)
      f.instanceId.assign(ids, ids + size_t(iw) * ih);
    else
      f.instanceId.assign(size_t(f.w) * f.h, ~0u);
    anariUnmapFrame(dev, frame, "channel.instanceId");
    return f;
  }
};

// --- scenes -----------------------------------------------------------------

// Two red/green quads at -/+X: N scalar instances.
static Frame scalarPair(ANARIDevice dev)
{
  Scene s(dev);
  ANARIGroup g = s.quadGroup();
  const float col[2][4] = {{1, 0, 0, 1}, {0, 1, 0, 1}};
  const float tx[2] = {-0.75f, 0.75f};
  const uint32_t ids[2] = {0xa1u, 0xb2u};
  std::vector<ANARIInstance> insts;
  for (int i = 0; i < 2; ++i) {
    ANARIInstance inst = s.instance(g);
    anariSetParameter(
        dev, inst, "transform", ANARI_FLOAT32_MAT4, mat4T(tx[i]).data());
    anariSetParameter(dev, inst, "color", ANARI_FLOAT32_VEC4, col[i]);
    anariSetParameter(dev, inst, "id", ANARI_UINT32, &ids[i]);
    anariCommitParameters(dev, inst);
    insts.push_back(inst);
  }
  s.setInstances(insts);
  return s.render();
}

int main(int argc, char **argv)
{
  const char *subtype = argc > 1 ? argv[1] : "storm";
  ANARILibrary lib = anariLoadLibrary("hydra", statusFunc, nullptr);
  if (!lib)
    return 2;
  ANARIDevice dev = anariNewDevice(lib, subtype);
  if (!dev)
    return 2;

  const uint32_t W = 128, H = 128;
  const uint32_t L = W / 6, R = 5 * W / 6, MID = H / 2;

  // --- 1. MAT4 vs MAT3x4 scalar transforms ---------------------------------
  {
    Frame a, b;
    {
      Scene s(dev);
      ANARIInstance inst = s.instance(s.quadGroup());
      anariSetParameter(
          dev, inst, "transform", ANARI_FLOAT32_MAT4, mat4T(-0.75f).data());
      const float red[4] = {1, 0, 0, 1};
      anariSetParameter(dev, inst, "color", ANARI_FLOAT32_VEC4, red);
      anariCommitParameters(dev, inst);
      s.setInstances({inst});
      a = s.render();
    }
    {
      Scene s(dev);
      ANARIInstance inst = s.instance(s.quadGroup());
      anariSetParameter(
          dev, inst, "transform", ANARI_FLOAT32_MAT3x4, mat3x4T(-0.75f).data());
      const float red[4] = {1, 0, 0, 1};
      anariSetParameter(dev, inst, "color", ANARI_FLOAT32_VEC4, red);
      anariCommitParameters(dev, inst);
      s.setInstances({inst});
      b = s.render();
    }
    std::printf("MAT4 cov=%d MAT3x4 cov=%d mismatches=%zu\n",
        a.coverage(),
        b.coverage(),
        mismatches(a, b));
    check(a.coverage() > 100 && a.dominant(L, MID) == 0,
        "MAT4 instance renders red at -X");
    check(b.coverage() > 100 && b.dominant(L, MID) == 0,
        "MAT3x4 instance renders red at -X");
    check(
        mismatches(a, b) == 0, "MAT3x4 frame is byte-identical to MAT4 frame");
  }

  // --- 2. N scalar instances vs one transform-array instance ----------------
  Frame ref = scalarPair(dev);
  check(ref.dominant(L, MID) == 0 && ref.dominant(R, MID) == 1,
      "scalar pair: left red, right green");
  check(ref.id(L, MID) == 0xa1u && ref.id(R, MID) == 0xb2u,
      "scalar pair: instance IDs reach the instanceId channel");
  {
    Scene s(dev);
    ANARIGroup g = s.quadGroup();
    ANARIInstance inst = s.instance(g);
    std::vector<float> xf = mat4T(-0.75f);
    std::vector<float> x1 = mat4T(0.75f);
    xf.insert(xf.end(), x1.begin(), x1.end());
    ANARIArray1D xfArr = s.array(xf.data(), ANARI_FLOAT32_MAT4, 2);
    const float cols[8] = {1, 0, 0, 1, 0, 1, 0, 1};
    ANARIArray1D colArr = s.array(cols, ANARI_FLOAT32_VEC4, 2);
    const uint32_t ids[2] = {0xa1u, 0xb2u};
    ANARIArray1D idArr = s.array(ids, ANARI_UINT32, 2);
    anariSetParameter(dev, inst, "transform", ANARI_ARRAY1D, &xfArr);
    anariSetParameter(dev, inst, "color", ANARI_ARRAY1D, &colArr);
    anariSetParameter(dev, inst, "id", ANARI_ARRAY1D, &idArr);
    anariCommitParameters(dev, inst);
    s.setInstances({inst});
    Frame f = s.render();
    std::printf("array pair: cov=%d mismatches vs scalar=%zu ids=%08x/%08x\n",
        f.coverage(),
        mismatches(ref, f),
        f.id(L, MID),
        f.id(R, MID));
    check(f.dominant(L, MID) == 0 && f.dominant(R, MID) == 1,
        "transform array + color array: left red, right green");
    check(mismatches(ref, f) == 0,
        "array instance frame is byte-identical to two scalar instances");
    check(f.id(L, MID) == 0xa1u && f.id(R, MID) == 0xb2u,
        "id array supplies a per-transform instanceId");

    // The same pair as an ARRAY1D of compact MAT3x4 elements.
    std::vector<float> cf = mat3x4T(-0.75f), c1 = mat3x4T(0.75f);
    cf.insert(cf.end(), c1.begin(), c1.end());
    ANARIArray1D cfArr = s.array(cf.data(), ANARI_FLOAT32_MAT3x4, 2);
    anariSetParameter(dev, inst, "transform", ANARI_ARRAY1D, &cfArr);
    anariCommitParameters(dev, inst);
    f = s.render();
    check(mismatches(ref, f) == 0 && f.id(L, MID) == 0xa1u,
        "MAT3x4 transform array is byte-identical to the MAT4 array / scalar pair");
    anariSetParameter(dev, inst, "transform", ANARI_ARRAY1D, &xfArr);
    anariCommitParameters(dev, inst);

    // In-place edit of the attribute array: swap the two colors.
    float *mappedCols = (float *)anariMapArray(dev, colArr);
    std::memcpy(mappedCols, cols + 4, 4 * sizeof(float));
    std::memcpy(mappedCols + 4, cols, 4 * sizeof(float));
    anariUnmapArray(dev, colArr);
    f = s.render();
    check(f.dominant(L, MID) == 1 && f.dominant(R, MID) == 0,
        "in-place color array edit swaps the per-instance colors");
    mappedCols = (float *)anariMapArray(dev, colArr);
    std::memcpy(mappedCols, cols, 8 * sizeof(float));
    anariUnmapArray(dev, colArr);

    // Scalar id with a transform array applies to every transform.
    anariUnsetParameter(dev, inst, "id");
    const uint32_t one = 0xc3u;
    anariSetParameter(dev, inst, "id", ANARI_UINT32, &one);
    anariCommitParameters(dev, inst);
    f = s.render();
    check(f.id(L, MID) == 0xc3u && f.id(R, MID) == 0xc3u,
        "scalar id with a transform array labels every transform");
    anariUnsetParameter(dev, inst, "id");
    anariCommitParameters(dev, inst);
    f = s.render();
    check(f.id(L, MID) == ~0u && f.id(R, MID) == ~0u,
        "no id: instanceId is UINT32_MAX for array transforms");

    // --- 3. Edits: in-place mutation, then array length change -------------
    float *mapped = (float *)anariMapArray(dev, xfArr);
    std::memcpy(mapped, mat4T(0.75f).data(), 16 * sizeof(float));
    std::memcpy(mapped + 16, mat4T(-0.75f).data(), 16 * sizeof(float));
    anariUnmapArray(dev, xfArr);
    f = s.render();
    check(f.dominant(L, MID) == 1 && f.dominant(R, MID) == 0,
        "in-place transform array edit swaps the instances (left green, right red)");

    const int cov2 = f.coverage();
    std::vector<float> xf3 = mat4T(-0.75f);
    for (float tx : {0.f, 0.75f}) {
      std::vector<float> m = mat4T(tx);
      xf3.insert(xf3.end(), m.begin(), m.end());
    }
    ANARIArray1D xfArr3 = s.array(xf3.data(), ANARI_FLOAT32_MAT4, 3);
    const float cols3[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    ANARIArray1D colArr3 = s.array(cols3, ANARI_FLOAT32_VEC3, 3);
    anariSetParameter(dev, inst, "transform", ANARI_ARRAY1D, &xfArr3);
    anariSetParameter(dev, inst, "color", ANARI_ARRAY1D, &colArr3);
    anariCommitParameters(dev, inst);
    f = s.render();
    std::printf("length 2->3: cov %d -> %d\n", cov2, f.coverage());
    // The three unit quads at x = -0.75/0/0.75 overlap: the union spans 2.5
    // units against 2.0 for the two-instance pair, so expect ~1.25x coverage.
    check(f.coverage() > cov2 * 6 / 5,
        "growing the transform array to 3 adds a third instance");
    check(f.dominant(L, MID) == 0 && f.dominant(W / 2, MID) == 1
            && f.dominant(R, MID) == 2,
        "3-element VEC3 color array: red, green, blue across the row");

    ANARIArray1D xfArr1 = s.array(mat4T(0.f).data(), ANARI_FLOAT32_MAT4, 1);
    anariSetParameter(dev, inst, "transform", ANARI_ARRAY1D, &xfArr1);
    anariCommitParameters(dev, inst);
    f = s.render();
    check(f.coverage() < cov2 * 7 / 10 && f.dominant(W / 2, MID) == 0,
        "shrinking to 1 transform leaves one (red, element 0 of the color array) instance");

    // Back to a scalar transform: the array is dropped, attribute array element
    // 0 applies, and a lingering id array is ignored (it is per-transform).
    anariUnsetParameter(dev, inst, "transform");
    anariSetParameter(
        dev, inst, "transform", ANARI_FLOAT32_MAT4, mat4T(-0.75f).data());
    anariSetParameter(dev, inst, "id", ANARI_ARRAY1D, &idArr);
    anariCommitParameters(dev, inst);
    f = s.render();
    check(f.dominant(L, MID) == 0 && f.dominant(R, MID) == -1,
        "scalar transform after an array: single instance, color array element 0");
    check(f.id(L, MID) == ~0u,
        "id array without a transform array is ignored (per-transform semantics)");
  }

  // --- 4. Attribute component defaults: FLOAT32 / VEC2 / VEC3 / VEC4 -------
  {
    struct Case
    {
      const char *label;
      ANARIDataType type;
      float v[4];
      int expectDominant;
    };
    const Case cases[] = {
        {"FLOAT32 1 -> (1,0,0,1) red", ANARI_FLOAT32, {1, 0, 0, 0}, 0},
        {"VEC2 (0,1) -> (0,1,0,1) green", ANARI_FLOAT32_VEC2, {0, 1, 0, 0}, 1},
        {"VEC3 (0,0,1) -> blue", ANARI_FLOAT32_VEC3, {0, 0, 1, 0}, 2},
        {"VEC4 (0,1,0,1) -> green", ANARI_FLOAT32_VEC4, {0, 1, 0, 1}, 1},
    };
    for (const char *attr : {"color", "attribute0", "attribute2"}) {
      Scene s(dev);
      ANARIInstance inst = s.instance(s.quadGroup(attr));
      anariSetParameter(
          dev, inst, "transform", ANARI_FLOAT32_MAT4, mat4T(0.f).data());
      anariCommitParameters(dev, inst);
      s.setInstances({inst});
      for (const Case &c : cases) {
        anariUnsetParameter(dev, inst, attr);
        anariSetParameter(dev, inst, attr, c.type, c.v);
        anariCommitParameters(dev, inst);
        Frame f = s.render();
        check(f.dominant(W / 2, MID) == c.expectDominant,
            std::string("scalar instance.") + attr + " " + c.label);
        // The same value as a one-element array.
        anariUnsetParameter(dev, inst, attr);
        ANARIArray1D arr = s.array(c.v, c.type, 1);
        anariSetParameter(dev, inst, attr, ANARI_ARRAY1D, &arr);
        anariCommitParameters(dev, inst);
        f = s.render();
        check(f.dominant(W / 2, MID) == c.expectDominant,
            std::string("array instance.") + attr + " " + c.label);
      }
      // A parameter holds one typed value: re-setting the name with a scalar
      // replaces the array, so the scalar applies.
      const float blue[3] = {0, 0, 1};
      const float red[4] = {1, 0, 0, 1};
      anariUnsetParameter(dev, inst, attr);
      ANARIArray1D arr = s.array(blue, ANARI_FLOAT32_VEC3, 1);
      anariSetParameter(dev, inst, attr, ANARI_ARRAY1D, &arr);
      anariCommitParameters(dev, inst);
      anariSetParameter(dev, inst, attr, ANARI_FLOAT32_VEC4, red);
      anariCommitParameters(dev, inst);
      Frame f = s.render();
      check(f.dominant(W / 2, MID) == 0,
          std::string("replacing the ") + attr
              + " array with a scalar applies the scalar");
    }
  }

  // --- 5. Multiple groups: scalar vs array instancing across two groups ----
  {
    Frame a, b;
    {
      Scene s(dev);
      ANARIGroup g1 = s.quadGroup("color", 0.4f);
      ANARIGroup g2 = s.quadGroup("attribute1", 0.25f);
      std::vector<ANARIInstance> insts;
      const float c1[2][4] = {{1, 0, 0, 1}, {0, 1, 0, 1}};
      const float c2[2][3] = {{0, 0, 1}, {1, 1, 0}};
      for (int i = 0; i < 2; ++i) {
        ANARIInstance inst = s.instance(g1);
        anariSetParameter(dev,
            inst,
            "transform",
            ANARI_FLOAT32_MAT4,
            mat4T(i ? 0.75f : -0.75f, 0.6f).data());
        anariSetParameter(dev, inst, "color", ANARI_FLOAT32_VEC4, c1[i]);
        anariCommitParameters(dev, inst);
        insts.push_back(inst);
        inst = s.instance(g2);
        anariSetParameter(dev,
            inst,
            "transform",
            ANARI_FLOAT32_MAT4,
            mat4T(i ? 0.75f : -0.75f, -0.6f).data());
        anariSetParameter(dev, inst, "attribute1", ANARI_FLOAT32_VEC3, c2[i]);
        anariCommitParameters(dev, inst);
        insts.push_back(inst);
      }
      s.setInstances(insts);
      a = s.render();
    }
    {
      Scene s(dev);
      ANARIGroup g1 = s.quadGroup("color", 0.4f);
      ANARIGroup g2 = s.quadGroup("attribute1", 0.25f);
      std::vector<float> x1 = mat4T(-0.75f, 0.6f), x2 = mat4T(-0.75f, -0.6f);
      std::vector<float> t = mat4T(0.75f, 0.6f);
      x1.insert(x1.end(), t.begin(), t.end());
      t = mat4T(0.75f, -0.6f);
      x2.insert(x2.end(), t.begin(), t.end());
      ANARIArray1D xa1 = s.array(x1.data(), ANARI_FLOAT32_MAT4, 2);
      ANARIArray1D xa2 = s.array(x2.data(), ANARI_FLOAT32_MAT4, 2);
      const float c1[8] = {1, 0, 0, 1, 0, 1, 0, 1};
      const float c2[6] = {0, 0, 1, 1, 1, 0};
      ANARIArray1D ca1 = s.array(c1, ANARI_FLOAT32_VEC4, 2);
      ANARIArray1D ca2 = s.array(c2, ANARI_FLOAT32_VEC3, 2);
      ANARIInstance i1 = s.instance(g1);
      anariSetParameter(dev, i1, "transform", ANARI_ARRAY1D, &xa1);
      anariSetParameter(dev, i1, "color", ANARI_ARRAY1D, &ca1);
      anariCommitParameters(dev, i1);
      ANARIInstance i2 = s.instance(g2);
      anariSetParameter(dev, i2, "transform", ANARI_ARRAY1D, &xa2);
      anariSetParameter(dev, i2, "attribute1", ANARI_ARRAY1D, &ca2);
      anariCommitParameters(dev, i2);
      s.setInstances({i1, i2});
      b = s.render();
    }
    std::printf("two groups: scalar cov=%d array cov=%d mismatches=%zu\n",
        a.coverage(),
        b.coverage(),
        mismatches(a, b));
    check(a.coverage() > 200, "two-group scalar scene renders");
    check(mismatches(a, b) == 0,
        "two groups instanced via arrays are byte-identical to scalar instancing");
    check(b.dominant(L, H / 4) == 2 && b.dominant(R, 3 * H / 4) == 1,
        "second group's attribute1 VEC3 array and first group's colors land per instance");
  }

  // --- 6. Group lights under transform arrays ------------------------------
  {
    auto lightScene = [&](bool useArray) {
      Scene s(dev);
      float verts[12] = {-1.5f,
          -1.5f,
          0.f,
          1.5f,
          -1.5f,
          0.f,
          1.5f,
          1.5f,
          0.f,
          -1.5f,
          1.5f,
          0.f};
      ANARIArray1D pos = s.array(verts, ANARI_FLOAT32_VEC3, 4);
      ANARIGeometry g = s.keep(anariNewGeometry(dev, "quad"));
      anariSetParameter(dev, g, "vertex.position", ANARI_ARRAY1D, &pos);
      anariCommitParameters(dev, g);
      ANARIMaterial mat = s.keep(anariNewMaterial(dev, "matte"));
      float color[3] = {0.9f, 0.9f, 0.9f};
      anariSetParameter(dev, mat, "color", ANARI_FLOAT32_VEC3, color);
      anariCommitParameters(dev, mat);
      ANARISurface surf = s.keep(anariNewSurface(dev));
      anariSetParameter(dev, surf, "geometry", ANARI_GEOMETRY, &g);
      anariSetParameter(dev, surf, "material", ANARI_MATERIAL, &mat);
      anariCommitParameters(dev, surf);
      ANARIArray1D surfArray = s.array(&surf, ANARI_SURFACE, 1);
      anariSetParameter(dev, s.world, "surface", ANARI_ARRAY1D, &surfArray);

      ANARILight light = s.keep(anariNewLight(dev, "point"));
      float lpos[3] = {0.f, 0.f, 0.4f};
      float intensity = 0.4f; // below saturation so the comparison bites
      anariSetParameter(dev, light, "position", ANARI_FLOAT32_VEC3, lpos);
      anariSetParameter(dev, light, "intensity", ANARI_FLOAT32, &intensity);
      anariCommitParameters(dev, light);
      ANARIGroup group = s.keep(anariNewGroup(dev));
      ANARIArray1D lightArray = s.array(&light, ANARI_LIGHT, 1);
      anariSetParameter(dev, group, "light", ANARI_ARRAY1D, &lightArray);
      anariCommitParameters(dev, group);

      std::vector<ANARIInstance> insts;
      if (useArray) {
        std::vector<float> xf = mat4T(0.9f), m = mat4T(-0.9f);
        xf.insert(xf.end(), m.begin(), m.end());
        ANARIArray1D xa = s.array(xf.data(), ANARI_FLOAT32_MAT4, 2);
        ANARIInstance inst = s.instance(group);
        anariSetParameter(dev, inst, "transform", ANARI_ARRAY1D, &xa);
        anariCommitParameters(dev, inst);
        insts.push_back(inst);
      } else {
        for (float tx : {0.9f, -0.9f}) {
          ANARIInstance inst = s.instance(group);
          anariSetParameter(
              dev, inst, "transform", ANARI_FLOAT32_MAT4, mat4T(tx).data());
          anariCommitParameters(dev, inst);
          insts.push_back(inst);
        }
      }
      s.setInstances(insts);
      return s.render();
    };
    Frame a = lightScene(false), b = lightScene(true);
    auto lum = [](const Frame &f, uint32_t x, uint32_t y) {
      const uint8_t *p = f.px(x, y);
      return (p[0] + p[1] + p[2]) / 3;
    };
    std::printf(
        "lights: scalar L/R=%d/%d array L/R=%d/%d corner=%d mismatches=%zu\n",
        lum(a, L, MID),
        lum(a, R, MID),
        lum(b, L, MID),
        lum(b, R, MID),
        lum(b, 2, 2),
        mismatches(a, b));
    check(lum(b, L, MID) > 20 && lum(b, R, MID) > 20,
        "group light instanced via a transform array lights both sides");
    check(lum(b, 2, 2) < lum(b, L, MID) && lum(b, L, MID) < 250,
        "array-instanced point lights fall off toward the quad corners (unsaturated)");
    check(mismatches(a, b) == 0,
        "array-instanced group light is byte-identical to two scalar instances");
  }

  // --- 7. Reflection: instance parameter list + extensions -----------------
  {
    const ANARIParameter *params = (const ANARIParameter *)anariGetObjectInfo(
        dev, ANARI_INSTANCE, "transform", "parameter", ANARI_PARAMETER_LIST);
    check(params != nullptr, "instance parameter list is non-null");
    auto has = [&](const char *name, ANARIDataType type) {
      for (const ANARIParameter *p = params; p && p->name; ++p)
        if (std::strcmp(p->name, name) == 0 && p->type == type)
          return true;
      return false;
    };
    check(has("transform", ANARI_FLOAT32_MAT4)
            && has("transform", ANARI_FLOAT32_MAT3x4)
            && has("transform", ANARI_ARRAY1D),
        "instance.transform reflects MAT4, MAT3x4 and ARRAY1D forms");
    check(has("id", ANARI_UINT32) && has("id", ANARI_ARRAY1D),
        "instance.id reflects scalar and array forms");
    check(has("color", ANARI_FLOAT32) && has("color", ANARI_FLOAT32_VEC2)
            && has("color", ANARI_FLOAT32_VEC3)
            && has("color", ANARI_FLOAT32_VEC4) && has("color", ANARI_ARRAY1D)
            && has("attribute3", ANARI_ARRAY1D),
        "instance attributes reflect scalar/vec2/vec3/vec4 and array forms");

    const char *const *ext = (const char *const *)anariGetObjectInfo(
        dev, ANARI_DEVICE, "default", "extension", ANARI_STRING_LIST);
    bool hasXfm = false, hasArr = false;
    for (const char *const *e = ext; e && *e; ++e) {
      hasXfm |= std::strcmp(*e, "ANARI_KHR_INSTANCE_TRANSFORM") == 0;
      hasArr |= std::strcmp(*e, "ANARI_KHR_INSTANCE_TRANSFORM_ARRAY") == 0;
    }
    check(hasXfm && hasArr,
        "KHR_INSTANCE_TRANSFORM and KHR_INSTANCE_TRANSFORM_ARRAY are advertised");
  }

  anariRelease(dev, dev);
  anariUnloadLibrary(lib);
  std::printf("\n%s (%d issues)\n",
      g_errors == 0 ? "ALL CHECKS PASSED" : "CHECKS FAILED",
      g_errors);
  return g_errors == 0 ? 0 : 1;
}
