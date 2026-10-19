// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Geometry.isph"
#include "shading/halcyon_util.isph"

// Vertex attribute hooks (Geometry_VertexAttributeFcn) ///////////////////////

// Barycentric interpolation of the values of 'a' at indices 'idx'
static inline float4 Triangle_interpolate(
    const uniform AttributeArray *uniform a, uint3 idx, float2 uv)
{
  return (1.f - uv.x - uv.y) * readAttribute(a, idx.x)
      + uv.x * readAttribute(a, idx.y) + uv.y * readAttribute(a, idx.z);
}

// Barycentric interpolation of the triangle's three vertex values
static varying float4 Triangle_getVertexAttribute(
    const uniform Geometry *uniform self,
    const uniform AttributeArray *uniform a,
    uint primID,
    float2 uv)
{
  return Triangle_interpolate(a, Triangle_indices(self, primID), uv);
}

// Barycentric interpolation of the triangle's three face-varying values,
// which are elements 3 * primID to 3 * primID + 2 whatever the indexing
static varying float4 Triangle_getFaceVaryingAttribute(
    const uniform Geometry *uniform self,
    const uniform AttributeArray *uniform a,
    uint primID,
    float2 uv)
{
  return Triangle_interpolate(a, 3 * primID + make_uint3(0, 1, 2), uv);
}

// Bilinear interpolation of the values of 'a' at indices 'idx'
static inline float4 Quad_interpolate(
    const uniform AttributeArray *uniform a, uint4 idx, float2 uv)
{
  return (1.f - uv.y) * (1.f - uv.x) * readAttribute(a, idx.x)
      + (1.f - uv.y) * uv.x * readAttribute(a, idx.y)
      + uv.y * uv.x * readAttribute(a, idx.z)
      + uv.y * (1.f - uv.x) * readAttribute(a, idx.w);
}

// Bilinear interpolation of the quad's four vertex values
static varying float4 Quad_getVertexAttribute(
    const uniform Geometry *uniform self,
    const uniform AttributeArray *uniform a,
    uint primID,
    float2 uv)
{
  return Quad_interpolate(a, Quad_indices(self, primID), uv);
}

// Bilinear interpolation of the quad's four face-varying values, which are
// elements 4 * primID to 4 * primID + 3 whatever the indexing
static varying float4 Quad_getFaceVaryingAttribute(
    const uniform Geometry *uniform self,
    const uniform AttributeArray *uniform a,
    uint primID,
    float2 uv)
{
  return Quad_interpolate(a, 4 * primID + make_uint4(0, 1, 2, 3), uv);
}

// Spheres don't interpolate: the value of the sphere's vertex
static varying float4 Sphere_getVertexAttribute(
    const uniform Geometry *uniform self,
    const uniform AttributeArray *uniform a,
    uint primID,
    float2 uv)
{
  const uniform uint32 *uniform indices = self->sphere.indices;
  uint32 idx = primID;
  if (indices != NULL) {
    const uint64 p = min((uint64)primID, self->sphere.numSpheres - 1);
#pragma ignore warning(perf)
    idx = indices[p];
  }
  return readAttribute(a, idx);
}

// Interpolation in u with the curve's basis: linear from the segment's start
// vertex to the next, or the cubic's weights of its four control points
// (unclamped: Catmull-Rom may overshoot)
static varying float4 Curve_getVertexAttribute(
    const uniform Geometry *uniform self,
    const uniform AttributeArray *uniform a,
    uint primID,
    float2 uv)
{
  const uint32 idx = Curve_segmentStart(self, primID);
  const uniform CurveBasis basis = self->curve.basis;
  if (basis == CURVE_BASIS_LINEAR) {
    return (1.f - uv.x) * readAttribute(a, idx)
        + uv.x * readAttribute(a, idx + 1);
  }
  // Embree's evaluation order
  const float4 w = Curve_cubicWeights(basis, uv.x);
  return w.x * readAttribute(a, idx)
      + (w.y * readAttribute(a, idx + 1)
          + (w.z * readAttribute(a, idx + 2) + w.w * readAttribute(a, idx + 3)));
}

// Cylinders and cones: linear interpolation in u between the primitive's two
// vertices
static varying float4 Cone_getVertexAttribute(
    const uniform Geometry *uniform self,
    const uniform AttributeArray *uniform a,
    uint primID,
    float2 uv)
{
  const uniform uint32 *uniform indices = self->cone.indices;
  uint2 idx = 2 * primID + make_uint2(0, 1);
  if (indices != NULL) {
    const uint64 p = min((uint64)primID, self->cone.numPrimitives - 1);
#pragma ignore warning(perf)
    idx = make_uint2(indices[2 * p + 0], indices[2 * p + 1]);
  }
  return (1.f - uv.x) * readAttribute(a, idx.x)
      + uv.x * readAttribute(a, idx.y);
}

// Triangle corner hook (Geometry_TriangleCornersFcn) ////////////////////////

// The triangle's corner positions and shading normals (Geometry_normalArray())
static bool Triangle_getCorners(const uniform Geometry *uniform self,
    uint primID,
    varying TriangleCorners &c)
{
  uniform bool fv;
  const uniform AttributeArray *uniform na = Geometry_normalArray(self, fv);
  if (!na || self->vertices.data == NULL)
    return false;
  const uint3 idx = Triangle_indices(self, primID);
  const uint3 i = fv ? 3 * primID + make_uint3(0, 1, 2) : idx;
  c.p0 = Geometry_position(self, idx.x);
  c.p1 = Geometry_position(self, idx.y);
  c.p2 = Geometry_position(self, idx.z);
  c.n0 = make_float3(readAttribute(na, i.x));
  c.n1 = make_float3(readAttribute(na, i.y));
  c.n2 = make_float3(readAttribute(na, i.z));
  return true;
}

// Exported interface /////////////////////////////////////////////////////////

export uniform ulong sizeof_Geometry()
{
  return sizeof(uniform Geometry);
}

export void Geometry_setPrimitiveIds(void *uniform _self,
    const uniform uint32 *uniform ids,
    uniform uint64 numIds)
{
  uniform Geometry *uniform self = object_cast<Geometry>(_self);
  self->primitiveIds = ids;
  self->numPrimitiveIds = numIds;
}

// Attribute 'attr' (< NUM_GEOMETRY_ATTRIBUTES): the uniform value (packed
// float4) and packed float4 primitive/vertex/face-varying arrays (size 0 if
// unset)
export void Geometry_setAttribute(void *uniform _self,
    uniform int attr,
    uniform bool isSet,
    const uniform float *uniform value,
    const uniform float *uniform primitiveData,
    uniform uint64 numPrimitiveValues,
    const uniform float *uniform vertexData,
    uniform uint64 numVertexValues,
    const uniform float *uniform faceVaryingData,
    uniform uint64 numFaceVaryingValues)
{
  uniform Geometry *uniform self = object_cast<Geometry>(_self);
  self->attributes[attr] = load_float4(value);
  if (isSet)
    self->attributesSet |= 1u << attr;
  else
    self->attributesSet &= ~(1u << attr);
  self->primitiveAttributes[attr].data =
      numPrimitiveValues > 0 ? primitiveData : NULL;
  self->primitiveAttributes[attr].size = numPrimitiveValues;
  self->vertexAttributes[attr].data = numVertexValues > 0 ? vertexData : NULL;
  self->vertexAttributes[attr].size = numVertexValues;
  self->faceVaryingAttributes[attr].data =
      numFaceVaryingValues > 0 ? faceVaryingData : NULL;
  self->faceVaryingAttributes[attr].size = numFaceVaryingValues;
}

// 'vertex.normal' and 'faceVarying.normal' as packed float4 (size 0 if unset)
export void Geometry_setNormals(void *uniform _self,
    const uniform float *uniform vertexData,
    uniform uint64 numVertexNormals,
    const uniform float *uniform faceVaryingData,
    uniform uint64 numFaceVaryingNormals)
{
  uniform Geometry *uniform self = object_cast<Geometry>(_self);
  self->vertexNormals.data = numVertexNormals > 0 ? vertexData : NULL;
  self->vertexNormals.size = numVertexNormals;
  self->faceVaryingNormals.data =
      numFaceVaryingNormals > 0 ? faceVaryingData : NULL;
  self->faceVaryingNormals.size = numFaceVaryingNormals;
}

// The tangent frame per primitive corner ('numCorners' packed float4
// tangents and bitangents), NULL/0 for none
export void Geometry_setTangentFrames(void *uniform _self,
    const uniform float *uniform tangents,
    const uniform float *uniform bitangents,
    uniform uint64 numCorners)
{
  uniform Geometry *uniform self = object_cast<Geometry>(_self);
  self->cornerTangents.data = numCorners > 0 ? tangents : NULL;
  self->cornerTangents.size = numCorners;
  self->cornerBitangents.data = numCorners > 0 ? bitangents : NULL;
  self->cornerBitangents.size = numCorners;
}

// Vertex positions for the derivative hooks: 'count' vertices of 'stride'
// floats (x, y, z first); NULL/0 for none (invalid geometry), which the
// derivative hooks must not be called without
export void Geometry_setVertices(void *uniform _self,
    const uniform float *uniform data,
    uniform uint64 count,
    uniform uint32 stride)
{
  uniform Geometry *uniform self = object_cast<Geometry>(_self);
  self->vertices.data = count > 0 ? data : NULL;
  self->vertices.count = count;
  self->vertices.stride = stride;
}

// Installs the triangle vertex and face-varying attribute, derivative, corner
// and surface position hooks; 'indices' is 'primitive.index' (packed uint3),
// NULL/0 for implicit indexing
export void Geometry_setTriangleIndices(void *uniform _self,
    const uniform uint32 *uniform indices,
    uniform uint64 numTriangles)
{
  uniform Geometry *uniform self = object_cast<Geometry>(_self);
  self->triangle.indices = numTriangles > 0 ? indices : NULL;
  self->triangle.numTriangles = numTriangles;
  self->getVertexAttribute = Triangle_getVertexAttribute;
  self->getDerivatives = Triangle_getDerivatives;
  self->getTriangleCorners = Triangle_getCorners;
  self->getSurfacePosition = Triangle_getSurfacePosition;
  self->getFaceVaryingAttribute = Triangle_getFaceVaryingAttribute;
}

// Installs the quad vertex and face-varying attribute, derivative and surface
// position hooks; 'indices' is 'primitive.index' (packed uint4), NULL/0 for
// implicit indexing
export void Geometry_setQuadIndices(void *uniform _self,
    const uniform uint32 *uniform indices,
    uniform uint64 numQuads)
{
  uniform Geometry *uniform self = object_cast<Geometry>(_self);
  self->quad.indices = numQuads > 0 ? indices : NULL;
  self->quad.numQuads = numQuads;
  self->getVertexAttribute = Quad_getVertexAttribute;
  self->getDerivatives = Quad_getDerivatives;
  self->getSurfacePosition = Quad_getSurfacePosition;
  self->getFaceVaryingAttribute = Quad_getFaceVaryingAttribute;
}

// Installs the sphere vertex attribute and derivative hooks; 'indices' is
// 'primitive.index' (uint32), NULL/0 for implicit indexing
export void Geometry_setSphereIndices(void *uniform _self,
    const uniform uint32 *uniform indices,
    uniform uint64 numSpheres)
{
  uniform Geometry *uniform self = object_cast<Geometry>(_self);
  self->sphere.indices = numSpheres > 0 ? indices : NULL;
  self->sphere.numSpheres = numSpheres;
  self->getVertexAttribute = Sphere_getVertexAttribute;
  self->getDerivatives = Sphere_getDerivatives;
}

// Installs the curve vertex attribute and derivative hooks for curves of
// 'basis'; 'indices' is the start vertex of each segment (uint32), NULL/0 for
// implicit indexing
export void Geometry_setCurveIndices(void *uniform _self,
    const uniform uint32 *uniform indices,
    uniform uint64 numSegments,
    uniform CurveBasis basis)
{
  uniform Geometry *uniform self = object_cast<Geometry>(_self);
  self->curve.indices = numSegments > 0 ? indices : NULL;
  self->curve.numSegments = numSegments;
  self->curve.basis = basis;
  self->getVertexAttribute = Curve_getVertexAttribute;
  self->getDerivatives = Curve_getDerivatives;
}

// Installs the cylinder/cone vertex attribute and derivative hooks; 'indices'
// is 'primitive.index' (packed uint2), NULL/0 for implicit indexing
export void Geometry_setConeIndices(void *uniform _self,
    const uniform uint32 *uniform indices,
    uniform uint64 numPrimitives)
{
  uniform Geometry *uniform self = object_cast<Geometry>(_self);
  self->cone.indices = numPrimitives > 0 ? indices : NULL;
  self->cone.numPrimitives = numPrimitives;
  self->getVertexAttribute = Cone_getVertexAttribute;
  self->getDerivatives = Cone_getDerivatives;
}
