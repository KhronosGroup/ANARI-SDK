// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Builds a small ISPC scene hierarchy by hand and exports getAttribute() on
// synthetic Embree hits for test_attributes.cpp.

#include "shading/halcyon_attributes.isph"

// from scene/surface/geometry/Geometry.ispc
extern "C" void Geometry_setTriangleIndices(void *uniform _self,
    const uniform uint32 *uniform indices,
    uniform uint64 numTriangles);
extern "C" void Geometry_setQuadIndices(void *uniform _self,
    const uniform uint32 *uniform indices,
    uniform uint64 numQuads);
extern "C" void Geometry_setSphereIndices(void *uniform _self,
    const uniform uint32 *uniform indices,
    uniform uint64 numSpheres);
extern "C" void Geometry_setCurveIndices(void *uniform _self,
    const uniform uint32 *uniform indices,
    uniform uint64 numSegments,
    uniform CurveBasis basis);
extern "C" void Geometry_setConeIndices(void *uniform _self,
    const uniform uint32 *uniform indices,
    uniform uint64 numPrimitives);

// Shared with test_attributes.cpp via the generated test_attributes_ispc.h
struct AttributeCase
{
  uint32 instID;
  uint32 instPrimID;
  uint32 geomID;
  uint32 primID;
  float u;
  float v;
  float tfar; // 0 for a miss
  float Ng[3];
  float org[3];
  float dir[3];
  uint32 attr; // Attribute
};

inline void initGeometry(uniform Geometry &g)
{
  for (uniform int i = 0; i < NUM_GEOMETRY_ATTRIBUTES; i++) {
    g.attributes[i] = defaultAttributeValue();
    g.primitiveAttributes[i].data = NULL;
    g.primitiveAttributes[i].size = 0;
    g.vertexAttributes[i].data = NULL;
    g.vertexAttributes[i].size = 0;
    g.faceVaryingAttributes[i].data = NULL;
    g.faceVaryingAttributes[i].size = 0;
  }
  g.vertexNormals.data = NULL;
  g.vertexNormals.size = 0;
  g.faceVaryingNormals.data = NULL;
  g.faceVaryingNormals.size = 0;
  g.getVertexAttribute = NULL;
  g.getFaceVaryingAttribute = NULL;
  g.primitiveIds = NULL;
  g.numPrimitiveIds = 0;
}

inline void setArray(uniform AttributeArray &a,
    const uniform float *uniform data,
    uniform uint64 size)
{
  a.data = data;
  a.size = size;
}

// Scene: one group of two triangle, two quad, two sphere, two curve and two
// cone surfaces, instanced twice.
//   geometry 0: indexed, triangles {0, 1, 2}, {2, 1, 3}
//     vertex.color {red, green, blue, white}
//     primitive.attribute0 {(10, 0, 0, 1), (11, 0, 0, 1)}
//     attribute1 (5, 6, 7, 8)
//     vertex.attribute2 {(0..5, 0, 0, 1)}, overridden by
//     faceVarying.attribute2 {(20..25, 0, 0, 1)}
//   geometry 1: implicitly indexed
//     vertex.attribute0 {(0..5, 0, 0, 1)}
//     vertex.normal {(0, 0, 1), (1, 0, 1), (0, 1, 1), (0, 0, 1) x 3}
//   geometry 2: indexed quads {0, 1, 2, 3}, {3, 2, 1, 0}
//     vertex.color {red, green, blue, white}
//     vertex.attribute2 {(0..7, 0, 0, 1)}, overridden by
//     faceVarying.attribute2 {(30..37, 0, 0, 1)}
//     vertex.normal {(1, 0, 0) x 4}, overridden by
//     faceVarying.normal {(0, 0, 1) x 8}, but (0, 1, 0) at corner 6
//   geometry 3: implicitly indexed quads
//     vertex.attribute0 {(0..7, 0, 0, 1)}
//     vertex.normal {(0, 0, 0) x 8}, degenerate
//   geometry 4: indexed spheres {3, 0, 2}
//     vertex.color {red, green, blue, white}
//     faceVarying.attribute2 {(20..25, 0, 0, 1)}, ignored (no hook)
//   geometry 5: implicitly indexed spheres
//     vertex.attribute0 {(0..5, 0, 0, 1)}
//   geometry 6: indexed curve segments starting at {2, 0}
//     vertex.color {red, green, blue, white}
//   geometry 7: implicitly indexed curve segments
//     vertex.attribute0 {(0..5, 0, 0, 1)}
//   geometry 8: indexed cones {3, 1}, {0, 2}
//     vertex.color {red, green, blue, white}
//   geometry 9: implicitly indexed cones
//     vertex.attribute0 {(0..5, 0, 0, 1)}
//   instance 0: one transform, translation (1, 2, 3), and a shear normal
//     matrix (the inverse transpose of x' = x + y)
//   instance 1: two identity transforms, color {(0.1..), (0.2..)} and
//     attribute3 (9, 9, 9, 9)
export void test_getAttribute(const uniform AttributeCase *uniform cases,
    uniform float *uniform results,
    uniform int n)
{
  uniform float vertexColors[] = {
      1, 0, 0, 1, 0, 1, 0, 1, 0, 0, 1, 1, 1, 1, 1, 1};
  uniform float primAttr0[] = {10, 0, 0, 1, 11, 0, 0, 1};
  uniform uint32 indices[] = {0, 1, 2, 2, 1, 3};
  uniform float vertexAttr0[] = {
      0, 0, 0, 1, 1, 0, 0, 1, 2, 0, 0, 1, 3, 0, 0, 1, 4, 0, 0, 1, 5, 0, 0, 1};
  // (20 + i, 0, 0, 1) for triangle corner i, (30 + i, 0, 0, 1) for quad
  // corner i
  uniform float triFaceVarying[4 * 6];
  for (uniform int i = 0; i < 6; i++) {
    triFaceVarying[4 * i + 0] = 20 + i;
    triFaceVarying[4 * i + 1] = 0.f;
    triFaceVarying[4 * i + 2] = 0.f;
    triFaceVarying[4 * i + 3] = 1.f;
  }
  uniform float quadFaceVarying[4 * 8];
  for (uniform int i = 0; i < 8; i++) {
    quadFaceVarying[4 * i + 0] = 30 + i;
    quadFaceVarying[4 * i + 1] = 0.f;
    quadFaceVarying[4 * i + 2] = 0.f;
    quadFaceVarying[4 * i + 3] = 1.f;
  }

  uniform Geometry geom0;
  initGeometry(geom0);
  setArray(geom0.vertexAttributes[ATTRIBUTE_COLOR], vertexColors, 4);
  setArray(geom0.vertexAttributes[ATTRIBUTE_2], vertexAttr0, 6);
  setArray(geom0.faceVaryingAttributes[ATTRIBUTE_2], triFaceVarying, 6);
  setArray(geom0.primitiveAttributes[ATTRIBUTE_0], primAttr0, 2);
  geom0.attributes[ATTRIBUTE_1] = make_float4(5.f, 6.f, 7.f, 8.f);
  Geometry_setTriangleIndices(&geom0, indices, 2);

  uniform uint32 quadIndices[] = {0, 1, 2, 3, 3, 2, 1, 0};
  uniform float quadAttr0[4 * 8];
  for (uniform int i = 0; i < 8; i++) {
    quadAttr0[4 * i + 0] = i;
    quadAttr0[4 * i + 1] = 0.f;
    quadAttr0[4 * i + 2] = 0.f;
    quadAttr0[4 * i + 3] = 1.f;
  }

  uniform float geom1Normals[] = {
      0, 0, 1, 0, 1, 0, 1, 0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0};
  uniform float geom2Normals[4 * 4];
  for (uniform int i = 0; i < 4; i++) {
    geom2Normals[4 * i + 0] = 1.f;
    geom2Normals[4 * i + 1] = 0.f;
    geom2Normals[4 * i + 2] = 0.f;
    geom2Normals[4 * i + 3] = 0.f;
  }
  uniform float geom2FaceVaryingNormals[4 * 8];
  for (uniform int i = 0; i < 8; i++) {
    geom2FaceVaryingNormals[4 * i + 0] = 0.f;
    geom2FaceVaryingNormals[4 * i + 1] = i == 6 ? 1.f : 0.f;
    geom2FaceVaryingNormals[4 * i + 2] = i == 6 ? 0.f : 1.f;
    geom2FaceVaryingNormals[4 * i + 3] = 0.f;
  }
  uniform float geom3Normals[4 * 8];
  for (uniform int i = 0; i < 4 * 8; i++)
    geom3Normals[i] = 0.f;

  uniform Geometry geom1;
  initGeometry(geom1);
  setArray(geom1.vertexAttributes[ATTRIBUTE_0], vertexAttr0, 6);
  setArray(geom1.vertexNormals, geom1Normals, 6);
  Geometry_setTriangleIndices(&geom1, NULL, 0);

  uniform Geometry geom2;
  initGeometry(geom2);
  setArray(geom2.vertexAttributes[ATTRIBUTE_COLOR], vertexColors, 4);
  setArray(geom2.vertexAttributes[ATTRIBUTE_2], quadAttr0, 8);
  setArray(geom2.faceVaryingAttributes[ATTRIBUTE_2], quadFaceVarying, 8);
  setArray(geom2.vertexNormals, geom2Normals, 4);
  setArray(geom2.faceVaryingNormals, geom2FaceVaryingNormals, 8);
  Geometry_setQuadIndices(&geom2, quadIndices, 2);

  uniform Geometry geom3;
  initGeometry(geom3);
  setArray(geom3.vertexAttributes[ATTRIBUTE_0], quadAttr0, 8);
  setArray(geom3.vertexNormals, geom3Normals, 8);
  Geometry_setQuadIndices(&geom3, NULL, 0);

  uniform uint32 sphereIndices[] = {3, 0, 2};

  uniform Geometry geom4;
  initGeometry(geom4);
  setArray(geom4.vertexAttributes[ATTRIBUTE_COLOR], vertexColors, 4);
  setArray(geom4.faceVaryingAttributes[ATTRIBUTE_2], triFaceVarying, 6);
  Geometry_setSphereIndices(&geom4, sphereIndices, 3);

  uniform Geometry geom5;
  initGeometry(geom5);
  setArray(geom5.vertexAttributes[ATTRIBUTE_0], vertexAttr0, 6);
  Geometry_setSphereIndices(&geom5, NULL, 0);

  uniform uint32 curveIndices[] = {2, 0};

  uniform Geometry geom6;
  initGeometry(geom6);
  setArray(geom6.vertexAttributes[ATTRIBUTE_COLOR], vertexColors, 4);
  Geometry_setCurveIndices(&geom6, curveIndices, 2, CURVE_BASIS_LINEAR);

  uniform Geometry geom7;
  initGeometry(geom7);
  setArray(geom7.vertexAttributes[ATTRIBUTE_0], vertexAttr0, 6);
  Geometry_setCurveIndices(&geom7, NULL, 0, CURVE_BASIS_LINEAR);

  uniform uint32 coneIndices[] = {3, 1, 0, 2};

  uniform Geometry geom8;
  initGeometry(geom8);
  setArray(geom8.vertexAttributes[ATTRIBUTE_COLOR], vertexColors, 4);
  Geometry_setConeIndices(&geom8, coneIndices, 2);

  uniform Geometry geom9;
  initGeometry(geom9);
  setArray(geom9.vertexAttributes[ATTRIBUTE_0], vertexAttr0, 6);
  Geometry_setConeIndices(&geom9, NULL, 0);

  uniform Surface surf0;
  surf0.geometry = &geom0;
  uniform Surface surf1;
  surf1.geometry = &geom1;
  uniform Surface surf2;
  surf2.geometry = &geom2;
  uniform Surface surf3;
  surf3.geometry = &geom3;
  uniform Surface surf4;
  surf4.geometry = &geom4;
  uniform Surface surf5;
  surf5.geometry = &geom5;
  uniform Surface surf6;
  surf6.geometry = &geom6;
  uniform Surface surf7;
  surf7.geometry = &geom7;
  uniform Surface surf8;
  surf8.geometry = &geom8;
  uniform Surface surf9;
  surf9.geometry = &geom9;
  uniform Surface *uniform surfaces[] = {&surf0,
      &surf1,
      &surf2,
      &surf3,
      &surf4,
      &surf5,
      &surf6,
      &surf7,
      &surf8,
      &surf9};

  uniform Group group;
  group.surfaces = surfaces;
  group.numSurfaces = 10;

  uniform uint32 ids[] = {0, 0};
  // column-major
  uniform float identity3[] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
  uniform float shearNormalXfm[] = {1, -1, 0, 0, 1, 0, 0, 0, 1};
  uniform float identity4x2[] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1,
      1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  uniform float invTranslation[] = {
      1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -1, -2, -3, 1};
  uniform float inst1Colors[] = {0.1, 0.1, 0.1, 1, 0.2, 0.2, 0.2, 1};
  uniform float inst1Attr3[] = {9, 9, 9, 9, 9, 9, 9, 9};

  uniform Instance inst0;
  inst0.group = &group;
  inst0.ids = ids;
  inst0.normalXfms = shearNormalXfm;
  inst0.invXfms = invTranslation;
  for (uniform int i = 0; i < NUM_GEOMETRY_ATTRIBUTES; i++)
    inst0.attributes[i] = NULL;

  uniform Instance inst1;
  inst1.group = &group;
  inst1.ids = ids;
  inst1.normalXfms = identity3; // only element 0 is read by these cases
  inst1.invXfms = identity4x2;
  for (uniform int i = 0; i < NUM_GEOMETRY_ATTRIBUTES; i++)
    inst1.attributes[i] = NULL;
  inst1.attributes[ATTRIBUTE_COLOR] = inst1Colors;
  inst1.attributes[ATTRIBUTE_3] = inst1Attr3;

  uniform Instance *uniform instances[] = {&inst0, &inst1};

  uniform World world;
  world.instances = instances;

  foreach (i = 0 ... n) {
    const uniform AttributeCase *varying c = cases + i;
    RayHit rh = makeRay(make_float3(c->org[0], c->org[1], c->org[2]),
        make_float3(c->dir[0], c->dir[1], c->dir[2]));
    if (cases[i].tfar > 0.f) {
      rh.ray.tfar = cases[i].tfar;
      rh.hit.instID = cases[i].instID;
      rh.hit.instPrimID = cases[i].instPrimID;
      rh.hit.geomID = cases[i].geomID;
      rh.hit.primID = cases[i].primID;
      rh.hit.u = cases[i].u;
      rh.hit.v = cases[i].v;
      rh.hit.Ng = make_float3(cases[i].Ng[0], cases[i].Ng[1], cases[i].Ng[2]);
    }

    const HitInfo info = getHitInfo(&world, rh);

    // getAttribute() takes a uniform attribute, as its callers will
    float4 value;
    const uint32 attr = cases[i].attr;
    foreach_unique (a in attr)
      value = getAttribute(info, rh, (uniform Attribute)a);

    results[4 * i + 0] = value.x;
    results[4 * i + 1] = value.y;
    results[4 * i + 2] = value.z;
    results[4 * i + 3] = value.w;
  }
}
