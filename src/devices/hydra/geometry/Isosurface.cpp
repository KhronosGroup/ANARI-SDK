// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Isosurface.h"

#include <pxr/imaging/hd/tokens.h>
// std
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <unordered_map>

PXR_NAMESPACE_USING_DIRECTIVE

namespace anari_hydra {

// Extraction /////////////////////////////////////////////////////////////////

namespace {

// Cube corners are bit masks (x = 1, y = 2, z = 4). The six Kuhn tetrahedra
// all share the 0 -> 7 diagonal, and each is a chain 0 ⊂ a ⊂ a|b ⊂ 7, so every
// tetrahedron edge runs from a corner to a superset corner along one of seven
// directions. Neighbouring cells split their shared faces the same way, which
// makes the extracted surface crack-free.
constexpr int kTets[6][4] = {
    {0, 1, 3, 7},
    {0, 1, 5, 7},
    {0, 2, 3, 7},
    {0, 2, 6, 7},
    {0, 4, 5, 7},
    {0, 4, 6, 7},
};

struct Extractor
{
  const AnariFieldData &grid;
  TessMesh &out;
  float iso{0.f};
  uint32_t primitive{0};
  // (lower grid vertex, direction) -> mesh vertex, for the current isovalue.
  std::unordered_map<uint64_t, int> edgeVertex;

  size_t vertexIndex(int i, int j, int k) const
  {
    return size_t(i)
        + size_t(grid.dims[0]) * (size_t(j) + size_t(grid.dims[1]) * k);
  }

  GfVec3f position(int i, int j, int k) const
  {
    return GfVec3f(grid.origin[0] + float(i) * grid.spacing[0],
        grid.origin[1] + float(j) * grid.spacing[1],
        grid.origin[2] + float(k) * grid.spacing[2]);
  }

  void cell(int i, int j, int k)
  {
    float s[8];
    GfVec3f p[8];
    size_t id[8];
    float lo = INFINITY, hi = -INFINITY;
    for (int c = 0; c < 8; ++c) {
      const int x = i + (c & 1), y = j + ((c >> 1) & 1), z = k + ((c >> 2) & 1);
      id[c] = vertexIndex(x, y, z);
      s[c] = grid.values[id[c]];
      if (!std::isfinite(s[c]))
        return;   // no interpolant across a non-finite sample
      p[c] = position(x, y, z);
      lo = std::min(lo, s[c]);
      hi = std::max(hi, s[c]);
    }
    // A corner is "above" when s > iso; a cell crosses only when it has both.
    if (!(hi > iso) || lo > iso)
      return;
    for (const auto &t : kTets)
      tet(t, s, p, id);
  }

  // Mesh vertex on the edge between corners a ⊂ b of the current cell.
  int edge(int a, int b, const float *s, const GfVec3f *p, const size_t *id)
  {
    const uint64_t key = uint64_t(id[a]) * 8u + uint64_t(b & ~a);
    auto it = edgeVertex.find(key);
    if (it != edgeVertex.end())
      return it->second;
    const float t = (iso - s[a]) / (s[b] - s[a]);
    const int v = int(out.points.size());
    out.points.push_back(p[a] + (p[b] - p[a]) * t);
    edgeVertex.emplace(key, v);
    return v;
  }

  void triangle(int v0, int v1, int v2, const GfVec3f &up)
  {
    const GfVec3f &a = out.points[v0], &b = out.points[v1],
                  &c = out.points[v2];
    const float facing = GfDot(GfCross(b - a, c - a), up);
    if (facing == 0.f)
      return;   // degenerate: the level set touches a grid vertex
    if (facing < 0.f)
      std::swap(v1, v2);
    out.faceVertexCounts.push_back(3);
    out.faceVertexIndices.push_back(v0);
    out.faceVertexIndices.push_back(v1);
    out.faceVertexIndices.push_back(v2);
    out.primitiveIndices.push_back(primitive);
  }

  void tet(const int (&t)[4], const float *s, const GfVec3f *p, const size_t *id)
  {
    int above[4], below[4], na = 0, nb = 0;
    GfVec3f aboveSum(0.f), belowSum(0.f);
    for (int v = 0; v < 4; ++v) {
      if (s[t[v]] > iso) {
        above[na++] = v;
        aboveSum += p[t[v]];
      } else {
        below[nb++] = v;
        belowSum += p[t[v]];
      }
    }
    if (na == 0 || nb == 0)
      return;
    // The interpolant is linear in a tetrahedron, so the above-corner mean
    // lies on the high side of the level plane: this orients every face
    // toward higher field values.
    const GfVec3f up = aboveSum / float(na) - belowSum / float(nb);
    // Tetrahedron corners are listed in chain order, so the lower local index
    // is always the subset corner edge() expects.
    auto edgeVertexOf = [&](int u, int v) {
      return u < v ? edge(t[u], t[v], s, p, id) : edge(t[v], t[u], s, p, id);
    };
    if (na == 1 || nb == 1) {
      const int apex = na == 1 ? above[0] : below[0];
      const int *rest = na == 1 ? below : above;
      triangle(edgeVertexOf(apex, rest[0]),
          edgeVertexOf(apex, rest[1]),
          edgeVertexOf(apex, rest[2]),
          up);
    } else {
      const int a = above[0], b = above[1], c = below[0], d = below[1];
      const int ac = edgeVertexOf(a, c), ad = edgeVertexOf(a, d);
      const int bd = edgeVertexOf(b, d), bc = edgeVertexOf(b, c);
      triangle(ac, ad, bd, up);
      triangle(ac, bd, bc, up);
    }
  }
};

} // namespace

void ExtractIsosurface(const AnariFieldData &grid,
    const std::vector<float> &isovalues,
    TessMesh &out)
{
  out = TessMesh();
  const GfVec3i &n = grid.dims;
  if (n[0] < 2 || n[1] < 2 || n[2] < 2
      || grid.values.size() < size_t(n[0]) * n[1] * n[2])
    return;
  Extractor extractor{grid, out};
  for (size_t v = 0; v < isovalues.size(); ++v) {
    if (!std::isfinite(isovalues[v]))
      continue;
    extractor.iso = isovalues[v];
    extractor.primitive = uint32_t(v);
    extractor.edgeVertex.clear();
    for (int k = 0; k + 1 < n[2]; ++k)
      for (int j = 0; j + 1 < n[1]; ++j)
        for (int i = 0; i + 1 < n[0]; ++i)
          extractor.cell(i, j, k);
  }
  // Drop the vertices only degenerate faces used: Storm rejects a mesh whose
  // points outnumber the vertices its topology references.
  std::vector<int> remap(out.points.size(), -1);
  VtVec3fArray points;
  points.reserve(out.points.size());
  for (int &v : out.faceVertexIndices) {
    if (remap[v] < 0) {
      remap[v] = int(points.size());
      points.push_back(out.points[v]);
    }
    v = remap[v];
  }
  out.points = std::move(points);
}

// Geometry ///////////////////////////////////////////////////////////////////

Isosurface::Isosurface(HydraGlobalState *s)
    : Geometry(s), m_field(this), m_isovalueArray(this)
{}

void Isosurface::commitParameters()
{
  Geometry::commitParameters();
  // The extracted surface has no ANARI vertices: vertex, face-varying and
  // index arrays do not apply (KHR_GEOMETRY_ISOSURFACE lists none).
  m_vertexPosition = nullptr;
  m_vertexNormal = nullptr;
  m_vertexColor = nullptr;
  m_vertexRadius = nullptr;
  m_primitiveRadius = nullptr;
  m_vertexCap = nullptr;
  m_vertexAttribute0 = nullptr;
  m_vertexAttribute1 = nullptr;
  m_vertexAttribute2 = nullptr;
  m_vertexAttribute3 = nullptr;
  m_faceVaryingNormal = nullptr;
  m_faceVaryingColor = nullptr;
  m_faceVaryingAttribute0 = nullptr;
  m_faceVaryingAttribute1 = nullptr;
  m_faceVaryingAttribute2 = nullptr;
  m_faceVaryingAttribute3 = nullptr;
  m_index = nullptr;

  m_field = getParamObject<SpatialField>("field");
  m_isovalueArray = getParamObject<helium::Array1D>("isovalue");
  m_hasScalarIsovalue = getParam("isovalue", ANARI_FLOAT32, &m_isovalue);
}

void Isosurface::finalize()
{
  Geometry::finalize();
  m_mesh = TessMesh();

  std::vector<float> isovalues;
  if (helium::Array1D *a = m_isovalueArray.get()) {
    if (a->elementType() != ANARI_FLOAT32) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "isosurface 'isovalue' array must be FLOAT32; nothing will render");
      return;
    }
    const float *v = static_cast<const float *>(a->data());
    isovalues.assign(v, v + a->size());
  } else if (m_hasScalarIsovalue) {
    isovalues.push_back(m_isovalue);
  }
  if (isovalues.empty()) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "isosurface geometry needs one or more values in 'isovalue'");
    return;
  }
  if (!m_field) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "isosurface geometry is missing its required 'field' parameter");
    return;
  }
  // An invalid field has no snapshot; its commit re-finalizes this geometry
  // through the field observer.
  const FieldSnapshotPtr field = m_field->snapshot();
  if (!field)
    return;

  // Primitive arrays are indexed by isovalue. A short attribute array is
  // dropped whole when authored (PrimBuilder) and a short primitive.id reads
  // "no ID" past its end (pickingIds); VisRTX drops both, so warn.
  const char *names[] = {
      "attribute0", "attribute1", "attribute2", "attribute3", "color"};
  auto warnShort = [&](const helium::Array1D *a, const char *name) {
    if (a && a->size() < isovalues.size())
      reportMessage(ANARI_SEVERITY_WARNING,
          "isosurface 'primitive.%s' has %zu element(s) for %zu isovalue(s)",
          name,
          a->size(),
          isovalues.size());
  };
  for (int k = 0; k < 5; ++k)
    warnShort(primitiveAttribute(k), names[k]);
  warnShort(m_primitiveId.get(), "id");

  const auto t0 = std::chrono::steady_clock::now();
  ExtractIsosurface(*field->grid, isovalues, m_mesh);
  const double ms = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - t0)
                        .count();
  // Resident for the geometry's lifetime: 12 bytes per point and 20 per
  // triangle (three indices, face count, isovalue index); the mirror shares
  // the point and topology arrays (VtArray copy-on-write).
  reportMessage(ANARI_SEVERITY_DEBUG,
      "isosurface extracted %zu triangle(s), %zu vertices from %d x %d x %d "
      "samples, %zu isovalue(s) in %.2f ms",
      m_mesh.faceVertexCounts.size(),
      m_mesh.points.size(),
      field->grid->dims[0],
      field->grid->dims[1],
      field->grid->dims[2],
      isovalues.size(),
      ms);
}

bool Isosurface::isValid() const
{
  return !m_mesh.faceVertexCounts.empty();
}

TfToken Isosurface::primType() const
{
  return HdPrimTypeTokens->mesh;
}

} // namespace anari_hydra
