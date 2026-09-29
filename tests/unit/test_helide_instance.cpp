// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Regression for helide's instanced normal transform: an instanced triangle
// must shade the same as the same triangle pre-transformed into world space.
// helide's renderer shades by the angle between the ray and the geometric
// normal, which embree reports in object space for instanced hits, so the
// normal must be carried to world space by the inverse transpose of the
// instance's linear part (per transform-array element).

#include "catch.hpp"

#include <anari/anari_cpp/ext/linalg.h>
#include <anari/anari_cpp.hpp>
// std
#include <cstdio>
#include <string>
#include <vector>

namespace {

using namespace anari::math;

constexpr uint2 kImageSize = {64, 64};

const std::vector<float3> kTriangle = {
    {-0.5f, -0.5f, 0.f}, {0.5f, -0.5f, 0.f}, {0.f, 0.5f, 0.f}};

void statusFunc(const void *,
    ANARIDevice,
    ANARIObject source,
    ANARIDataType,
    ANARIStatusSeverity severity,
    ANARIStatusCode,
    const char *message)
{
  if (severity == ANARI_SEVERITY_FATAL_ERROR
      || severity == ANARI_SEVERITY_ERROR) {
    fprintf(stderr, "[ANARI][ERROR][%p] %s\n", source, message);
  }
}

mat4 translation(float3 t)
{
  return linalg::translation_matrix(t);
}

mat4 rotation(float3 axis, float angle)
{
  return linalg::rotation_matrix(linalg::rotation_quat(axis, angle));
}

mat4 scaling(float3 s)
{
  return linalg::scaling_matrix(s);
}

std::vector<float3> transformed(const std::vector<float3> &v, const mat4 &m)
{
  std::vector<float3> r;
  for (auto &p : v)
    r.push_back(linalg::mul(m, float4(p, 1.f)).xyz());
  return r;
}

anari::Surface makeSurface(anari::Device d, const std::vector<float3> &verts)
{
  auto geom = anari::newObject<anari::Geometry>(d, "triangle");
  anari::setParameterArray1D(
      d, geom, "vertex.position", verts.data(), verts.size());
  anari::commitParameters(d, geom);

  auto mat = anari::newObject<anari::Material>(d, "matte");
  anari::setParameter(d, mat, "color", float3(0.8f));
  anari::commitParameters(d, mat);

  auto surface = anari::newObject<anari::Surface>(d);
  anari::setAndReleaseParameter(d, surface, "geometry", geom);
  anari::setAndReleaseParameter(d, surface, "material", mat);
  anari::commitParameters(d, surface);
  return surface;
}

void setSurfaces(anari::Device d,
    anari::Object o,
    const std::vector<anari::Surface> &surfaces)
{
  anari::setParameterArray1D(d, o, "surface", surfaces.data(), surfaces.size());
  anari::commitParameters(d, o);
  for (auto s : surfaces)
    anari::release(d, s);
}

// One instance of 'verts'; more than one transform uses a transform array.
anari::World makeInstancedWorld(anari::Device d,
    const std::vector<float3> &verts,
    const std::vector<mat4> &xfms)
{
  auto group = anari::newObject<anari::Group>(d);
  setSurfaces(d, group, {makeSurface(d, verts)});

  auto inst = anari::newObject<anari::Instance>(d, "transform");
  anari::setAndReleaseParameter(d, inst, "group", group);
  if (xfms.size() == 1)
    anari::setParameter(d, inst, "transform", xfms[0]);
  else
    anari::setParameterArray1D(d, inst, "transform", xfms.data(), xfms.size());
  anari::commitParameters(d, inst);

  auto world = anari::newObject<anari::World>(d);
  anari::setParameterArray1D(d, world, "instance", &inst, 1);
  anari::commitParameters(d, world);
  anari::release(d, inst);
  return world;
}

// One non-instanced surface per transform, with 'verts' pre-transformed.
anari::World makePretransformedWorld(anari::Device d,
    const std::vector<float3> &verts,
    const std::vector<mat4> &xfms)
{
  std::vector<anari::Surface> surfaces;
  for (auto &m : xfms)
    surfaces.push_back(makeSurface(d, transformed(verts, m)));

  auto world = anari::newObject<anari::World>(d);
  setSurfaces(d, world, surfaces);
  return world;
}

std::vector<float4> render(
    anari::Device d, anari::World world, const std::string &mode)
{
  auto camera = anari::newObject<anari::Camera>(d, "perspective");
  anari::setParameter(d, camera, "position", float3(0.f, 0.f, 2.f));
  anari::setParameter(d, camera, "direction", float3(0.f, 0.f, -1.f));
  anari::setParameter(d, camera, "up", float3(0.f, 1.f, 0.f));
  anari::setParameter(d, camera, "aspect", 1.f);
  anari::commitParameters(d, camera);

  auto renderer = anari::newObject<anari::Renderer>(d, "default");
  anari::setParameter(d, renderer, "background", float4(0.f, 0.f, 0.f, 1.f));
  anari::setParameter(d, renderer, "mode", mode);
  anari::commitParameters(d, renderer);

  auto frame = anari::newObject<anari::Frame>(d);
  anari::setParameter(d, frame, "size", kImageSize);
  anari::setParameter(d, frame, "channel.color", ANARI_FLOAT32_VEC4);
  anari::setAndReleaseParameter(d, frame, "camera", camera);
  anari::setAndReleaseParameter(d, frame, "renderer", renderer);
  anari::setParameter(d, frame, "world", world);
  anari::commitParameters(d, frame);

  anari::render(d, frame);
  anari::wait(d, frame);

  auto fb = anari::map<float4>(d, frame, "channel.color");
  std::vector<float4> pixels(fb.data, fb.data + fb.width * fb.height);
  anari::unmap(d, frame, "channel.color");

  anari::release(d, frame);
  return pixels;
}

struct Comparison
{
  size_t pixels{0};
  size_t hits{0};
  size_t mismatches{0};
};

Comparison compare(const std::vector<float4> &a, const std::vector<float4> &b)
{
  Comparison c;
  c.pixels = a.size();
  for (size_t i = 0; i < a.size(); i++) {
    if (a[i].xyz() != float3(0.f))
      c.hits++;
    if (linalg::maxelem(linalg::abs(a[i] - b[i])) > 1e-3f)
      c.mismatches++;
  }
  return c;
}

void checkInstancedMatchesPretransformed(
    anari::Device d, const std::vector<mat4> &xfms, const std::string &mode)
{
  auto instanced = makeInstancedWorld(d, kTriangle, xfms);
  auto pretransformed = makePretransformedWorld(d, kTriangle, xfms);
  const auto c =
      compare(render(d, instanced, mode), render(d, pretransformed, mode));
  anari::release(d, instanced);
  anari::release(d, pretransformed);

  INFO(c.hits << " hit pixels, " << c.mismatches << " mismatches");
  // the triangle must actually cover part of the image...
  CHECK(c.hits > c.pixels / 50);
  // ...and match, allowing a few hit/miss flips along triangle edges
  CHECK(c.mismatches <= c.pixels / 100);
}

} // namespace

TEST_CASE("instanced normals are transformed by the inverse transpose",
    "[helide][helide_instance]")
{
  anari::Library lib = anari::loadLibrary("helide", statusFunc, nullptr);
  if (lib == nullptr) {
    WARN("helide library not available; skipping instance normal test");
    return;
  }

  anari::Device d = anari::newDevice(lib, "default");

  const mat4 rot = rotation(float3(0.f, 1.f, 0.f), 1.f);
  const mat4 rotScale = linalg::mul(rot,
      linalg::mul(rotation(float3(1.f, 0.f, 0.f), 0.5f),
          scaling(float3(1.5f, 0.75f, 2.f))));
  const std::vector<mat4> xfmArray = {
      linalg::mul(translation(float3(-0.6f, 0.f, 0.f)), rot),
      linalg::mul(translation(float3(0.6f, 0.f, 0.f)),
          linalg::mul(rotation(float3(1.f, 0.f, 0.f), -0.8f),
              scaling(float3(0.8f, 1.2f, 1.f))))};

  for (const char *mode : {"default", "opacityHeatmap"}) {
    DYNAMIC_SECTION("mode '" << mode << "': rotation")
    {
      checkInstancedMatchesPretransformed(d, {rot}, mode);
    }
    DYNAMIC_SECTION("mode '" << mode << "': rotation + non-uniform scale")
    {
      checkInstancedMatchesPretransformed(d, {rotScale}, mode);
    }
    DYNAMIC_SECTION("mode '" << mode << "': transform array")
    {
      checkInstancedMatchesPretransformed(d, xfmArray, mode);
    }
  }

  anari::release(d, d);
  anari::unloadLibrary(lib);
}
