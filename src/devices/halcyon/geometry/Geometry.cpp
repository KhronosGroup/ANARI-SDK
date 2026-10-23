// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Geometry.h"
#include "GeometryDerivatives_ispc.h"
#include "Geometry_ispc.h"
#include "MeshTangents.h"
// std
#include <algorithm>
#include <cassert>
#include <functional>
#include <memory>
#include <numeric>
#include <string>
#include <vector>
// embree
#include "algorithms/parallel_for.h"

namespace halcyon {

// The 'primitive.index' element types the spec allows for 'components'
// (1 to 4) indices per primitive: UINT32 or UINT64 with that many components
static bool isIndexType(anari::DataType type, size_t components)
{
  static const anari::DataType types32[] = {
      ANARI_UINT32, ANARI_UINT32_VEC2, ANARI_UINT32_VEC3, ANARI_UINT32_VEC4};
  static const anari::DataType types64[] = {
      ANARI_UINT64, ANARI_UINT64_VEC2, ANARI_UINT64_VEC3, ANARI_UINT64_VEC4};
  return type == types32[components - 1] || type == types64[components - 1];
}

// Whether index type 'type' (see isIndexType()) has 32-bit components, not
// 64-bit ones
static bool isUint32Based(anari::DataType type)
{
  return anari::sizeOf(type) == anari::componentsOf(type) * sizeof(uint32_t);
}

// Copies the values of 'a', whose elements must be UINT32 or UINT64 scalars
// or vectors (callers check), into 'out' as uint32; true if a UINT64 value was
// above UINT32_MAX and got truncated
static bool copyAsUint32(const Array1D &a, std::vector<uint32_t> &out)
{
  const anari::DataType type = a.elementType();
  const size_t components = anari::componentsOf(type);
  assert(components >= 1 && components <= 4 && isIndexType(type, components));
  out.resize(a.size() * components);
  if (isUint32Based(type)) {
    const auto *values = static_cast<const uint32_t *>(a.begin());
    std::copy(values, values + out.size(), out.begin());
    return false;
  }
  const auto *values = static_cast<const uint64_t *>(a.begin());
  bool truncated = false;
  std::transform(values, values + out.size(), out.begin(), [&](uint64_t v) {
    truncated |= v > UINT32_MAX;
    return uint32_t(v);
  });
  return truncated;
}

// Reads 'primitive.index' array 'index' of 'subtype' geometry 'g', with
// 'components' indices per primitive, into 'indices' as packed uint32 (UINT64
// values are truncated, with a warning); false, with a warning, if its
// element type isn't one the spec allows
static bool readIndices(const Geometry &g,
    const Array1D &index,
    const char *subtype,
    size_t components,
    std::vector<uint32_t> &indices)
{
  const anari::DataType type = index.elementType();
  if (!isIndexType(type, components)) {
    const char *suffix[] = {"", "_VEC2", "_VEC3", "_VEC4"};
    g.reportMessage(ANARI_SEVERITY_WARNING,
        "%s 'primitive.index' array elements are %s, but need to be "
        "ANARI_UINT32%s or ANARI_UINT64%s; the %s is invalid",
        subtype,
        anari::toString(type),
        suffix[components - 1],
        suffix[components - 1],
        subtype);
    return false;
  }
  if (copyAsUint32(index, indices)) {
    g.reportMessage(ANARI_SEVERITY_WARNING,
        "%s 'primitive.index' values above UINT32_MAX are truncated to 32 bits",
        subtype);
  }
  return true;
}

// 'primitive.index' of geometry 'g' as the subtype and its ISPC hook read it,
// in 'indices': UINT32 in place (shared, like 'vertex.position'), UINT64
// converted into 'copy'; false, with a warning, if the element type isn't one
// the spec allows
static bool sharedIndices(const Geometry &g,
    const Array1D &index,
    const char *subtype,
    size_t components,
    std::vector<uint32_t> &copy,
    const uint32_t *&indices)
{
  const anari::DataType type = index.elementType();
  if (isIndexType(type, components) && isUint32Based(type)) {
    copy.clear();
    indices = static_cast<const uint32_t *>(index.begin());
    return true;
  }
  if (!readIndices(g, index, subtype, components, copy))
    return false;
  indices = copy.data();
  return true;
}

// Radii for Embree's float4 (position + radius) vertices, as sphere, curve,
// cylinder and cone build them: element i of a radius array where it has one,
// else the global 'radius'
struct Radii
{
  const float *values{nullptr};
  size_t count{0};
  float fallback{0.f};

  float operator[](size_t i) const
  {
    return i < count ? values[i] : fallback;
  }
};

// Radius array 'array' (parameter 'name', may be null) with 'fallback' for
// the elements it lacks; warns if it has fewer than 'needed' 'elements'
static Radii readRadii(const Geometry &g,
    const Array1D *array,
    const char *name,
    float fallback,
    size_t needed,
    const char *elements)
{
  Radii radii;
  radii.values = array ? array->beginAs<float>() : nullptr;
  radii.count = array ? array->size() : 0;
  radii.fallback = fallback;
  if (array && radii.count < needed) {
    g.reportMessage(ANARI_SEVERITY_WARNING,
        "'%s' has %zu elements for %zu %s; the rest use 'radius'",
        name,
        radii.count,
        needed,
        elements);
  }
  return radii;
}

// An Embree float4 vertex: position 'p', radius 'r'
static float4 embreeVertex(const float3 &p, float r)
{
  return float4(p.x, p.y, p.z, r);
}

// Normal array 'a' (FLOAT32_VEC3 or FIXED16_VEC3, see commitNormals()) as the
// packed float4 ISPC reads, FIXED16 components divided by 32767; empty if 'a'
// is null
static std::vector<float4> toNormals(const Array1D *a)
{
  if (!a)
    return {};
  std::vector<float4> normals(a->size());
  if (a->elementType() == ANARI_FLOAT32_VEC3) {
    const float3 *n = a->beginAs<float3>();
    for (size_t i = 0; i < normals.size(); i++)
      normals[i] = float4(n[i].x, n[i].y, n[i].z, 0.f);
  } else {
    const auto *n = static_cast<const int16_t *>(a->begin());
    for (size_t i = 0; i < normals.size(); i++) {
      normals[i] = float4(n[3 * i] / 32767.f,
          n[3 * i + 1] / 32767.f,
          n[3 * i + 2] / 32767.f,
          0.f);
    }
  }
  return normals;
}

// Tangent array 'a' (FLOAT32_VEC3/VEC4 or FIXED16_VEC3/VEC4, see
// commitTangents()) as packed float4, FIXED16 components divided by 32767; w
// the handedness, 1 for VEC3 arrays; empty if 'a' is null
static std::vector<float4> toTangents(const Array1D *a)
{
  if (!a)
    return {};
  const anari::DataType type = a->elementType();
  const size_t components = anari::componentsOf(type);
  const bool fixed16 = type == ANARI_FIXED16_VEC3 || type == ANARI_FIXED16_VEC4;
  std::vector<float4> tangents(a->size(), float4(0.f, 0.f, 0.f, 1.f));
  for (size_t i = 0; i < tangents.size(); i++) {
    for (size_t c = 0; c < components; c++) {
      tangents[i][c] = fixed16
          ? static_cast<const int16_t *>(a->begin())[components * i + c]
              / 32767.f
          : static_cast<const float *>(a->begin())[components * i + c];
    }
  }
  return tangents;
}

// Runs 'f(begin, end)' over [0, n) in chunks on Embree's tasking threads (a
// ParallelFor, MeshTangents.h)
static void parallelFor(
    size_t n, const std::function<void(size_t begin, size_t end)> &f)
{
  constexpr size_t CHUNK = 16384;
  const size_t numChunks = (n + CHUNK - 1) / CHUNK;
  embree::parallel_for(numChunks, [&](size_t chunk) {
    f(chunk * CHUNK, std::min(n, (chunk + 1) * CHUNK));
  });
}

// The texture coordinates Typhoon's smoothed tangents follow: the
// pathtracer's (PATH_TEXCOORD_ATTRIBUTE, renderer/pathtracer/
// ShadingContext.isph)
constexpr int TANGENT_TEXCOORD_ATTRIBUTE = 0;

// Geometry subtypes //////////////////////////////////////////////////////////

struct Triangle : public Geometry
{
  Triangle(HalcyonDeviceGlobalState *s)
      : Geometry(s), m_index(this), m_vertexPosition(this)
  {
    m_embreeGeometry =
        rtcNewGeometry(s->embreeDevice, RTC_GEOMETRY_TYPE_TRIANGLE);
  }

  void commitParameters() override
  {
    Geometry::commitParameters();
    m_index = getParamObject<Array1D>("primitive.index");
    m_vertexPosition = getParamObject<Array1D>("vertex.position");
    commitFaceVaryingAttributes();
    commitNormals();
    commitTangents();
  }

  bool finalizeSubtype() override
  {
    ispc::Geometry_setTriangleIndices(getIE(), nullptr, 0);

    if (!m_vertexPosition) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "missing required parameter 'vertex.position' on triangle geometry");
      return false;
    }

    const uint32_t *indices = nullptr;
    if (m_index
        && !sharedIndices(*this, *m_index, "triangle", 3, m_indexData, indices))
      return false;
    const size_t numTris =
        m_index ? m_index->size() : m_vertexPosition->size() / 3;
    if (m_index)
      ispc::Geometry_setTriangleIndices(getIE(), indices, numTris);

    // Unpadded: Embree may read 4 bytes past the end (AGENTS.md, Array data).
    rtcSetSharedGeometryBuffer(embreeGeometry(),
        RTC_BUFFER_TYPE_VERTEX,
        0,
        RTC_FORMAT_FLOAT3,
        m_vertexPosition->beginAs<float3>(),
        0,
        sizeof(float3),
        m_vertexPosition->size());
    ispc::Geometry_setVertices(getIE(),
        reinterpret_cast<const float *>(m_vertexPosition->beginAs<float3>()),
        m_vertexPosition->size(),
        3);

    if (m_index) {
      rtcSetSharedGeometryBuffer(embreeGeometry(),
          RTC_BUFFER_TYPE_INDEX,
          0,
          RTC_FORMAT_UINT3,
          indices,
          0,
          sizeof(uint3),
          numTris);
    } else {
      auto *vr = (uint32_t *)rtcSetNewGeometryBuffer(embreeGeometry(),
          RTC_BUFFER_TYPE_INDEX,
          0,
          RTC_FORMAT_UINT3,
          sizeof(uint3),
          numTris);
      std::iota(vr, vr + (numTris * 3), 0);
    }

    if (m_primitiveId && m_primitiveId->size() < numTris) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "'primitive.id' has %zu elements for %zu triangles; "
          "the rest report their primitive index",
          m_primitiveId->size(),
          numTris);
    }
    checkFaceVaryingSizes(3 * numTris, numTris, "triangles");
    finalizeTangentFrames(3,
        numTris,
        indices,
        m_vertexPosition->beginAs<float3>(),
        m_vertexPosition->size());

    rtcCommitGeometry(embreeGeometry());
    return true;
  }

 private:
  helium::ChangeObserverPtr<Array1D> m_index;
  helium::ChangeObserverPtr<Array1D> m_vertexPosition;
  // UINT64 'primitive.index' converted to uint32 (UINT32 is shared)
  std::vector<uint32_t> m_indexData;
};

struct Quad : public Geometry
{
  Quad(HalcyonDeviceGlobalState *s)
      : Geometry(s), m_index(this), m_vertexPosition(this)
  {
    m_embreeGeometry = rtcNewGeometry(s->embreeDevice, RTC_GEOMETRY_TYPE_QUAD);
  }

  void commitParameters() override
  {
    Geometry::commitParameters();
    m_index = getParamObject<Array1D>("primitive.index");
    m_vertexPosition = getParamObject<Array1D>("vertex.position");
    commitFaceVaryingAttributes();
    commitNormals();
    commitTangents();
  }

  bool finalizeSubtype() override
  {
    ispc::Geometry_setQuadIndices(getIE(), nullptr, 0);

    if (!m_vertexPosition) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "missing required parameter 'vertex.position' on quad geometry");
      return false;
    }

    const uint32_t *indices = nullptr;
    if (m_index
        && !sharedIndices(*this, *m_index, "quad", 4, m_indexData, indices))
      return false;
    const size_t numQuads =
        m_index ? m_index->size() : m_vertexPosition->size() / 4;
    if (m_index)
      ispc::Geometry_setQuadIndices(getIE(), indices, numQuads);

    // Unpadded: Embree may read 4 bytes past the end (AGENTS.md, Array data).
    rtcSetSharedGeometryBuffer(embreeGeometry(),
        RTC_BUFFER_TYPE_VERTEX,
        0,
        RTC_FORMAT_FLOAT3,
        m_vertexPosition->beginAs<float3>(),
        0,
        sizeof(float3),
        m_vertexPosition->size());
    ispc::Geometry_setVertices(getIE(),
        reinterpret_cast<const float *>(m_vertexPosition->beginAs<float3>()),
        m_vertexPosition->size(),
        3);

    if (m_index) {
      rtcSetSharedGeometryBuffer(embreeGeometry(),
          RTC_BUFFER_TYPE_INDEX,
          0,
          RTC_FORMAT_UINT4,
          indices,
          0,
          sizeof(uint4),
          numQuads);
    } else {
      auto *vr = (uint32_t *)rtcSetNewGeometryBuffer(embreeGeometry(),
          RTC_BUFFER_TYPE_INDEX,
          0,
          RTC_FORMAT_UINT4,
          sizeof(uint4),
          numQuads);
      std::iota(vr, vr + (numQuads * 4), 0);
    }

    if (m_primitiveId && m_primitiveId->size() < numQuads) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "'primitive.id' has %zu elements for %zu quads; "
          "the rest report their primitive index",
          m_primitiveId->size(),
          numQuads);
    }
    checkFaceVaryingSizes(4 * numQuads, numQuads, "quads");
    finalizeTangentFrames(4,
        numQuads,
        indices,
        m_vertexPosition->beginAs<float3>(),
        m_vertexPosition->size());

    rtcCommitGeometry(embreeGeometry());
    return true;
  }

 private:
  helium::ChangeObserverPtr<Array1D> m_index;
  helium::ChangeObserverPtr<Array1D> m_vertexPosition;
  // UINT64 'primitive.index' converted to uint32 (UINT32 is shared)
  std::vector<uint32_t> m_indexData;
};

struct Sphere : public Geometry
{
  // 'radius' default, as in khr_geometry_sphere (helide's is 0.01)
  static constexpr float DEFAULT_RADIUS = 1.f;

  Sphere(HalcyonDeviceGlobalState *s)
      : Geometry(s), m_index(this), m_vertexPosition(this), m_vertexRadius(this)
  {
    m_embreeGeometry =
        rtcNewGeometry(s->embreeDevice, RTC_GEOMETRY_TYPE_SPHERE_POINT);
  }

  void commitParameters() override
  {
    Geometry::commitParameters();
    m_index = getParamObject<Array1D>("primitive.index");
    m_vertexPosition = getParamObject<Array1D>("vertex.position");
    m_vertexRadius = getParamObject<Array1D>("vertex.radius");
    m_globalRadius = getParam<float>("radius", DEFAULT_RADIUS);
  }

  bool finalizeSubtype() override
  {
    ispc::Geometry_setSphereIndices(getIE(), nullptr, 0);

    if (!m_vertexPosition) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "missing required parameter 'vertex.position' on sphere geometry");
      return false;
    }

    const uint32_t *indices = nullptr;
    if (m_index
        && !sharedIndices(*this, *m_index, "sphere", 1, m_indexData, indices))
      return false;

    const auto *vertices = m_vertexPosition->beginAs<float3>();
    const size_t numVertices = m_vertexPosition->size();
    const Radii radii = readRadii(*this,
        m_vertexRadius.get(),
        "vertex.radius",
        m_globalRadius,
        numVertices,
        "vertices");

    const size_t numSpheres =
        numVertices == 0 ? 0 : (m_index ? m_index->size() : numVertices);

    // Embree reads position and radius as one float4 per sphere
    auto *vr = (float4 *)rtcSetNewGeometryBuffer(embreeGeometry(),
        RTC_BUFFER_TYPE_VERTEX,
        0,
        RTC_FORMAT_FLOAT4,
        sizeof(float4),
        numSpheres);

    bool clamped = false;
    for (size_t i = 0; i < numSpheres; i++) {
      size_t v = indices ? indices[i] : i;
      if (v >= numVertices) {
        clamped = true;
        v = numVertices - 1;
      }
      vr[i] = embreeVertex(vertices[v], radii[v]);
    }
    ispc::Geometry_setVertices(
        getIE(), reinterpret_cast<const float *>(vr), numSpheres, 4);
    if (clamped) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "'primitive.index' has indices past the %zu vertices; "
          "they use the last vertex",
          numVertices);
    }

    if (m_index)
      ispc::Geometry_setSphereIndices(getIE(), indices, numSpheres);

    if (m_primitiveId && m_primitiveId->size() < numSpheres) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "'primitive.id' has %zu elements for %zu spheres; "
          "the rest report their primitive index",
          m_primitiveId->size(),
          numSpheres);
    }

    rtcCommitGeometry(embreeGeometry());
    return true;
  }

 private:
  helium::ChangeObserverPtr<Array1D> m_index;
  helium::ChangeObserverPtr<Array1D> m_vertexPosition;
  helium::ChangeObserverPtr<Array1D> m_vertexRadius;
  float m_globalRadius{DEFAULT_RADIUS};
  // UINT64 'primitive.index' converted to uint32 (UINT32 is shared)
  std::vector<uint32_t> m_indexData;
};

struct Curve : public Geometry
{
  // 'radius' default, as in helide and khr_geometry_curve
  static constexpr float DEFAULT_RADIUS = 1.f;

  Curve(HalcyonDeviceGlobalState *s)
      : Geometry(s), m_index(this), m_vertexPosition(this), m_vertexRadius(this)
  {
    m_embreeGeometry = rtcNewGeometry(s->embreeDevice, embreeType(m_basis));
  }

  void commitParameters() override
  {
    Geometry::commitParameters();
    m_index = getParamObject<Array1D>("primitive.index");
    m_vertexPosition = getParamObject<Array1D>("vertex.position");
    m_vertexRadius = getParamObject<Array1D>("vertex.radius");
    m_globalRadius = getParam<float>("radius", DEFAULT_RADIUS);

    // HALCYON_GEOMETRY_CURVE_BASIS
    const std::string basis = getParamString("basis", "linear");
    if (basis == "bspline")
      m_basis = ispc::CURVE_BASIS_BSPLINE;
    else if (basis == "catmullRom")
      m_basis = ispc::CURVE_BASIS_CATMULL_ROM;
    else {
      m_basis = ispc::CURVE_BASIS_LINEAR;
      if (basis != "linear") {
        reportMessage(ANARI_SEVERITY_WARNING,
            "unknown curve 'basis' '%s'; using 'linear'",
            basis.c_str());
      }
    }
  }

  bool finalizeSubtype() override
  {
    ispc::Geometry_setCurveIndices(getIE(), nullptr, 0, m_basis);
    // Embree's curve type is fixed at creation: a new basis needs a new one
    if (m_embreeBasis != m_basis) {
      replaceEmbreeGeometry(embreeType(m_basis));
      m_embreeBasis = m_basis;
    }

    if (!m_vertexPosition) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "missing required parameter 'vertex.position' on curve geometry");
      return false;
    }

    // UINT32 indices are read in place, UINT64 ones converted into this
    std::vector<uint32_t> converted;
    const uint32_t *indices = nullptr;
    if (m_index
        && !sharedIndices(*this, *m_index, "curve", 1, converted, indices))
      return false;

    const auto *vertices = m_vertexPosition->beginAs<float3>();
    const size_t numVertices = m_vertexPosition->size();
    const Radii radii = readRadii(*this,
        m_vertexRadius.get(),
        "vertex.radius",
        m_globalRadius,
        numVertices,
        "vertices");

    // a segment spans two vertices, or four control points for cubic bases,
    // which can't draw anything with fewer
    const bool cubic = m_basis != ispc::CURVE_BASIS_LINEAR;
    const size_t verticesPerSegment = cubic ? 4 : 2;
    if (numVertices < verticesPerSegment) {
      if (cubic) {
        reportMessage(ANARI_SEVERITY_WARNING,
            "curve geometry with basis '%s' needs at least 4 vertices, has %zu",
            basisName(m_basis),
            numVertices);
        return false;
      }
      reportMessage(ANARI_SEVERITY_WARNING,
          "curve geometry needs at least 2 vertices, has %zu",
          numVertices);
    }
    const size_t numSegments = numVertices < verticesPerSegment
        ? 0
        : (m_index ? m_index->size() : numVertices - (verticesPerSegment - 1));

    // Embree reads position and radius as one float4 per vertex
    auto *vr = (float4 *)rtcSetNewGeometryBuffer(embreeGeometry(),
        RTC_BUFFER_TYPE_VERTEX,
        0,
        RTC_FORMAT_FLOAT4,
        sizeof(float4),
        numVertices);
    for (size_t i = 0; i < numVertices; i++)
      vr[i] = embreeVertex(vertices[i], radii[i]);
    ispc::Geometry_setVertices(
        getIE(), reinterpret_cast<const float *>(vr), numVertices, 4);

    // Segment start indices, copied so the segment's last vertex can be
    // clamped into range
    auto *segmentStarts = (uint32_t *)rtcSetNewGeometryBuffer(embreeGeometry(),
        RTC_BUFFER_TYPE_INDEX,
        0,
        RTC_FORMAT_UINT,
        sizeof(uint32_t),
        numSegments);
    if (m_index) {
      const uint32_t lastStart = uint32_t(numVertices - verticesPerSegment);
      bool clamped = false;
      for (size_t i = 0; i < numSegments; i++) {
        clamped |= indices[i] > lastStart;
        segmentStarts[i] = std::min(indices[i], lastStart);
      }
      if (clamped) {
        reportMessage(ANARI_SEVERITY_WARNING,
            "'primitive.index' has segments ending past the %zu vertices; "
            "they use the last segment",
            numVertices);
      }
      // the clamped copy, so attributes interpolate over the segments Embree
      // intersects; it lives until the next finalize() replaces it
      ispc::Geometry_setCurveIndices(
          getIE(), segmentStarts, numSegments, m_basis);
    } else {
      std::iota(segmentStarts, segmentStarts + numSegments, 0);
    }

    if (m_primitiveId && m_primitiveId->size() < numSegments) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "'primitive.id' has %zu elements for %zu curve segments; "
          "the rest report their primitive index",
          m_primitiveId->size(),
          numSegments);
    }

    rtcCommitGeometry(embreeGeometry());
    return true;
  }

 private:
  static RTCGeometryType embreeType(ispc::CurveBasis basis)
  {
    switch (basis) {
    case ispc::CURVE_BASIS_BSPLINE:
      return RTC_GEOMETRY_TYPE_ROUND_BSPLINE_CURVE;
    case ispc::CURVE_BASIS_CATMULL_ROM:
      return RTC_GEOMETRY_TYPE_ROUND_CATMULL_ROM_CURVE;
    default:
      return RTC_GEOMETRY_TYPE_ROUND_LINEAR_CURVE;
    }
  }

  static const char *basisName(ispc::CurveBasis basis)
  {
    switch (basis) {
    case ispc::CURVE_BASIS_BSPLINE:
      return "bspline";
    case ispc::CURVE_BASIS_CATMULL_ROM:
      return "catmullRom";
    default:
      return "linear";
    }
  }

  helium::ChangeObserverPtr<Array1D> m_index;
  helium::ChangeObserverPtr<Array1D> m_vertexPosition;
  helium::ChangeObserverPtr<Array1D> m_vertexRadius;
  float m_globalRadius{DEFAULT_RADIUS};
  // 'basis', and the basis of the Embree geometry's type
  ispc::CurveBasis m_basis{ispc::CURVE_BASIS_LINEAR};
  ispc::CurveBasis m_embreeBasis{ispc::CURVE_BASIS_LINEAR};
};

// Cylinders and cones: RTC_GEOMETRY_TYPE_CONE_LINEAR_CURVE with two Embree
// vertices (position + radius) per primitive. They differ only in where the
// radii come from: per primitive for cylinders, per vertex for cones.
struct LinearCone : public Geometry
{
  // 'radius' default, as in helide and khr_geometry_cylinder (the cone spec
  // has no 'radius'; helide accepts it anyway)
  static constexpr float DEFAULT_RADIUS = 1.f;

  enum class RadiusSource
  {
    PRIMITIVE, // 'primitive.radius' (cylinder)
    VERTEX // 'vertex.radius' (cone)
  };

  LinearCone(HalcyonDeviceGlobalState *s, RadiusSource radiusSource)
      : Geometry(s),
        m_radiusSource(radiusSource),
        m_index(this),
        m_vertexPosition(this),
        m_radius(this)
  {
    m_embreeGeometry =
        rtcNewGeometry(s->embreeDevice, RTC_GEOMETRY_TYPE_CONE_LINEAR_CURVE);
  }

  void commitParameters() override
  {
    Geometry::commitParameters();
    m_index = getParamObject<Array1D>("primitive.index");
    m_vertexPosition = getParamObject<Array1D>("vertex.position");
    m_radius = getParamObject<Array1D>(radiusParameter());
    m_globalRadius = getParam<float>("radius", DEFAULT_RADIUS);
  }

  bool finalizeSubtype() override
  {
    ispc::Geometry_setConeIndices(getIE(), nullptr, 0);

    if (!m_vertexPosition) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "missing required parameter 'vertex.position' on %s geometry",
          subtype());
      return false;
    }

    const uint32_t *packedIndices = nullptr;
    if (m_index
        && !sharedIndices(
            *this, *m_index, subtype(), 2, m_indexData, packedIndices))
      return false;

    const auto *vertices = m_vertexPosition->beginAs<float3>();
    const size_t numVertices = m_vertexPosition->size();
    if (!m_index && numVertices % 2 != 0) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "%s geometry without 'primitive.index' has an odd number of "
          "vertices (%zu); the last one is ignored",
          subtype(),
          numVertices);
    }
    const size_t numPrimitives =
        numVertices == 0 ? 0 : (m_index ? m_index->size() : numVertices / 2);

    const bool perVertex = m_radiusSource == RadiusSource::VERTEX;
    const Radii radii = readRadii(*this,
        m_radius.get(),
        radiusParameter(),
        m_globalRadius,
        perVertex ? numVertices : numPrimitives,
        perVertex ? "vertices" : "primitives");

    // Embree reads position and radius as one float4 per vertex, two
    // vertices per primitive
    auto *vr = (float4 *)rtcSetNewGeometryBuffer(embreeGeometry(),
        RTC_BUFFER_TYPE_VERTEX,
        0,
        RTC_FORMAT_FLOAT4,
        sizeof(float4),
        2 * numPrimitives);

    const auto *indices = reinterpret_cast<const uint2 *>(packedIndices);
    bool clamped = false;
    for (size_t i = 0; i < numPrimitives; i++) {
      const uint2 idx =
          indices ? indices[i] : uint2(uint32_t(2 * i), uint32_t(2 * i + 1));
      for (size_t end = 0; end < 2; end++) {
        size_t v = end == 0 ? idx.x : idx.y;
        if (v >= numVertices) {
          clamped = true;
          v = numVertices - 1;
        }
        vr[2 * i + end] = embreeVertex(vertices[v], radii[perVertex ? v : i]);
      }
    }
    ispc::Geometry_setVertices(
        getIE(), reinterpret_cast<const float *>(vr), 2 * numPrimitives, 4);
    if (clamped) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "'primitive.index' has indices past the %zu vertices; "
          "they use the last vertex",
          numVertices);
    }

    if (m_index)
      ispc::Geometry_setConeIndices(getIE(), packedIndices, numPrimitives);

    // Primitive i spans Embree vertices 2 * i and 2 * i + 1
    auto *starts = (uint32_t *)rtcSetNewGeometryBuffer(embreeGeometry(),
        RTC_BUFFER_TYPE_INDEX,
        0,
        RTC_FORMAT_UINT,
        sizeof(uint32_t),
        numPrimitives);
    for (size_t i = 0; i < numPrimitives; i++)
      starts[i] = uint32_t(2 * i);

    if (m_primitiveId && m_primitiveId->size() < numPrimitives) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "'primitive.id' has %zu elements for %zu %ss; "
          "the rest report their primitive index",
          m_primitiveId->size(),
          numPrimitives,
          subtype());
    }

    rtcCommitGeometry(embreeGeometry());
    return true;
  }

 private:
  const char *subtype() const
  {
    return m_radiusSource == RadiusSource::VERTEX ? "cone" : "cylinder";
  }

  const char *radiusParameter() const
  {
    return m_radiusSource == RadiusSource::VERTEX ? "vertex.radius"
                                                  : "primitive.radius";
  }

  RadiusSource m_radiusSource;
  helium::ChangeObserverPtr<Array1D> m_index;
  helium::ChangeObserverPtr<Array1D> m_vertexPosition;
  helium::ChangeObserverPtr<Array1D> m_radius;
  float m_globalRadius{DEFAULT_RADIUS};
  // UINT64 'primitive.index' converted to uint32 (UINT32 is shared)
  std::vector<uint32_t> m_indexData;
};

// Geometry definitions ///////////////////////////////////////////////////////

Geometry::Geometry(HalcyonDeviceGlobalState *s)
    : Object(ANARI_GEOMETRY, s, ispc::sizeof_Geometry()),
      m_primitiveAttr{{{this}, {this}, {this}, {this}, {this}}},
      m_vertexAttr{{{this}, {this}, {this}, {this}, {this}}},
      m_faceVaryingAttr{{{this}, {this}, {this}, {this}, {this}}},
      m_primitiveId(this),
      m_vertexNormal(this),
      m_faceVaryingNormal(this),
      m_vertexTangent(this),
      m_faceVaryingTangent(this)
{}

Geometry::~Geometry()
{
  rtcReleaseGeometry(m_embreeGeometry);
}

Geometry *Geometry::createInstance(
    std::string_view subtype, HalcyonDeviceGlobalState *s)
{
  if (subtype == "triangle")
    return new Triangle(s);
  else if (subtype == "quad")
    return new Quad(s);
  else if (subtype == "sphere")
    return new Sphere(s);
  else if (subtype == "curve")
    return new Curve(s);
  else if (subtype == "cylinder")
    return new LinearCone(s, LinearCone::RadiusSource::PRIMITIVE);
  else if (subtype == "cone")
    return new LinearCone(s, LinearCone::RadiusSource::VERTEX);
  else
    return (Geometry *)new UnknownObject(
        ANARI_GEOMETRY, s, ispc::sizeof_Geometry());
}

RTCGeometry Geometry::embreeGeometry() const
{
  return m_embreeGeometry;
}

void Geometry::commitParameters()
{
  for (int i = 0; i < NUM_ATTRIBUTES; i++) {
    float4 attrV = DEFAULT_ATTRIBUTE_VALUE;
    m_uniformAttr[i].reset();
    if (getParam(ATTRIBUTE_NAMES[i], ANARI_FLOAT32_VEC4, &attrV))
      m_uniformAttr[i] = attrV;
  }
  for (int i = 0; i < NUM_ATTRIBUTES; i++) {
    const std::string name = ATTRIBUTE_NAMES[i];
    m_primitiveAttr[i] = getParamObject<Array1D>("primitive." + name);
    m_vertexAttr[i] = getParamObject<Array1D>("vertex." + name);
  }
  m_primitiveId = getParamObject<Array1D>("primitive.id");
  if (m_primitiveId && m_primitiveId->elementType() != ANARI_UINT32
      && m_primitiveId->elementType() != ANARI_UINT64) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "'primitive.id' array elements are %s, but need to be %s or %s",
        anari::toString(m_primitiveId->elementType()),
        anari::toString(ANARI_UINT32),
        anari::toString(ANARI_UINT64));
    m_primitiveId = nullptr;
  }
}

void Geometry::commitFaceVaryingAttributes()
{
  for (int i = 0; i < NUM_ATTRIBUTES; i++) {
    m_faceVaryingAttr[i] = getParamObject<Array1D>(
        std::string("faceVarying.") + ATTRIBUTE_NAMES[i]);
  }
}

void Geometry::commitNormals()
{
  auto commitNormal = [&](helium::ChangeObserverPtr<Array1D> &normal,
                          const char *name) {
    normal = getParamObject<Array1D>(name);
    if (normal && normal->elementType() != ANARI_FLOAT32_VEC3
        && normal->elementType() != ANARI_FIXED16_VEC3) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "'%s' array elements are %s, but need to be %s or %s; "
          "it is ignored",
          name,
          anari::toString(normal->elementType()),
          anari::toString(ANARI_FLOAT32_VEC3),
          anari::toString(ANARI_FIXED16_VEC3));
      normal = nullptr;
    }
  };
  commitNormal(m_vertexNormal, "vertex.normal");
  commitNormal(m_faceVaryingNormal, "faceVarying.normal");
}

void Geometry::commitTangents()
{
  auto commitTangent = [&](helium::ChangeObserverPtr<Array1D> &tangent,
                           const char *name) {
    tangent = getParamObject<Array1D>(name);
    if (!tangent)
      return;
    const anari::DataType type = tangent->elementType();
    if (type != ANARI_FLOAT32_VEC3 && type != ANARI_FLOAT32_VEC4
        && type != ANARI_FIXED16_VEC3 && type != ANARI_FIXED16_VEC4) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "'%s' array elements are %s, but need to be %s, %s, %s or %s; "
          "it is ignored",
          name,
          anari::toString(type),
          anari::toString(ANARI_FLOAT32_VEC3),
          anari::toString(ANARI_FLOAT32_VEC4),
          anari::toString(ANARI_FIXED16_VEC3),
          anari::toString(ANARI_FIXED16_VEC4));
      tangent = nullptr;
    }
  };
  commitTangent(m_vertexTangent, "vertex.tangent");
  commitTangent(m_faceVaryingTangent, "faceVarying.tangent");
}

void Geometry::finalizeTangentFrames(uint32_t cornersPerPrimitive,
    size_t numPrimitives,
    const uint32_t *indices,
    const float3 *positions,
    size_t numPositions)
{
  MeshTangentInput mesh;
  mesh.cornersPerPrimitive = cornersPerPrimitive;
  mesh.numPrimitives = numPositions > 0 ? numPrimitives : 0;
  mesh.indices = indices;
  mesh.positions = positions;
  mesh.numPositions = numPositions;
  mesh.faceVaryingNormals = m_faceVaryingNormalData.data();
  mesh.numFaceVaryingNormals = m_faceVaryingNormalData.size();
  mesh.vertexNormals = m_vertexNormalData.data();
  mesh.numVertexNormals = m_vertexNormalData.size();
  const size_t numCorners = mesh.numCorners();
  m_cornerTangentData.reset(new float[4 * numCorners]);
  m_cornerBitangentData.reset(new float[4 * numCorners]);
  auto *tangents = reinterpret_cast<float4 *>(m_cornerTangentData.get());
  auto *bitangents = reinterpret_cast<float4 *>(m_cornerBitangentData.get());

  // authored tangents, face-varying ones first, as for normals
  const Array1D *authored = nullptr;
  if (m_faceVaryingTangent && m_faceVaryingTangent->size() > 0)
    authored = m_faceVaryingTangent.get();
  else if (m_vertexTangent && m_vertexTangent->size() > 0)
    authored = m_vertexTangent.get();
  if (authored) {
    const std::vector<float4> values = toTangents(authored);
    const bool faceVarying = authored == m_faceVaryingTangent.get();
    parallelFor(numCorners, [&](size_t begin, size_t end) {
      authoredTangentFrames(mesh,
          values.data(),
          values.size(),
          faceVarying,
          begin,
          end,
          tangents,
          bitangents);
    });
  } else if (numCorners > 0) {
    // packed float3 per corner, left uninitialized
    std::unique_ptr<float[]> dPdu(new float[3 * numCorners]);
    std::unique_ptr<float[]> dPdv(new float[3 * numCorners]);
    parallelFor(numCorners, [&](size_t begin, size_t end) {
      ispc::Geometry_getCornerDerivatives(getIE(),
          TANGENT_TEXCOORD_ATTRIBUTE,
          cornersPerPrimitive,
          begin,
          end,
          dPdu.get(),
          dPdv.get());
    });
    const MeshCornersByVertex byVertex(mesh, parallelFor);
    parallelFor(byVertex.numVertices(), [&](size_t begin, size_t end) {
      smoothTangentFrames(mesh,
          reinterpret_cast<const float3 *>(dPdu.get()),
          reinterpret_cast<const float3 *>(dPdv.get()),
          byVertex,
          begin,
          end,
          tangents,
          bitangents);
    });
  }
  ispc::Geometry_setTangentFrames(getIE(),
      m_cornerTangentData.get(),
      m_cornerBitangentData.get(),
      numCorners);
}

void Geometry::checkFaceVaryingSizes(
    size_t corners, size_t numPrimitives, const char *primitives) const
{
  auto check = [&](const Array1D *a, const char *name) {
    if (a && a->size() < corners) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "'faceVarying.%s' has %zu elements for the %zu corners of %zu %s; "
          "the rest use its last element",
          name,
          a->size(),
          corners,
          numPrimitives,
          primitives);
    }
  };
  for (int i = 0; i < NUM_ATTRIBUTES; i++)
    check(m_faceVaryingAttr[i].get(), ATTRIBUTE_NAMES[i]);
  check(m_faceVaryingNormal.get(), "normal");
  check(m_faceVaryingTangent.get(), "tangent");
}

bool Geometry::isValid() const
{
  return m_valid;
}

void Geometry::finalize()
{
  finalizePrimitiveIds();
  finalizeAttributes();
  finalizeNormals();
  // the subtype sets them again once it has built its vertices
  ispc::Geometry_setVertices(getIE(), nullptr, 0, 0);
  ispc::Geometry_setTangentFrames(getIE(), nullptr, nullptr, 0);
  m_cornerTangentData.reset();
  m_cornerBitangentData.reset();
  m_valid = finalizeSubtype();
}

void Geometry::markFinalized()
{
  Object::markFinalized();
  auto &updates = deviceState()->objectUpdates;
  updates.lastBLSCommitSceneRequest = helium::newTimeStamp();
  // groups pick their surfaces by validity, and attach their Embree
  // geometry, when they rebuild their scenes
  if (m_valid != m_validInScenes || m_embreeGeometryReplaced) {
    updates.lastBLSReconstructSceneRequest = helium::newTimeStamp();
    m_validInScenes = m_valid;
    m_embreeGeometryReplaced = false;
  }
}

void Geometry::replaceEmbreeGeometry(RTCGeometryType type)
{
  // scenes the old geometry is attached to keep their own reference
  rtcReleaseGeometry(m_embreeGeometry);
  m_embreeGeometry = rtcNewGeometry(deviceState()->embreeDevice, type);
  m_embreeGeometryReplaced = true;
}

void Geometry::finalizePrimitiveIds()
{
  m_primitiveIdData.clear();
  // the ID frame channels are 32-bit, so wider values can't be reported
  if (m_primitiveId && copyAsUint32(*m_primitiveId, m_primitiveIdData)) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "'primitive.id' values above UINT32_MAX are truncated to 32 bits");
  }

  ispc::Geometry_setPrimitiveIds(getIE(),
      m_primitiveIdData.empty() ? nullptr : m_primitiveIdData.data(),
      m_primitiveIdData.size());
}

void Geometry::finalizeAttributes()
{
  for (int i = 0; i < NUM_ATTRIBUTES; i++) {
    m_primitiveAttrData[i] = toAttributeValues(m_primitiveAttr[i].get());
    m_vertexAttrData[i] = toAttributeValues(m_vertexAttr[i].get());
    m_faceVaryingAttrData[i] = toAttributeValues(m_faceVaryingAttr[i].get());
    const float4 value = m_uniformAttr[i].value_or(DEFAULT_ATTRIBUTE_VALUE);
    ispc::Geometry_setAttribute(getIE(),
        i,
        m_uniformAttr[i].has_value(),
        &value.x,
        reinterpret_cast<const float *>(m_primitiveAttrData[i].data()),
        m_primitiveAttrData[i].size(),
        reinterpret_cast<const float *>(m_vertexAttrData[i].data()),
        m_vertexAttrData[i].size(),
        reinterpret_cast<const float *>(m_faceVaryingAttrData[i].data()),
        m_faceVaryingAttrData[i].size());
  }
}

void Geometry::finalizeNormals()
{
  m_vertexNormalData = toNormals(m_vertexNormal.get());
  m_faceVaryingNormalData = toNormals(m_faceVaryingNormal.get());
  ispc::Geometry_setNormals(getIE(),
      reinterpret_cast<const float *>(m_vertexNormalData.data()),
      m_vertexNormalData.size(),
      reinterpret_cast<const float *>(m_faceVaryingNormalData.data()),
      m_faceVaryingNormalData.size());
}

} // namespace halcyon

HALCYON_ANARI_TYPEFOR_DEFINITION(halcyon::Geometry *);
