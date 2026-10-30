// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// KHR_CAMERA_DEPTH_OF_FIELD: renders a square whose edges lie on pixel
// boundaries through perspective and orthographic cameras and reads, from the
// albedo channel, the fraction of each pixel's samples that hit it. Through a
// pinhole, or a lens focused on the square, every pixel is all in or all out;
// a lens focused in front of the square blurs its edges over several pixels.
// Either way the square's total coverage stays its area. The 'preview'
// renderer doesn't sample the lens, so it renders a defocused camera sharp.

// anari
#define ANARI_LINALG_DEFINITIONS
#include <anari/anari_cpp/ext/linalg.h>
#include <anari/anari_cpp.hpp>
// std
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

using namespace anari::math;

// The cameras sit at z = 2 looking down -z and see a height of 2 at z = 0,
// 0.05 per pixel: the square's edges, +-0.4, fall on pixel boundaries
constexpr uint2 IMAGE_SIZE = {40, 40};
constexpr float HALF_WIDTH = 0.4f;
constexpr float VIEW_HEIGHT = 2.f;
constexpr float CAMERA_DISTANCE = 2.f;
// the square in pixels
constexpr float SQUARE_AREA = 16.f * 16.f;

// Whether a pixel is partly covered, beyond what rounding leaves
constexpr float PARTIAL = 0.02f;

int g_errors = 0;

void statusFunc(const void *,
    ANARIDevice,
    ANARIObject,
    ANARIDataType,
    ANARIStatusSeverity severity,
    ANARIStatusCode,
    const char *message)
{
  if (severity <= ANARI_SEVERITY_WARNING) {
    std::fprintf(stderr, "[ANARI] %s\n", message);
    g_errors++;
  }
}

bool report(const char *name, bool ok)
{
  std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
  return ok;
}

anari::World makeWorld(anari::Device d)
{
  const float h = HALF_WIDTH;
  const std::vector<float3> verts = {
      {-h, -h, 0.f}, {h, -h, 0.f}, {h, h, 0.f}, {-h, h, 0.f}};
  const std::vector<uint3> indices = {{0, 1, 2}, {0, 2, 3}};

  auto geom = anari::newObject<anari::Geometry>(d, "triangle");
  anari::setParameterArray1D(
      d, geom, "vertex.position", verts.data(), verts.size());
  anari::setParameterArray1D(
      d, geom, "primitive.index", indices.data(), indices.size());
  anari::commitParameters(d, geom);

  auto mat = anari::newObject<anari::Material>(d, "matte");
  anari::commitParameters(d, mat);

  auto surface = anari::newObject<anari::Surface>(d);
  anari::setAndReleaseParameter(d, surface, "geometry", geom);
  anari::setAndReleaseParameter(d, surface, "material", mat);
  anari::commitParameters(d, surface);

  auto world = anari::newObject<anari::World>(d);
  anari::setParameterArray1D(d, world, "surface", &surface, 1);
  anari::commitParameters(d, world);
  anari::release(d, surface);
  return world;
}

// A camera of 'type' with no aperture parameters if 'apertureRadius' is 0
anari::Camera makeCamera(anari::Device d,
    const char *type,
    float apertureRadius = 0.f,
    float focusDistance = 1.f)
{
  auto camera = anari::newObject<anari::Camera>(d, type);
  anari::setParameter(d, camera, "position", float3(0.f, 0.f, CAMERA_DISTANCE));
  anari::setParameter(d, camera, "direction", float3(0.f, 0.f, -1.f));
  anari::setParameter(d, camera, "up", float3(0.f, 1.f, 0.f));
  anari::setParameter(
      d, camera, "fovy", 2.f * std::atan(0.5f * VIEW_HEIGHT / CAMERA_DISTANCE));
  anari::setParameter(d, camera, "height", VIEW_HEIGHT);
  if (apertureRadius != 0.f) {
    anari::setParameter(d, camera, "apertureRadius", apertureRadius);
    anari::setParameter(d, camera, "focusDistance", focusDistance);
  }
  anari::commitParameters(d, camera);
  return camera;
}

// The red albedo of every pixel of one render of the square through 'camera'
std::vector<float> render(anari::Device d,
    anari::World world,
    const char *rendererType,
    anari::Camera camera)
{
  auto renderer = anari::newObject<anari::Renderer>(d, rendererType);
  if (std::string(rendererType) == "quality") {
    // the whole image in one render, every pixel taking every sample
    anari::setParameter(d, renderer, "resolutionDivider", 0);
    anari::setParameter(d, renderer, "pixelSamples", 64);
  }
  anari::commitParameters(d, renderer);

  auto frame = anari::newObject<anari::Frame>(d);
  anari::setParameter(d, frame, "size", IMAGE_SIZE);
  anari::setParameter(d, frame, "channel.color", ANARI_FLOAT32_VEC4);
  anari::setParameter(d, frame, "channel.albedo", ANARI_FLOAT32_VEC3);
  anari::setParameter(d, frame, "world", world);
  anari::setParameter(d, frame, "camera", camera);
  anari::setAndReleaseParameter(d, frame, "renderer", renderer);
  anari::commitParameters(d, frame);
  anari::render(d, frame);
  anari::wait(d, frame);

  std::vector<float> result;
  auto fb = anari::map<float3>(d, frame, "channel.albedo");
  if (fb.data && fb.width == IMAGE_SIZE.x && fb.height == IMAGE_SIZE.y) {
    for (uint32_t i = 0; i < fb.width * fb.height; i++)
      result.push_back(fb.data[i].x);
  } else {
    std::printf("FAIL %s: could not map channel.albedo\n", rendererType);
  }
  anari::unmap(d, frame, "channel.albedo");
  anari::release(d, frame);
  return result;
}

struct Coverage
{
  // pixels neither all in nor all out of the square
  int partial{0};
  // in pixels
  float total{0.f};
};

// Coverage of an image of albedos, as fractions of the square's 'albedo'
Coverage coverage(const std::vector<float> &image, float albedo)
{
  Coverage c;
  for (float a : image) {
    const float f = a / albedo;
    if (f > PARTIAL && f < 1.f - PARTIAL)
      c.partial++;
    c.total += f;
  }
  return c;
}

bool checkCamera(anari::Device d, anari::World world, const char *type)
{
  bool pass = true;
  char name[256];

  auto pinholeCamera = makeCamera(d, type);
  const auto pinhole = render(d, world, "quality", pinholeCamera);
  anari::release(d, pinholeCamera);
  if (pinhole.empty())
    return false;
  // the centre of the square, 8 pixels from its edges
  const float albedo = pinhole[20 * IMAGE_SIZE.x + 20];
  if (!(albedo > 0.f)) {
    std::snprintf(name, sizeof(name), "%s: the square has an albedo", type);
    return report(name, false);
  }

  // Blur radius at the square, 0.2 * |2 - 1| / 1 = 0.2, is 4 pixels
  struct Case
  {
    const char *what;
    float apertureRadius;
    float focusDistance;
    bool blurred;
  };
  const Case cases[] = {
      {"pinhole", 0.f, 1.f, false},
      {"focused on the square", 0.2f, CAMERA_DISTANCE, false},
      {"focused in front of the square", 0.2f, 1.f, true},
  };
  for (const auto &c : cases) {
    auto camera = makeCamera(d, type, c.apertureRadius, c.focusDistance);
    const auto image = render(d, world, "quality", camera);
    anari::release(d, camera);
    if (image.empty())
      return false;
    const Coverage cov = coverage(image, albedo);

    std::snprintf(name,
        sizeof(name),
        "quality, %s, %s: %s (%d partly covered pixels)",
        type,
        c.what,
        c.blurred ? "edges blurred" : "edges sharp",
        cov.partial);
    // the square's perimeter is 64 pixels; blurred over 4 pixels each side,
    // several hundred are partly covered
    pass &= report(name, c.blurred ? cov.partial > 200 : cov.partial == 0);

    std::snprintf(name,
        sizeof(name),
        "quality, %s, %s: total coverage %.1f of %.0f pixels",
        type,
        c.what,
        cov.total,
        SQUARE_AREA);
    pass &=
        report(name, std::abs(cov.total - SQUARE_AREA) < 0.03f * SQUARE_AREA);
  }

  // The preview renderer renders a defocused camera through a pinhole
  auto camera = makeCamera(d, type, 0.2f, 1.f);
  const auto preview = render(d, world, "preview", camera);
  anari::release(d, camera);
  if (preview.empty())
    return false;
  const float previewAlbedo = preview[20 * IMAGE_SIZE.x + 20];
  const Coverage cov = coverage(preview, previewAlbedo);
  std::snprintf(name,
      sizeof(name),
      "preview, %s: ignores the aperture (%d partly covered pixels)",
      type,
      cov.partial);
  pass &= report(name, previewAlbedo > 0.f && cov.partial == 0);

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
  auto world = makeWorld(d);

  bool pass = true;
  pass &= checkCamera(d, world, "perspective");
  pass &= checkCamera(d, world, "orthographic");

  anari::release(d, world);
  anari::release(d, d);
  anari::unloadLibrary(library);

  pass &= report("no warnings or errors", g_errors == 0);
  return pass ? 0 : 1;
}
