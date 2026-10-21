// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// White furnaces of closures with a delta lobe inside a mix
// (renderer/pathtracer/ClosureSampling.h): a mix evaluates to the
// lerp of its children, so a delta sample of the child it chose passes up
// unscaled. Typhoon divides it by the choice's probability, which renders a
// smooth half-metal 1.5 times too bright in these furnaces. Each sphere is
// lossless (or nearly), so it must render to the radiance that surrounds it:
// - lit by an enclosing box emitting radiance 1 inwards, which only BSDF
//   sampling finds (no lights: every sample weight is the BSDF sample's), and
// - lit by the ambient light of radiance 1, which next-event estimation
//   samples and BSDF samples gather by MIS.
// The spheres: physicallyBased of white base colour, roughness 0 and
// metallic 0.3 and 0.5 (ANARI's specular 0: a Lambertian mixed with a delta
// metal of reflectance 1, under a dielectric reflection of reflectance 0
// that the layer still chooses now and then, so the samples' weights vary
// but average to 1), shaded natively and through its graph
// ('nativeMaterials' false), which must render the same image (bitwise
// where the C++ rounds as PathPbm.ispc does, native_rounding.h); and
// materialx documents of a Lambertian mixed with a white generalized Schlick
// mirror and of a translucent lobe mixed with smooth glass (caustics on),
// whose BSDF samples all weigh 1 (to rounding) on every path through the
// sphere.

#include "native_rounding.h"
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

// Materials //////////////////////////////////////////////////////////////////

// White, smooth, partly metallic (specular 0: no dielectric reflection)
anari::Material makeHalfMetal(anari::Device d, float metallic)
{
  auto mat = anari::newObject<anari::Material>(d, "physicallyBased");
  anari::setParameter(d, mat, "baseColor", float3(1.f));
  anari::setParameter(d, mat, "metallic", metallic);
  anari::setParameter(d, mat, "roughness", 0.f);
  anari::commitParameters(d, mat);
  return mat;
}

anari::Material makeDocument(anari::Device d, const char *document)
{
  auto mat = anari::newObject<anari::Material>(d, "materialx");
  anari::setParameter(d, mat, "sourceType", "documentInline");
  anari::setParameter(d, mat, "source", document);
  anari::commitParameters(d, mat);
  return mat;
}

// A Lambertian mixed with a white delta mirror (0.3 of the mirror)
const char *const DIFFUSE_MIRROR_DOCUMENT = R"(<?xml version="1.0"?>
<materialx version="1.39">
  <oren_nayar_diffuse_bsdf name="diffuse" type="BSDF">
    <input name="color" type="color3" value="1, 1, 1" />
    <input name="roughness" type="float" value="0" />
  </oren_nayar_diffuse_bsdf>
  <generalized_schlick_bsdf name="mirror" type="BSDF">
    <input name="color0" type="color3" value="1, 1, 1" />
    <input name="color82" type="color3" value="1, 1, 1" />
    <input name="color90" type="color3" value="1, 1, 1" />
    <input name="roughness" type="vector2" value="0, 0" />
  </generalized_schlick_bsdf>
  <mix name="mixed" type="BSDF">
    <input name="fg" type="BSDF" nodename="mirror" />
    <input name="bg" type="BSDF" nodename="diffuse" />
    <input name="mix" type="float" value="0.3" />
  </mix>
  <surface name="shader" type="surfaceshader">
    <input name="bsdf" type="BSDF" nodename="mixed" />
  </surface>
  <surfacematerial name="DiffuseMirror" type="material">
    <input name="surfaceshader" type="surfaceshader" nodename="shader" />
  </surfacematerial>
</materialx>
)";

// A white translucent lobe mixed half and half with smooth thick glass
const char *const TRANSLUCENT_GLASS_DOCUMENT = R"(<?xml version="1.0"?>
<materialx version="1.39">
  <translucent_bsdf name="translucent" type="BSDF">
    <input name="color" type="color3" value="1, 1, 1" />
  </translucent_bsdf>
  <dielectric_bsdf name="glass" type="BSDF">
    <input name="ior" type="float" value="1.5" />
    <input name="roughness" type="vector2" value="0, 0" />
    <input name="scatter_mode" type="string" value="RT" />
  </dielectric_bsdf>
  <mix name="mixed" type="BSDF">
    <input name="fg" type="BSDF" nodename="glass" />
    <input name="bg" type="BSDF" nodename="translucent" />
    <input name="mix" type="float" value="0.5" />
  </mix>
  <surface name="shader" type="surfaceshader">
    <input name="bsdf" type="BSDF" nodename="mixed" />
  </surface>
  <surfacematerial name="TranslucentGlass" type="material">
    <input name="surfaceshader" type="surfaceshader" nodename="shader" />
  </surfacematerial>
</materialx>
)";

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

anari::Geometry makeSphere(anari::Device d)
{
  const float3 center(0.f);
  auto geom = anari::newObject<anari::Geometry>(d, "sphere");
  anari::setParameterArray1D(d, geom, "vertex.position", &center, 1);
  anari::setParameter(d, geom, "radius", 1.f);
  anari::commitParameters(d, geom);
  return geom;
}

// The box [-r, r]^3 of triangles, faces inward, black and non-metallic,
// emitting radiance 1
anari::Surface makeEnclosure(anari::Device d, float r)
{
  std::vector<float3> v;
  for (int i = 0; i < 8; i++)
    v.push_back(float3(i & 1 ? r : -r, i & 2 ? r : -r, i & 4 ? r : -r));
  const uint3 indices[] = {{0, 3, 2},
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
      {1, 5, 7}};
  auto geom = anari::newObject<anari::Geometry>(d, "triangle");
  anari::setParameterArray1D(d, geom, "vertex.position", v.data(), v.size());
  anari::setParameterArray1D(d, geom, "primitive.index", indices, 12);
  anari::commitParameters(d, geom);

  auto mat = anari::newObject<anari::Material>(d, "physicallyBased");
  anari::setParameter(d, mat, "baseColor", float3(0.f));
  anari::setParameter(d, mat, "metallic", 0.f);
  anari::setParameter(d, mat, "emissive", float3(1.f));
  anari::commitParameters(d, mat);
  return makeSurface(d, geom, mat);
}

// The unit sphere of 'mat' (released), in the enclosure or not
anari::World makeWorld(anari::Device d, anari::Material mat, bool enclosure)
{
  std::vector<anari::Surface> surfaces = {makeSurface(d, makeSphere(d), mat)};
  if (enclosure)
    surfaces.push_back(makeEnclosure(d, 50.f));
  auto world = anari::newObject<anari::World>(d);
  anari::setParameterArray1D(
      d, world, "surface", surfaces.data(), surfaces.size());
  anari::commitParameters(d, world);
  for (auto s : surfaces)
    anari::release(d, s);
  return world;
}

// Rendering //////////////////////////////////////////////////////////////////

struct Settings
{
  uint2 size{32, 32};
  int pixelSamples{64};
  // the ambient light's radiance (white)
  float ambient{0.f};
  bool caustics{false};
  bool nativeMaterials{true};
  int seed{1};
};

using Image = std::vector<float4>;

// The sphere seen from +z by an orthographic camera of view height 2.2
Image render(anari::Device d, anari::World world, const Settings &s)
{
  auto renderer = anari::newObject<anari::Renderer>(d, "quality");
  anari::setParameter(d, renderer, "background", float4(0.f, 0.f, 0.f, 1.f));
  anari::setParameter(d, renderer, "ambientColor", float3(1.f));
  anari::setParameter(d, renderer, "ambientRadiance", s.ambient);
  anari::setParameter(d, renderer, "pixelSamples", s.pixelSamples);
  anari::setParameter(d, renderer, "maxBounces", 64);
  anari::setParameter(d, renderer, "minBouncesBeforeRR", 64);
  anari::setParameter(d, renderer, "fireflyClampThreshold", 0.f);
  anari::setParameter(d, renderer, "causticsClampThreshold", 0.f);
  anari::setParameter(d, renderer, "minSamplesBeforeAdaptive", -1);
  anari::setParameter(d, renderer, "enableCaustics", s.caustics);
  anari::setParameter(d, renderer, "nativeMaterials", s.nativeMaterials);
  anari::setParameter(d, renderer, "randomNumberSeed", s.seed);
  anari::commitParameters(d, renderer);

  auto camera = anari::newObject<anari::Camera>(d, "orthographic");
  anari::setParameter(d, camera, "position", float3(0.f, 0.f, 6.f));
  anari::setParameter(d, camera, "direction", float3(0.f, 0.f, -1.f));
  anari::setParameter(d, camera, "up", float3(0.f, 1.f, 0.f));
  anari::setParameter(d, camera, "height", 2.2f);
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

// The pixels of the sphere's central disc (radius 0.8 of the sphere's)
std::vector<float4> disc(const Image &image, uint2 size)
{
  std::vector<float4> pixels;
  if (image.size() != size_t(size.x) * size.y)
    return pixels;
  for (uint32_t y = 0; y < size.y; y++) {
    for (uint32_t x = 0; x < size.x; x++) {
      const float px = ((x + 0.5f) / size.x - 0.5f) * 2.2f;
      const float py = ((y + 0.5f) / size.y - 0.5f) * 2.2f;
      if (px * px + py * py < 0.64f)
        pixels.push_back(image[size_t(y) * size.x + x]);
    }
  }
  return pixels;
}

struct Furnace
{
  // the disc's mean over its pixels, channels and the seeds, its standard
  // error over the seeds, and the largest difference of a pixel's channel
  // from 1 in any seed
  double mean{0.};
  double standardError{0.};
  double largestError{0.};
};

constexpr int SEEDS = 4;

Furnace furnace(anari::Device d, anari::World world, Settings s)
{
  Furnace f;
  double means[SEEDS] = {};
  for (int k = 0; k < SEEDS; k++) {
    s.seed = k + 1;
    const auto pixels = disc(render(d, world, s), s.size);
    double sum = 0.;
    for (const float4 &p : pixels) {
      for (int c = 0; c < 3; c++) {
        sum += p[c];
        f.largestError = std::max(f.largestError, std::abs(p[c] - 1.));
      }
    }
    means[k] = pixels.empty() ? 0. : sum / (3. * pixels.size());
    f.mean += means[k] / SEEDS;
  }
  double var = 0.;
  for (double m : means)
    var += (m - f.mean) * (m - f.mean);
  f.standardError = std::sqrt(var / (double(SEEDS - 1) * SEEDS));
  return f;
}

// Checks a furnace's mean: exactly 1 (every BSDF sample's weight 1: each
// pixel within 'exact' of it), or within 4 standard errors and 'tolerance'
// of 1
bool checkFurnace(const std::string &name,
    const Furnace &f,
    double tolerance,
    double exact = 0.)
{
  std::printf("  %s: %.6f (standard error %.6f, largest pixel error %.2e)\n",
      name.c_str(),
      f.mean,
      f.standardError,
      f.largestError);
  if (exact > 0.)
    return report(name + " renders every pixel to 1", f.largestError <= exact);
  const double diff = f.mean - 1.;
  return report(name + " renders to 1 (within the noise)",
      std::abs(diff) <= tolerance
          && std::abs(diff) <= 4. * f.standardError + 1e-5);
}

bool testPhysicallyBased(anari::Device d)
{
  bool ok = true;
  for (float metallic : {0.3f, 0.5f}) {
    char name[96];
    std::snprintf(
        name, sizeof(name), "smooth physicallyBased of metallic %g", metallic);
    Image natives[2], graphs[2];
    for (int lit = 0; lit < 2; lit++) {
      const bool enclosure = lit == 0;
      for (int native = 0; native < 2; native++) {
        auto world = makeWorld(d, makeHalfMetal(d, metallic), enclosure);
        Settings s;
        s.ambient = enclosure ? 0.f : 1.f;
        s.nativeMaterials = native == 1;
        const std::string label = std::string(name)
            + (native ? " (native)" : " (its graph)")
            + (enclosure ? " by BSDF sampling" : " under the ambient light");
        ok &= checkFurnace(label, furnace(d, world, s), 0.005);
        (native ? natives : graphs)[lit] = render(d, world, s);
        anari::release(d, world);
      }
    }
    if (native_rounding::BITWISE) {
      const auto bitwise = [](const Image &a, const Image &b) {
        return !a.empty() && a.size() == b.size()
            && std::memcmp(a.data(), b.data(), a.size() * sizeof(float4)) == 0;
      };
      const bool same =
          bitwise(natives[0], graphs[0]) && bitwise(natives[1], graphs[1]);
      ok &= report(
          std::string(name) + ": native shading renders bitwise as its graph",
          same);
    }
  }
  return ok;
}

bool testDocuments(anari::Device d)
{
  bool ok = true;
  for (int lit = 0; lit < 2; lit++) {
    const bool enclosure = lit == 0;
    const std::string how =
        enclosure ? " by BSDF sampling" : " under the ambient light";
    Settings s;
    s.ambient = enclosure ? 0.f : 1.f;

    auto mirror =
        makeWorld(d, makeDocument(d, DIFFUSE_MIRROR_DOCUMENT), enclosure);
    // by BSDF sampling, every sample weighs 1 (a Lambertian's weight to
    // rounding, the mirror's exactly), so every pixel is 1
    ok &= checkFurnace("a Lambertian mixed with a delta mirror" + how,
        furnace(d, mirror, s),
        0.005,
        enclosure ? 1e-4 : 0.);
    anari::release(d, mirror);

    s.caustics = true;
    auto glass =
        makeWorld(d, makeDocument(d, TRANSLUCENT_GLASS_DOCUMENT), enclosure);
    ok &= checkFurnace("a translucent lobe mixed with smooth glass" + how,
        furnace(d, glass, s),
        0.005,
        enclosure ? 1e-4 : 0.);
    anari::release(d, glass);
  }
  return ok;
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
  ok &= testPhysicallyBased(d);
  ok &= testDocuments(d);

  anari::release(d, d);
  anari::unloadLibrary(library);

  ok &= report("no errors or warnings", g_errors == 0 && g_warnings == 0);
  return ok ? 0 : 1;
}
