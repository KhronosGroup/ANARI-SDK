// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0
//
// Exercises attribute-bound material inputs (SPEC §6.1). A matte material's
// `color` is set to the ANARI attribute STRING "color" instead of a constant or
// a sampler; the device authors a UsdPrimvarReader_float3 reading the geometry's
// per-vertex `color` primvar and connects it to the UsdPreviewSurface
// diffuseColor. The triangle carries a distinct primary at each corner
// (red/green/blue), so the spec-guaranteed observable is a COLOR GRADIENT across
// the face: near each vertex that vertex's channel dominates. A constant color
// could not produce three differently-dominant regions, so the gradient proves
// the primvar reader is bound and interpolating. Confirmed on Storm.

#include <anari/anari.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>
#include <string>

static int g_errors = 0;

static void statusFunc(const void *, ANARIDevice, ANARIObject, ANARIDataType,
    ANARIStatusSeverity severity, ANARIStatusCode, const char *message)
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

struct Img
{
  uint32_t w = 0, h = 0;
  std::vector<uint8_t> rgba;
  const uint8_t *at(int x, int y) const { return &rgba[(size_t(y) * w + x) * 4]; }
};

// Average color over an axis-aligned window, skipping the dark clear color.
static void windowAvg(const Img &im, int cx, int cy, int r, double out[3])
{
  double s[3] = {0, 0, 0};
  long n = 0;
  for (int y = cy - r; y <= cy + r; ++y)
    for (int x = cx - r; x <= cx + r; ++x) {
      if (x < 0 || y < 0 || x >= int(im.w) || y >= int(im.h))
        continue;
      const uint8_t *p = im.at(x, y);
      if (p[0] > 20 || p[1] > 20 || p[2] > 20) {
        s[0] += p[0];
        s[1] += p[1];
        s[2] += p[2];
        ++n;
      }
    }
  for (int k = 0; k < 3; ++k)
    out[k] = n ? s[k] / n : 0;
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

  // A single triangle with a distinct primary at each vertex.
  //   v0 top       -> red
  //   v1 bottom-left  -> green
  //   v2 bottom-right -> blue
  float verts[9] = {
      0.0f, 0.8f, 0.f, -0.8f, -0.6f, 0.f, 0.8f, -0.6f, 0.f};
  float vcolor[9] = {
      1.f, 0.f, 0.f, /* red */ 0.f, 1.f, 0.f, /* green */ 0.f, 0.f, 1.f /* blue */};
  ANARIArray1D pos =
      anariNewArray1D(dev, verts, nullptr, nullptr, ANARI_FLOAT32_VEC3, 3);
  ANARIArray1D col =
      anariNewArray1D(dev, vcolor, nullptr, nullptr, ANARI_FLOAT32_VEC3, 3);
  ANARIGeometry g = anariNewGeometry(dev, "triangle");
  anariSetParameter(dev, g, "vertex.position", ANARI_ARRAY1D, &pos);
  anariSetParameter(dev, g, "vertex.color", ANARI_ARRAY1D, &col);
  anariCommitParameters(dev, g);

  // matte with color bound to the ANARI attribute "color" (a STRING, SPEC §6.1).
  ANARIMaterial mat = anariNewMaterial(dev, "matte");
  const char *attr = "color";
  anariSetParameter(dev, mat, "color", ANARI_STRING, attr);
  anariCommitParameters(dev, mat);

  ANARISurface surf = anariNewSurface(dev);
  anariSetParameter(dev, surf, "geometry", ANARI_GEOMETRY, &g);
  anariSetParameter(dev, surf, "material", ANARI_MATERIAL, &mat);
  anariCommitParameters(dev, surf);

  ANARIWorld world = anariNewWorld(dev);
  ANARIArray1D surfArray =
      anariNewArray1D(dev, &surf, nullptr, nullptr, ANARI_SURFACE, 1);
  anariSetParameter(dev, world, "surface", ANARI_ARRAY1D, &surfArray);
  anariCommitParameters(dev, world);

  ANARICamera cam = anariNewCamera(dev, "perspective");
  const float eye[3] = {0.f, 0.f, 3.f};
  const float dir[3] = {0.f, 0.f, -1.f};
  const float up[3] = {0.f, 1.f, 0.f};
  const float fovy = 0.6f;
  anariSetParameter(dev, cam, "position", ANARI_FLOAT32_VEC3, eye);
  anariSetParameter(dev, cam, "direction", ANARI_FLOAT32_VEC3, dir);
  anariSetParameter(dev, cam, "up", ANARI_FLOAT32_VEC3, up);
  anariSetParameter(dev, cam, "fovy", ANARI_FLOAT32, &fovy);
  anariCommitParameters(dev, cam);

  ANARIRenderer ren = anariNewRenderer(dev, "default");
  anariCommitParameters(dev, ren);

  ANARIFrame frame = anariNewFrame(dev);
  uint32_t size[2] = {256, 256};
  ANARIDataType colorFmt = ANARI_UFIXED8_RGBA_SRGB;
  anariSetParameter(dev, frame, "size", ANARI_UINT32_VEC2, size);
  anariSetParameter(dev, frame, "channel.color", ANARI_DATA_TYPE, &colorFmt);
  anariSetParameter(dev, frame, "world", ANARI_WORLD, &world);
  anariSetParameter(dev, frame, "camera", ANARI_CAMERA, &cam);
  anariSetParameter(dev, frame, "renderer", ANARI_RENDERER, &ren);
  anariCommitParameters(dev, frame);

  anariRenderFrame(dev, frame);
  anariFrameReady(dev, frame, ANARI_WAIT);

  Img im;
  ANARIDataType pt = ANARI_UNKNOWN;
  const uint8_t *pixels = (const uint8_t *)anariMapFrame(
      dev, frame, "channel.color", &im.w, &im.h, &pt);
  if (pixels)
    im.rgba.assign(pixels, pixels + size_t(im.w) * im.h * 4);
  anariUnmapFrame(dev, frame, "channel.color");

  // Project a world point (z=0 plane) to screen. Square frame, vertical fovy,
  // camera at (0,0,3) looking down -z: half-extent = dist*tan(fovy/2). The
  // mapped frame stores rows bottom-to-top for this delegate (world +y -> larger
  // row), so screen row grows with world +y (verified empirically).
  const float halfExt = 3.f * std::tan(fovy * 0.5f);
  auto project = [&](float wx, float wy, int &sx, int &sy) {
    sx = int((wx / halfExt * 0.5f + 0.5f) * im.w);
    sy = int((wy / halfExt * 0.5f + 0.5f) * im.h);
  };

  // Sample a point 70% toward each vertex from the centroid: still inside the
  // triangle (so covered), but that vertex's barycentric weight (~0.7)
  // dominates the interpolated color.
  const float cx = 0.f, cy = (0.8f - 0.6f - 0.6f) / 3.f;   // centroid y
  auto near = [&](float vx, float vy, double out[3]) {
    const float px = 0.7f * vx + 0.3f * cx;
    const float py = 0.7f * vy + 0.3f * cy;
    int sx, sy;
    project(px, py, sx, sy);
    windowAvg(im, sx, sy, 4, out);
  };

  // Confirm the triangle is visible.
  int x0 = im.w, y0 = im.h, x1 = 0, y1 = 0;
  for (uint32_t y = 0; y < im.h; ++y)
    for (uint32_t x = 0; x < im.w; ++x) {
      const uint8_t *p = im.at(x, y);
      if (p[0] > 20 || p[1] > 20 || p[2] > 20) {
        x0 = std::min<int>(x0, x);
        y0 = std::min<int>(y0, y);
        x1 = std::max<int>(x1, x);
        y1 = std::max<int>(y1, y);
      }
    }
  check(x1 > x0 && y1 > y0, "attribute-shaded triangle is visible");

  double red[3], grn[3], blu[3];
  near(0.0f, 0.8f, red);     // top vertex
  near(-0.8f, -0.6f, grn);   // bottom-left
  near(0.8f, -0.6f, blu);    // bottom-right

  std::printf("  near-vertex  top   =%.0f %.0f %.0f\n", red[0], red[1], red[2]);
  std::printf("               left  =%.0f %.0f %.0f\n", grn[0], grn[1], grn[2]);
  std::printf("               right =%.0f %.0f %.0f\n", blu[0], blu[1], blu[2]);

  // Each corner's own channel dominates its region -> the per-vertex `color`
  // primvar reaches diffuseColor and interpolates across the face. A constant
  // color could not make three regions each favor a different channel.
  check(red[0] > red[1] + 30 && red[0] > red[2] + 30,
      "near top vertex red dominates");
  check(grn[1] > grn[0] + 30 && grn[1] > grn[2] + 30,
      "near bottom-left vertex green dominates");
  check(blu[2] > blu[0] + 30 && blu[2] > blu[1] + 30,
      "near bottom-right vertex blue dominates");

  // Removing the vertex array reveals the geometry's constant fallback.
  const float fallback[4] = {0.f, 1.f, 0.f, 1.f};
  anariUnsetParameter(dev, g, "vertex.color");
  anariSetParameter(dev, g, "color", ANARI_FLOAT32_VEC4, fallback);
  anariCommitParameters(dev, g);
  anariRenderFrame(dev, frame);
  anariFrameReady(dev, frame, ANARI_WAIT);
  pixels = (const uint8_t *)anariMapFrame(
      dev, frame, "channel.color", &im.w, &im.h, &pt);
  if (pixels)
    im.rgba.assign(pixels, pixels + size_t(im.w) * im.h * 4);
  anariUnmapFrame(dev, frame, "channel.color");
  double constant[3];
  windowAvg(im, int(im.w / 2), int(im.h / 2), 4, constant);
  check(constant[1] > constant[0] + 30 && constant[1] > constant[2] + 30,
      "constant geometry color survives vertex parameter removal");

  const float primitiveBlue[3] = {0.f, 0.f, 1.f};
  ANARIArray1D primitiveColor = anariNewArray1D(
      dev, primitiveBlue, nullptr, nullptr, ANARI_FLOAT32_VEC3, 1);
  anariSetParameter(dev, g, "primitive.color", ANARI_ARRAY1D, &primitiveColor);
  anariCommitParameters(dev, g);
  anariRenderFrame(dev, frame);
  anariFrameReady(dev, frame, ANARI_WAIT);
  pixels = (const uint8_t *)anariMapFrame(
      dev, frame, "channel.color", &im.w, &im.h, &pt);
  if (pixels)
    im.rgba.assign(pixels, pixels + size_t(im.w) * im.h * 4);
  anariUnmapFrame(dev, frame, "channel.color");
  windowAvg(im, int(im.w / 2), int(im.h / 2), 4, constant);
  check(constant[2] > constant[0] + 30 && constant[2] > constant[1] + 30,
      "primitive geometry color takes precedence over constant");
  anariRelease(dev, primitiveColor);


  // Exercise each material-readable channel on indexed meshes and generated
  // tubes. Position order is deliberately opposite primitive order.
  for (const char *shape : {"triangle", "cone", "cylinder"}) {
    const auto *params = (const ANARIParameter *)anariGetObjectInfo(
        dev, ANARI_GEOMETRY, shape, "parameter", ANARI_PARAMETER_LIST);
    bool hasPrimitive = false, hasConstant = false;
    for (const auto *p = params; p && p->name; ++p) {
      hasPrimitive |= std::string(p->name) == "primitive.attribute3" && p->type == ANARI_ARRAY1D;
      hasConstant |= std::string(p->name) == "attribute3" && p->type == ANARI_FLOAT32_VEC4;
    }
    check(hasPrimitive && hasConstant, "geometry query exposes primitive and constant attributes");
    ANARIGeometry geometry = anariNewGeometry(dev, shape);
    const bool mesh = std::string(shape) == "triangle";
    const float positions[] = {
        .45f, .65f, 0, .15f, -.65f, 0, .75f, -.65f, 0,
        -.45f, .65f, 0, -.75f, -.65f, 0, -.15f, -.65f, 0};
    const uint32_t triangles[] = {3,4,5,0,1,2};
    const float endpoints[] = {.45f,-.65f,0, .45f,.65f,0,
        -.45f,-.65f,0, -.45f,.65f,0};
    const uint32_t segments[] = {2,3,0,1};
    auto positionsArray = anariNewArray1D(dev, mesh ? positions : endpoints,
        nullptr, nullptr, ANARI_FLOAT32_VEC3, mesh ? 6 : 4);
    auto indicesArray = anariNewArray1D(dev, mesh ? triangles : segments,
        nullptr, nullptr, mesh ? ANARI_UINT32_VEC3 : ANARI_UINT32_VEC2, 2);
    anariSetParameter(dev, geometry, "vertex.position", ANARI_ARRAY1D, &positionsArray);
    anariSetParameter(dev, geometry, "primitive.index", ANARI_ARRAY1D, &indicesArray);
    const float radius = .18f;
    anariSetParameter(dev, geometry, "radius", ANARI_FLOAT32, &radius);
    anariSetParameter(dev, geometry, "caps", ANARI_STRING, "both");
    anariSetParameter(dev, surf, "geometry", ANARI_GEOMETRY, &geometry);
    anariCommitParameters(dev, surf);
    for (const char *attribute : {"color", "attribute0", "attribute1", "attribute2", "attribute3"}) {
      std::printf("  %s / %s\n", shape, attribute);
      anariSetParameter(dev, mat, "color", ANARI_STRING, attribute);
      anariCommitParameters(dev, mat);
      const std::string vertexName = std::string("vertex.") + attribute;
      const std::string primitiveName = std::string("primitive.") + attribute;
      const std::string cornerName = std::string("faceVarying.") + attribute;
      const float green[] = {0,1,0,1};
      const float primitiveValues[] = {0,0,1, 1,0,0};
      const float vertexValues[] = {0,0,1, 0,0,1, 0,0,1,
                                   1,0,0, 1,0,0, 1,0,0};
      const float tubeValues[] = {0,0,1, 0,1,0, 1,0,0, 0,1,0};
      const float corners[] = {1,1,0,1,1,0,1,1,0, 1,0,0,1,0,0,1,0,0};
      auto primitive = anariNewArray1D(dev, primitiveValues, nullptr, nullptr, ANARI_FLOAT32_VEC3, 2);
      auto vertex = anariNewArray1D(dev, mesh ? vertexValues : tubeValues,
          nullptr, nullptr, ANARI_FLOAT32_VEC3, mesh ? 6 : 4);
      auto corner = anariNewArray1D(dev, corners, nullptr, nullptr, ANARI_FLOAT32_VEC3, 6);
      auto sample = [&](float x, float y, int channel, const char *message,
                        bool commit = true) {
        if (commit)
          anariCommitParameters(dev, geometry);
        anariRenderFrame(dev, frame);
        anariFrameReady(dev, frame, ANARI_WAIT);
        pixels = (const uint8_t *)anariMapFrame(dev, frame, "channel.color", &im.w, &im.h, &pt);
        if (pixels)
          im.rgba.assign(pixels, pixels + size_t(im.w) * im.h * 4);
        anariUnmapFrame(dev, frame, "channel.color");
        int sx, sy;
        project(x, y, sx, sy);
        double rgb[3];
        windowAvg(im, sx, sy, 2, rgb);
        check(channel == 3
                ? rgb[0] > rgb[2] + 25 && rgb[1] > rgb[2] + 25
                : rgb[channel] > rgb[(channel+1)%3] + 25
                    && rgb[channel] > rgb[(channel+2)%3] + 25, message);
      };
      anariSetParameter(dev, geometry, attribute, ANARI_FLOAT32_VEC4, green);
      sample(-.45f, -.25f, 1, "constant attribute");
      anariSetParameter(dev, geometry, primitiveName.c_str(), ANARI_ARRAY1D, &primitive);
      sample(-.45f, -.25f, 2, "primitive overrides constant, indexed left primitive");
      sample(.45f, -.25f, 0, "indexed right primitive");
      anariSetParameter(dev, geometry, vertexName.c_str(), ANARI_ARRAY1D, &vertex);
      sample(-.45f, -.4f, 0, "vertex source mapping preserves left endpoint");
      if (!mesh)
        sample(-.45f, .4f, 1, "tube interpolates endpoint attributes");
      if (mesh) {
        anariSetParameter(dev, geometry, cornerName.c_str(), ANARI_ARRAY1D, &corner);
        sample(-.45f, -.25f, 3, "face-varying overrides vertex");
        anariUnsetParameter(dev, geometry, cornerName.c_str());
      }
      anariCommitParameters(dev, geometry);
      sample(-.45f, -.4f, 0, "vertex baseline before array-only edit", false);
      float *editedVertex = (float *)anariMapArray(dev, vertex);
      for (int i = 0; i < (mesh ? 6 : 4); ++i) {
        editedVertex[3*i] = 0;
        editedVertex[3*i+1] = 0;
        editedVertex[3*i+2] = 1;
      }
      anariUnmapArray(dev, vertex);
      sample(-.45f, -.4f, 2, "live vertex edits refresh generated attributes", false);
      anariUnsetParameter(dev, geometry, vertexName.c_str());
      anariCommitParameters(dev, geometry);
      sample(-.45f, -.25f, 2, "primitive baseline before array-only edit", false);
      float *mapped = (float *)anariMapArray(dev, primitive);
      mapped[0]=0; mapped[1]=1; mapped[2]=0;
      anariUnmapArray(dev, primitive);
      sample(-.45f, -.25f, 1, "live primitive edits refresh copied attributes", false);
      anariUnsetParameter(dev, geometry, primitiveName.c_str());
      sample(.45f, -.25f, 1, "primitive removal reveals constant");
      anariUnsetParameter(dev, geometry, attribute);
      anariRelease(dev, corner);
      anariRelease(dev, vertex);
      anariRelease(dev, primitive);
    }
    anariRelease(dev, indicesArray);
    anariRelease(dev, positionsArray);
    anariRelease(dev, geometry);
  }

  // Two independently bound materials consume the same geometry.
  ANARIMaterial secondMaterial = anariNewMaterial(dev, "physicallyBased");
  anariSetParameter(dev, secondMaterial, "baseColor", ANARI_STRING, "attribute1");
  const float noMetal = 0.f;
  anariSetParameter(dev, secondMaterial, "metallic", ANARI_FLOAT32, &noMetal);
  anariSetParameter(dev, secondMaterial, "opacity", ANARI_STRING, "attribute0");
  anariSetParameter(dev, secondMaterial, "alphaMode", ANARI_STRING, "mask");
  anariCommitParameters(dev, secondMaterial);
  ANARISurface secondSurface = anariNewSurface(dev);
  anariSetParameter(dev, secondSurface, "geometry", ANARI_GEOMETRY, &g);
  anariSetParameter(dev, secondSurface, "material", ANARI_MATERIAL, &secondMaterial);
  anariCommitParameters(dev, secondSurface);
  const float sharedRed[4] = {1,0,0,1};
  const float sharedGreen[4] = {0,1,0,1};
  anariSetParameter(dev, g, "attribute0", ANARI_FLOAT32_VEC4, sharedRed);
  anariSetParameter(dev, g, "attribute1", ANARI_FLOAT32_VEC4, sharedGreen);
  anariCommitParameters(dev, g);
  anariSetParameter(dev, surf, "geometry", ANARI_GEOMETRY, &g);
  anariCommitParameters(dev, surf);
  anariSetParameter(dev, mat, "color", ANARI_STRING, "attribute0");
  anariCommitParameters(dev, mat);
  ANARIWorld secondWorld = anariNewWorld(dev);
  auto secondSurfaces = anariNewArray1D(dev, &secondSurface, nullptr, nullptr, ANARI_SURFACE, 1);
  anariSetParameter(dev, secondWorld, "surface", ANARI_ARRAY1D, &secondSurfaces);
  anariCommitParameters(dev, secondWorld);
  for (int i : {0,1,0}) {
    const ANARIWorld selected = i ? secondWorld : world;
    anariSetParameter(dev, frame, "world", ANARI_WORLD, &selected);
    anariCommitParameters(dev, frame);
    anariRenderFrame(dev, frame);
    anariFrameReady(dev, frame, ANARI_WAIT);
    pixels = (const uint8_t *)anariMapFrame(dev, frame, "channel.color", &im.w, &im.h, &pt);
    if (pixels)
      im.rgba.assign(pixels, pixels + size_t(im.w)*im.h*4);
    anariUnmapFrame(dev, frame, "channel.color");
    windowAvg(im, int(im.w/2), int(im.h/2), 4, constant);
    check(constant[i] > constant[1-i] + 30 && constant[i] > constant[2] + 30,
        "shared geometry preserves independent material attribute bindings");
  }
  anariSetParameter(dev, g, "attribute0", ANARI_FLOAT32_VEC4, sharedGreen);
  anariCommitParameters(dev, g);
  anariSetParameter(dev, frame, "world", ANARI_WORLD, &secondWorld);
  anariCommitParameters(dev, frame);
  anariRenderFrame(dev, frame);
  anariFrameReady(dev, frame, ANARI_WAIT);
  pixels = (const uint8_t *)anariMapFrame(dev, frame, "channel.color", &im.w, &im.h, &pt);
  if (pixels)
    im.rgba.assign(pixels, pixels + size_t(im.w)*im.h*4);
  anariUnmapFrame(dev, frame, "channel.color");
  windowAvg(im, int(im.w/2), int(im.h/2), 4, constant);
  check(constant[0] + constant[1] + constant[2] < 30,
      "shared float4 attribute supplies x to a scalar material reader");
  anariRelease(dev, secondSurfaces);
  anariRelease(dev, secondWorld);
  anariRelease(dev, secondSurface);
  anariRelease(dev, secondMaterial);

  anariRelease(dev, frame);
  anariRelease(dev, ren);
  anariRelease(dev, cam);
  anariRelease(dev, surfArray);
  anariRelease(dev, world);
  anariRelease(dev, surf);
  anariRelease(dev, g);
  anariRelease(dev, col);
  anariRelease(dev, pos);
  anariRelease(dev, mat);

  anariRelease(dev, dev);
  anariUnloadLibrary(lib);
  std::printf("\n%s (%d issues)\n",
      g_errors == 0 ? "ALL CHECKS PASSED" : "CHECKS FAILED", g_errors);
  return g_errors == 0 ? 0 : 1;
}
