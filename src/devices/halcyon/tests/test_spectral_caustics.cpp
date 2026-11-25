// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// The 'quality''s hero-wavelength dispersion and caustics policy
// (renderer/pathtracer/HeroWavelength.isph), against
// analytic values and each other:
// - dispersion separates wavelengths: a white emissive stripe seen through a
//   prism of dispersive OpenPBR glass shows a band whose blue lies above its
//   red (blue refracts more), carrying about the light the band of a
//   non-dispersive prism does, which shows every pixel grey (r = g = b
//   exactly, as RGB paths stay RGB);
// - a closure that could disperse but doesn't (an Abbe number of 0) renders
//   bitwise as one without dispersion parameters (the path stays RGB);
// - a furnace: a sphere of white dispersive glass under the ambient light
//   renders to its radiance in every channel, which needs the spectral
//   weights and their neutral balance;
// - interior media in hero-wavelength mode: an emitter seen through a block of
//   dispersive glass with a grey absorbing interior renders as through a
//   non-dispersive one;
// - caustics: a floor beside a mirror under a sphere light gets the mirror
//   image's irradiance only with enableCaustics (exactly, in expectation),
//   and with a bright light that contribution is clamped to
//   causticsClampThreshold (0 doesn't clamp);
// - shadow rays: with caustics off a floor under a thick glass slab is lit
//   through it (the straight-through approximation), with caustics on it is
//   black under a directional light (no path can find a delta light through
//   the slab), but lit under a sphere light by refraction through it;
// - a scene without caustic-class lobes renders bitwise alike with caustics on
//   and off;
// - MIS under caustic pruning: a matte floor beside a
//   sphere whose transmission lobe pruning removes renders under the ambient
//   light as under an enclosure emitting the same radiance, which only BSDF
//   sampling finds (Typhoon's folding of a missed ray's pdf by the unpruned
//   closure makes it 0.25% brighter).
//
// HALCYON_SPECTRAL_IMAGES=<directory> writes the images as PPMs.

// anari
#define ANARI_LINALG_DEFINITIONS
#include <anari/anari_cpp/ext/linalg.h>
#include <anari/anari_cpp.hpp>
// std
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

using namespace anari::math;

constexpr float PI = 3.14159265358979323846f;

int g_errors = 0;
int g_warnings = 0;

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
    g_errors++;
  } else if (severity == ANARI_SEVERITY_WARNING) {
    std::fprintf(stderr, "[ANARI WARNING] %s\n", message);
    g_warnings++;
  }
}

bool report(const std::string &name, bool ok)
{
  std::printf("%s %s\n", ok ? "PASS" : "FAIL", name.c_str());
  return ok;
}

std::string str(float3 v)
{
  char buf[96];
  std::snprintf(buf, sizeof(buf), "(%g, %g, %g)", v.x, v.y, v.z);
  return buf;
}

// Materials //////////////////////////////////////////////////////////////////

// A smooth thick OpenPBR glass of IOR 1.5 and white tint; dispersive with a
// 'dispersionScale' above 0 (OpenPBR's transmission_dispersion_scale, its
// Abbe number 'abbe'; neither set where it is negative); an 'interior' below
// 1 gives it an absorbing interior medium of that colour at transmission
// depth 1
anari::Material makeGlass(anari::Device d,
    float dispersionScale,
    float abbe = 10.f,
    float interior = 1.f)
{
  auto mat = anari::newObject<anari::Material>(d, "openPBR");
  anari::setParameter(d, mat, "base_weight", 0.f);
  anari::setParameter(d, mat, "specular_weight", 1.f);
  anari::setParameter(d, mat, "specular_roughness", 0.f);
  anari::setParameter(d, mat, "specular_ior", 1.5f);
  anari::setParameter(d, mat, "transmission_weight", 1.f);
  anari::setParameter(d, mat, "transmission_color", float3(interior));
  if (dispersionScale >= 0.f) {
    anari::setParameter(
        d, mat, "transmission_dispersion_scale", dispersionScale);
    anari::setParameter(d, mat, "transmission_dispersion_abbe_number", abbe);
  }
  if (interior < 1.f)
    anari::setParameter(d, mat, "transmission_depth", 1.f);
  anari::setParameter(d, mat, "geometry_thin_walled", false);
  anari::commitParameters(d, mat);
  return mat;
}

// A black surface that emits 'emission' (native physicallyBased)
anari::Material makeEmitter(anari::Device d, float3 emission)
{
  auto mat = anari::newObject<anari::Material>(d, "physicallyBased");
  anari::setParameter(d, mat, "baseColor", float3(0.f));
  anari::setParameter(d, mat, "emissive", emission);
  anari::commitParameters(d, mat);
  return mat;
}

// A perfect white mirror (native physicallyBased: a smooth metal, whose
// Schlick reflectance of F0 1 is 1)
anari::Material makeMirror(anari::Device d)
{
  auto mat = anari::newObject<anari::Material>(d, "physicallyBased");
  anari::setParameter(d, mat, "baseColor", float3(1.f));
  anari::setParameter(d, mat, "metallic", 1.f);
  anari::setParameter(d, mat, "roughness", 0.f);
  anari::commitParameters(d, mat);
  return mat;
}

anari::Material makeMatte(anari::Device d, float3 color)
{
  auto mat = anari::newObject<anari::Material>(d, "matte");
  anari::setParameter(d, mat, "color", color);
  anari::commitParameters(d, mat);
  return mat;
}

// Geometry and worlds ////////////////////////////////////////////////////////

anari::Surface makeSurface(
    anari::Device d, anari::Geometry geom, anari::Material mat)
{
  auto surface = anari::newObject<anari::Surface>(d);
  anari::setAndReleaseParameter(d, surface, "geometry", geom);
  anari::setAndReleaseParameter(d, surface, "material", mat);
  anari::commitParameters(d, surface);
  return surface;
}

anari::Geometry makeTriangles(anari::Device d,
    const std::vector<float3> &v,
    const std::vector<uint3> &indices)
{
  auto geom = anari::newObject<anari::Geometry>(d, "triangle");
  anari::setParameterArray1D(d, geom, "vertex.position", v.data(), v.size());
  anari::setParameterArray1D(
      d, geom, "primitive.index", indices.data(), indices.size());
  anari::commitParameters(d, geom);
  return geom;
}

// The quad of corners 'a', 'b', 'c', 'd' (in that order), facing
// (b - a) x (c - a)
anari::Geometry makeQuad(
    anari::Device d, float3 a, float3 b, float3 c, float3 e)
{
  return makeTriangles(d, {a, b, c, e}, {{0, 1, 2}, {0, 2, 3}});
}

// The box [lo, hi] of triangles, faces outward
anari::Geometry makeBox(anari::Device d, float3 lo, float3 hi)
{
  std::vector<float3> v;
  for (int i = 0; i < 8; i++) {
    v.push_back(
        float3(i & 1 ? hi.x : lo.x, i & 2 ? hi.y : lo.y, i & 4 ? hi.z : lo.z));
  }
  return makeTriangles(d,
      v,
      {{0, 2, 3},
          {0, 3, 1},
          {4, 5, 7},
          {4, 7, 6},
          {0, 1, 5},
          {0, 5, 4},
          {2, 6, 7},
          {2, 7, 3},
          {0, 4, 6},
          {0, 6, 2},
          {1, 3, 7},
          {1, 7, 5}});
}

// A prism along x over [-3, 3] whose cross-section is the isosceles triangle
// of apex (y, z) = (1, 0), apex angle 40 degrees and base at y = -1, faces
// outward (flat: no normals)
anari::Geometry makePrism(anari::Device d)
{
  const float h = 2.f * std::tan(20.f * PI / 180.f);
  std::vector<float3> v;
  for (float x : {-3.f, 3.f}) {
    v.push_back(float3(x, 1.f, 0.f));
    v.push_back(float3(x, -1.f, h));
    v.push_back(float3(x, -1.f, -h));
  }
  return makeTriangles(d,
      v,
      {{0, 2, 1},
          {3, 4, 5},
          {0, 1, 4},
          {0, 4, 3},
          {0, 3, 5},
          {0, 5, 2},
          {1, 2, 5},
          {1, 5, 4}});
}

anari::Geometry makeSphere(anari::Device d, float3 center, float r)
{
  auto geom = anari::newObject<anari::Geometry>(d, "sphere");
  anari::setParameterArray1D(d, geom, "vertex.position", &center, 1);
  anari::setParameter(d, geom, "radius", r);
  anari::commitParameters(d, geom);
  return geom;
}

// A world of 'surfaces' and 'lights' (all released)
anari::World makeWorld(anari::Device d,
    std::vector<anari::Surface> surfaces,
    std::vector<anari::Light> lights = {})
{
  auto world = anari::newObject<anari::World>(d);
  anari::setParameterArray1D(
      d, world, "surface", surfaces.data(), surfaces.size());
  if (!lights.empty())
    anari::setParameterArray1D(d, world, "light", lights.data(), lights.size());
  anari::commitParameters(d, world);
  for (auto s : surfaces)
    anari::release(d, s);
  for (auto l : lights)
    anari::release(d, l);
  return world;
}

// Rendering //////////////////////////////////////////////////////////////////

struct Settings
{
  uint2 size{48, 48};
  int pixelSamples{64};
  int maxBounces{16};
  // the ambient light's radiance (white)
  float ambient{0.f};
  bool caustics{false};
  // causticsClampThreshold; negative: the default
  float causticsClamp{-1.f};
  int seed{-1};
  // the orthographic camera: position, direction, up and view height
  float3 position{0.f, 0.f, 6.f};
  float3 direction{0.f, 0.f, -1.f};
  float3 up{0.f, 1.f, 0.f};
  float height{2.f};
};

// Colours, bottom row first
using Image = std::vector<float4>;

Image render(anari::Device d, anari::World world, const Settings &s)
{
  auto renderer = anari::newObject<anari::Renderer>(d, "quality");
  // one render is the whole first render (no progressive resolution)
  anari::setParameter(d, renderer, "resolutionDivider", 0);
  anari::setParameter(d, renderer, "background", float4(0.f, 0.f, 0.f, 1.f));
  anari::setParameter(d, renderer, "ambientColor", float3(1.f));
  anari::setParameter(d, renderer, "ambientRadiance", s.ambient);
  anari::setParameter(d, renderer, "pixelSamples", s.pixelSamples);
  anari::setParameter(d, renderer, "maxBounces", s.maxBounces);
  anari::setParameter(d, renderer, "fireflyClampThreshold", 0.f);
  anari::setParameter(d, renderer, "minSamplesBeforeAdaptive", -1);
  anari::setParameter(d, renderer, "enableCaustics", s.caustics);
  anari::setParameter(d, renderer, "randomNumberSeed", s.seed);
  if (s.causticsClamp >= 0.f) {
    anari::setParameter(d, renderer, "causticsClampThreshold", s.causticsClamp);
  }
  anari::commitParameters(d, renderer);

  auto camera = anari::newObject<anari::Camera>(d, "orthographic");
  anari::setParameter(d, camera, "position", s.position);
  anari::setParameter(d, camera, "direction", s.direction);
  anari::setParameter(d, camera, "up", s.up);
  anari::setParameter(d, camera, "height", s.height);
  anari::setParameter(d, camera, "aspect", float(s.size.x) / float(s.size.y));
  anari::commitParameters(d, camera);

  auto frame = anari::newObject<anari::Frame>(d);
  anari::setParameter(d, frame, "size", s.size);
  anari::setParameter(d, frame, "channel.color", ANARI_FLOAT32_VEC4);
  anari::setAndReleaseParameter(d, frame, "camera", camera);
  anari::setAndReleaseParameter(d, frame, "renderer", renderer);
  anari::setParameter(d, frame, "world", world);
  anari::commitParameters(d, frame);

  anari::render(d, frame);
  anari::wait(d, frame);
  Image image;
  auto fb = anari::map<float4>(d, frame, "channel.color");
  if (fb.data && fb.width == s.size.x && fb.height == s.size.y)
    image.assign(fb.data, fb.data + fb.width * fb.height);
  anari::unmap(d, frame, "channel.color");
  anari::release(d, frame);
  return image;
}

// Writes 'image' to $HALCYON_SPECTRAL_IMAGES/<name>.ppm, if set, top row
// first, clamped to [0, 1]
void writeImage(const char *name, const Image &image, uint2 size)
{
  const char *dir = std::getenv("HALCYON_SPECTRAL_IMAGES");
  if (!dir || image.empty())
    return;
  const std::string path = std::string(dir) + "/" + name + ".ppm";
  FILE *f = std::fopen(path.c_str(), "wb");
  if (!f)
    return;
  std::fprintf(f, "P6\n%u %u\n255\n", size.x, size.y);
  for (uint32_t y = size.y; y-- > 0;) {
    for (uint32_t x = 0; x < size.x; x++) {
      const float4 p = image[size_t(y) * size.x + x];
      for (int c = 0; c < 3; c++) {
        const float v = std::clamp(p[c], 0.f, 1.f);
        std::fputc(int(std::lround(255.f * std::pow(v, 1.f / 2.2f))), f);
      }
    }
  }
  std::fclose(f);
}

bool bitwiseEqual(const Image &a, const Image &b)
{
  return !a.empty() && a.size() == b.size()
      && std::memcmp(a.data(), b.data(), a.size() * sizeof(float4)) == 0;
}

// Dispersion /////////////////////////////////////////////////////////////////

// The prism of makePrism() between the camera (looking down -z) and a white
// emissive stripe behind it, which the prism's deviation brings into view
anari::World makePrismWorld(anari::Device d, anari::Material glass)
{
  return makeWorld(d,
      {makeSurface(d, makePrism(d), glass),
          makeSurface(d,
              makeQuad(d,
                  float3(-3.f, -1.15f, -3.f),
                  float3(3.f, -1.15f, -3.f),
                  float3(3.f, -1.05f, -3.f),
                  float3(-3.f, -1.05f, -3.f)),
              makeEmitter(d, float3(1.f)))});
}

// Per channel, the image's sum and the row (bottom first) its rows' sums
// centre on
struct Band
{
  float3 sum{0.f};
  float3 centroid{0.f};
};

Band band(const Image &image, uint2 size)
{
  Band b;
  double sum[3] = {}, moment[3] = {};
  for (uint32_t y = 0; y < size.y; y++) {
    for (uint32_t x = 0; x < size.x; x++) {
      const float4 p = image[size_t(y) * size.x + x];
      for (int c = 0; c < 3; c++) {
        sum[c] += p[c];
        moment[c] += p[c] * (y + 0.5);
      }
    }
  }
  for (int c = 0; c < 3; c++) {
    b.sum[c] = float(sum[c]);
    b.centroid[c] = sum[c] > 0. ? float(moment[c] / sum[c]) : 0.f;
  }
  return b;
}

bool testDispersion(anari::Device d)
{
  Settings s;
  s.size = uint2(64, 64);
  s.pixelSamples = 64;
  s.height = 1.6f;
  s.position = float3(0.f, 0.1f, 6.f);

  auto clear = makePrismWorld(d, makeGlass(d, 0.f));
  const Image plain = render(d, clear, s);
  anari::release(d, clear);
  auto dispersive = makePrismWorld(d, makeGlass(d, 1.f, 10.f));
  const Image spectral = render(d, dispersive, s);
  anari::release(d, dispersive);
  // an Abbe number of 0 disperses nothing
  auto none = makePrismWorld(d, makeGlass(d, 1.f, 0.f));
  const Image noDispersion = render(d, none, s);
  anari::release(d, none);
  writeImage("prism", plain, s.size);
  writeImage("prism_dispersive", spectral, s.size);

  bool grey = !plain.empty();
  for (const float4 &p : plain)
    grey = grey && p.x == p.y && p.y == p.z;
  const Band a = band(plain, s.size);
  const Band b = band(spectral, s.size);
  std::printf("  clear prism: sum %s, centroid rows %s\n",
      str(a.sum).c_str(),
      str(a.centroid).c_str());
  std::printf("  dispersive prism: sum %s, centroid rows %s\n",
      str(b.sum).c_str(),
      str(b.centroid).c_str());

  bool ok = report("a non-dispersive prism shows the stripe grey (RGB paths)",
      grey && a.sum.x > 0.f);
  ok &= report("a closure of Abbe number 0 renders as one without dispersion",
      bitwiseEqual(plain, noDispersion));
  // blue refracts more, so the camera sees the stripe's blue higher
  ok &= report("dispersion separates blue above red by over 2 pixels",
      b.centroid.z - b.centroid.x > 2.f);
  ok &= report("green lies between them",
      b.centroid.y > b.centroid.x && b.centroid.y < b.centroid.z);
  ok &= report("the clear prism's band lies between the dispersive one's",
      a.centroid.x > b.centroid.x && a.centroid.x < b.centroid.z);
  bool energy = true;
  for (int c = 0; c < 3; c++)
    energy = energy && std::abs(b.sum[c] / a.sum[c] - 1.f) < 0.05f;
  ok &=
      report("the dispersive band carries the clear band's light (5%)", energy);
  return ok;
}

// The mean colour over the disc of radius 'fraction' of the image's half
// height around its centre
float3 discMean(const Image &image, uint2 size, float fraction)
{
  double sum[3] = {};
  size_t n = 0;
  for (uint32_t y = 0; y < size.y; y++) {
    for (uint32_t x = 0; x < size.x; x++) {
      const float cx = (x + 0.5f) / size.x - 0.5f;
      const float cy = (y + 0.5f) / size.y - 0.5f;
      if (cx * cx + cy * cy >= 0.25f * fraction * fraction)
        continue;
      const float4 p = image[size_t(y) * size.x + x];
      for (int c = 0; c < 3; c++)
        sum[c] += p[c];
      n++;
    }
  }
  return n ? float3(float(sum[0]), float(sum[1]), float(sum[2])) / float(n)
           : float3(0.f);
}

bool within(float3 v, float3 expected, float tolerance)
{
  bool ok = true;
  for (int c = 0; c < 3; c++)
    ok = ok && std::abs(v[c] / expected[c] - 1.f) <= tolerance;
  return ok;
}

bool testFurnaces(anari::Device d)
{
  Settings s;
  s.size = uint2(48, 48);
  s.pixelSamples = 256;
  s.maxBounces = 64;
  s.ambient = 1.f;
  s.height = 2.2f;

  bool ok = true;
  auto glass = makeWorld(d,
      {makeSurface(
          d, makeSphere(d, float3(0.f), 1.f), makeGlass(d, 1.f, 10.f))});
  const float3 g = discMean(render(d, glass, s), s.size, 0.8f);
  anari::release(d, glass);
  std::printf("  dispersive glass sphere: %s\n", str(g).c_str());
  ok &= report(
      "a white dispersive glass sphere in a furnace renders to the "
      "ambient radiance (1.5%)",
      within(g, float3(1.f), 0.015f));

  // an emitter behind a block of glass of a grey absorbing interior
  Settings b;
  b.size = uint2(32, 32);
  b.pixelSamples = 256;
  b.height = 1.f;
  float3 means[2];
  for (int k = 0; k < 2; k++) {
    auto world = makeWorld(d,
        {makeSurface(d,
             makeBox(d, float3(-1.f, -1.f, -0.5f), float3(1.f, 1.f, 0.5f)),
             makeGlass(d, k == 0 ? 0.f : 1.f, 10.f, 0.5f)),
            makeSurface(d,
                makeQuad(d,
                    float3(-2.f, -2.f, -1.f),
                    float3(2.f, -2.f, -1.f),
                    float3(2.f, 2.f, -1.f),
                    float3(-2.f, 2.f, -1.f)),
                makeEmitter(d, float3(1.f)))});
    means[k] = discMean(render(d, world, b), b.size, 1.f);
    anari::release(d, world);
  }
  std::printf("  emitter through absorbing glass: clear %s, dispersive %s\n",
      str(means[0]).c_str(),
      str(means[1]).c_str());
  // (the glass's reflectance follows the wavelength's IOR: blue loses a
  // little more at the faces)
  ok &= report(
      "an absorbing interior attenuates a dispersive path as an RGB "
      "one (3%)",
      means[0].x > 0.f && within(means[1], means[0], 0.03f));
  return ok;
}

// Caustics ///////////////////////////////////////////////////////////////////

// The mirror scene: a matte floor y = 0 of albedo FLOOR_ALBEDO, a mirror wall
// x = MIRROR_X facing -x, a sphere light of radius LIGHT_RADIUS at
// LIGHT_CENTER (not visible to the camera), seen from above
constexpr float FLOOR_ALBEDO = 0.5f;
constexpr float MIRROR_X = 2.f;
constexpr float LIGHT_RADIUS = 0.25f;
const float3 LIGHT_CENTER(0.5f, 1.5f, 0.f);

anari::World makeMirrorWorld(anari::Device d, float radiance)
{
  auto light = anari::newObject<anari::Light>(d, "point");
  anari::setParameter(d, light, "position", LIGHT_CENTER);
  anari::setParameter(d, light, "radius", LIGHT_RADIUS);
  anari::setParameter(d, light, "radiance", radiance);
  anari::setParameter(d, light, "visible", false);
  anari::commitParameters(d, light);
  return makeWorld(d,
      {makeSurface(d,
           makeQuad(d,
               float3(-4.f, 0.f, -4.f),
               float3(-4.f, 0.f, 4.f),
               float3(4.f, 0.f, 4.f),
               float3(4.f, 0.f, -4.f)),
           makeMatte(d, float3(FLOOR_ALBEDO))),
          makeSurface(d,
              makeQuad(d,
                  float3(MIRROR_X, 0.f, -4.f),
                  float3(MIRROR_X, 0.f, 4.f),
                  float3(MIRROR_X, 4.f, 4.f),
                  float3(MIRROR_X, 4.f, -4.f)),
              makeMirror(d))},
      {light});
}

// The irradiance at floor point 'p' (normal +y) from a sphere of radiance 'L'
// and radius LIGHT_RADIUS centred at 'c' wholly above the floor:
// pi L (r / d)^2 cos(theta)
float sphereIrradiance(float3 p, float3 c, float L)
{
  const float3 v = c - p;
  const float d2 = dot(v, v);
  return PI * L * LIGHT_RADIUS * LIGHT_RADIUS / d2 * (v.y / std::sqrt(d2));
}

// The mean over the floor pixels of the mirror scene's camera (looking down
// over x, z in [-1.6, 1.6]) of the image, and of the floor's expected
// radiance from the light directly and from its mirror image
struct FloorMeans
{
  float rendered{0.f};
  float direct{0.f};
  float image{0.f};
};

FloorMeans floorMeans(const Image &image, const Settings &s, float L)
{
  const float3 mirrored(
      2.f * MIRROR_X - LIGHT_CENTER.x, LIGHT_CENTER.y, LIGHT_CENTER.z);
  FloorMeans m;
  double rendered = 0., direct = 0., virt = 0.;
  for (uint32_t j = 0; j < s.size.y; j++) {
    for (uint32_t i = 0; i < s.size.x; i++) {
      // the camera looks down -y with up -z: image x is world x, image y
      // (bottom first) is world -z
      const float x = ((i + 0.5f) / s.size.x - 0.5f) * s.height;
      const float z = -((j + 0.5f) / s.size.y - 0.5f) * s.height;
      const float3 p(x, 0.f, z);
      rendered += image[size_t(j) * s.size.x + i].x;
      direct += FLOOR_ALBEDO / PI * sphereIrradiance(p, LIGHT_CENTER, L);
      virt += FLOOR_ALBEDO / PI * sphereIrradiance(p, mirrored, L);
    }
  }
  const double n = double(s.size.x) * s.size.y;
  m.rendered = float(rendered / n);
  m.direct = float(direct / n);
  m.image = float(virt / n);
  return m;
}

bool testCaustics(anari::Device d)
{
  Settings s;
  s.size = uint2(48, 48);
  s.pixelSamples = 512;
  // the floor scatters, the mirror scatters, the ray leaving it finds the
  // light: no other paths
  s.maxBounces = 1;
  s.position = float3(0.f, 5.f, 0.f);
  s.direction = float3(0.f, -1.f, 0.f);
  s.up = float3(0.f, 0.f, -1.f);
  s.height = 3.2f;

  bool ok = true;
  // a caustic contribution is the floor's albedo times the radiance: 2
  // here, under the default clamp of 5
  const float L = 4.f;
  auto world = makeMirrorWorld(d, L);
  const Image off = render(d, world, s);
  s.caustics = true;
  const Image on = render(d, world, s);
  anari::release(d, world);
  writeImage("mirror_caustics_off", off, s.size);
  writeImage("mirror_caustics_on", on, s.size);

  const FloorMeans mOff = floorMeans(off, s, L);
  const FloorMeans mOn = floorMeans(on, s, L);
  std::printf(
      "  floor: caustics off %g, on %g; expected direct %g, mirror "
      "image %g\n",
      mOff.rendered,
      mOn.rendered,
      mOff.direct,
      mOff.image);
  ok &= report("caustics off: the floor gets the light's irradiance only (2%)",
      std::abs(mOff.rendered / mOff.direct - 1.f) < 0.02f);
  ok &= report("caustics on: it gets the mirror image's irradiance too (4%)",
      std::abs((mOn.rendered - mOff.rendered) / mOff.image - 1.f) < 0.04f);

  // a light 10 times as bright: each caustic contribution, 20, is clamped to
  // 5 (a quarter); a threshold of 0 doesn't clamp
  const float bright = 40.f;
  auto brightWorld = makeMirrorWorld(d, bright);
  s.caustics = false;
  const FloorMeans bOff = floorMeans(render(d, brightWorld, s), s, bright);
  s.caustics = true;
  const FloorMeans bOn = floorMeans(render(d, brightWorld, s), s, bright);
  s.causticsClamp = 0.f;
  const FloorMeans bUnclamped =
      floorMeans(render(d, brightWorld, s), s, bright);
  anari::release(d, brightWorld);
  const float clampedShare = (bOn.rendered - bOff.rendered) / bOff.image;
  const float unclampedShare =
      (bUnclamped.rendered - bOff.rendered) / bOff.image;
  std::printf(
      "  bright light: caustic share clamped %g (expected 0.25), "
      "unclamped %g (expected 1)\n",
      clampedShare,
      unclampedShare);
  ok &= report("causticsClampThreshold clamps each caustic contribution (4%)",
      std::abs(clampedShare / 0.25f - 1.f) < 0.04f);
  ok &= report("causticsClampThreshold 0 doesn't clamp (4%)",
      std::abs(unclampedShare - 1.f) < 0.04f);
  return ok;
}

// A floor y = 0 under a thick glass slab over x, z in [-0.5, 0.5], y in
// [0.5, 0.7], lit by a directional light from a little off the vertical (so
// that its glints on the glass don't face the camera), seen from above
bool testShadows(anari::Device d)
{
  Settings s;
  s.size = uint2(32, 32);
  s.pixelSamples = 16;
  // a camera ray through the slab reaches the floor at the third vertex
  s.maxBounces = 2;
  s.position = float3(0.f, 5.f, 0.f);
  s.direction = float3(0.f, -1.f, 0.f);
  s.up = float3(0.f, 0.f, -1.f);
  s.height = 2.f;

  const float3 direction = normalize(float3(0.2f, -1.f, 0.1f));
  auto light = anari::newObject<anari::Light>(d, "directional");
  anari::setParameter(d, light, "direction", direction);
  anari::setParameter(d, light, "irradiance", PI / -direction.y);
  anari::commitParameters(d, light);
  auto world = makeWorld(d,
      {makeSurface(d,
           makeQuad(d,
               float3(-4.f, 0.f, -4.f),
               float3(-4.f, 0.f, 4.f),
               float3(4.f, 0.f, 4.f),
               float3(4.f, 0.f, -4.f)),
           makeMatte(d, float3(1.f))),
          makeSurface(d,
              makeBox(d, float3(-0.5f, 0.5f, -0.5f), float3(0.5f, 0.7f, 0.5f)),
              makeGlass(d, -1.f))},
      {light});
  const Image off = render(d, world, s);
  s.caustics = true;
  const Image on = render(d, world, s);
  anari::release(d, world);
  writeImage("slab_caustics_off", off, s.size);
  writeImage("slab_caustics_on", on, s.size);
  if (off.empty() || on.empty())
    return report("slab renders", false);

  // pixels under the slab's middle are seen through it from above, in its
  // shadow; pixels at the image's corners see the floor beside it
  double under[2] = {}, beside[2] = {};
  int nUnder = 0, nBeside = 0;
  for (uint32_t j = 0; j < s.size.y; j++) {
    for (uint32_t i = 0; i < s.size.x; i++) {
      const float x = ((i + 0.5f) / s.size.x - 0.5f) * s.height;
      const float z = ((j + 0.5f) / s.size.y - 0.5f) * s.height;
      const size_t k = size_t(j) * s.size.x + i;
      if (std::abs(x) < 0.3f && std::abs(z) < 0.3f) {
        under[0] += off[k].x;
        under[1] += on[k].x;
        nUnder++;
      } else if (std::abs(x) > 0.7f && std::abs(z) > 0.7f) {
        beside[0] += off[k].x;
        beside[1] += on[k].x;
        nBeside++;
      }
    }
  }
  for (int k = 0; k < 2; k++) {
    under[k] /= std::max(nUnder, 1);
    beside[k] /= std::max(nBeside, 1);
  }
  std::printf("  under the slab: caustics off %g, on %g; beside it %g, %g\n",
      under[0],
      under[1],
      beside[0],
      beside[1]);
  // beside the slab the white floor under irradiance pi renders to 1; under
  // it, seen and lit through the slab's two faces, to about (1 - F)^4 = 0.85
  // (F = 0.04 at normal incidence)
  bool ok = report(
      "caustics off: the floor is lit through a thick glass slab (straight "
      "shadow rays: 0.75 to 0.95 of beside it)",
      under[0] > 0.75 * beside[0] && under[0] < 0.95 * beside[0]);
  ok &= report(
      "caustics on: under a directional light it is black (shadow rays are "
      "blocked, and no path finds a delta light)",
      under[1] == 0.0);

  // under a sphere light above the slab, with caustics on, light reaches the
  // floor under it only by refraction through the slab (shadow rays are
  // still blocked): a caustic path that BSDF sampling finds
  auto sphereLight = anari::newObject<anari::Light>(d, "point");
  anari::setParameter(d, sphereLight, "position", float3(0.f, 3.f, 0.f));
  anari::setParameter(d, sphereLight, "radius", 0.5f);
  anari::setParameter(d, sphereLight, "radiance", 10.f);
  anari::setParameter(d, sphereLight, "visible", false);
  anari::commitParameters(d, sphereLight);
  auto areaWorld = makeWorld(d,
      {makeSurface(d,
           makeQuad(d,
               float3(-4.f, 0.f, -4.f),
               float3(-4.f, 0.f, 4.f),
               float3(4.f, 0.f, 4.f),
               float3(4.f, 0.f, -4.f)),
           makeMatte(d, float3(1.f))),
          makeSurface(d,
              makeBox(d, float3(-0.5f, 0.5f, -0.5f), float3(0.5f, 0.7f, 0.5f)),
              makeGlass(d, -1.f))},
      {sphereLight});
  s.pixelSamples = 64;
  s.maxBounces = 4;
  const Image areaOn = render(d, areaWorld, s);
  anari::release(d, areaWorld);
  writeImage("slab_area_caustics_on", areaOn, s.size);
  double areaUnder = 0., areaBeside = 0.;
  for (uint32_t j = 0; j < s.size.y; j++) {
    for (uint32_t i = 0; i < s.size.x; i++) {
      const float x = ((i + 0.5f) / s.size.x - 0.5f) * s.height;
      const float z = ((j + 0.5f) / s.size.y - 0.5f) * s.height;
      const float v = areaOn.empty() ? 0.f : areaOn[size_t(j) * s.size.x + i].x;
      if (std::abs(x) < 0.3f && std::abs(z) < 0.3f)
        areaUnder += v / nUnder;
      else if (std::abs(x) > 0.7f && std::abs(z) > 0.7f)
        areaBeside += v / nBeside;
    }
  }
  std::printf(
      "  under a sphere light, caustics on: under the slab %g, beside "
      "it %g\n",
      areaUnder,
      areaBeside);
  ok &= report(
      "caustics on: an area light lights the floor under the slab through "
      "refraction (over a quarter of beside it)",
      areaUnder > 0.25 * areaBeside);
  return ok;
}

// A matte box room with a matte sphere under the ambient light: no
// caustic-class lobes, so caustics change nothing
bool testNoCausticLobes(anari::Device d)
{
  Settings s;
  s.size = uint2(32, 32);
  s.pixelSamples = 8;
  s.ambient = 1.f;
  auto world = makeWorld(d,
      {makeSurface(d,
           makeQuad(d,
               float3(-2.f, -1.f, -2.f),
               float3(-2.f, -1.f, 2.f),
               float3(2.f, -1.f, 2.f),
               float3(2.f, -1.f, -2.f)),
           makeMatte(d, float3(0.8f, 0.6f, 0.4f))),
          makeSurface(d,
              makeSphere(d, float3(0.f), 0.6f),
              makeMatte(d, float3(0.3f, 0.7f, 0.5f)))});
  const Image off = render(d, world, s);
  s.caustics = true;
  const Image on = render(d, world, s);
  anari::release(d, world);
  return report(
      "a scene without caustic-class lobes renders bitwise alike "
      "with caustics on",
      bitwiseEqual(off, on));
}

// MIS with caustic pruning ///////////////////////////////////////////////////

// A sphere of OpenPBR (base colour 0.8, specular roughness 0.5, transmission
// weight 0.5, thick) whose transmission is black, so that shadow rays with
// caustics off don't pass it (the straight-through approximation would make
// the reference below estimate something else)
anari::Material makeHalfTransmissive(anari::Device d)
{
  auto mat = anari::newObject<anari::Material>(d, "openPBR");
  anari::setParameter(d, mat, "base_color", float3(0.8f));
  anari::setParameter(d, mat, "specular_roughness", 0.5f);
  anari::setParameter(d, mat, "transmission_weight", 0.5f);
  anari::setParameter(d, mat, "transmission_color", float3(0.f));
  anari::setParameter(d, mat, "geometry_thin_walled", false);
  anari::commitParameters(d, mat);
  return mat;
}

// The box [-r, r]^3 of triangles, faces inward
anari::Geometry makeEnclosure(anari::Device d, float r)
{
  std::vector<float3> v;
  for (int i = 0; i < 8; i++)
    v.push_back(float3(i & 1 ? r : -r, i & 2 ? r : -r, i & 4 ? r : -r));
  return makeTriangles(d,
      v,
      {{0, 3, 2},
          {0, 1, 3},
          {4, 7, 5},
          {4, 6, 7},
          {0, 5, 1},
          {0, 4, 5},
          {2, 7, 6},
          {2, 3, 7},
          {0, 6, 4},
          {0, 2, 6},
          {1, 7, 3},
          {1, 5, 7}});
}

// The mean colour of the floor around the sphere (as seen from above, the
// annulus 0.7 to 2.5 around its centre) in an image of 'height' world units
float annulusMean(const Image &image, uint2 size, float height)
{
  double sum = 0.;
  size_t n = 0;
  for (uint32_t y = 0; y < size.y; y++) {
    for (uint32_t x = 0; x < size.x; x++) {
      const float px = ((x + 0.5f) / size.x - 0.5f) * height;
      const float py = ((y + 0.5f) / size.y - 0.5f) * height;
      const float r = std::sqrt(px * px + py * py);
      if (r < 0.7f || r > 2.5f)
        continue;
      const float4 p = image[size_t(y) * size.x + x];
      sum += (p.x + p.y + p.z) / 3.;
      n++;
    }
  }
  return n ? float(sum / n) : 0.f;
}

// Where caustic pruning removes a transmission lobe, next-event estimation
// samples the ambient light (a dome) in the reflection hemisphere; a missed
// BSDF ray must weigh it by the same hemisphere's pdf for the two MIS
// weights to sum to 1 (Typhoon takes the unpruned closure's flag there, which
// makes the floor beside such a sphere about 0.4% too bright, measured on this
// scene). The reference is unbiased by construction:
// the dome replaced by an enclosing box emitting the same radiance inwards,
// which only BSDF sampling finds (no MIS, nothing folded). Pruning, shadows
// and everything else are the same, so the floor around the sphere must
// agree within the noise (measured over seeds).
bool testMisWithPruning(anari::Device d)
{
  Settings s;
  s.size = uint2(48, 48);
  s.pixelSamples = 64;
  s.maxBounces = 64;
  s.position = float3(0.f, 5.f, 0.f);
  s.direction = float3(0.f, -1.f, 0.f);
  s.up = float3(0.f, 0.f, 1.f);
  s.height = 6.f;
  const auto scene = [&](bool enclosure) {
    std::vector<anari::Surface> surfaces = {makeSurface(d,
                                                makeQuad(d,
                                                    float3(-3.f, -1.f, -3.f),
                                                    float3(-3.f, -1.f, 3.f),
                                                    float3(3.f, -1.f, 3.f),
                                                    float3(3.f, -1.f, -3.f)),
                                                makeMatte(d, float3(0.5f))),
        makeSurface(
            d, makeSphere(d, float3(0.f), 0.6f), makeHalfTransmissive(d))};
    if (enclosure) {
      // black and non-metallic (physicallyBased's default metal of colour
      // 0 still reflects at grazing angles)
      auto emitter = anari::newObject<anari::Material>(d, "physicallyBased");
      anari::setParameter(d, emitter, "baseColor", float3(0.f));
      anari::setParameter(d, emitter, "metallic", 0.f);
      anari::setParameter(d, emitter, "emissive", float3(1.f));
      anari::commitParameters(d, emitter);
      surfaces.push_back(makeSurface(d, makeEnclosure(d, 50.f), emitter));
    }
    return makeWorld(d, surfaces);
  };

  constexpr int SEEDS = 8;
  double mean[2] = {}, var[2] = {};
  for (int enclosure = 0; enclosure < 2; enclosure++) {
    auto world = scene(enclosure);
    float m[SEEDS];
    for (int k = 0; k < SEEDS; k++) {
      Settings r = s;
      r.seed = k + 1;
      r.ambient = enclosure ? 0.f : 1.f;
      m[k] = annulusMean(render(d, world, r), s.size, s.height);
      mean[enclosure] += m[k] / SEEDS;
    }
    for (int k = 0; k < SEEDS; k++)
      var[enclosure] += (m[k] - mean[enclosure]) * (m[k] - mean[enclosure]);
    var[enclosure] /= double(SEEDS - 1) * SEEDS;
    anari::release(d, world);
  }
  const double diff = mean[0] - mean[1];
  const double se = std::sqrt(var[0] + var[1]);
  std::printf(
      "  floor around the sphere: ambient light %.6f, enclosure %.6f "
      "(%+.3f%%, %.1f standard errors)\n",
      mean[0],
      mean[1],
      100. * diff / mean[1],
      diff / se);
  return report(
      "with caustics pruning a transmission lobe, the ambient light's MIS "
      "weights sum to 1: the floor beside the sphere as under an emitting "
      "enclosure (within 4 standard errors and 0.1%)",
      std::abs(diff) <= 4. * se && std::abs(diff) <= 1e-3 * mean[1]);
}

} // namespace

int main()
{
  auto library = anari::loadLibrary("halcyon", statusFunc);
  if (!library) {
    std::fprintf(stderr, "could not load the halcyon library\n");
    return 1;
  }
  auto d = anari::newDevice(library, "default");
  if (!d) {
    std::fprintf(stderr, "could not create the halcyon device\n");
    return 1;
  }

  bool ok = true;
  ok &= testDispersion(d);
  ok &= testFurnaces(d);
  ok &= testCaustics(d);
  ok &= testShadows(d);
  ok &= testNoCausticLobes(d);
  ok &= testMisWithPruning(d);

  anari::release(d, d);
  anari::unloadLibrary(library);

  ok &= report("no errors or warnings", g_errors == 0 && g_warnings == 0);
  return ok ? 0 : 1;
}
