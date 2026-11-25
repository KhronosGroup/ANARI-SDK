// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Builds a small ISPC scene hierarchy by hand and exports getHitInfo() on
// synthetic Embree hits for test_hitinfo.cpp.

#include "shading/halcyon_hitInfo.isph"

// Shared with test_hitinfo.cpp via the generated test_hitinfo_ispc.h
struct HitCase
{
  uint32 hit;
  uint32 instID;
  uint32 instPrimID;
  uint32 geomID;
  uint32 primID;
};

struct HitIds
{
  uint32 primId;
  uint32 objId;
  uint32 instId;
};

// Scene: two instances of one group holding two surfaces.
//   instance 0: transform array with ids {10, 11, 12}
//   instance 1: single transform, id 20
//   surface 0: id 7, geometry with 'primitive.id' {100, 101, 102, 103}
//   surface 1: id 8, geometry without 'primitive.id'
export void test_getHitInfoIds(const uniform HitCase *uniform cases,
    uniform HitIds *uniform results,
    uniform int n)
{
  uniform uint32 primIds[] = {100, 101, 102, 103};
  uniform uint32 inst0Ids[] = {10, 11, 12};
  uniform uint32 inst1Ids[] = {20};

  uniform Geometry geom0;
  geom0.primitiveIds = primIds;
  geom0.numPrimitiveIds = 4;
  uniform Geometry geom1;
  geom1.primitiveIds = NULL;
  geom1.numPrimitiveIds = 0;

  uniform Surface surf0;
  surf0.geometry = &geom0;
  surf0.id = 7;
  uniform Surface surf1;
  surf1.geometry = &geom1;
  surf1.id = 8;
  uniform Surface *uniform surfaces[] = {&surf0, &surf1};

  uniform Group group;
  group.surfaces = surfaces;
  group.numSurfaces = 2;

  uniform Instance inst0;
  inst0.group = &group;
  inst0.ids = inst0Ids;
  uniform Instance inst1;
  inst1.group = &group;
  inst1.ids = inst1Ids;
  uniform Instance *uniform instances[] = {&inst0, &inst1};

  uniform World world;
  world.instances = instances;

  foreach (i = 0 ... n) {
    RayHit rh = makeRay(make_float3(0.f), make_float3(0.f, 0.f, 1.f));
    if (cases[i].hit) {
      rh.hit.instID = cases[i].instID;
      rh.hit.instPrimID = cases[i].instPrimID;
      rh.hit.geomID = cases[i].geomID;
      rh.hit.primID = cases[i].primID;
    }

    const HitInfo info = getHitInfo(&world, rh);
    results[i].primId = info.primId;
    results[i].objId = info.objId;
    results[i].instId = info.instId;
  }
}
