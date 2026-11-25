// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: LicenseRef-TOST-1.0

// The surface derivative and surface position hooks (Geometry_DerivativesFcn,
// Geometry_SurfacePositionFcn) that Geometry.ispc's setters install, for the
// pathtracer's shading context, and the corner derivatives the mesh tangent
// frames start from (MeshTangents.h).
//
// Triangle and curve follow Typhoon's mesh and curve frames, and the triangle
// position its ResolveObjectSpaceSurfacePosition()
// (renderer/geometry/surfaceDerivatives.cpp, and the per-triangle dPdu/dPdv
// HdEmbreeMesh::_UpdateSurfaceDerivativeCache() caches in delegate/mesh.cpp);
// quad, sphere and cone are halcyon's own, in the same conventions. Ported
// from Typhoon (NVIDIA-Omniverse/OpenUSD, branch typhoon/main, commit
// 70c45e83b08a7b45588cd6cdbd2ae67f5147befb), copyright 2017-2026 Pixar,
// licensed under the Tomorrow Open Source Technology License 1.0 (see
// THIRD-PARTY.md); keep in step with those files when the pin moves.
//
// Compiled without fused multiply-adds (--opt=disable-fma, CMakeLists.txt),
// so that the derivatives round like Typhoon's C++ (x86-64, no FMA): with
// them, the triangle edges' combination cancels to a residue instead of 0 on
// degenerate triangles, and Typhoon's degenerate frame isn't taken.

#include "Geometry.isph"
#include "shading/halcyon_derivatives.isph"

static inline float2 xy(float4 v)
{
  return make_float2(v.x, v.y);
}

// The array of 'texcoord' whose corner values vary over a triangle or quad:
// its face-varying values, else its vertex values (primitive and uniform
// values are constant over the primitive), or NULL
static inline const uniform AttributeArray *uniform Geometry_texcoordArray(
    const uniform Geometry *uniform self,
    uniform Attribute texcoord,
    uniform bool &faceVarying)
{
  faceVarying = false;
  if (texcoord >= NUM_GEOMETRY_ATTRIBUTES)
    return NULL;
  if (self->getFaceVaryingAttribute
      && self->faceVaryingAttributes[texcoord].size > 0) {
    faceVarying = true;
    return &self->faceVaryingAttributes[texcoord];
  }
  if (self->vertexAttributes[texcoord].size > 0)
    return &self->vertexAttributes[texcoord];
  return NULL;
}

// Chain rule from the primitive's parameters (u, v) to the texture
// coordinates (s, t) whose derivatives along u and v are 'dstdu' and 'dstdv',
// with determinant 'det' (dstdu.x dstdv.y - dstdu.y dstdv.x): replaces 'a'
// and 'b' (derivatives along u and v) by their derivatives along s and t;
// false, leaving them, where the coordinates are degenerate (Typhoon's
// st-space dPdu/dPdv and dndu/dndv of triangles, with its threshold)
static inline bool toTexcoordDerivatives(
    float2 dstdu, float2 dstdv, float det, float3 &a, float3 &b)
{
  if (!(abs(det) > 1e-9f))
    return false;
  const float invDet = 1.f / det;
  const float3 da = (dstdv.y * a - dstdu.y * b) * invDet;
  const float3 db = (-dstdv.x * a + dstdu.x * b) * invDet;
  a = da;
  b = db;
  return true;
}

// The determinant of the texture coordinates' derivatives as Typhoon takes it
// for dPdu/dPdv (its mesh's derivative cache: plain float products)
static inline float positionDet(float2 dstdu, float2 dstdv)
{
  return dstdu.x * dstdv.y - dstdu.y * dstdv.x;
}

// ... and for dndu/dndv (ComputeTriangleSurfaceDerivatives(): without the
// cancellation, DifferenceOfProducts())
static inline float normalDet(float2 dstdu, float2 dstdv)
{
  return differenceOfProducts(dstdu.x, dstdv.y, dstdu.y, dstdv.x);
}

// Position derivatives 'dPdu', 'dPdv' made a frame where they are degenerate
// (Typhoon: the orthonormal frame around the face normal 'faceNormal', or
// around +z where that is degenerate too)
static inline void fixDegenerateFrame(
    float3 faceNormal, varying SurfaceDerivatives &d)
{
  if (length2(cross(d.dPdu, d.dPdv)) < 1e-18f) {
    float3 n = make_float3(0.f, 0.f, 1.f);
    if (length2(faceNormal) > 1e-18f)
      n = normalize(faceNormal);
    buildOrthonormalFrame(n, d.dPdu, d.dPdv);
  }
}

// Derivatives of the unit normal from those of the interpolated (unnormalized)
// normal 'sampled', faced like the shading normal 'N' (Typhoon); zero where
// 'sampled' is degenerate
static inline void normalizeNormalDerivatives(
    float3 sampled, float3 N, varying SurfaceDerivatives &d)
{
  const float l2 = dot(sampled, sampled);
  if (isFinite(sampled) && isfinite(l2) && l2 > 1e-18f) {
    const float l = sqrt(l2);
    const float3 un = sampled / l;
    d.dndu = (d.dndu - un * dot(un, d.dndu)) / l;
    d.dndv = (d.dndv - un * dot(un, d.dndv)) / l;
    if (dot(un, N) < 0.f) {
      d.dndu = -d.dndu;
      d.dndv = -d.dndv;
    }
  } else {
    d.dndu = make_float3(0.f);
    d.dndv = make_float3(0.f);
  }
}

// Embree's barycentric parameterization P = (1-u-v) p0 + u p1 + v p2: dPdu
// and dPdv are the edges from p0, taken to the texture coordinates' (s, t)
// where they vary over the triangle (Typhoon's cached per-triangle dPdu/dPdv);
// dndu/dndv from the corner normals, along the same parameters
void Triangle_getDerivatives(const uniform Geometry *uniform self,
    uniform Attribute texcoord,
    uint primID,
    float2 uv,
    float3 P,
    float3 N,
    varying SurfaceDerivatives &d)
{
  const uint3 idx = Triangle_indices(self, primID);
  const uint3 corners = 3 * primID + make_uint3(0, 1, 2);
  const float3 p0 = Geometry_position(self, idx.x);
  const float3 dP1 = Geometry_position(self, idx.y) - p0;
  const float3 dP2 = Geometry_position(self, idx.z) - p0;
  d.dPdu = dP1;
  d.dPdv = dP2;

  uniform bool fv;
  const uniform AttributeArray *uniform st =
      Geometry_texcoordArray(self, texcoord, fv);
  bool stVaries = false;
  float2 dst1, dst2;
  if (st) {
    const uint3 i = fv ? corners : idx;
    const float2 st0 = xy(readAttribute(st, i.x));
    dst1 = xy(readAttribute(st, i.y)) - st0;
    dst2 = xy(readAttribute(st, i.z)) - st0;
    stVaries = true;
    toTexcoordDerivatives(dst1, dst2, positionDet(dst1, dst2), d.dPdu, d.dPdv);
  }
  fixDegenerateFrame(cross(dP1, dP2), d);

  d.dndu = make_float3(0.f);
  d.dndv = make_float3(0.f);
  const uniform AttributeArray *uniform na = Geometry_normalArray(self, fv);
  if (na) {
    const uint3 i = fv ? corners : idx;
    const float3 n0 = make_float3(readAttribute(na, i.x));
    const float3 e1 = make_float3(readAttribute(na, i.y)) - n0;
    const float3 e2 = make_float3(readAttribute(na, i.z)) - n0;
    const float3 sampled = n0 + uv.x * e1 + uv.y * e2;
    d.dndu = e1;
    d.dndv = e2;
    if (stVaries)
      toTexcoordDerivatives(dst1, dst2, normalDet(dst1, dst2), d.dndu, d.dndv);
    normalizeNormalDerivatives(sampled, N, d);
  }
}

// The bilinear parameterization the quad's attributes interpolate with
// (Quad_interpolate()): derivatives of the bilinear patch through the four
// vertices, along (u, v) or the texture coordinates' (s, t); dndu/dndv from
// the bilinear corner normals. The same conventions as Triangle's; Typhoon
// has no quads (it triangulates them).
void Quad_getDerivatives(const uniform Geometry *uniform self,
    uniform Attribute texcoord,
    uint primID,
    float2 uv,
    float3 P,
    float3 N,
    varying SurfaceDerivatives &d)
{
  const uint4 idx = Quad_indices(self, primID);
  const uint4 corners = 4 * primID + make_uint4(0, 1, 2, 3);
  const float u = uv.x;
  const float v = uv.y;
  const float3 p0 = Geometry_position(self, idx.x);
  const float3 p1 = Geometry_position(self, idx.y);
  const float3 p2 = Geometry_position(self, idx.z);
  const float3 p3 = Geometry_position(self, idx.w);
  d.dPdu = (1.f - v) * (p1 - p0) + v * (p2 - p3);
  d.dPdv = (1.f - u) * (p3 - p0) + u * (p2 - p1);
  const float3 faceNormal = cross(d.dPdu, d.dPdv);

  uniform bool fv;
  const uniform AttributeArray *uniform st =
      Geometry_texcoordArray(self, texcoord, fv);
  bool stVaries = false;
  float2 dstdu, dstdv;
  if (st) {
    const uint4 i = fv ? corners : idx;
    const float2 st0 = xy(readAttribute(st, i.x));
    const float2 st1 = xy(readAttribute(st, i.y));
    const float2 st2 = xy(readAttribute(st, i.z));
    const float2 st3 = xy(readAttribute(st, i.w));
    dstdu = (1.f - v) * (st1 - st0) + v * (st2 - st3);
    dstdv = (1.f - u) * (st3 - st0) + u * (st2 - st1);
    stVaries = true;
    toTexcoordDerivatives(
        dstdu, dstdv, positionDet(dstdu, dstdv), d.dPdu, d.dPdv);
  }
  fixDegenerateFrame(faceNormal, d);

  d.dndu = make_float3(0.f);
  d.dndv = make_float3(0.f);
  const uniform AttributeArray *uniform na = Geometry_normalArray(self, fv);
  if (na) {
    const uint4 i = fv ? corners : idx;
    const float3 n0 = make_float3(readAttribute(na, i.x));
    const float3 n1 = make_float3(readAttribute(na, i.y));
    const float3 n2 = make_float3(readAttribute(na, i.z));
    const float3 n3 = make_float3(readAttribute(na, i.w));
    const float3 sampled = (1.f - v) * (1.f - u) * n0 + (1.f - v) * u * n1
        + v * u * n2 + v * (1.f - u) * n3;
    d.dndu = (1.f - v) * (n1 - n0) + v * (n2 - n3);
    d.dndv = (1.f - u) * (n3 - n0) + u * (n2 - n1);
    if (stVaries)
      toTexcoordDerivatives(
          dstdu, dstdv, normalDet(dstdu, dstdv), d.dndu, d.dndv);
    normalizeNormalDerivatives(sampled, N, d);
  }
}

// Longitude and latitude around the sphere's center c: u = phi / 2pi
// counterclockwise around +z from +x, v = latitude from 0 at the -z pole to
// 1 at the +z pole, so dPdu x dPdv points out. At the poles, where dPdu
// vanishes, the orthonormal frame around N. The unit normal's derivatives are
// the position's over the radius. Typhoon's sphere points have no
// parameterization (the orthonormal frame, no normal derivatives).
void Sphere_getDerivatives(const uniform Geometry *uniform self,
    uniform Attribute texcoord,
    uint primID,
    float2 uv,
    float3 P,
    float3 N,
    varying SurfaceDerivatives &d)
{
  const float3 c = Geometry_position(self, primID);
  const float3 p = P - c;
  const float r = length(p);
  const float rho = sqrt(p.x * p.x + p.y * p.y);
  if (!(rho > 1e-6f * r)) {
    buildOrthonormalFrame(N, d.dPdu, d.dPdv);
    d.dndu = make_float3(0.f);
    d.dndv = make_float3(0.f);
    return;
  }
  const float cosPhi = p.x / rho;
  const float sinPhi = p.y / rho;
  d.dPdu = (2.f * PI) * make_float3(-p.y, p.x, 0.f);
  d.dPdv = PI * make_float3(-p.z * cosPhi, -p.z * sinPhi, rho);
  d.dndu = d.dPdu / r;
  d.dndv = d.dPdv / r;
}

// Typhoon's curve frame (ComputeCurveSurfaceDerivatives()) at 'u' from the
// centerline's 'derivative' in u at the surface normal 'N': dPdu the
// derivative projected off N (its length kept), dPdv the unit N x dPdu; the
// orthonormal frame around N where that is degenerate. No normal
// derivatives.
static inline void Curve_frame(
    float3 derivative, float u, float3 N, varying SurfaceDerivatives &d)
{
  d.dndu = make_float3(0.f);
  d.dndv = make_float3(0.f);
  float3 n;
  if (!tryNormalizeDirection(N, n))
    n = make_float3(0.f, 0.f, 1.f);

  const float3 tangent = derivative - n * dot(n, derivative);
  float3 tangentDirection, bitangent;
  if (!isfinite(u) || u < 0.f || u > 1.f || !isFinite(derivative)
      || !tryNormalizeDirection(tangent, tangentDirection)
      || !tryNormalizeDirection(cross(n, tangentDirection), bitangent)) {
    buildOrthonormalFrame(n, d.dPdu, d.dPdv);
    return;
  }
  d.dPdu = tangent;
  d.dPdv = bitangent;
}

// Typhoon's curve frame on the hit segment: of the line from its start
// vertex to the next, or of the cubic over its four control points (the
// derivative Embree's rtcInterpolate() gives Typhoon)
void Curve_getDerivatives(const uniform Geometry *uniform self,
    uniform Attribute texcoord,
    uint primID,
    float2 uv,
    float3 P,
    float3 N,
    varying SurfaceDerivatives &d)
{
  const uint32 idx = Curve_segmentStart(self, primID);
  const uniform CurveBasis basis = self->curve.basis;
  float3 derivative;
  if (basis == CURVE_BASIS_LINEAR) {
    derivative =
        Geometry_position(self, idx + 1) - Geometry_position(self, idx);
  } else {
    // relative to the second control point (the weights sum to 0), so a
    // segment of one point repeated has no derivative (the orthonormal
    // frame), whatever the rounding of the weights; Embree's derivative
    // there, which Typhoon reads, is its rounding, 0 or not
    const float4 w = Curve_cubicDerivativeWeights(basis, uv.x);
    const float3 p1 = Geometry_position(self, idx + 1);
    derivative = w.x * (Geometry_position(self, idx) - p1)
        + (w.z * (Geometry_position(self, idx + 2) - p1)
            + w.w * (Geometry_position(self, idx + 3) - p1));
  }
  Curve_frame(derivative, uv.x, N, d);
}

// Cylinders and cones: Typhoon's curve frame along the axis, between the
// primitive's two Embree vertices (2 primID and 2 primID + 1); at the caps,
// whose normal is the axis, the orthonormal frame around N
void Cone_getDerivatives(const uniform Geometry *uniform self,
    uniform Attribute texcoord,
    uint primID,
    float2 uv,
    float3 P,
    float3 N,
    varying SurfaceDerivatives &d)
{
  Curve_frame(Geometry_position(self, 2 * (uint64)primID + 1)
          - Geometry_position(self, 2 * (uint64)primID),
      uv.x,
      N,
      d);
}

// Surface position hooks (Geometry_SurfacePositionFcn) //////////////////////

// Typhoon's InterpolateTrianglePosition() (renderer/geometry/triangleMesh.h):
// Embree's barycentric interpolation, in its order of operations
static inline float3 interpolateTrianglePosition(
    float3 p0, float3 p1, float3 p2, float u, float v)
{
  const float bary0 = 1.f - u - v;
  return bary0 * p0 + (u * p1 + v * p2);
}

// The triangle's corners interpolated at the hit (Typhoon's
// ResolveObjectSpaceSurfacePosition() for triangle meshes), or 'P' where
// that isn't finite. Indices are clamped into range, as for the other hooks.
// Where its corners can't be read or the result isn't finite, Typhoon falls
// back to Embree's interpolation of the triangle (rtcInterpolate1()), then
// to 'P' where that isn't finite either: the same where it matters, as
// Embree's interpolation of corners that aren't finite isn't finite either.
varying float3 Triangle_getSurfacePosition(
    const uniform Geometry *uniform self, uint primID, float2 uv, float3 P)
{
  const uint3 idx = Triangle_indices(self, primID);
  const float3 p = interpolateTrianglePosition(Geometry_position(self, idx.x),
      Geometry_position(self, idx.y),
      Geometry_position(self, idx.z),
      uv.x,
      uv.y);
  return isFinite(p) ? p : P;
}

// The quad's surface at the hit: Embree's own interpolation of a quad
// (rtcInterpolate()), barycentric over the triangle Embree hit, (v0, v1, v3)
// where u + v <= 1, else (v2, v3, v1) at (1 - u, 1 - v), so the exact surface
// Embree intersects (the attributes interpolate bilinearly instead, which
// leaves the surface where the quad isn't a parallelogram). Typhoon has no
// quads (it triangulates them); the same arithmetic as its triangles'.
varying float3 Quad_getSurfacePosition(
    const uniform Geometry *uniform self, uint primID, float2 uv, float3 P)
{
  const uint4 idx = Quad_indices(self, primID);
  const float3 p0 = Geometry_position(self, idx.x);
  const float3 p1 = Geometry_position(self, idx.y);
  const float3 p2 = Geometry_position(self, idx.z);
  const float3 p3 = Geometry_position(self, idx.w);
  const bool left = uv.x + uv.y <= 1.f;
  const float3 p = left
      ? interpolateTrianglePosition(p0, p1, p3, uv.x, uv.y)
      : interpolateTrianglePosition(p2, p3, p1, 1.f - uv.x, 1.f - uv.y);
  return isFinite(p) ? p : P;
}

// The surface derivatives at corners [cornerBegin, cornerEnd) of triangles
// ('corners' 3) or quads ('corners' 4), corners numbered like face-varying
// values, along the texture coordinates in attribute 'texcoord' where they
// vary (the derivative hook's dPdu and dPdv at the corner's (u, v)), into
// 'dPdu' and 'dPdv' as packed float3 per corner (indexed by corner number):
// what Typhoon's smoothed mesh tangents start from (MeshTangents.h). 'self'
// must have its vertices and indices set.
export void Geometry_getCornerDerivatives(const void *uniform _self,
    uniform int texcoord,
    uniform uint32 corners,
    uniform uint64 cornerBegin,
    uniform uint64 cornerEnd,
    uniform float *uniform dPdu,
    uniform float *uniform dPdv)
{
  const uniform Geometry *uniform self = (const uniform Geometry *uniform)_self;
  for (uniform uint64 base = cornerBegin; base < cornerEnd;
       base += programCount) {
    const uint64 c = base + programIndex;
    if (c >= cornerEnd)
      continue;
    const uint32 i = (uint32)(c % corners);
    // the corner's (u, v): Embree's barycentric coordinates of a triangle's
    // corners, the bilinear patch's of a quad's (Quad_interpolate())
    float2 uv = make_float2(0.f, 0.f);
    if (i == 1)
      uv = make_float2(1.f, 0.f);
    else if (i == 2)
      uv = corners == 3 ? make_float2(0.f, 1.f) : make_float2(1.f, 1.f);
    else if (i == 3)
      uv = make_float2(0.f, 1.f);
    SurfaceDerivatives d;
    self->getDerivatives(self,
        (uniform Attribute)texcoord,
        (uint32)(c / corners),
        uv,
        make_float3(0.f),
        make_float3(0.f, 0.f, 1.f),
        d);
#pragma ignore warning(perf)
    dPdu[3 * c + 0] = d.dPdu.x;
#pragma ignore warning(perf)
    dPdu[3 * c + 1] = d.dPdu.y;
#pragma ignore warning(perf)
    dPdu[3 * c + 2] = d.dPdu.z;
#pragma ignore warning(perf)
    dPdv[3 * c + 0] = d.dPdv.x;
#pragma ignore warning(perf)
    dPdv[3 * c + 1] = d.dPdv.y;
#pragma ignore warning(perf)
    dPdv[3 * c + 2] = d.dPdv.z;
  }
}
