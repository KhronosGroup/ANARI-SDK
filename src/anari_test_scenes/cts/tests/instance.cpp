// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "../BuildContext.h"
#include "../TestBuilder.h"
#include "../WorldBuilder.h"
#include "Categories.h"
// std
#include <cstring>

namespace anari {
namespace cts {

namespace {

using anari::math::float3;
using anari::math::float4;
using anari::math::mat4;

} // namespace

void registerInstanceTests(Catalog &catalog)
{
  makeTest("instance", "instance")
      .build([](BuildContext &ctx) {
        auto d = ctx.device();
        GeometryOptions o;
        o.subtype = "triangle";
        o.shape = "triangle";
        o.primitiveCount = 1;
        auto geom = buildGeometry(d, o);
        auto mat = makeMatteMaterial(d, float3(0.7f, 0.5f, 0.3f));
        auto surface = makeSurface(d, geom, mat);

        // One group, shared by both instances.
        auto group = anari::newObject<anari::Group>(d);
        anari::setAndReleaseParameter(
            d, group, "surface", anari::newArray1D(d, &surface, 1));
        anari::commitParameters(d, group);

        auto makeInst = [&](const mat4 &xfm) {
          auto inst = anari::newObject<anari::Instance>(d, "transform");
          float m[16];
          std::memcpy(m, &xfm, sizeof(m));
          anari::setParameter(d, inst, "transform", m);
          anari::setParameter(d, inst, "group", group);
          anari::commitParameters(d, inst);
          return inst;
        };
        auto i0 = makeInst(anari::math::identity);
        auto i1 = makeInst(mat4(float4(-1.f, -1.f, 0.f, 0.f),
            float4(1.f, -1.f, 0.f, 0.f),
            float4(0.f, 0.f, -1.f, 0.f),
            float4(1.f, 1.f, 1.f, 1.f)));

        WorldContents wc;
        wc.instances = {i0, i1};
        auto world = assembleWorld(d, wc);

        anari::release(d, geom);
        anari::release(d, mat);
        anari::release(d, surface);
        anari::release(d, group);
        anari::release(d, i0);
        anari::release(d, i1);
        return world;
      })
      .channels({Channel::Color, Channel::Depth})
      .requireFeatures(
          {"ANARI_KHR_GEOMETRY_TRIANGLE", "ANARI_KHR_INSTANCE_TRANSFORM"})
      .registerInto(catalog);
}

} // namespace cts
} // namespace anari
