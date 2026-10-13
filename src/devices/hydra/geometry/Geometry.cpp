// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Geometry.h"
#include <pxr/imaging/hd/tokens.h>
#include "Cone.h"
#include "Curve.h"
#include "Cylinder.h"
#include "Isosurface.h"
#include "Quad.h"
#include "Sphere.h"
#include "Triangle.h"

namespace anari_hydra {

Geometry::Geometry(HydraGlobalState *s)
    : Object(ANARI_GEOMETRY, s),
      m_vertexPosition(this),
      m_vertexNormal(this),
      m_vertexColor(this),
      m_vertexRadius(this),
      m_primitiveRadius(this),
      m_vertexCap(this),
      m_vertexAttribute0(this),
      m_vertexAttribute1(this),
      m_vertexAttribute2(this),
      m_vertexAttribute3(this),
      m_faceVaryingNormal(this),
      m_faceVaryingColor(this),
      m_faceVaryingAttribute0(this),
      m_faceVaryingAttribute1(this),
      m_faceVaryingAttribute2(this),
      m_faceVaryingAttribute3(this),
      m_index(this),
      m_primitiveId(this),
      m_primitiveAttributes{this, this, this, this, this}
{}

Geometry *Geometry::createInstance(
    std::string_view subtype, HydraGlobalState *s)
{
  if (subtype == "triangle")
    return new Triangle(s);
  else if (subtype == "quad")
    return new Quad(s);
  else if (subtype == "sphere")
    return new Sphere(s);
  else if (subtype == "curve")
    return new Curve(s);
  else if (subtype == "cone")
    return new Cone(s);
  else if (subtype == "cylinder")
    return new Cylinder(s);
  else if (subtype == "isosurface")
    return new Isosurface(s);
  else
    return (Geometry *)new UnknownObject(ANARI_GEOMETRY, s);
}

void Geometry::commitParameters()
{
  m_vertexPosition = getParamObject<helium::Array1D>("vertex.position");
  m_vertexNormal = getParamObject<helium::Array1D>("vertex.normal");
  m_vertexColor = getParamObject<helium::Array1D>("vertex.color");
  m_vertexRadius = getParamObject<helium::Array1D>("vertex.radius");
  m_primitiveRadius = getParamObject<helium::Array1D>("primitive.radius");
  m_vertexCap = getParamObject<helium::Array1D>("vertex.cap");
  m_vertexAttribute0 = getParamObject<helium::Array1D>("vertex.attribute0");
  m_vertexAttribute1 = getParamObject<helium::Array1D>("vertex.attribute1");
  m_vertexAttribute2 = getParamObject<helium::Array1D>("vertex.attribute2");
  m_vertexAttribute3 = getParamObject<helium::Array1D>("vertex.attribute3");
  m_faceVaryingNormal = getParamObject<helium::Array1D>("faceVarying.normal");
  m_faceVaryingColor = getParamObject<helium::Array1D>("faceVarying.color");
  m_faceVaryingAttribute0 = getParamObject<helium::Array1D>("faceVarying.attribute0");
  m_faceVaryingAttribute1 = getParamObject<helium::Array1D>("faceVarying.attribute1");
  m_faceVaryingAttribute2 = getParamObject<helium::Array1D>("faceVarying.attribute2");
  m_faceVaryingAttribute3 = getParamObject<helium::Array1D>("faceVarying.attribute3");
  m_index = getParamObject<helium::Array1D>("primitive.index");
  m_primitiveId = getParamObject<helium::Array1D>("primitive.id");
  m_radius = getParam<float>("radius", 1.f);
  const char *names[] = {"attribute0", "attribute1", "attribute2", "attribute3", "color"};
  for (int i = 0; i < 5; ++i) {
    m_primitiveAttributes[i] = getParamObject<helium::Array1D>(
        std::string("primitive.") + names[i]);
    m_hasConstantAttribute[i] = hasParam(names[i], ANARI_FLOAT32_VEC4);
    m_constantAttributes[i] = getParam<helium::float4>(names[i], helium::float4(0.f, 0.f, 0.f, 1.f));
  }
}

std::vector<uint32_t> Geometry::pickingIds(bool triangulated) const
{
  std::vector<uint32_t> ids;
  if (const auto *mesh = tessellation())
    ids = mesh->primitiveIndices;
  else if (primType() == pxr::HdPrimTypeTokens->basisCurves && index()
      && index()->size() > 0) {
    const auto *indices = static_cast<const uint32_t *>(index()->data());
    for (size_t i = 0; i < index()->size(); ++i)
      if (size_t(indices[i]) + 1 < vertexPosition()->size())
        ids.push_back(uint32_t(i));
  } else {
    size_t n = vertexPosition()->size();
    if (primType() == pxr::HdPrimTypeTokens->mesh)
      n = index() && index()->size() > 0 ? index()->size()
                                         : n / meshVertsPerFace();
    else if (primType() == pxr::HdPrimTypeTokens->basisCurves)
      n = n > 0 ? n - 1 : 0;
    for (size_t i = 0; i < n; ++i)
      ids.push_back(uint32_t(i));
  }
  if (m_primitiveId) {
    for (auto &id : ids) {
      if (id >= m_primitiveId->size()) {
        id = ~0u;
      } else if (m_primitiveId->elementType() == ANARI_UINT32) {
        id = static_cast<const uint32_t *>(m_primitiveId->data())[id];
      } else if (m_primitiveId->elementType() == ANARI_UINT64) {
        id = uint32_t(static_cast<const uint64_t *>(m_primitiveId->data())[id]);
      }
    }
  }
  if (triangulated && primType() == pxr::HdPrimTypeTokens->mesh) {
    std::vector<uint32_t> triangles;
    const auto *mesh = tessellation();
    for (size_t face = 0; face < ids.size(); ++face) {
      const int vertices =
          mesh ? mesh->faceVertexCounts[face] : meshVertsPerFace();
      for (int triangle = 0; triangle < vertices - 2; ++triangle)
        triangles.push_back(ids[face]);
    }
    return triangles;
  }
  return ids;
}

Bounds Geometry::bounds() const
{
  Bounds result;
  if (!isValid())
    return result;
  if (const auto *mesh = tessellation()) {
    for (const auto &p : mesh->points)
      result.UnionWith(p);
    return result;
  }
  const auto *points = static_cast<const pxr::GfVec3f *>(vertexPosition()->data());
  const size_t count = vertexPosition()->size();
  const auto *indices = index() && index()->size() > 0
      ? static_cast<const uint32_t *>(index()->data()) : nullptr;
  if (primType() == pxr::HdPrimTypeTokens->basisCurves) {
    const auto *radii = vertexRadius()
        ? static_cast<const float *>(vertexRadius()->data()) : nullptr;
    auto extend = [&](size_t vertex) {
      if (radii && vertex >= vertexRadius()->size())
        return;
      const pxr::GfVec3f radius(radii ? radii[vertex] : globalRadius());
      result.UnionWith(points[vertex] - radius);
      result.UnionWith(points[vertex] + radius);
    };
    const size_t segments = indices ? index()->size() : (count ? count - 1 : 0);
    for (size_t i = 0; i < segments; ++i) {
      const size_t first = indices ? indices[i] : i;
      if (first + 1 < count) {
        extend(first);
        extend(first + 1);
      }
    }
  } else {
    const size_t vertices = indices ? index()->size() * meshVertsPerFace()
                                   : count / meshVertsPerFace() * meshVertsPerFace();
    for (size_t i = 0; i < vertices; ++i) {
      const size_t vertex = indices ? indices[i] : i;
      if (vertex < count)
        result.UnionWith(points[vertex]);
    }
  }
  return result;
}

bool Geometry::hasCopiedPrimvars() const
{
  for (const auto &array : m_primitiveAttributes)
    if (array)
      return true;
  for (bool present : m_hasConstantAttribute)
    if (present)
      return true;
  return m_vertexNormal || m_vertexColor || m_vertexAttribute0
      || m_vertexAttribute1 || m_vertexAttribute2 || m_vertexAttribute3
      || m_faceVaryingNormal || m_faceVaryingColor || m_faceVaryingAttribute0
      || m_faceVaryingAttribute1 || m_faceVaryingAttribute2
      || m_faceVaryingAttribute3;
}

void Geometry::finalize()
{
  deviceState()->mirrorDirty.mark(this);
}

bool Geometry::isValid() const
{
  return m_vertexPosition;
}

} // namespace anari_hydra

namespace anari {
ANARI_TYPEFOR_DEFINITION(anari_hydra::Geometry *);
}
