// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Checks the attribute values getAttribute() resolves for synthetic hits
// against the scene built in test_attributes.ispc.

#include "test_attributes_ispc.h"
// std
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

// helium::Attribute order
enum Attr : uint32_t
{
  ATTR0,
  ATTR1,
  ATTR2,
  ATTR3,
  COLOR,
  WORLD_POSITION,
  WORLD_NORMAL,
  OBJECT_POSITION,
  OBJECT_NORMAL,
  NONE
};

struct Value
{
  float x, y, z, w;
};

struct Case
{
  const char *name;
  ispc::AttributeCase in;
  Value expected;
};

// A hit of instance 'inst' (transform 'xfm'), surface 'geom', primitive
// 'prim' at (u, v), from (1, 2, 10) straight down -z at t = 5
ispc::AttributeCase hit(uint32_t inst,
    uint32_t xfm,
    uint32_t geom,
    uint32_t prim,
    float u,
    float v,
    uint32_t attr,
    float nx = 0.f,
    float ny = 0.f,
    float nz = 1.f)
{
  return {inst,
      xfm,
      geom,
      prim,
      u,
      v,
      5.f,
      {nx, ny, nz},
      {1.f, 2.f, 10.f},
      {0.f, 0.f, -1.f},
      attr};
}

const float RSQRT2 = 1.f / std::sqrt(2.f);

const std::vector<Case> CASES = {
    {"miss",
        {0, 0, 0, 0, 0.f, 0.f, 0.f, {}, {}, {0, 0, 1}, COLOR},
        {0, 0, 0, 1}},
    {"ATTRIBUTE_NONE", hit(0, 0, 0, 0, 0.f, 0.f, NONE), {0, 0, 0, 1}},
    {"vertex.color, indexed",
        hit(0, 0, 0, 1, .25f, .5f, COLOR),
        {.5f, .75f, .75f, 1}},
    {"vertex.color at a vertex",
        hit(0, 0, 0, 0, 1.f, 0.f, COLOR),
        {0, 1, 0, 1}},
    {"vertex.attribute0, implicit indices",
        hit(0, 0, 1, 1, .25f, .5f, ATTR0),
        {4.25f, 0, 0, 1}},
    // bilinear: (1-v)(1-u)a + (1-v)u b + vu c + v(1-u) d
    {"quad vertex.color, indexed",
        hit(0, 0, 2, 0, .5f, .25f, COLOR),
        {.5f, .5f, .25f, 1}},
    {"quad vertex.color, second indexed quad",
        hit(0, 0, 2, 1, .5f, .25f, COLOR),
        {.5f, .5f, .75f, 1}},
    {"quad vertex.color at vertex c",
        hit(0, 0, 2, 1, 1.f, 1.f, COLOR),
        {0, 1, 0, 1}},
    {"quad vertex.attribute0, implicit indices",
        hit(0, 0, 3, 1, .25f, .5f, ATTR0),
        {5.5f, 0, 0, 1}},
    // spheres: the vertex value, not interpolated
    {"sphere vertex.color, indexed",
        hit(0, 0, 4, 0, .3f, .6f, COLOR),
        {1, 1, 1, 1}},
    {"sphere vertex.color, second indexed sphere",
        hit(0, 0, 4, 1, .9f, .1f, COLOR),
        {1, 0, 0, 1}},
    {"sphere vertex.color, indexed, clamps primID",
        hit(0, 0, 4, 7, .5f, .5f, COLOR),
        {0, 0, 1, 1}},
    {"sphere vertex.attribute0, implicit indices",
        hit(0, 0, 5, 4, .7f, .2f, ATTR0),
        {4, 0, 0, 1}},
    // curves: linear in u from the segment's start vertex to the next
    {"curve vertex.color, indexed",
        hit(0, 0, 6, 0, .25f, .8f, COLOR),
        {.25f, .25f, 1, 1}},
    {"curve vertex.color, second indexed segment",
        hit(0, 0, 6, 1, .5f, .1f, COLOR),
        {.5f, .5f, 0, 1}},
    {"curve vertex.color, indexed, clamps primID",
        hit(0, 0, 6, 7, 1.f, 0.f, COLOR),
        {0, 1, 0, 1}},
    {"curve vertex.attribute0, implicit indices",
        hit(0, 0, 7, 3, .25f, .6f, ATTR0),
        {3.25f, 0, 0, 1}},
    // cylinders/cones: linear in u between the primitive's two vertices
    {"cone vertex.color, indexed",
        hit(0, 0, 8, 0, .25f, .8f, COLOR),
        {.75f, 1, .75f, 1}},
    {"cone vertex.color, second indexed primitive",
        hit(0, 0, 8, 1, .5f, .1f, COLOR),
        {.5f, 0, .5f, 1}},
    {"cone vertex.color, indexed, clamps primID",
        hit(0, 0, 8, 5, 1.f, 0.f, COLOR),
        {0, 0, 1, 1}},
    {"cone vertex.attribute0, implicit indices",
        hit(0, 0, 9, 2, .25f, .6f, ATTR0),
        {4.25f, 0, 0, 1}},
    // face-varying: corners 3 * primID.. (4 * primID.. for quads), whatever
    // the indexing, interpolated like vertex values and overriding them
    {"faceVarying.attribute2 overrides vertex.attribute2",
        hit(0, 0, 0, 1, .25f, .5f, ATTR2),
        {24.25f, 0, 0, 1}},
    {"faceVarying.attribute2 at a corner",
        hit(0, 0, 0, 1, 1.f, 0.f, ATTR2),
        {24, 0, 0, 1}},
    {"faceVarying.attribute2 clamps to its last element",
        hit(0, 0, 0, 5, .25f, .5f, ATTR2),
        {25, 0, 0, 1}},
    {"quad faceVarying.attribute2 overrides vertex.attribute2",
        hit(0, 0, 2, 1, .5f, .25f, ATTR2),
        {35, 0, 0, 1}},
    {"quad faceVarying.attribute2 at corner c",
        hit(0, 0, 2, 1, 1.f, 1.f, ATTR2),
        {36, 0, 0, 1}},
    {"sphere ignores faceVarying.attribute2",
        hit(0, 0, 4, 0, .3f, .6f, ATTR2),
        {0, 0, 0, 1}},
    {"primitive.attribute0", hit(0, 0, 0, 1, 0.f, 0.f, ATTR0), {11, 0, 0, 1}},
    {"primitive.attribute0 clamps primID",
        hit(0, 0, 0, 5, 0.f, 0.f, ATTR0),
        {11, 0, 0, 1}},
    {"uniform attribute1", hit(0, 0, 0, 0, 0.f, 0.f, ATTR1), {5, 6, 7, 8}},
    {"unset attribute2", hit(0, 0, 1, 0, 0.f, 0.f, ATTR2), {0, 0, 0, 1}},
    {"instance color array element 1 overrides vertex.color",
        hit(1, 1, 0, 0, 1.f, 0.f, COLOR),
        {.2f, .2f, .2f, 1}},
    {"instance uniform attribute3",
        hit(1, 0, 1, 0, 0.f, 0.f, ATTR3),
        {9, 9, 9, 9}},
    {"worldPosition", hit(0, 0, 0, 0, 0.f, 0.f, WORLD_POSITION), {1, 2, 5, 1}},
    {"objectPosition",
        hit(0, 0, 0, 0, 0.f, 0.f, OBJECT_POSITION),
        {0, 0, 2, 1}},
    // plane x = 0 under the shear x' = x + y has normal (1, -1, 0)
    {"worldNormal uses the normal matrix",
        hit(0, 0, 0, 0, 0.f, 0.f, WORLD_NORMAL, 2.f, 0.f, 0.f),
        {RSQRT2, -RSQRT2, 0, 1}},
    {"objectNormal",
        hit(0, 0, 0, 0, 0.f, 0.f, OBJECT_NORMAL, 2.f, 0.f, 0.f),
        {1, 0, 0, 1}},
    // .25 (0, 0, 1) + .25 (1, 0, 1) + .5 (0, 1, 1) = (.25, .5, 1)
    {"worldNormal interpolates vertex.normal",
        hit(1, 0, 1, 0, .25f, .5f, WORLD_NORMAL),
        {.25f / 1.1456439f, .5f / 1.1456439f, 1.f / 1.1456439f, 1}},
    {"objectNormal interpolates vertex.normal",
        hit(0, 0, 1, 0, .25f, .5f, OBJECT_NORMAL),
        {.25f / 1.1456439f, .5f / 1.1456439f, 1.f / 1.1456439f, 1}},
    // the shear's normal matrix takes (.25, .5, 1) to (.25, .25, 1)
    {"worldNormal transforms vertex.normal by the normal matrix",
        hit(0, 0, 1, 0, .25f, .5f, WORLD_NORMAL),
        {.25f / 1.0606602f, .25f / 1.0606602f, 1.f / 1.0606602f, 1}},
    {"quad faceVarying.normal overrides vertex.normal",
        hit(1, 0, 2, 1, 1.f, 1.f, WORLD_NORMAL),
        {0, 1, 0, 1}},
    {"quad faceVarying.normal at corners without override",
        hit(1, 0, 2, 0, .5f, .5f, WORLD_NORMAL),
        {0, 0, 1, 1}},
    {"zero-length vertex.normal falls back to Ng",
        hit(1, 0, 3, 0, .5f, .5f, WORLD_NORMAL, 0.f, 2.f, 0.f),
        {0, 1, 0, 1}},
    {"objectNormal of zero-length vertex.normal falls back to Ng",
        hit(1, 0, 3, 0, .5f, .5f, OBJECT_NORMAL, 0.f, 2.f, 0.f),
        {0, 1, 0, 1}},
};

bool near(float a, float b)
{
  return std::fabs(a - b) < 1e-5f;
}

} // namespace

int main()
{
  // Repeat the cases so they span several SIMD gangs with mixed lanes
  std::vector<ispc::AttributeCase> cases;
  std::vector<const Case *> expected;
  for (int r = 0; r < 7; r++) {
    for (auto &c : CASES) {
      cases.push_back(c.in);
      expected.push_back(&c);
    }
  }

  std::vector<Value> results(cases.size());
  ispc::test_getAttribute(cases.data(), &results[0].x, int(cases.size()));

  int failures = 0;
  for (size_t i = 0; i < results.size(); i++) {
    const auto &e = expected[i]->expected;
    const auto &r = results[i];
    if (!near(r.x, e.x) || !near(r.y, e.y) || !near(r.z, e.z)
        || !near(r.w, e.w)) {
      std::printf("FAIL %s: got (%g, %g, %g, %g), expected (%g, %g, %g, %g)\n",
          expected[i]->name,
          r.x,
          r.y,
          r.z,
          r.w,
          e.x,
          e.y,
          e.z,
          e.w);
      failures++;
    }
  }

  std::printf("%s: %zu hits, %d failures\n",
      failures ? "FAIL" : "PASS",
      results.size(),
      failures);
  return failures ? 1 : 0;
}
