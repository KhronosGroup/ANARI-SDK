// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0
//
// Proves channel.normal carries a world-space, float-precision shading normal
// (ANARI_KHR_FRAME_CHANNEL_NORMAL). Each scene is one quad, most with an
// app-authored vertex.normal (the device never generates normals), and the
// center pixel's normal is compared against the expected world direction:
//   - +z quad, camera on +z: (0,0,1); pixelType FLOAT32_VEC3, frame-sized.
//   - tilted authored normal (-0.6,0,0.8): negative components survive and the
//     value is exact to float precision — a UNorm8 target would clamp x to 0.
//   - camera moved to +x looking -x at a +x quad: still (1,0,0) in WORLD space
//     (an eye-space result would read (0,0,1)).
//   - +z quad rotated by an INSTANCE transform (+90° about y) with the camera
//   on
//     +x: (1,0,0) — instance rotation reaches the normal independently of the
//     camera rotation.
//   - a quad with no authored normal: reports the delegate's own geometric
//     normal (dominant |z|); the device adds nothing.
//   - the +z quad seen from behind (camera on -z): a unit normal along ±z whose
//     sign is the delegate's — Storm flips Neye toward the viewer on back
//     faces (reads (0,0,-1)); HdEmbree reports the authored (0,0,1).
//   - the corner (background) pixel is (0,0,0) on every delegate.
//   - the delegate advertises ANARI_KHR_FRAME_CHANNEL_NORMAL.

#include <anari/anari.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <set>
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
  const char *tag = severity == ANARI_SEVERITY_FATAL_ERROR ? "FATAL"
      : severity == ANARI_SEVERITY_ERROR                   ? "ERROR"
      : severity == ANARI_SEVERITY_WARNING                 ? "WARN"
                                                           : "INFO";
  if (severity <= ANARI_SEVERITY_WARNING)
    std::printf("[%s] %s\n", tag, message);
  if (severity <= ANARI_SEVERITY_ERROR)
    ++g_errors;
}

static void check(bool ok, const char *what)
{
  std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok)
    ++g_errors;
}

struct Vec3
{
  float x, y, z;
};

// Author one quad: `verts` (4 corners) and optional per-vertex `normals` (4
// vectors). Both arrays are shared with the device, so the caller owns them
// for the life of the render.
static ANARISurface makeQuad(
    ANARIDevice dev, const float verts[12], const float *normals)
{
  ANARIArray1D pos =
      anariNewArray1D(dev, verts, nullptr, nullptr, ANARI_FLOAT32_VEC3, 4);
  ANARIGeometry g = anariNewGeometry(dev, "quad");
  anariSetParameter(dev, g, "vertex.position", ANARI_ARRAY1D, &pos);
  ANARIArray1D norm = nullptr;
  if (normals) {
    norm =
        anariNewArray1D(dev, normals, nullptr, nullptr, ANARI_FLOAT32_VEC3, 4);
    anariSetParameter(dev, g, "vertex.normal", ANARI_ARRAY1D, &norm);
  }
  anariCommitParameters(dev, g);

  ANARIMaterial mat = anariNewMaterial(dev, "matte");
  const float rgb[3] = {0.8f, 0.8f, 0.8f};
  anariSetParameter(dev, mat, "color", ANARI_FLOAT32_VEC3, rgb);
  anariCommitParameters(dev, mat);

  ANARISurface surf = anariNewSurface(dev);
  anariSetParameter(dev, surf, "geometry", ANARI_GEOMETRY, &g);
  anariSetParameter(dev, surf, "material", ANARI_MATERIAL, &mat);
  anariCommitParameters(dev, surf);

  anariRelease(dev, pos);
  if (norm)
    anariRelease(dev, norm);
  anariRelease(dev, g);
  anariRelease(dev, mat);
  return surf;
}

struct Scene
{
  const char *name;
  const float *verts; // 4 corners, caller-owned
  bool authorNormal;
  Vec3 normal; // authored vertex.normal (when authorNormal)
  const float *instance; // optional column-major 4x4 instance transform
  float eye[3];
  float dir[3];
  Vec3 expected; // expected world-space normal at the image center
  bool exact; // assert within tolerance (false: dominant |z| only)
};

// What one render of the normal channel yields: the center pixel, the corner
// (background) pixel, and the mapped format/dimensions.
struct NormalReadback
{
  bool mapped{false};
  Vec3 center{0, 0, 0};
  Vec3 corner{0, 0, 0};
  ANARIDataType type{ANARI_UNKNOWN};
  uint32_t w{0}, h{0};
};

static NormalReadback renderScene(ANARIDevice dev, const Scene &s)
{
  NormalReadback r;
  float normals[12];
  for (int i = 0; i < 4; ++i) {
    normals[i * 3 + 0] = s.normal.x;
    normals[i * 3 + 1] = s.normal.y;
    normals[i * 3 + 2] = s.normal.z;
  }
  ANARISurface surf =
      makeQuad(dev, s.verts, s.authorNormal ? normals : nullptr);
  ANARIWorld world = anariNewWorld(dev);
  ANARIArray1D surfArray =
      anariNewArray1D(dev, &surf, nullptr, nullptr, ANARI_SURFACE, 1);
  ANARIGroup group = nullptr;
  ANARIInstance inst = nullptr;
  ANARIArray1D instArray = nullptr;
  if (s.instance) {
    group = anariNewGroup(dev);
    anariSetParameter(dev, group, "surface", ANARI_ARRAY1D, &surfArray);
    anariCommitParameters(dev, group);
    inst = anariNewInstance(dev, "transform");
    anariSetParameter(dev, inst, "group", ANARI_GROUP, &group);
    anariSetParameter(dev, inst, "transform", ANARI_FLOAT32_MAT4, s.instance);
    anariCommitParameters(dev, inst);
    instArray =
        anariNewArray1D(dev, &inst, nullptr, nullptr, ANARI_INSTANCE, 1);
    anariSetParameter(dev, world, "instance", ANARI_ARRAY1D, &instArray);
  } else {
    anariSetParameter(dev, world, "surface", ANARI_ARRAY1D, &surfArray);
  }
  anariCommitParameters(dev, world);

  ANARICamera cam = anariNewCamera(dev, "perspective");
  const float up[3] = {0.f, 1.f, 0.f};
  const float fovy = 0.6f;
  anariSetParameter(dev, cam, "position", ANARI_FLOAT32_VEC3, s.eye);
  anariSetParameter(dev, cam, "direction", ANARI_FLOAT32_VEC3, s.dir);
  anariSetParameter(dev, cam, "up", ANARI_FLOAT32_VEC3, up);
  anariSetParameter(dev, cam, "fovy", ANARI_FLOAT32, &fovy);
  anariCommitParameters(dev, cam);

  ANARIRenderer ren = anariNewRenderer(dev, "default");
  anariCommitParameters(dev, ren);

  ANARIFrame frame = anariNewFrame(dev);
  const uint32_t size[2] = {128, 96};
  ANARIDataType colorFmt = ANARI_UFIXED8_RGBA_SRGB;
  ANARIDataType f32v3 = ANARI_FLOAT32_VEC3;
  anariSetParameter(dev, frame, "size", ANARI_UINT32_VEC2, size);
  anariSetParameter(dev, frame, "channel.color", ANARI_DATA_TYPE, &colorFmt);
  anariSetParameter(dev, frame, "channel.normal", ANARI_DATA_TYPE, &f32v3);
  anariSetParameter(dev, frame, "world", ANARI_WORLD, &world);
  anariSetParameter(dev, frame, "camera", ANARI_CAMERA, &cam);
  anariSetParameter(dev, frame, "renderer", ANARI_RENDERER, &ren);
  anariCommitParameters(dev, frame);

  anariRenderFrame(dev, frame);
  anariFrameReady(dev, frame, ANARI_WAIT);

  const float *nrm = static_cast<const float *>(
      anariMapFrame(dev, frame, "channel.normal", &r.w, &r.h, &r.type));
  r.mapped = nrm && r.w == size[0] && r.h == size[1];
  if (r.mapped) {
    const size_t ic = size_t(r.h / 2) * r.w + r.w / 2;
    r.center = {nrm[ic * 3], nrm[ic * 3 + 1], nrm[ic * 3 + 2]};
    r.corner = {nrm[0], nrm[1], nrm[2]};
  }
  anariUnmapFrame(dev, frame, "channel.normal");

  anariRelease(dev, frame);
  anariRelease(dev, ren);
  anariRelease(dev, cam);
  anariRelease(dev, world);
  if (instArray)
    anariRelease(dev, instArray);
  if (inst)
    anariRelease(dev, inst);
  if (group)
    anariRelease(dev, group);
  anariRelease(dev, surfArray);
  anariRelease(dev, surf);
  return r;
}

static std::set<std::string> toSet(const char *const *list)
{
  std::set<std::string> s;
  for (; list && *list; ++list)
    s.insert(*list);
  return s;
}

int main(int argc, char **argv)
{
  const char *subtype = argc > 1 ? argv[1] : "storm";

  ANARILibrary lib = anariLoadLibrary("hydra", statusFunc, nullptr);
  if (!lib) {
    std::printf("FAIL: could not load anari library 'hydra'\n");
    return 2;
  }
  const std::set<std::string> ext =
      toSet(anariGetDeviceExtensions(lib, subtype));
  check(ext.count("ANARI_KHR_FRAME_CHANNEL_NORMAL") > 0,
      "delegate advertises ANARI_KHR_FRAME_CHANNEL_NORMAL");

  ANARIDevice dev = anariNewDevice(lib, subtype);
  if (!dev) {
    std::printf("FAIL: could not create device '%s'\n", subtype);
    return 2;
  }

  // Quad in the xy-plane at z=0 (faces +z); quad in the yz-plane at x=0 (+x).
  const float quadZ[12] = {
      -0.8f, -0.8f, 0.f, 0.8f, -0.8f, 0.f, 0.8f, 0.8f, 0.f, -0.8f, 0.8f, 0.f};
  const float quadX[12] = {
      0.f, -0.8f, 0.8f, 0.f, -0.8f, -0.8f, 0.f, 0.8f, -0.8f, 0.f, 0.8f, 0.8f};
  // +90° about y: (0,0,1) -> (1,0,0). Column-major: images of the basis.
  const float rotY90[16] = {0.f,
      0.f,
      -1.f,
      0.f,
      0.f,
      1.f,
      0.f,
      0.f,
      1.f,
      0.f,
      0.f,
      0.f,
      0.f,
      0.f,
      0.f,
      1.f};

  const float tilt = 0.6f, tiltZ = 0.8f; // unit length: 0.36 + 0.64
  const std::vector<Scene> scenes = {
      {"front-facing +z quad, camera on +z",
          quadZ,
          true,
          {0.f, 0.f, 1.f},
          nullptr,
          {0.f, 0.f, 3.f},
          {0.f, 0.f, -1.f},
          {0.f, 0.f, 1.f},
          true},
      {"tilted authored normal (-0.6,0,0.8)",
          quadZ,
          true,
          {-tilt, 0.f, tiltZ},
          nullptr,
          {0.f, 0.f, 3.f},
          {0.f, 0.f, -1.f},
          {-tilt, 0.f, tiltZ},
          true},
      {"camera on +x looking -x at a +x quad",
          quadX,
          true,
          {1.f, 0.f, 0.f},
          nullptr,
          {3.f, 0.f, 0.f},
          {-1.f, 0.f, 0.f},
          {1.f, 0.f, 0.f},
          true},
      {"+z quad rotated +90deg about y by an instance, camera on +x",
          quadZ,
          true,
          {0.f, 0.f, 1.f},
          rotY90,
          {3.f, 0.f, 0.f},
          {-1.f, 0.f, 0.f},
          {1.f, 0.f, 0.f},
          true},
      {"quad without authored normals (delegate geometric normal)",
          quadZ,
          false,
          {0.f, 0.f, 0.f},
          nullptr,
          {0.f, 0.f, 3.f},
          {0.f, 0.f, -1.f},
          {0.f, 0.f, 1.f},
          false},
      {"+z quad seen from behind (camera on -z): delegate-owned sign",
          quadZ,
          true,
          {0.f, 0.f, 1.f},
          nullptr,
          {0.f, 0.f, -3.f},
          {0.f, 0.f, 1.f},
          {0.f, 0.f, 1.f},
          false},
  };

  for (const Scene &s : scenes) {
    std::printf("scene: %s\n", s.name);
    const NormalReadback r = renderScene(dev, s);
    check(r.mapped, "normal channel maps at the frame size");
    check(r.type == ANARI_FLOAT32_VEC3, "normal pixelType is FLOAT32_VEC3");
    if (!r.mapped)
      continue;
    const Vec3 c = r.center, bg = r.corner;
    std::printf(
        "  center = (%.5f %.5f %.5f)  expected (%.3f %.3f %.3f)  "
        "corner = (%.3f %.3f %.3f)\n",
        c.x,
        c.y,
        c.z,
        s.expected.x,
        s.expected.y,
        s.expected.z,
        bg.x,
        bg.y,
        bg.z);
    const float len = std::sqrt(c.x * c.x + c.y * c.y + c.z * c.z);
    check(std::fabs(len - 1.f) < 1e-3f, "center normal is unit length");
    if (s.exact) {
      const float err = std::fabs(c.x - s.expected.x)
          + std::fabs(c.y - s.expected.y) + std::fabs(c.z - s.expected.z);
      check(err < 2e-3f, "center normal matches the expected world direction");
    } else {
      check(
          std::fabs(c.z) >= std::fabs(c.x) && std::fabs(c.z) >= std::fabs(c.y),
          "geometric normal has dominant |z|");
    }
    check(bg.x == 0.f && bg.y == 0.f && bg.z == 0.f,
        "background pixel reads (0,0,0)");
  }

  anariRelease(dev, dev);
  anariUnloadLibrary(lib);
  std::printf("\n%s (%d issues)\n",
      g_errors == 0 ? "ALL CHECKS PASSED" : "CHECKS FAILED",
      g_errors);
  return g_errors == 0 ? 0 : 1;
}
