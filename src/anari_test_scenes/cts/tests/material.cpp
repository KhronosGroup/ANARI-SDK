// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Material conformance tests, migrated from cts/test_scenes/material/**.
// Each builds a single triangle surface whose material parameters are driven by
// heterogeneous axes: a constant, an attribute name, a bound sampler, or unset
// (the old JSON null). The base geometry carries attribute0 (palette colors),
// attribute1 (texcoords for samplers), and attribute2 (opacity) so any of those
// bindings resolves.

#include "../BuildContext.h"
#include "../TestBuilder.h"
#include "../WorldBuilder.h"
#include "Categories.h"
#include "generators/ColorPalette.h"
// std
#include <algorithm>
#include <functional>
#include <string>
#include <vector>

namespace anari {
namespace cts {

namespace {

using anari::math::float2;
using anari::math::float3;
using anari::math::float4;
using V = std::vector<Any>;

const char *kMatte = "ANARI_KHR_MATERIAL_MATTE";
const char *kPbr = "ANARI_KHR_MATERIAL_PHYSICALLY_BASED";

struct SamplerSpec
{
  std::string subtype;
  std::string inAttribute;
  bool normalMap{false};
};

using ApplyFn = std::function<void(BuildContext &,
    anari::Device,
    anari::Material,
    const std::vector<anari::Sampler> &)>;

// The canonical triangle Layout (ADR-0007) carrying attribute0 = palette
// colors, attribute1 = planar texcoords, attribute2 = a left-to-right opacity
// ramp, so material bindings to "attribute0"/"attribute2" and samplers reading
// "attribute1" all resolve. The UVs are derived planarly from the grid's XY
// extent (scaled so the checkerboard sampler tiles), and opacity ramps across
// the grid so an opacity binding visibly varies.
anari::Geometry materialBaseGeometry(anari::Device d, int count)
{
  auto verts = layoutTriangleSoup(count);

  float minX = verts[0].x, maxX = verts[0].x;
  float minY = verts[0].y, maxY = verts[0].y;
  for (const auto &v : verts) {
    minX = std::min(minX, v.x);
    maxX = std::max(maxX, v.x);
    minY = std::min(minY, v.y);
    maxY = std::max(maxY, v.y);
  }
  const float spanX = maxX > minX ? maxX - minX : 1.f;
  const float spanY = maxY > minY ? maxY - minY : 1.f;

  std::vector<float2> uv(verts.size());
  std::vector<float> op(verts.size());
  for (size_t i = 0; i < verts.size(); ++i) {
    const float u = (verts[i].x - minX) / spanX;
    const float w = (verts[i].y - minY) / spanY;
    uv[i] = float2(2.f * u, 2.f * w); // [0,2] so the checkerboard tiles
    op[i] = u; // left (transparent) -> right (opaque)
  }

  auto geom = anari::newObject<anari::Geometry>(d, "triangle");
  anari::setAndReleaseParameter(d,
      geom,
      "vertex.position",
      anari::newArray1D(d, verts.data(), verts.size()));
  auto cols = scenes::colors::getColorVectorFromPalette(verts.size());
  anari::setAndReleaseParameter(d,
      geom,
      "vertex.attribute0",
      anari::newArray1D(d, cols.data(), cols.size()));
  anari::setAndReleaseParameter(
      d, geom, "vertex.attribute1", anari::newArray1D(d, uv.data(), uv.size()));
  anari::setAndReleaseParameter(
      d, geom, "vertex.attribute2", anari::newArray1D(d, op.data(), op.size()));
  anari::commitParameters(d, geom);
  return geom;
}

anari::World materialWorld(BuildContext &ctx,
    const char *subtype,
    int count,
    const std::vector<SamplerSpec> &specs,
    const ApplyFn &apply)
{
  auto d = ctx.device();
  auto geom = materialBaseGeometry(d, count);

  std::vector<anari::Sampler> samplers;
  for (const auto &s : specs) {
    auto sm = newImageSampler(d, s.subtype, s.inAttribute, s.normalMap);
    anari::commitParameters(d, sm);
    samplers.push_back(sm);
  }

  auto mat = anari::newObject<anari::Material>(d, subtype);
  apply(ctx, d, mat, samplers);
  anari::commitParameters(d, mat);

  auto surface = makeSurface(d, geom, mat);
  auto light = makeDirectionalLight(d, float3(0.f, -1.f, -1.f));
  WorldContents wc;
  wc.surfaces = {surface};
  wc.lights = {light};
  auto world = assembleWorld(d, wc);

  anari::release(d, geom);
  for (auto sm : samplers)
    anari::release(d, sm);
  anari::release(d, mat);
  anari::release(d, surface);
  anari::release(d, light);
  return world;
}

// Apply one axis value to a material parameter (constant / attribute / sampler).
void bind(BuildContext &ctx,
    anari::Device d,
    anari::Material mat,
    const char *param,
    const std::vector<anari::Sampler> &s)
{
  setBoundParameter(d, mat, param, ctx.value(param), s);
}

} // namespace

void registerMaterialTests(Catalog &catalog)
{
  // ---- matte ----------------------------------------------------------------
  makeTest("material", "matte_alpha_mode")
      .build([](BuildContext &ctx) {
        return materialWorld(ctx,
            "matte",
            16,
            {},
            [](BuildContext &ctx,
                anari::Device d,
                anari::Material mat,
                const std::vector<anari::Sampler> &s) {
              anari::setParameter(d, mat, "color", float3(1.f, 0.f, 0.f));
              anari::setParameter(d, mat, "opacity", "attribute2");
              bind(ctx, d, mat, "alphaMode", s);
              bind(ctx, d, mat, "alphaCutoff", s);
            });
      })
      .permute("alphaMode", V{Any("mask"), none(), Any("opaque"), Any("blend")})
      .permute("alphaCutoff", V{none(), Any(0.1f)})
      .requireFeature(kMatte)
      .registerInto(catalog);

  makeTest("material", "matte_color")
      .build([](BuildContext &ctx) {
        return materialWorld(ctx,
            "matte",
            16,
            {{"image2D", "attribute1", false}},
            [](BuildContext &ctx,
                anari::Device d,
                anari::Material mat,
                const std::vector<anari::Sampler> &s) {
              bind(ctx, d, mat, "color", s);
            });
      })
      .permute("color",
          V{none(),
              Any(float3(0.98f, 0.5f, 0.44f)),
              Any("attribute0"),
              Any("ref_sampler_0")})
      .requireFeature(kMatte)
      .registerInto(catalog);

  makeTest("material", "matte_opacity")
      .build([](BuildContext &ctx) {
        return materialWorld(ctx,
            "matte",
            16,
            {{"image1D", "attribute1", false}},
            [](BuildContext &ctx,
                anari::Device d,
                anari::Material mat,
                const std::vector<anari::Sampler> &s) {
              anari::setParameter(d, mat, "color", float3(1.f, 0.f, 0.f));
              anari::setParameter(d, mat, "alphaMode", "blend");
              bind(ctx, d, mat, "opacity", s);
            });
      })
      .permute("opacity",
          V{none(), Any(0.5f), Any("attribute2"), Any("ref_sampler_0")})
      .requireFeature(kMatte)
      .registerInto(catalog);

  // ---- physicallyBased ------------------------------------------------------
  makeTest("material", "pbr_alpha_mode")
      .build([](BuildContext &ctx) {
        return materialWorld(ctx,
            "physicallyBased",
            16,
            {},
            [](BuildContext &ctx,
                anari::Device d,
                anari::Material mat,
                const std::vector<anari::Sampler> &s) {
              anari::setParameter(d, mat, "baseColor", float3(0.98f, 0.5f, 0.44f));
              anari::setParameter(d, mat, "opacity", "attribute2");
              bind(ctx, d, mat, "alphaMode", s);
              bind(ctx, d, mat, "alphaCutoff", s);
            });
      })
      .permute("alphaMode", V{none(), Any("opaque"), Any("blend"), Any("mask")})
      .permute("alphaCutoff", V{none(), Any(0.1f)})
      .requireFeature(kPbr)
      .registerInto(catalog);

  makeTest("material", "pbr_base_color")
      .build([](BuildContext &ctx) {
        return materialWorld(ctx,
            "physicallyBased",
            16,
            {{"image2D", "attribute1", false}},
            [](BuildContext &ctx,
                anari::Device d,
                anari::Material mat,
                const std::vector<anari::Sampler> &s) {
              bind(ctx, d, mat, "baseColor", s);
            });
      })
      .permute("baseColor",
          V{none(),
              Any(float3(0.98f, 0.5f, 0.44f)),
              Any("attribute0"),
              Any("ref_sampler_0")})
      .requireFeature(kPbr)
      .registerInto(catalog);

  makeTest("material", "pbr_clearcoat")
      .build([](BuildContext &ctx) {
        return materialWorld(ctx,
            "physicallyBased",
            16,
            {{"image1D", "attribute1", false}, {"image2D", "attribute1", true}},
            [](BuildContext &ctx,
                anari::Device d,
                anari::Material mat,
                const std::vector<anari::Sampler> &s) {
              bind(ctx, d, mat, "clearcoat", s);
              bind(ctx, d, mat, "clearcoatRoughness", s);
              bind(ctx, d, mat, "clearcoatNormal", s);
            });
      })
      .simplified()
      .permute("clearcoat",
          V{Any(0.5f), none(), Any("attribute0"), Any("ref_sampler_0")})
      .permute("clearcoatRoughness",
          V{Any(0.5f), none(), Any("attribute0"), Any("ref_sampler_0")})
      .permute("clearcoatNormal", V{none(), Any("ref_sampler_1")})
      .requireFeature(kPbr)
      .registerInto(catalog);

  makeTest("material", "pbr_emissive")
      .build([](BuildContext &ctx) {
        return materialWorld(ctx,
            "physicallyBased",
            16,
            {{"image2D", "attribute1", false}},
            [](BuildContext &ctx,
                anari::Device d,
                anari::Material mat,
                const std::vector<anari::Sampler> &s) {
              bind(ctx, d, mat, "emissive", s);
            });
      })
      .permute("emissive",
          V{none(),
              Any(float3(1.f, 0.f, 0.f)),
              Any("attribute0"),
              Any("ref_sampler_0")})
      .requireFeature(kPbr)
      .registerInto(catalog);

  makeTest("material", "pbr_ior")
      .build([](BuildContext &ctx) {
        return materialWorld(ctx,
            "physicallyBased",
            2,
            {},
            [](BuildContext &ctx,
                anari::Device d,
                anari::Material mat,
                const std::vector<anari::Sampler> &s) {
              anari::setParameter(d, mat, "transmission", 0.5f);
              anari::setParameter(d, mat, "thickness", 0.5f);
              bind(ctx, d, mat, "ior", s);
            });
      })
      .permute("ior", V{none(), Any(2.42f)})
      .requireFeature(kPbr)
      .registerInto(catalog);

  makeTest("material", "pbr_iridescence")
      .build([](BuildContext &ctx) {
        return materialWorld(ctx,
            "physicallyBased",
            16,
            {{"image1D", "attribute1", false}},
            [](BuildContext &ctx,
                anari::Device d,
                anari::Material mat,
                const std::vector<anari::Sampler> &s) {
              bind(ctx, d, mat, "iridescence", s);
              bind(ctx, d, mat, "iridescenceIor", s);
              bind(ctx, d, mat, "iridescenceThickness", s);
            });
      })
      .simplified()
      .permute("iridescence",
          V{Any(0.5f), none(), Any("attribute0"), Any("ref_sampler_0")})
      .permute("iridescenceIor", V{none(), Any(2.0f)})
      .permute("iridescenceThickness",
          V{Any(0.5f), none(), Any("attribute0"), Any("ref_sampler_0")})
      .requireFeature(kPbr)
      .registerInto(catalog);

  makeTest("material", "pbr_metallic_roughness")
      .build([](BuildContext &ctx) {
        return materialWorld(ctx,
            "physicallyBased",
            16,
            {{"image1D", "attribute1", false}, {"image2D", "attribute1", true}},
            [](BuildContext &ctx,
                anari::Device d,
                anari::Material mat,
                const std::vector<anari::Sampler> &s) {
              bind(ctx, d, mat, "metallic", s);
              bind(ctx, d, mat, "roughness", s);
              bind(ctx, d, mat, "normal", s);
            });
      })
      .simplified()
      .permute("metallic",
          V{Any(0.5f), none(), Any("attribute0"), Any("ref_sampler_0")})
      .permute("roughness",
          V{Any(0.5f), none(), Any("attribute0"), Any("ref_sampler_0")})
      .permute("normal", V{none(), Any("ref_sampler_1")})
      .requireFeature(kPbr)
      .registerInto(catalog);

  makeTest("material", "pbr_opacity")
      .build([](BuildContext &ctx) {
        return materialWorld(ctx,
            "physicallyBased",
            16,
            {{"image1D", "attribute1", false}},
            [](BuildContext &ctx,
                anari::Device d,
                anari::Material mat,
                const std::vector<anari::Sampler> &s) {
              anari::setParameter(d, mat, "baseColor", float3(1.f, 0.f, 0.f));
              anari::setParameter(d, mat, "alphaMode", "blend");
              bind(ctx, d, mat, "opacity", s);
            });
      })
      .permute("opacity",
          V{none(), Any(0.5f), Any("attribute2"), Any("ref_sampler_0")})
      .requireFeature(kPbr)
      .registerInto(catalog);

  makeTest("material", "pbr_sheen")
      .build([](BuildContext &ctx) {
        return materialWorld(ctx,
            "physicallyBased",
            16,
            {{"image2D", "attribute1", false},
                {"image1D", "attribute1", false}},
            [](BuildContext &ctx,
                anari::Device d,
                anari::Material mat,
                const std::vector<anari::Sampler> &s) {
              bind(ctx, d, mat, "sheenColor", s);
              bind(ctx, d, mat, "sheenRoughness", s);
            });
      })
      .simplified()
      .permute("sheenColor",
          V{Any(float3(0.98f, 0.5f, 0.44f)),
              none(),
              Any("attribute0"),
              Any("ref_sampler_0")})
      .permute("sheenRoughness",
          V{Any(0.5f), none(), Any("attribute0"), Any("ref_sampler_1")})
      .requireFeature(kPbr)
      .registerInto(catalog);

  makeTest("material", "pbr_specular")
      .build([](BuildContext &ctx) {
        return materialWorld(ctx,
            "physicallyBased",
            16,
            {{"image2D", "attribute1", false},
                {"image1D", "attribute1", false}},
            [](BuildContext &ctx,
                anari::Device d,
                anari::Material mat,
                const std::vector<anari::Sampler> &s) {
              bind(ctx, d, mat, "specular", s);
              bind(ctx, d, mat, "specularColor", s);
            });
      })
      .simplified()
      .permute("specular",
          V{Any(0.5f), none(), Any("attribute0"), Any("ref_sampler_1")})
      .permute("specularColor",
          V{Any(float3(0.98f, 0.5f, 0.44f)),
              none(),
              Any("attribute0"),
              Any("ref_sampler_0")})
      .requireFeature(kPbr)
      .registerInto(catalog);

  makeTest("material", "pbr_thickness")
      .build([](BuildContext &ctx) {
        return materialWorld(ctx,
            "physicallyBased",
            16,
            {{"image1D", "attribute1", false}},
            [](BuildContext &ctx,
                anari::Device d,
                anari::Material mat,
                const std::vector<anari::Sampler> &s) {
              anari::setParameter(d, mat, "transmission", 0.5f);
              bind(ctx, d, mat, "thickness", s);
              bind(ctx, d, mat, "attenuationDistance", s);
              bind(ctx, d, mat, "attenuationColor", s);
            });
      })
      .simplified()
      .permute("thickness",
          V{Any(0.5f), none(), Any("attribute0"), Any("ref_sampler_0")})
      .permute("attenuationDistance", V{Any(0.5f), none()})
      .permute("attenuationColor", V{Any(float3(0.98f, 0.5f, 0.44f)), none()})
      .requireFeature(kPbr)
      .registerInto(catalog);

  makeTest("material", "pbr_transmission")
      .build([](BuildContext &ctx) {
        return materialWorld(ctx,
            "physicallyBased",
            16,
            {{"image1D", "attribute1", false}},
            [](BuildContext &ctx,
                anari::Device d,
                anari::Material mat,
                const std::vector<anari::Sampler> &s) {
              bind(ctx, d, mat, "transmission", s);
            });
      })
      .permute("transmission",
          V{none(), Any(0.5f), Any("attribute0"), Any("ref_sampler_0")})
      .requireFeature(kPbr)
      .registerInto(catalog);
}

} // namespace cts
} // namespace anari
