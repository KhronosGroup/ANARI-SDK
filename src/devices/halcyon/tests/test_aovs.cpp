// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// The albedo and normal frame channels (KHR_FRAME_CHANNEL_ALBEDO,
// KHR_FRAME_CHANNEL_NORMAL) of both renderers: the 'preview' renderer's
// surface colour and shading normal at the pixel center's first visible
// surface, the 'quality''s closure albedo and shading normal at the first
// non-specular hit (through a mirror), averaged over its samples and
// accumulated renders; every pixel type the extensions allow; and that
// asking for them changes none of the other channels.

// anari
#define ANARI_LINALG_DEFINITIONS
#include <anari/anari_cpp/ext/linalg.h>
#include <anari/anari_cpp.hpp>
// std
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace {

using namespace anari::math;

// not a multiple of any ISPC program count, to exercise partial SIMD chunks
constexpr uint2 IMAGE_SIZE = {37, 37};
const float3 FLOOR_COLOR = {0.8f, 0.5f, 0.25f};
const float3 WALL_COLOR = {0.3f, 0.6f, 0.9f};
// the device's default invalidMaterialColor
const float3 INVALID_COLOR = {1.f, 0.f, 1.f};

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

bool report(const std::string &name, bool ok, const std::string &detail = {})
{
  std::printf("%s %s%s%s\n",
      ok ? "PASS" : "FAIL",
      name.c_str(),
      detail.empty() ? "" : ": ",
      detail.c_str());
  return ok;
}

std::string str(float3 v)
{
  return "(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ", "
      + std::to_string(v.z) + ")";
}

bool near(float3 a, float3 b, float tolerance)
{
  const float3 d = linalg::abs(a - b);
  return d.x <= tolerance && d.y <= tolerance && d.z <= tolerance;
}

// Scene /////////////////////////////////////////////////////////////////////

using SetMaterial = std::function<void(anari::Device, anari::Surface)>;

SetMaterial matte(float3 color)
{
  return [=](anari::Device d, anari::Surface surface) {
    auto mat = anari::newObject<anari::Material>(d, "matte");
    anari::setParameter(d, mat, "color", color);
    anari::commitParameters(d, mat);
    anari::setAndReleaseParameter(d, surface, "material", mat);
  };
}

SetMaterial physicallyBased(
    float3 baseColor, float metallic, float roughness, float specular)
{
  return [=](anari::Device d, anari::Surface surface) {
    auto mat = anari::newObject<anari::Material>(d, "physicallyBased");
    anari::setParameter(d, mat, "baseColor", baseColor);
    anari::setParameter(d, mat, "metallic", metallic);
    anari::setParameter(d, mat, "roughness", roughness);
    anari::setParameter(d, mat, "specular", specular);
    anari::setParameter(d, mat, "ior", 1.5f);
    anari::commitParameters(d, mat);
    anari::setAndReleaseParameter(d, surface, "material", mat);
  };
}

// A quad with corners 'v' (counter-clockwise seen from its front), its
// material set by 'setMaterial' (none if empty)
anari::Surface makeQuad(anari::Device d,
    const std::vector<float3> &v,
    const SetMaterial &setMaterial)
{
  const std::vector<uint3> indices = {{0, 1, 2}, {0, 2, 3}};
  auto geom = anari::newObject<anari::Geometry>(d, "triangle");
  anari::setParameterArray1D(d, geom, "vertex.position", v.data(), v.size());
  anari::setParameterArray1D(
      d, geom, "primitive.index", indices.data(), indices.size());
  anari::commitParameters(d, geom);

  auto surface = anari::newObject<anari::Surface>(d);
  anari::setAndReleaseParameter(d, surface, "geometry", geom);
  anari::setParameter(d, surface, "id", 3u);
  if (setMaterial)
    setMaterial(d, surface);
  anari::commitParameters(d, surface);
  return surface;
}

anari::World makeWorld(anari::Device d, std::vector<anari::Surface> surfaces)
{
  auto world = anari::newObject<anari::World>(d);
  anari::setParameterArray1D(
      d, world, "surface", surfaces.data(), surfaces.size());
  anari::commitParameters(d, world);
  for (auto s : surfaces)
    anari::release(d, s);
  return world;
}

// The floor z = 0 over the left half of the frame (x in [-1, 0]), facing the
// camera; the right half misses
anari::World makeFloorWorld(anari::Device d, const SetMaterial &setMaterial)
{
  return makeWorld(d,
      {makeQuad(d,
          {{-1.f, -1.f, 0.f},
              {0.f, -1.f, 0.f},
              {0.f, 1.f, 0.f},
              {-1.f, 1.f, 0.f}},
          setMaterial)});
}

// A mirror z = y over the whole frame (its normal (0, -1, 1) / sqrt(2)),
// which turns the camera's rays to -y, onto a matte wall y = -3 facing +y
// that the camera doesn't see
anari::World makeMirrorWorld(anari::Device d)
{
  return makeWorld(d,
      {makeQuad(d,
           {{-1.f, -1.f, -1.f},
               {1.f, -1.f, -1.f},
               {1.f, 1.f, 1.f},
               {-1.f, 1.f, 1.f}},
           physicallyBased(float3(1.f), 1.f, 0.f, 1.f)),
          makeQuad(d,
              {{-2.f, -3.f, -2.f},
                  {-2.f, -3.f, 2.f},
                  {2.f, -3.f, 2.f},
                  {2.f, -3.f, -2.f}},
              matte(WALL_COLOR))});
}

// Orthographic camera at z = 3 looking down -z at the [-1, 1]^2 square
anari::Camera makeCamera(anari::Device d)
{
  auto camera = anari::newObject<anari::Camera>(d, "orthographic");
  anari::setParameter(d, camera, "position", float3(0.f, 0.f, 3.f));
  anari::setParameter(d, camera, "direction", float3(0.f, 0.f, -1.f));
  anari::setParameter(d, camera, "up", float3(0.f, 1.f, 0.f));
  anari::setParameter(d, camera, "height", 2.f);
  anari::setParameter(d, camera, "aspect", 1.f);
  anari::commitParameters(d, camera);
  return camera;
}

using SetParams = std::function<void(anari::Device, anari::Renderer)>;

SetParams withSamples(int pixelSamples)
{
  return [=](anari::Device d, anari::Renderer r) {
    anari::setParameter(d, r, "ambientRadiance", 1.f);
    anari::setParameter(d, r, "pixelSamples", pixelSamples);
  };
}

struct Channels
{
  ANARIDataType albedo{ANARI_UNKNOWN};
  ANARIDataType normal{ANARI_UNKNOWN};
  bool depthAndIds{false};
};

// A frame of 'world' through makeCamera() with a renderer of 'subtype' set up
// by 'setParams', a float color channel and 'channels'
struct Frame
{
  anari::Device d;
  anari::Frame frame;

  Frame(anari::Device d,
      anari::World world,
      const char *subtype,
      const SetParams &setParams,
      Channels channels,
      bool accumulation = false)
      : d(d)
  {
    auto renderer = anari::newObject<anari::Renderer>(d, subtype);
    if (setParams)
      setParams(d, renderer);
    anari::commitParameters(d, renderer);

    auto camera = makeCamera(d);
    frame = anari::newObject<anari::Frame>(d);
    anari::setParameter(d, frame, "size", IMAGE_SIZE);
    anari::setParameter(d, frame, "channel.color", ANARI_FLOAT32_VEC4);
    if (channels.albedo != ANARI_UNKNOWN)
      anari::setParameter(d, frame, "channel.albedo", channels.albedo);
    if (channels.normal != ANARI_UNKNOWN)
      anari::setParameter(d, frame, "channel.normal", channels.normal);
    if (channels.depthAndIds) {
      anari::setParameter(d, frame, "channel.depth", ANARI_FLOAT32);
      anari::setParameter(d, frame, "channel.primitiveId", ANARI_UINT32);
      anari::setParameter(d, frame, "channel.objectId", ANARI_UINT32);
      anari::setParameter(d, frame, "channel.instanceId", ANARI_UINT32);
    }
    anari::setParameter(d, frame, "accumulation", accumulation);
    anari::setAndReleaseParameter(d, frame, "camera", camera);
    anari::setAndReleaseParameter(d, frame, "renderer", renderer);
    anari::setParameter(d, frame, "world", world);
    anari::commitParameters(d, frame);
  }

  ~Frame()
  {
    anari::release(d, frame);
  }

  void render(int n = 1)
  {
    for (int i = 0; i < n; i++) {
      anari::render(d, frame);
      anari::wait(d, frame);
    }
  }

  // The channel's bytes, empty (with a FAIL line) unless it maps as 'type'
  std::vector<uint8_t> bytes(
      const char *name, ANARIDataType type, size_t pixelBytes)
  {
    std::vector<uint8_t> result;
    uint32_t width = 0, height = 0;
    ANARIDataType mapped = ANARI_UNKNOWN;
    auto *data = (const uint8_t *)anariMapFrame(
        d, frame, name, &width, &height, &mapped);
    if (data && mapped == type && width == IMAGE_SIZE.x
        && height == IMAGE_SIZE.y)
      result.assign(data, data + size_t(width) * height * pixelBytes);
    else
      std::printf("FAIL map %s\n", name);
    anariUnmapFrame(d, frame, name);
    return result;
  }

  // A FLOAT32_VEC3 channel
  std::vector<float3> vec3(const char *name)
  {
    const auto b = bytes(name, ANARI_FLOAT32_VEC3, sizeof(float3));
    std::vector<float3> result(b.size() / sizeof(float3));
    if (!b.empty())
      std::memcpy(result.data(), b.data(), b.size());
    return result;
  }

  template <typename T>
  std::vector<T> channel(const char *name)
  {
    std::vector<T> pixels;
    auto fb = anari::map<T>(d, frame, name);
    if (fb.data)
      pixels.assign(fb.data, fb.data + fb.width * fb.height);
    anari::unmap(d, frame, name);
    return pixels;
  }
};

size_t pixelIndex(float2 pos)
{
  const auto x = uint32_t((pos.x + 1.f) / 2.f * IMAGE_SIZE.x);
  const auto y = uint32_t((pos.y + 1.f) / 2.f * IMAGE_SIZE.y);
  return size_t(y) * IMAGE_SIZE.x + x;
}

const float2 FLOOR_POS = {-0.5f, 0.f};
const float2 MISS_POS = {0.5f, 0.f};

const char *const RENDERERS[] = {"preview", "quality"};

// Tests /////////////////////////////////////////////////////////////////////

bool testIntrospection(anari::Library library)
{
  bool albedo = false;
  bool normal = false;
  if (auto *extensions = anariGetDeviceExtensions(library, "default")) {
    for (int i = 0; extensions[i]; i++) {
      albedo |=
          std::strcmp(extensions[i], "ANARI_KHR_FRAME_CHANNEL_ALBEDO") == 0;
      normal |=
          std::strcmp(extensions[i], "ANARI_KHR_FRAME_CHANNEL_NORMAL") == 0;
    }
  }
  bool pass = report("KHR_FRAME_CHANNEL_ALBEDO declared", albedo);
  pass &= report("KHR_FRAME_CHANNEL_NORMAL declared", normal);
  return pass;
}

// Both renderers: the floor's albedo is its matte colour and its normal +z;
// misses are 0 in both channels; a surface without a material has the
// device's invalid material colour
bool testFloor(anari::Device d)
{
  bool pass = true;
  auto world = makeFloorWorld(d, matte(FLOOR_COLOR));
  auto invalid = makeFloorWorld(d, {});
  const Channels channels{ANARI_FLOAT32_VEC3, ANARI_FLOAT32_VEC3};

  for (const char *subtype : RENDERERS) {
    const std::string r = subtype;
    {
      Frame f(d, world, subtype, withSamples(4), channels);
      f.render();
      const auto albedo = f.vec3("channel.albedo");
      const auto normal = f.vec3("channel.normal");
      if (albedo.empty() || normal.empty())
        return false;
      const float3 a = albedo[pixelIndex(FLOOR_POS)];
      const float3 n = normal[pixelIndex(FLOOR_POS)];
      pass &= report(r + ": floor albedo is its matte colour",
          near(a, FLOOR_COLOR, 1e-6f),
          str(a));
      pass &= report(r + ": floor normal is +z",
          near(n, float3(0.f, 0.f, 1.f), 1e-6f),
          str(n));
      pass &= report(r + ": misses are 0",
          near(albedo[pixelIndex(MISS_POS)], float3(0.f), 0.f)
              && near(normal[pixelIndex(MISS_POS)], float3(0.f), 0.f));
    }
    {
      Frame f(d, invalid, subtype, withSamples(1), channels);
      f.render();
      const auto albedo = f.vec3("channel.albedo");
      pass &= report(r + ": no material: the invalid material colour",
          !albedo.empty()
              && near(albedo[pixelIndex(FLOOR_POS)], INVALID_COLOR, 0.f));
    }
  }

  // the albedo describes the material, not its lighting
  {
    Frame f(d, world, "quality", {}, channels);
    f.render();
    const auto albedo = f.vec3("channel.albedo");
    const auto color = f.channel<float4>("channel.color");
    pass &= report("pathtracer: unlit floor (ambientRadiance 0) has its albedo",
        !albedo.empty() && !color.empty()
            && near(albedo[pixelIndex(FLOOR_POS)], FLOOR_COLOR, 1e-6f)
            && color[pixelIndex(FLOOR_POS)].x == 0.f);
  }

  anari::release(d, world);
  anari::release(d, invalid);
  return pass;
}

// A usdPreviewSurface of 'diffuseColor', 'metallic' 0, 'roughness' 0.5,
// 'ior' 1.5
SetMaterial usdPreviewSurface(float3 diffuseColor)
{
  return [=](anari::Device d, anari::Surface surface) {
    auto mat = anari::newObject<anari::Material>(d, "usdPreviewSurface");
    anari::setParameter(d, mat, "diffuseColor", diffuseColor);
    anari::setParameter(d, mat, "metallic", 0.f);
    anari::setParameter(d, mat, "roughness", 0.5f);
    anari::setParameter(d, mat, "ior", 1.5f);
    anari::commitParameters(d, mat);
    anari::setAndReleaseParameter(d, surface, "material", mat);
  };
}

// The pathtracer's albedo of a closure: its summary. physicallyBased (glTF
// PBR) layers a dielectric reflection over the base colour, or is a metal of
// the base colour; usdPreviewSurface adds its specular reflection to its
// diffuse lobe.
bool testClosureAlbedo(anari::Device d)
{
  bool pass = true;
  const float3 base = {0.6f, 0.3f, 0.1f};
  // ((1.5 - 1) / (1.5 + 1))^2
  constexpr float F0 = 0.04f;

  struct Case
  {
    const char *name;
    SetMaterial material;
    float3 expected;
  };
  const Case cases[] = {
      {"physicallyBased dielectric without specular: the base colour",
          physicallyBased(base, 0.f, 0.5f, 0.f),
          base},
      {"physicallyBased dielectric with specular 1: F0 + (1 - F0) * base",
          physicallyBased(base, 0.f, 0.5f, 1.f),
          float3(F0) + (1.f - F0) * base},
      {"physicallyBased rough metal: the base colour",
          physicallyBased(base, 1.f, 0.5f, 1.f),
          base},
      {"usdPreviewSurface: diffuse + F0",
          usdPreviewSurface(base),
          base + float3(F0)},
  };
  for (const Case &c : cases) {
    auto world = makeFloorWorld(d, c.material);
    Frame f(d, world, "quality", withSamples(2), {ANARI_FLOAT32_VEC3});
    f.render();
    const auto albedo = f.vec3("channel.albedo");
    const float3 a =
        albedo.empty() ? float3(-1.f) : albedo[pixelIndex(FLOOR_POS)];
    pass &= report(std::string("albedo of ") + c.name,
        near(a, c.expected, 1e-5f),
        str(a) + " expected " + str(c.expected));
    anari::release(d, world);
  }
  return pass;
}

// The pathtracer's channels describe the first non-specular hit: through a
// perfect mirror (reflectance 1), the wall it shows, with the wall's normal.
// The 'preview' renderer's describe the mirror.
bool testMirror(anari::Device d)
{
  bool pass = true;
  auto world = makeMirrorWorld(d);
  const Channels channels{ANARI_FLOAT32_VEC3, ANARI_FLOAT32_VEC3};
  const float2 pos = {0.f, 0.f};

  {
    Frame f(d, world, "quality", withSamples(4), channels);
    f.render();
    const auto albedo = f.vec3("channel.albedo");
    const auto normal = f.vec3("channel.normal");
    if (albedo.empty() || normal.empty())
      return false;
    pass &= report("pathtracer: albedo through a mirror is the wall's",
        near(albedo[pixelIndex(pos)], WALL_COLOR, 1e-5f),
        str(albedo[pixelIndex(pos)]));
    pass &= report("pathtracer: normal through a mirror is the wall's",
        near(normal[pixelIndex(pos)], float3(0.f, 1.f, 0.f), 1e-5f),
        str(normal[pixelIndex(pos)]));
  }
  {
    Frame f(d, world, "default", {}, channels);
    f.render();
    const auto albedo = f.vec3("channel.albedo");
    const auto normal = f.vec3("channel.normal");
    if (albedo.empty() || normal.empty())
      return false;
    const float s = 1.f / std::sqrt(2.f);
    pass &= report("default: albedo is the mirror's colour",
        near(albedo[pixelIndex(pos)], float3(1.f), 1e-6f),
        str(albedo[pixelIndex(pos)]));
    pass &= report("default: normal is the mirror's",
        near(normal[pixelIndex(pos)], float3(0.f, -s, s), 1e-6f),
        str(normal[pixelIndex(pos)]));
  }
  // negative components in FIXED16_VEC3, after a frame commit changes the
  // channel's type
  {
    Frame f(d, world, "default", {}, channels);
    f.render();
    anari::setParameter(d, f.frame, "channel.normal", ANARI_FIXED16_VEC3);
    anari::commitParameters(d, f.frame);
    f.render();
    const auto normal =
        f.bytes("channel.normal", ANARI_FIXED16_VEC3, 3 * sizeof(int16_t));
    int16_t n[3] = {0, 0, 0};
    if (!normal.empty()) {
      std::memcpy(
          n, normal.data() + 3 * sizeof(int16_t) * pixelIndex(pos), sizeof(n));
    }
    pass &= report("default: FIXED16_VEC3 mirror normal after a recommit",
        n[0] == 0 && n[1] == -23170 && n[2] == 23170);
  }
  // the pathtracer's debug modes trace paths for the channels alone
  {
    Frame f(
        d,
        world,
        "quality",
        [](anari::Device d, anari::Renderer r) {
          anari::setParameter(d, r, "mode", "normal");
        },
        channels);
    f.render();
    const auto albedo = f.vec3("channel.albedo");
    pass &= report("pathtracer debug mode: albedo through a mirror",
        !albedo.empty() && near(albedo[pixelIndex(pos)], WALL_COLOR, 1e-5f));
  }

  anari::release(d, world);
  return pass;
}

// The pathtracer's channels are means over its samples, which accumulate
// like the colour: N renders of 1 sample equal 1 render of N, at pixels
// where the floor's edge mixes floor and miss
bool testAccumulation(anari::Device d)
{
  constexpr int N = 8;
  auto world = makeFloorWorld(d, matte(FLOOR_COLOR));
  const Channels channels{ANARI_FLOAT32_VEC3, ANARI_FLOAT32_VEC3};

  Frame many(d, world, "quality", withSamples(1), channels, true);
  many.render(N);
  Frame once(d, world, "quality", withSamples(N), channels, true);
  once.render();
  const auto a = many.vec3("channel.albedo");
  const auto b = once.vec3("channel.albedo");
  const auto na = many.vec3("channel.normal");
  const auto nb = once.vec3("channel.normal");

  bool same = !a.empty() && a.size() == b.size() && na.size() == nb.size();
  for (size_t i = 0; same && i < a.size(); i++)
    same = near(a[i], b[i], 1e-6f) && near(na[i], nb[i], 1e-6f);

  // the pixel column the edge x = 0 crosses is partly floor
  const size_t edge = (IMAGE_SIZE.y / 2) * IMAGE_SIZE.x + IMAGE_SIZE.x / 2;
  const bool mixed = !a.empty() && a[edge].x > 0.f && a[edge].x < FLOOR_COLOR.x
      && na[edge].z > 0.f && na[edge].z < 1.f;

  bool pass = report("N renders of 1 sample == 1 render of N samples", same);
  pass &= report("edge pixels average floor and miss",
      mixed,
      a.empty() ? "" : str(a[edge]));
  anari::release(d, world);
  return pass;
}

// Every pixel type the extensions allow, in both renderers
bool testPixelTypes(anari::Device d)
{
  bool pass = true;
  auto world = makeFloorWorld(d, matte(FLOOR_COLOR));
  const size_t i = pixelIndex(FLOOR_POS);
  const auto code = [](float v) { return uint8_t(255.f * v + 0.5f); };
  const auto srgb = [&](float v) {
    return code(v <= 0.0031308f ? 12.92f * v
                                : 1.055f * std::pow(v, 1.f / 2.4f) - 0.055f);
  };

  for (const char *subtype : RENDERERS) {
    const std::string r = subtype;
    {
      Frame f(d,
          world,
          subtype,
          withSamples(1),
          {ANARI_UFIXED8_VEC3, ANARI_FIXED16_VEC3});
      f.render();
      const auto albedo = f.bytes("channel.albedo", ANARI_UFIXED8_VEC3, 3);
      const auto normal =
          f.bytes("channel.normal", ANARI_FIXED16_VEC3, 3 * sizeof(int16_t));
      pass &= report(r + ": UFIXED8_VEC3 albedo",
          !albedo.empty() && albedo[3 * i] == code(FLOOR_COLOR.x)
              && albedo[3 * i + 1] == code(FLOOR_COLOR.y)
              && albedo[3 * i + 2] == code(FLOOR_COLOR.z));
      int16_t n[3] = {-1, -1, -1};
      if (!normal.empty())
        std::memcpy(n, normal.data() + 3 * sizeof(int16_t) * i, sizeof(n));
      pass &= report(
          r + ": FIXED16_VEC3 normal", n[0] == 0 && n[1] == 0 && n[2] == 32767);
    }
    {
      Frame f(d, world, subtype, withSamples(1), {ANARI_UFIXED8_RGB_SRGB});
      f.render();
      const auto albedo = f.bytes("channel.albedo", ANARI_UFIXED8_RGB_SRGB, 3);
      pass &= report(r + ": UFIXED8_RGB_SRGB albedo",
          !albedo.empty() && albedo[3 * i] == srgb(FLOOR_COLOR.x)
              && albedo[3 * i + 1] == srgb(FLOOR_COLOR.y)
              && albedo[3 * i + 2] == srgb(FLOOR_COLOR.z));
    }
  }

  anari::release(d, world);
  return pass;
}

// Asking for the albedo and normal changes none of the other channels
bool testOtherChannelsUnchanged(anari::Device d)
{
  bool pass = true;
  auto world = makeMirrorWorld(d);
  for (const char *subtype : RENDERERS) {
    Frame with(d,
        world,
        subtype,
        withSamples(2),
        {ANARI_FLOAT32_VEC3, ANARI_FLOAT32_VEC3, true});
    Frame without(d,
        world,
        subtype,
        withSamples(2),
        {ANARI_UNKNOWN, ANARI_UNKNOWN, true});
    with.render();
    without.render();
    const bool same = with.channel<float4>("channel.color")
            == without.channel<float4>("channel.color")
        && with.channel<float>("channel.depth")
            == without.channel<float>("channel.depth")
        && with.channel<uint32_t>("channel.primitiveId")
            == without.channel<uint32_t>("channel.primitiveId")
        && with.channel<uint32_t>("channel.objectId")
            == without.channel<uint32_t>("channel.objectId")
        && with.channel<uint32_t>("channel.instanceId")
            == without.channel<uint32_t>("channel.instanceId");
    pass &= report(std::string(subtype) + ": other channels unchanged", same);

    uint32_t w = 1, h = 1;
    ANARIDataType type = ANARI_FLOAT32;
    const void *unmapped =
        anariMapFrame(d, without.frame, "channel.albedo", &w, &h, &type);
    anariUnmapFrame(d, without.frame, "channel.albedo");
    pass &= report(std::string(subtype) + ": albedo not mapped unless asked",
        unmapped == nullptr && type == ANARI_UNKNOWN);
  }
  anari::release(d, world);
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

  bool pass = testIntrospection(library);
  pass &= testFloor(d);
  pass &= testClosureAlbedo(d);
  pass &= testMirror(d);
  pass &= testAccumulation(d);
  pass &= testPixelTypes(d);
  pass &= testOtherChannelsUnchanged(d);

  anari::release(d, d);
  anari::unloadLibrary(library);

  pass &= report("no errors", g_errors == 0);
  // the surface without a material's
  pass &= report("only the missing material warning", g_warnings == 1);
  return pass ? 0 : 1;
}
