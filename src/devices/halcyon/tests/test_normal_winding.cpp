// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Shading normals against the winding: both renderers
// turn the interpolated shading normal into the geometric normal's hemisphere
// at every hit, as Typhoon does, so a mesh wound against its normals renders
// as one wound with them.
// - Negating a mesh's normals changes nothing, bitwise: triangle and quad
//   meshes, 'vertex.normal' and 'faceVarying.normal', both windings; in the
//   'preview' renderer's modes that read normals, its albedo and normal
//   channels and the 'worldNormal'/'objectNormal' attributes (through a
//   sampler), and in the 'quality''s path tracing (native matte and
//   physicallyBased, with a normal map, and openPBR through its graph, coated
//   and anisotropic), its debug modes of the shading context and its albedo
//   and normal channels.
// - A mesh wound against its normals renders as the same mesh wound with them
//   (within rounding; the hits' coordinates differ): its shading normals point
//   the other way, to its winding's side, and what doesn't depend on that is
//   alike, the path-traced image of opaque materials among it (where the
//   normals were left unturned, it rendered black).
// - Also under a mirroring instance (scale (-1, 1, 1)), where the normals
//   still land on Ng's side, and with authored tangents when their
//   handedness w is negated with the normals.
// - The winding, not the normals, decides a thick surface's inside: glass
//   over a checkerboard renders alike with its normals negated, otherwise in
//   the other winding (well beyond the noise between seeds).
// - The turn is decided in world space, as Typhoon decides it: under an
//   instance's non-uniform scale, a normal that makes an acute angle with Ng
//   in object space but an obtuse one in world space is turned, in both
//   renderers' normals and in the 'objectNormal' attribute.
// - The 'preview' renderer's 'Ns.flipped' mode shows where the normal was
//   turned (red), kept (green) or absent (blue).

// anari
#define ANARI_LINALG_DEFINITIONS
#include <anari/anari_cpp/ext/linalg.h>
#include <anari/anari_cpp.hpp>
// std
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace {

using namespace anari::math;

// not a multiple of any ISPC program count, to exercise partial SIMD chunks
constexpr uint2 IMAGE_SIZE = {37, 33};

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
  char buf[96];
  std::snprintf(buf, sizeof(buf), "(%.6f, %.6f, %.6f)", v.x, v.y, v.z);
  return buf;
}

// Mesh //////////////////////////////////////////////////////////////////////

enum class Subtype
{
  TRIANGLE,
  QUAD
};

enum class Source
{
  VERTEX,
  FACE_VARYING
};

// A curved height field z = h(x, y) over [-0.9, 0.9]^2, GRID x GRID cells,
// with its smooth normals, texture coordinates (x, y) in 'attribute0' and a
// colour per vertex
constexpr int GRID = 6;

float height(float x, float y)
{
  return 0.25f * std::sin(2.f * x) * std::cos(1.5f * y);
}

float3 heightNormal(float x, float y)
{
  const float dhdx = 0.5f * std::cos(2.f * x) * std::cos(1.5f * y);
  const float dhdy = -0.375f * std::sin(2.f * x) * std::sin(1.5f * y);
  return linalg::normalize(float3(-dhdx, -dhdy, 1.f));
}

struct MeshSetup
{
  Subtype subtype;
  Source source;
  // false: counter-clockwise seen from +z (the side the normals point to);
  // true: the other way round
  bool reversed;
  // the normals negated (and the authored tangents' handedness w with them)
  bool negated;
  // authored 'vertex.tangent' (FLOAT32_VEC4): its bitangent sign(w) x N x
  // tangent follows the authored normal, so negating the normals alone
  // mirrors it; negating w too keeps it
  bool tangents = false;
};

std::string name(const MeshSetup &m)
{
  std::string s = m.subtype == Subtype::TRIANGLE ? "triangle" : "quad";
  s += m.source == Source::VERTEX ? ", vertex.normal" : ", faceVarying.normal";
  s += m.reversed ? ", reversed winding" : ", winding";
  if (m.tangents)
    s += ", vertex.tangent";
  return s;
}

anari::Geometry makeMesh(anari::Device d, const MeshSetup &m)
{
  std::vector<float3> positions;
  std::vector<float3> normals;
  std::vector<float2> texcoords;
  std::vector<float4> colors;
  auto vertex = [](int i, int j) { return j * (GRID + 1) + i; };
  for (int j = 0; j <= GRID; j++) {
    for (int i = 0; i <= GRID; i++) {
      const float x = -0.9f + 1.8f * i / GRID;
      const float y = -0.9f + 1.8f * j / GRID;
      positions.push_back(float3(x, y, height(x, y)));
      normals.push_back(heightNormal(x, y));
      texcoords.push_back(float2(x, y));
      colors.push_back(float4(0.5f + 0.4f * x, 0.6f, 0.5f - 0.4f * y, 1.f));
    }
  }

  // primitives as corner lists, counter-clockwise seen from +z unless
  // reversed
  std::vector<std::vector<int>> prims;
  for (int j = 0; j < GRID; j++) {
    for (int i = 0; i < GRID; i++) {
      const int a = vertex(i, j), b = vertex(i + 1, j),
                c = vertex(i + 1, j + 1), e = vertex(i, j + 1);
      if (m.subtype == Subtype::TRIANGLE) {
        prims.push_back({a, b, c});
        prims.push_back({a, c, e});
      } else {
        prims.push_back({a, b, c, e});
      }
    }
  }
  if (m.reversed) {
    for (auto &p : prims)
      std::reverse(p.begin() + 1, p.end());
  }

  const float sign = m.negated ? -1.f : 1.f;
  auto geom = anari::newObject<anari::Geometry>(
      d, m.subtype == Subtype::TRIANGLE ? "triangle" : "quad");
  anari::setParameterArray1D(
      d, geom, "vertex.position", positions.data(), positions.size());
  anari::setParameterArray1D(
      d, geom, "vertex.attribute0", texcoords.data(), texcoords.size());
  anari::setParameterArray1D(
      d, geom, "vertex.color", colors.data(), colors.size());
  if (m.source == Source::VERTEX) {
    std::vector<float3> n;
    for (const float3 &v : normals)
      n.push_back(v * sign);
    anari::setParameterArray1D(d, geom, "vertex.normal", n.data(), n.size());
  } else {
    // per corner: the vertex normal tilted by a different amount on each
    // primitive, so that the normals are discontinuous across edges
    std::vector<float3> n;
    for (size_t p = 0; p < prims.size(); p++) {
      const float3 tilt(0.15f * std::sin(1.7f * p), 0.1f * std::cos(2.3f * p),
          0.f);
      for (int corner : prims[p])
        n.push_back(linalg::normalize(normals[corner] + tilt) * sign);
    }
    anari::setParameterArray1D(
        d, geom, "faceVarying.normal", n.data(), n.size());
  }
  if (m.tangents) {
    // along +x on the height field, handedness +1 (bitangent N x T along +y)
    std::vector<float4> t;
    for (const float3 &p : positions) {
      const float dhdx = 0.5f * std::cos(2.f * p.x) * std::cos(1.5f * p.y);
      const float3 tx = linalg::normalize(float3(1.f, 0.f, dhdx));
      t.push_back(float4(tx, sign));
    }
    anari::setParameterArray1D(d, geom, "vertex.tangent", t.data(), t.size());
  }
  if (m.subtype == Subtype::TRIANGLE) {
    std::vector<uint3> indices;
    for (const auto &p : prims)
      indices.push_back(uint3(p[0], p[1], p[2]));
    anari::setParameterArray1D(
        d, geom, "primitive.index", indices.data(), indices.size());
  } else {
    std::vector<uint4> indices;
    for (const auto &p : prims)
      indices.push_back(uint4(p[0], p[1], p[2], p[3]));
    anari::setParameterArray1D(
        d, geom, "primitive.index", indices.data(), indices.size());
  }
  anari::commitParameters(d, geom);
  return geom;
}

// Materials /////////////////////////////////////////////////////////////////

using MakeMaterial = std::function<anari::Material(anari::Device)>;

anari::Material matte(anari::Device d)
{
  auto m = anari::newObject<anari::Material>(d, "matte");
  anari::setParameter(d, m, "color", float3(0.7f, 0.6f, 0.5f));
  anari::commitParameters(d, m);
  return m;
}

// A matte whose colour is the ray-derived attribute 'attribute' (a normal)
// mapped from [-1, 1] to [0, 1] by a transform sampler
MakeMaterial matteOfAttribute(const char *attribute)
{
  return [=](anari::Device d) {
    auto s = anari::newObject<anari::Sampler>(d, "transform");
    anari::setParameter(d, s, "inAttribute", attribute);
    anari::setParameter(d, s, "outTransform", mat4(linalg::scaling_matrix(
        float3(0.5f))));
    anari::setParameter(d, s, "outOffset", float4(0.5f, 0.5f, 0.5f, 0.f));
    anari::commitParameters(d, s);
    auto m = anari::newObject<anari::Material>(d, "matte");
    anari::setAndReleaseParameter(d, m, "color", s);
    anari::commitParameters(d, m);
    return m;
  };
}

anari::Material physicallyBased(anari::Device d)
{
  auto m = anari::newObject<anari::Material>(d, "physicallyBased");
  anari::setParameter(d, m, "baseColor", float3(0.8f, 0.5f, 0.3f));
  anari::setParameter(d, m, "metallic", 0.5f);
  anari::setParameter(d, m, "roughness", 0.3f);
  anari::setParameter(d, m, "specular", 1.f);
  anari::commitParameters(d, m);
  return m;
}

// physicallyBased with a tangent-space normal map over the texture
// coordinates, and a clearcoat
anari::Material physicallyBasedNormalMap(anari::Device d)
{
  std::vector<float3> texels;
  for (int j = 0; j < 4; j++) {
    for (int i = 0; i < 4; i++) {
      const float3 n(
          0.4f * std::sin(1.3f * i + 0.7f * j), 0.3f * std::cos(2.1f * j), 1.f);
      texels.push_back(linalg::normalize(n));
    }
  }
  auto s = anari::newObject<anari::Sampler>(d, "image2D");
  anari::setParameterArray2D(d, s, "image", texels.data(), 4, 4);
  anari::setParameter(d, s, "inAttribute", "attribute0");
  anari::setParameter(d, s, "filter", "linear");
  anari::setParameter(d, s, "wrapMode1", "repeat");
  anari::setParameter(d, s, "wrapMode2", "repeat");
  anari::commitParameters(d, s);

  auto m = anari::newObject<anari::Material>(d, "physicallyBased");
  anari::setParameter(d, m, "baseColor", float3(0.4f, 0.6f, 0.8f));
  anari::setParameter(d, m, "metallic", 0.f);
  anari::setParameter(d, m, "roughness", 0.4f);
  anari::setParameter(d, m, "specular", 1.f);
  anari::setParameter(d, m, "clearcoat", 0.5f);
  anari::setAndReleaseParameter(d, m, "normal", s);
  anari::commitParameters(d, m);
  return m;
}

// openPBR through its MaterialXCpp graph: coated, anisotropic specular, so
// the tangent frame and the coat's normal matter
anari::Material openPbr(anari::Device d)
{
  auto m = anari::newObject<anari::Material>(d, "openPBR");
  anari::setParameter(d, m, "base_color", float3(0.6f, 0.7f, 0.4f));
  anari::setParameter(d, m, "specular_roughness", 0.35f);
  anari::setParameter(d, m, "specular_roughness_anisotropy", 0.6f);
  anari::setParameter(d, m, "coat_weight", 0.5f);
  anari::setParameter(d, m, "coat_roughness", 0.2f);
  anari::commitParameters(d, m);
  return m;
}

// Scene and frames //////////////////////////////////////////////////////////

anari::World makeWorld(anari::Device d,
    anari::Geometry geom,
    anari::Material mat,
    const mat4 &transform = linalg::identity)
{
  auto surface = anari::newObject<anari::Surface>(d);
  anari::setParameter(d, surface, "geometry", geom);
  anari::setParameter(d, surface, "material", mat);
  anari::commitParameters(d, surface);
  auto group = anari::newObject<anari::Group>(d);
  anari::setParameterArray1D(d, group, "surface", &surface, 1);
  anari::commitParameters(d, group);
  auto instance = anari::newObject<anari::Instance>(d, "transform");
  anari::setParameter(d, instance, "transform", transform);
  anari::setAndReleaseParameter(d, instance, "group", group);
  anari::commitParameters(d, instance);

  auto light = anari::newObject<anari::Light>(d, "directional");
  anari::setParameter(
      d, light, "direction", linalg::normalize(float3(-0.4f, 0.3f, -1.f)));
  anari::setParameter(d, light, "irradiance", 2.f);
  anari::commitParameters(d, light);

  auto world = anari::newObject<anari::World>(d);
  anari::setParameterArray1D(d, world, "instance", &instance, 1);
  anari::setParameterArray1D(d, world, "light", &light, 1);
  anari::commitParameters(d, world);
  anari::release(d, surface);
  anari::release(d, instance);
  anari::release(d, light);
  return world;
}

struct View
{
  float3 position;
  float3 direction;
  float3 up;
};

// Perspective camera looking at the height field from above, off its axis
const View MESH_VIEW = {float3(0.4f, -0.9f, 2.6f),
    linalg::normalize(float3(-0.4f, 0.9f, -2.6f)),
    float3(0.f, 0.f, 1.f)};

struct Images
{
  std::vector<float4> color;
  std::vector<float3> albedo;
  std::vector<float3> normal;
};

using SetParams = std::function<void(anari::Device, anari::Renderer)>;

Images render(anari::Device d,
    anari::World world,
    const char *subtype,
    const char *mode,
    const View &view,
    const SetParams &setParams = {})
{
  auto camera = anari::newObject<anari::Camera>(d, "perspective");
  anari::setParameter(d, camera, "position", view.position);
  anari::setParameter(d, camera, "direction", view.direction);
  anari::setParameter(d, camera, "up", view.up);
  anari::setParameter(d, camera, "aspect", float(IMAGE_SIZE.x) / IMAGE_SIZE.y);
  anari::commitParameters(d, camera);

  auto renderer = anari::newObject<anari::Renderer>(d, subtype);
  anari::setParameter(d, renderer, "mode", mode);
  if (std::strcmp(subtype, "quality") == 0) {
    anari::setParameter(d, renderer, "pixelSamples", 4);
    anari::setParameter(d, renderer, "maxBounces", 4);
    anari::setParameter(d, renderer, "ambientRadiance", 0.3f);
  }
  if (setParams)
    setParams(d, renderer);
  anari::commitParameters(d, renderer);

  auto frame = anari::newObject<anari::Frame>(d);
  anari::setParameter(d, frame, "size", IMAGE_SIZE);
  anari::setParameter(d, frame, "channel.color", ANARI_FLOAT32_VEC4);
  anari::setParameter(d, frame, "channel.albedo", ANARI_FLOAT32_VEC3);
  anari::setParameter(d, frame, "channel.normal", ANARI_FLOAT32_VEC3);
  anari::setAndReleaseParameter(d, frame, "camera", camera);
  anari::setAndReleaseParameter(d, frame, "renderer", renderer);
  anari::setParameter(d, frame, "world", world);
  anari::commitParameters(d, frame);
  anari::render(d, frame);
  anari::wait(d, frame);

  Images images;
  const size_t n = size_t(IMAGE_SIZE.x) * IMAGE_SIZE.y;
  auto color = anari::map<float4>(d, frame, "channel.color");
  if (color.data)
    images.color.assign(color.data, color.data + n);
  anari::unmap(d, frame, "channel.color");
  auto albedo = anari::map<float3>(d, frame, "channel.albedo");
  if (albedo.data)
    images.albedo.assign(albedo.data, albedo.data + n);
  anari::unmap(d, frame, "channel.albedo");
  auto normal = anari::map<float3>(d, frame, "channel.normal");
  if (normal.data)
    images.normal.assign(normal.data, normal.data + n);
  anari::unmap(d, frame, "channel.normal");
  anari::release(d, frame);
  return images;
}

template <typename T>
bool bitwise(const std::vector<T> &a, const std::vector<T> &b)
{
  return !a.empty() && a.size() == b.size()
      && std::memcmp(a.data(), b.data(), a.size() * sizeof(T)) == 0;
}

// The largest difference of any component
template <typename T>
float maxDifference(const std::vector<T> &a, const std::vector<T> &b)
{
  if (a.empty() || a.size() != b.size())
    return INFINITY;
  constexpr size_t N = sizeof(T) / sizeof(float);
  float m = 0.f;
  for (size_t i = 0; i < a.size(); i++) {
    const float *pa = (const float *)&a[i];
    const float *pb = (const float *)&b[i];
    for (size_t k = 0; k < N; k++)
      m = std::max(m, std::fabs(pa[k] - pb[k]));
  }
  return m;
}

float4 mean(const std::vector<float4> &v)
{
  float4 m(0.f);
  for (const float4 &c : v)
    m += c;
  return v.empty() ? m : m / float(v.size());
}

struct Case
{
  const char *renderer;
  const char *mode;
  const char *material;
  MakeMaterial makeMaterial;
};

std::vector<Case> cases()
{
  std::vector<Case> c;
  for (const char *mode : {"default",
           "Ns",
           "Ns.abs",
           "Ng",
           "backface",
           "opacityHeatmap",
           "ambientOcclusion",
           "ambientOcclusion.unblurred"})
    c.push_back({"default", mode, "matte", matte});
  c.push_back(
      {"default", "default", "worldNormal", matteOfAttribute("worldNormal")});
  c.push_back(
      {"default", "default", "objectNormal", matteOfAttribute("objectNormal")});

  for (const char *mode : {"normal",
           "tangent",
           "bitangent",
           "dPdu",
           "dPdv",
           "dndu",
           "dndv",
           "texcoordDerivatives"})
    c.push_back({"quality", mode, "matte", matte});
  c.push_back({"quality", "default", "matte", matte});
  c.push_back(
      {"quality", "default", "physicallyBased", physicallyBased});
  c.push_back({"quality",
      "default",
      "physicallyBased with a normal map",
      physicallyBasedNormalMap});
  c.push_back({"quality", "default", "openPBR", openPbr});
  c.push_back({"quality",
      "default",
      "worldNormal",
      matteOfAttribute("worldNormal")});
  c.push_back({"quality",
      "default",
      "objectNormal",
      matteOfAttribute("objectNormal")});
  return c;
}

std::string caseName(const Case &c, const MeshSetup &m)
{
  return std::string(c.renderer) + " '" + c.mode + "', " + c.material + ", "
      + name(m);
}

// Negating the normals changes nothing: in either winding, one of the two
// meshes is wound against its normals, and turning them gives the other's
Images renderMesh(anari::Device d,
    const Case &c,
    const MeshSetup &m,
    const View &view,
    const mat4 &transform = linalg::identity)
{
  auto geom = makeMesh(d, m);
  auto mat = c.makeMaterial(d);
  auto world = makeWorld(d, geom, mat, transform);
  Images images = render(d, world, c.renderer, c.mode, view);
  anari::release(d, world);
  anari::release(d, mat);
  anari::release(d, geom);
  return images;
}

// 'what' names the variant ('transform' on the instance, 'tangents' authored)
bool testNegation(anari::Device d,
    const std::vector<Case> &cases,
    const std::string &what = {},
    const mat4 &transform = linalg::identity,
    bool tangents = false)
{
  bool pass = true;
  for (const Case &c : cases) {
    for (Subtype subtype : {Subtype::TRIANGLE, Subtype::QUAD}) {
      for (Source source : {Source::VERTEX, Source::FACE_VARYING}) {
        for (bool reversed : {false, true}) {
          MeshSetup m{subtype, source, reversed, false};
          m.tangents = tangents;
          MeshSetup negated = m;
          negated.negated = true;
          const Images a = renderMesh(d, c, m, MESH_VIEW, transform);
          const Images b = renderMesh(d, c, negated, MESH_VIEW, transform);
          const bool same = bitwise(a.color, b.color)
              && bitwise(a.albedo, b.albedo) && bitwise(a.normal, b.normal);
          char detail[160];
          std::snprintf(detail,
              sizeof(detail),
              "colour %g, albedo %g, normal %g apart",
              maxDifference(a.color, b.color),
              maxDifference(a.albedo, b.albedo),
              maxDifference(a.normal, b.normal));
          pass &= report("negated normals render bitwise alike"
                  + (what.empty() ? "" : " (" + what + ")") + ": "
                  + caseName(c, m),
              same,
              same ? "" : detail);
        }
      }
    }
  }
  return pass;
}

// Negated three-component colours (the raw normals the 'Ns' and 'normal'
// modes show); alpha kept
std::vector<float4> negated(std::vector<float4> v)
{
  for (float4 &c : v)
    c = float4(-c.x, -c.y, -c.z, c.w);
  return v;
}

std::vector<float3> negated(std::vector<float3> v)
{
  for (float3 &c : v)
    c = -c;
  return v;
}

bool is(const char *a, const char *b)
{
  return std::strcmp(a, b) == 0;
}

// A mesh wound against its normals renders as the same mesh wound with them,
// within rounding (the hits' coordinates differ): its normals turned, so
// pointing the other way, and what doesn't depend on that alike, the
// path-traced image of opaque materials among it
bool testWinding(anari::Device d)
{
  bool pass = true;
  for (const Case &c : cases()) {
    const bool pathtracer = is(c.renderer, "quality");
    const bool pathTraced = pathtracer && is(c.mode, "default");
    // colours alike, negated (the raw normal), or left out: Ng and the side
    // seen are the winding's own; the attributes (shown as colours) are the
    // turned normals; the surface parameterization (derivatives and the
    // tangent frame) follows Embree's hit coordinates, which the winding
    // changes; and a normal map's relief, in a tangent frame around the
    // turned normal, is inverted, as in Typhoon
    enum
    {
      SAME,
      NEGATED,
      NONE
    } colour = SAME;
    if (is(c.mode, "Ns") || is(c.mode, "normal"))
      colour = NEGATED;
    else if (is(c.mode, "Ng") || is(c.mode, "backface")
        || is(c.material, "worldNormal") || is(c.material, "objectNormal")
        || (pathtracer && !is(c.mode, "default"))
        || is(c.material, "physicallyBased with a normal map"))
      colour = NONE;

    for (Subtype subtype : {Subtype::TRIANGLE, Subtype::QUAD}) {
      for (Source source : {Source::VERTEX, Source::FACE_VARYING}) {
        const MeshSetup with{subtype, source, false, false};
        const MeshSetup against{subtype, source, true, false};
        const Images a = renderMesh(d, c, with, MESH_VIEW);
        const Images b = renderMesh(d, c, against, MESH_VIEW);
        const std::string n = std::string(c.renderer) + " '" + c.mode + "', "
            + c.material + ", "
            + (subtype == Subtype::TRIANGLE ? "triangle" : "quad")
            + (source == Source::VERTEX ? ", vertex.normal"
                                        : ", faceVarying.normal");
        if (pathTraced && colour == SAME) {
          // the same samples along paths a few ulps apart: the means agree
          // closely
          const float4 ma = mean(a.color);
          const float4 mb = mean(b.color);
          const float rel = std::max({std::fabs(ma.x - mb.x) / ma.x,
              std::fabs(ma.y - mb.y) / ma.y,
              std::fabs(ma.z - mb.z) / ma.z});
          char detail[160];
          std::snprintf(detail,
              sizeof(detail),
              "means %s and %s, %.3g%% apart",
              str(float3(ma.x, ma.y, ma.z)).c_str(),
              str(float3(mb.x, mb.y, mb.z)).c_str(),
              100.f * rel);
          pass &= report(
              "wound against its normals path-traces as wound with them: " + n,
              rel < 2e-3f && ma.x > 0.05f,
              detail);
        } else if (colour != NONE) {
          const float diff = maxDifference(
              a.color, colour == NEGATED ? negated(b.color) : b.color);
          pass &= report(
              std::string("wound against its normals renders as wound with "
                          "them")
                  + (colour == NEGATED ? " (its normals turned): " : ": ") + n,
              diff < 1e-4f,
              std::to_string(diff) + " apart");
        }
        // the normal channel: the shading normal (preview renderer) or that
        // of the first non-specular hit, averaged (pathtracer); misses are 0
        // in both
        const float diff = maxDifference(a.normal, negated(b.normal));
        pass &= report("normal channel turned with the winding: " + n,
            diff < (pathtracer ? 2e-3f : 1e-4f),
            std::to_string(diff) + " apart");
      }
    }
  }
  return pass;
}

// The 'Ns.flipped' mode shows where the shading normal was turned: red where
// the mesh is wound against its normals (reversed winding or negated
// normals, not both), green where with them, on every pixel the mesh covers;
// the same under a mirroring instance, which flips Ng and the normal matrix
// alike; and blue on a mesh without normals
bool testFlippedMode(anari::Device d, const mat4 &mirror)
{
  const float4 red(1.f, 0.f, 0.f, 1.f);
  const float4 green(0.f, 1.f, 0.f, 1.f);
  const float4 blue(0.f, 0.f, 1.f, 1.f);
  const float4 background(0.f, 0.f, 0.f, 1.f);
  // counts the pixels of 'expected' colour; -1 if any hit shows another
  auto count = [&](const std::vector<float4> &image, float4 expected) {
    int n = 0;
    for (const float4 &c : image) {
      if (c == expected)
        n++;
      else if (c != background)
        return -1;
    }
    return n;
  };

  bool pass = true;
  const Case c{"default", "Ns.flipped", "matte", matte};
  for (bool mirrored : {false, true}) {
    for (Subtype subtype : {Subtype::TRIANGLE, Subtype::QUAD}) {
      for (Source source : {Source::VERTEX, Source::FACE_VARYING}) {
        for (bool reversed : {false, true}) {
          for (bool negatedNormals : {false, true}) {
            MeshSetup m{subtype, source, reversed, negatedNormals};
            const Images im = renderMesh(
                d, c, m, MESH_VIEW, mirrored ? mirror : linalg::identity);
            const bool against = reversed != negatedNormals;
            const int n = count(im.color, against ? red : green);
            pass &= report(std::string("'Ns.flipped' shows ")
                    + (against ? "red" : "green")
                    + (negatedNormals ? " (normals negated)" : "")
                    + (mirrored ? " (mirrored instance)" : "") + ": "
                    + name(m),
                n > int(im.color.size() / 4),
                std::to_string(n) + " pixels of "
                    + std::to_string(im.color.size()));
          }
        }
      }
    }
  }

  // without normals: blue
  auto geom = makeMesh(d, {Subtype::TRIANGLE, Source::VERTEX, false, false});
  anari::unsetParameter(d, geom, "vertex.normal");
  anari::commitParameters(d, geom);
  auto mat = matte(d);
  auto world = makeWorld(d, geom, mat);
  const Images im = render(d, world, "default", "Ns.flipped", MESH_VIEW);
  const int n = count(im.color, blue);
  pass &= report("'Ns.flipped' shows blue without normals",
      n > int(im.color.size() / 4),
      std::to_string(n) + " pixels");
  anari::release(d, world);
  anari::release(d, mat);
  anari::release(d, geom);
  return pass;
}

// Under a scale of y by 1/10 (normal matrix diag(1, 10, 1)), object-space Ng
// (1, 0.5, 0) and normal (1, -0.5, 0) make an acute angle, their world-space
// images (1, 5, 0) and (1, -5, 0) an obtuse one: the normal is turned, to
// world (-1, 5, 0) and object (-1, 0.5, 0)
bool testWorldSpace(anari::Device d)
{
  // object-space plane through the origin, normal (1, 0.5, 0): spanned by
  // (-0.5, 1, 0) and (0, 0, 1), wound so that Embree's Ng is +(1, 0.5, 0)
  const float3 s(-0.5f, 1.f, 0.f);
  const float3 t(0.f, 0.f, 1.f);
  std::vector<float3> positions = {-s - t, s - t, s + t, -s + t};
  const std::vector<uint3> indicesA = {{0, 1, 2}, {0, 2, 3}};
  const std::vector<uint3> indicesB = {{0, 2, 1}, {0, 3, 2}};
  const float3 objectN = linalg::normalize(float3(1.f, -0.5f, 0.f));
  const std::vector<float3> normals(4, objectN);
  const mat4 scale = linalg::scaling_matrix(float3(1.f, 0.1f, 1.f));
  const float3 worldNg = linalg::normalize(float3(1.f, 5.f, 0.f));
  const float3 expectedWorld = linalg::normalize(float3(-1.f, 5.f, 0.f));
  const float3 expectedObject = linalg::normalize(float3(-1.f, 0.5f, 0.f));
  const View view = {worldNg * 3.f, -worldNg, float3(0.f, 0.f, 1.f)};
  const size_t center =
      size_t(IMAGE_SIZE.y / 2) * IMAGE_SIZE.x + IMAGE_SIZE.x / 2;

  bool pass = true;
  int facingWindings = 0;
  for (int winding = 0; winding < 2; winding++) {
    auto geom = anari::newObject<anari::Geometry>(d, "triangle");
    anari::setParameterArray1D(
        d, geom, "vertex.position", positions.data(), positions.size());
    anari::setParameterArray1D(
        d, geom, "vertex.normal", normals.data(), normals.size());
    const auto &indices = winding == 0 ? indicesA : indicesB;
    anari::setParameterArray1D(
        d, geom, "primitive.index", indices.data(), indices.size());
    anari::commitParameters(d, geom);
    auto mat = matte(d);
    auto world = makeWorld(d, geom, mat, scale);
    // which winding gives Ng +(1, 5, 0)
    const Images ng = render(d, world, "default", "Ng", view);
    const float3 n = ng.color.empty()
        ? float3(0.f)
        : float3(ng.color[center].x, ng.color[center].y, ng.color[center].z);
    const bool facing = linalg::length(n - worldNg) < 1e-5f;
    if (facing) {
      facingWindings++;
      pass &= report("world space: Ng is (1, 5, 0) / |...|", true, str(n));
      const Images ns = render(d, world, "default", "Ns", view);
      const float3 v(ns.color[center].x, ns.color[center].y, ns.color[center].z);
      pass &= report("world space: default 'Ns' turned in world space",
          linalg::length(v - expectedWorld) < 1e-5f,
          str(v));
      const Images pt = render(d, world, "quality", "normal", view);
      const float3 p(pt.color[center].x, pt.color[center].y, pt.color[center].z);
      pass &= report("world space: pathtracer 'normal' turned in world space",
          linalg::length(p - expectedWorld) < 1e-5f,
          str(p));
      pass &= report("world space: default normal channel turned",
          linalg::length(ns.normal[center] - expectedWorld) < 1e-5f,
          str(ns.normal[center]));
      const Images flipped = render(d, world, "default", "Ns.flipped", view);
      pass &= report("world space: 'Ns.flipped' shows the turn (red)",
          !flipped.color.empty()
              && flipped.color[center] == float4(1.f, 0.f, 0.f, 1.f),
          flipped.color.empty() ? "" : str(float3(flipped.color[center].x,
              flipped.color[center].y, flipped.color[center].z)));

      auto objectMat = matteOfAttribute("objectNormal")(d);
      auto objectWorld = makeWorld(d, geom, objectMat, scale);
      const Images on = render(d,
          objectWorld,
          "default",
          "default",
          view,
          [](anari::Device d, anari::Renderer r) {
            anari::setParameter(d, r, "eyeLightBlendRatio", 0.f);
            anari::setParameter(d, r, "ambientOcclusion", false);
          });
      const float3 o =
          float3(on.color[center].x, on.color[center].y, on.color[center].z)
              * 2.f
          - float3(1.f);
      pass &= report("world space: 'objectNormal' turned (by the world-space "
                     "angle)",
          linalg::length(o - expectedObject) < 1e-5f,
          str(o));
      anari::release(d, objectWorld);
      anari::release(d, objectMat);
    }
    anari::release(d, world);
    anari::release(d, mat);
    anari::release(d, geom);
  }
  pass &= report("world space: one winding gives Ng (1, 5, 0) / |...|",
      facingWindings == 1);
  return pass;
}

// Under a mirroring instance (a negative determinant flips Ng and the normal
// matrix alike), the shading normal still lands on Ng's side: per pixel in
// the 'preview' renderer's normal channel and the 'quality''s 'normal'
// mode, against the 'preview' renderer's 'Ng' mode
bool testMirrorSide(anari::Device d, const mat4 &mirror)
{
  bool pass = true;
  for (Subtype subtype : {Subtype::TRIANGLE, Subtype::QUAD}) {
    for (bool reversed : {false, true}) {
      for (bool negatedNormals : {false, true}) {
        const MeshSetup m{
            subtype, Source::VERTEX, reversed, negatedNormals};
        const Case ng{"default", "Ng", "matte", matte};
        const Case pt{"quality", "normal", "matte", matte};
        const Images a = renderMesh(d, ng, m, MESH_VIEW, mirror);
        const Images b = renderMesh(d, pt, m, MESH_VIEW, mirror);
        size_t hits = 0, wrongSide = 0;
        for (size_t i = 0; i < a.normal.size() && i < b.color.size(); i++) {
          const float3 n = a.normal[i];
          if (n == float3(0.f))
            continue; // a miss
          hits++;
          const float3 g(a.color[i].x, a.color[i].y, a.color[i].z);
          const float3 p(b.color[i].x, b.color[i].y, b.color[i].z);
          if (linalg::dot(n, g) < 0.f || linalg::dot(p, g) < 0.f)
            wrongSide++;
        }
        pass &= report("mirrored instance: normals on Ng's side: " + name(m)
                + (negatedNormals ? ", normals negated" : ""),
            hits > 100 && wrongSide == 0,
            std::to_string(wrongSide) + " of " + std::to_string(hits)
                + " hits on the other side");
      }
    }
  }
  return pass;
}

// A thick transmissive physicallyBased (glass of IOR 1.5)
anari::Material glass(anari::Device d)
{
  auto m = anari::newObject<anari::Material>(d, "physicallyBased");
  anari::setParameter(d, m, "baseColor", float3(1.f));
  anari::setParameter(d, m, "metallic", 0.f);
  anari::setParameter(d, m, "roughness", 0.f);
  anari::setParameter(d, m, "transmission", 1.f);
  anari::setParameter(d, m, "ior", 1.5f);
  anari::setParameter(d, m, "thickness", 1.f);
  anari::commitParameters(d, m);
  return m;
}

// The glass mesh over a matte floor (z = -0.6) under the directional light,
// without the ambient light, so that where refraction sends a ray matters
Images renderOverFloor(anari::Device d, const MeshSetup &m, int seed = 0)
{
  auto geom = makeMesh(d, m);
  auto mat = glass(d);
  // a checkerboard of 16 x 16 quads over [-4, 4]^2, so that where a ray
  // lands shows
  std::vector<float3> floorPositions;
  std::vector<uint4> floorIndices;
  std::vector<float4> checker;
  constexpr int F = 16;
  for (int j = 0; j <= F; j++)
    for (int i = 0; i <= F; i++)
      floorPositions.push_back(
          float3(-4.f + 8.f * i / F, -4.f + 8.f * j / F, -0.6f));
  for (int j = 0; j < F; j++) {
    for (int i = 0; i < F; i++) {
      const uint32_t v = j * (F + 1) + i;
      floorIndices.push_back(uint4(v, v + 1, v + F + 2, v + F + 1));
      checker.push_back((i + j) % 2 ? float4(0.9f, 0.8f, 0.7f, 1.f)
                                    : float4(0.05f, 0.1f, 0.15f, 1.f));
    }
  }
  auto floorGeom = anari::newObject<anari::Geometry>(d, "quad");
  anari::setParameterArray1D(d,
      floorGeom,
      "vertex.position",
      floorPositions.data(),
      floorPositions.size());
  anari::setParameterArray1D(d,
      floorGeom,
      "primitive.index",
      floorIndices.data(),
      floorIndices.size());
  anari::setParameterArray1D(
      d, floorGeom, "primitive.color", checker.data(), checker.size());
  anari::commitParameters(d, floorGeom);
  auto floorMat = anari::newObject<anari::Material>(d, "matte");
  anari::setParameter(d, floorMat, "color", "color");
  anari::commitParameters(d, floorMat);

  anari::Surface surfaces[2] = {anari::newObject<anari::Surface>(d),
      anari::newObject<anari::Surface>(d)};
  anari::setParameter(d, surfaces[0], "geometry", geom);
  anari::setParameter(d, surfaces[0], "material", mat);
  anari::setParameter(d, surfaces[1], "geometry", floorGeom);
  anari::setParameter(d, surfaces[1], "material", floorMat);
  for (auto s : surfaces)
    anari::commitParameters(d, s);
  auto light = anari::newObject<anari::Light>(d, "directional");
  anari::setParameter(
      d, light, "direction", linalg::normalize(float3(-0.4f, 0.3f, -1.f)));
  anari::setParameter(d, light, "irradiance", 2.f);
  anari::commitParameters(d, light);
  auto world = anari::newObject<anari::World>(d);
  anari::setParameterArray1D(d, world, "surface", surfaces, 2);
  anari::setParameterArray1D(d, world, "light", &light, 1);
  anari::commitParameters(d, world);

  Images images = render(d,
      world,
      "quality",
      "default",
      MESH_VIEW,
      [=](anari::Device d, anari::Renderer r) {
        anari::setParameter(d, r, "ambientRadiance", 0.f);
        anari::setParameter(d, r, "pixelSamples", 64);
        anari::setParameter(d, r, "randomNumberSeed", seed);
      });
  anari::release(d, world);
  anari::release(d, light);
  for (auto s : surfaces)
    anari::release(d, s);
  anari::release(d, floorMat);
  anari::release(d, floorGeom);
  anari::release(d, mat);
  anari::release(d, geom);
  return images;
}

// The winding, not the normals, decides a thick surface's inside: glass
// seen from its outside (rays enter, eta 1 / 1.5) renders otherwise than
// from its inside (rays leave, total internal reflection at grazing angles),
// whichever way the normals point
bool testInside(anari::Device d)
{
  bool pass = true;
  for (Subtype subtype : {Subtype::TRIANGLE, Subtype::QUAD}) {
    const Images out =
        renderOverFloor(d, MeshSetup{subtype, Source::VERTEX, false, false});
    const Images outNegated =
        renderOverFloor(d, MeshSetup{subtype, Source::VERTEX, false, true});
    const Images in =
        renderOverFloor(d, MeshSetup{subtype, Source::VERTEX, true, false});
    const Images inNegated =
        renderOverFloor(d, MeshSetup{subtype, Source::VERTEX, true, true});
    const std::string n = subtype == Subtype::TRIANGLE ? "triangle" : "quad";
    pass &= report("glass: the normals don't decide the inside: " + n,
        bitwise(out.color, outNegated.color)
            && bitwise(in.color, inNegated.color));
    // against the noise: the same glass from its outside at another seed
    const Images outSeed = renderOverFloor(
        d, MeshSetup{subtype, Source::VERTEX, false, false}, 1);
    auto rms = [](const std::vector<float4> &a, const std::vector<float4> &b) {
      double sum = 0.0;
      for (size_t i = 0; i < a.size() && i < b.size(); i++) {
        const float4 x = a[i] - b[i];
        sum += x.x * x.x + x.y * x.y + x.z * x.z;
      }
      return a.empty() ? 0.0 : std::sqrt(sum / (3.0 * a.size()));
    };
    const double sides = rms(out.color, in.color);
    const double noise = rms(out.color, outSeed.color);
    char detail[160];
    std::snprintf(detail,
        sizeof(detail),
        "RMS %g between the sides, %g between seeds",
        sides,
        noise);
    pass &= report("glass: the winding decides the inside: " + n,
        sides > 3.0 * noise,
        detail);
  }
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

  bool pass = true;
  pass &= testNegation(d, cases());
  // a mirroring instance
  const mat4 mirror = linalg::scaling_matrix(float3(-1.f, 1.f, 1.f));
  pass &= testNegation(d, cases(), "mirrored instance", mirror);
  pass &= testMirrorSide(d, mirror);
  pass &= testFlippedMode(d, mirror);
  // authored tangents, their handedness negated with the normals
  {
    std::vector<Case> tangentCases;
    for (const Case &c : cases()) {
      if (is(c.renderer, "quality")
          && (!is(c.mode, "default") || is(c.material, "openPBR")
              || is(c.material, "physicallyBased with a normal map")))
        tangentCases.push_back(c);
    }
    pass &= testNegation(
        d, tangentCases, "w negated too", linalg::identity, true);
  }
  pass &= testInside(d);
  pass &= testWinding(d);
  pass &= testWorldSpace(d);

  anari::release(d, d);
  anari::unloadLibrary(library);

  pass &= report("no device errors", g_errors == 0);
  pass &= report("no device warnings", g_warnings == 0);
  return pass ? 0 : 1;
}
