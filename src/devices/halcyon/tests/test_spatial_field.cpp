// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0
//
// The C++ reference here ports helide's StructuredRegularField (ANARI-SDK,
// src/devices/helide/spatial_field/), Copyright 2021-2026 The Khronos
// Group, licensed under Apache-2.0.

// Checks the structuredRegular spatial field: ISPC sampling, bounds and step
// size against a C++ port of helide's StructuredRegularField for every voxel
// type, invalid setups, and the device object's validity.

#include "SpatialField_ispc.h"
#include "test_spatial_field_ispc.h"
// anari
#define ANARI_LINALG_DEFINITIONS
#include <anari/anari_cpp/ext/linalg.h>
#include <anari/anari_cpp.hpp>
// std
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace {

using namespace anari::math;

// Reference (helide's StructuredRegularField) ////////////////////////////////

struct HelideField
{
  uint3 dims;
  float3 origin;
  float3 spacing;
  std::vector<float> values; // voxels already converted to float, x fastest

  float voxel(uint3 i) const
  {
    return values[i.x + size_t(dims.x) * (i.y + size_t(dims.y) * i.z)];
  }

  float sampleAt(float3 p) const
  {
    const float3 local = 1.f / spacing * (p - origin);
    // written so that NaN coordinates are outside too, as the device's hook
    // does (helide's tests each bound separately, so a NaN coordinate goes on
    // to an out-of-range uint3 conversion: index 0 with SSE, ~0u with AVX-512)
    if (!(local.x >= 0.f && local.x <= dims.x - 1.f && local.y >= 0.f
            && local.y <= dims.y - 1.f && local.z >= 0.f
            && local.z <= dims.z - 1.f))
      return NAN;

    const uint3 vi0 = uint3(local);
    const uint3 vi1 = linalg::clamp(vi0 + 1u, uint3(0u), dims - 1u);
    const float3 f = local - float3(vi0);

    auto v = [&](uint32_t x, uint32_t y, uint32_t z) {
      return voxel(uint3(x, y, z));
    };
    const float v00 =
        linalg::lerp(v(vi0.x, vi0.y, vi0.z), v(vi1.x, vi0.y, vi0.z), f.x);
    const float v01 =
        linalg::lerp(v(vi0.x, vi1.y, vi0.z), v(vi1.x, vi1.y, vi0.z), f.x);
    const float v10 =
        linalg::lerp(v(vi0.x, vi0.y, vi1.z), v(vi1.x, vi0.y, vi1.z), f.x);
    const float v11 =
        linalg::lerp(v(vi0.x, vi1.y, vi1.z), v(vi1.x, vi1.y, vi1.z), f.x);
    return linalg::lerp(
        linalg::lerp(v00, v01, f.y), linalg::lerp(v10, v11, f.y), f.z);
  }
};

// ISPC field storage, as the device's registry slot holds it
struct IspcField
{
  IspcField() : storage(new uint64_t[(ispc::sizeof_SpatialField() + 7) / 8])
  {
    ispc::SpatialField_setDefaults(ie());
  }
  void *ie() const
  {
    return storage.get();
  }
  std::unique_ptr<uint64_t[]> storage;
};

bool sameFloat(float a, float b)
{
  if (std::isnan(a) || std::isnan(b))
    return std::isnan(a) && std::isnan(b);
  return std::fabs(a - b) <= 1e-5f * std::max(1.f, std::fabs(b));
}

// Points on and around the field: every half voxel from one voxel outside
// to one outside the far corner, a few off-grid points, and a NaN
std::vector<float3> samplePoints(const HelideField &f)
{
  std::vector<float3> points;
  const float3 upper = f.origin + (float3(f.dims) - 1.f) * f.spacing;
  for (float z = -1.f; z <= f.dims.z; z += 0.5f)
    for (float y = -1.f; y <= f.dims.y; y += 0.5f)
      for (float x = -1.f; x <= f.dims.x; x += 0.5f)
        points.push_back(f.origin + float3(x, y, z) * f.spacing);
  for (int i = 0; i < 97; i++) {
    const float3 t(
        (i * 37 % 101) / 100.f, (i * 53 % 103) / 102.f, (i * 71 % 107) / 106.f);
    points.push_back(f.origin + t * (upper - f.origin));
  }
  points.push_back(upper);
  points.push_back(float3(NAN, f.origin.y, f.origin.z));
  return points;
}

template <typename T>
bool testVoxelType(const char *name,
    ANARIDataType type,
    const HelideField &shape,
    const std::vector<T> &voxels,
    float divisor)
{
  HelideField ref = shape;
  ref.values.clear();
  for (T v : voxels)
    ref.values.push_back(float(v) / divisor);

  IspcField field;
  const bool valid = ispc::SpatialField_setStructuredRegular(field.ie(),
      voxels.data(),
      type,
      &ref.dims.x,
      &ref.origin.x,
      &ref.spacing.x);

  if (!valid || !ispc::test_isValid(field.ie())) {
    std::printf("FAIL %s: field is invalid\n", name);
    return false;
  }

  const auto points = samplePoints(ref);
  std::vector<float> results(points.size());
  ispc::test_sampleAt(
      field.ie(), &points[0].x, results.data(), int(points.size()));

  int failures = 0;
  int inside = 0;
  for (size_t i = 0; i < points.size(); i++) {
    const float expected = ref.sampleAt(points[i]);
    inside += !std::isnan(expected);
    if (!sameFloat(results[i], expected) && failures++ < 5) {
      std::printf("FAIL %s: sampleAt(%g, %g, %g) = %g, expected %g\n",
          name,
          points[i].x,
          points[i].y,
          points[i].z,
          results[i],
          expected);
    }
  }
  if (inside < 10 || inside == int(points.size())) {
    std::printf("FAIL %s: sample points don't straddle the bounds\n", name);
    failures++;
  }

  // bounds: [origin, origin + (dims - 1) * spacing]
  float3 lower, upper;
  ispc::SpatialField_getBounds(field.ie(), &lower.x, &upper.x);
  const float3 expectedUpper =
      ref.origin + (float3(ref.dims) - 1.f) * ref.spacing;
  for (int d = 0; d < 3; d++) {
    if (!sameFloat(lower[d], ref.origin[d])
        || !sameFloat(upper[d], expectedUpper[d])) {
      std::printf("FAIL %s: bounds (%g, %g, %g)-(%g, %g, %g)\n",
          name,
          lower.x,
          lower.y,
          lower.z,
          upper.x,
          upper.y,
          upper.z);
      failures++;
      break;
    }
  }

  // stepSize: min(spacing) / 2
  const float step = ispc::SpatialField_getStepSize(field.ie());
  if (!sameFloat(step, linalg::minelem(ref.spacing) / 2.f)) {
    std::printf("FAIL %s: stepSize %g\n", name, step);
    failures++;
  }

  if (failures == 0)
    std::printf(
        "PASS %s (%d of %zu points inside)\n", name, inside, points.size());
  return failures == 0;
}

template <typename T>
std::vector<T> voxelValues(size_t n, double lo, double hi)
{
  std::vector<T> v(n);
  for (size_t i = 0; i < n; i++)
    v[i] = T(lo + (hi - lo) * double((i * 7919) % n) / double(n - 1));
  return v;
}

bool testSampling()
{
  const HelideField shape{{5, 4, 3}, {-1.f, 0.5f, 2.f}, {0.5f, 1.f, 2.f}, {}};
  const size_t n = 5 * 4 * 3;

  bool pass = true;
  pass &= testVoxelType(
      "FLOAT32", ANARI_FLOAT32, shape, voxelValues<float>(n, -3.0, 7.0), 1.f);
  pass &= testVoxelType(
      "FLOAT64", ANARI_FLOAT64, shape, voxelValues<double>(n, -3.0, 7.0), 1.f);
  pass &= testVoxelType(
      "UFIXED8", ANARI_UFIXED8, shape, voxelValues<uint8_t>(n, 0, 255), 255.f);
  pass &= testVoxelType("UFIXED16",
      ANARI_UFIXED16,
      shape,
      voxelValues<uint16_t>(n, 0, 65535),
      65535.f);
  pass &= testVoxelType("FIXED16",
      ANARI_FIXED16,
      shape,
      voxelValues<int16_t>(n, -32768, 32767),
      32767.f);

  // a single voxel layer in y, default origin and spacing
  const HelideField flat{{3, 1, 2}, float3(0.f), float3(1.f), {}};
  pass &= testVoxelType("FLOAT32, one voxel in y",
      ANARI_FLOAT32,
      flat,
      voxelValues<float>(6, 0.0, 1.0),
      1.f);

  return pass;
}

// Setups that leave the field invalid: empty bounds, no step size
bool expectInvalid(const char *name,
    const void *voxels,
    ANARIDataType type,
    uint3 dims,
    float3 spacing = float3(1.f))
{
  IspcField field;
  const float3 origin(0.f);
  const bool valid = ispc::SpatialField_setStructuredRegular(
      field.ie(), voxels, type, &dims.x, &origin.x, &spacing.x);

  float3 lower, upper;
  ispc::SpatialField_getBounds(field.ie(), &lower.x, &upper.x);
  const bool pass = !valid && !ispc::test_isValid(field.ie())
      && lower.x > upper.x && ispc::SpatialField_getStepSize(field.ie()) == 0.f;
  std::printf("%s invalid: %s\n", pass ? "PASS" : "FAIL", name);
  return pass;
}

bool testInvalid()
{
  const float floats[8] = {};
  const int32_t ints[8] = {};

  bool pass = true;
  {
    IspcField field;
    const bool ok = !ispc::test_isValid(field.ie());
    std::printf("%s invalid: defaults\n", ok ? "PASS" : "FAIL");
    pass &= ok;
  }
  pass &= expectInvalid("no voxels", nullptr, ANARI_FLOAT32, uint3(2u));
  pass &= expectInvalid("INT32 voxels", ints, ANARI_INT32, uint3(2u));
  pass &= expectInvalid("empty dimension", floats, ANARI_FLOAT32, {2, 0, 2});
  pass &= expectInvalid(
      "zero spacing", floats, ANARI_FLOAT32, uint3(2u), float3(1.f, 0.f, 1.f));
  pass &= expectInvalid("negative spacing",
      floats,
      ANARI_FLOAT32,
      uint3(2u),
      float3(1.f, 1.f, -1.f));

  // a valid field set up again without voxels loses its hook
  IspcField field;
  const uint3 dims(2u);
  const float3 origin(0.f), spacing(1.f);
  ispc::SpatialField_setStructuredRegular(
      field.ie(), floats, ANARI_FLOAT32, &dims.x, &origin.x, &spacing.x);
  ispc::SpatialField_setStructuredRegular(
      field.ie(), nullptr, ANARI_FLOAT32, &dims.x, &origin.x, &spacing.x);
  const bool reset = !ispc::test_isValid(field.ie());
  std::printf("%s invalid: reset without voxels\n", reset ? "PASS" : "FAIL");
  return pass && reset;
}

// Device objects /////////////////////////////////////////////////////////////

// device errors (e.g. ISPC object size mismatches) fail the test
bool g_deviceError = false;

void statusFunc(const void *,
    ANARIDevice,
    ANARIObject,
    ANARIDataType,
    ANARIStatusSeverity severity,
    ANARIStatusCode,
    const char *message)
{
  if (severity <= ANARI_SEVERITY_ERROR) {
    g_deviceError = true;
    std::fprintf(stderr, "[ANARI] %s\n", message);
  }
}

bool isValid(anari::Device d, anari::SpatialField f)
{
  bool valid = false;
  anari::getProperty(d, f, "valid", valid, ANARI_WAIT);
  return valid;
}

bool expectValid(
    anari::Device d, anari::SpatialField f, bool expected, const char *name)
{
  const bool pass = isValid(d, f) == expected;
  std::printf("%s object: %s\n", pass ? "PASS" : "FAIL", name);
  return pass;
}

bool testObjects(anari::Device d)
{
  bool pass = true;

  auto unknown = anari::newObject<anari::SpatialField>(d, "unknown");
  anari::commitParameters(d, unknown);
  pass &= expectValid(d, unknown, false, "unknown subtype is invalid");
  anari::release(d, unknown);

  auto f = anari::newObject<anari::SpatialField>(d, "structuredRegular");
  anari::commitParameters(d, f);
  pass &= expectValid(d, f, false, "structuredRegular without data is invalid");

  std::vector<float> voxels(4 * 3 * 2, 0.5f);
  anari::setParameterArray3D(d, f, "data", voxels.data(), 4, 3, 2);
  anari::setParameter(d, f, "origin", float3(1.f, 2.f, 3.f));
  anari::setParameter(d, f, "spacing", float3(0.5f));
  anari::commitParameters(d, f);
  pass &= expectValid(d, f, true, "FLOAT32 data is valid");

  // normalized types need an explicitly typed array (uint8_t is UINT8)
  std::vector<uint8_t> bytes(4 * 3 * 2, 128);
  auto fixed8 = anariNewArray3D(
      d, bytes.data(), nullptr, nullptr, ANARI_UFIXED8, 4, 3, 2);
  anari::setAndReleaseParameter(d, f, "data", fixed8);
  anari::commitParameters(d, f);
  pass &= expectValid(d, f, true, "UFIXED8 data is valid");

  std::vector<int32_t> ints(4 * 3 * 2, 1);
  anari::setParameterArray3D(d, f, "data", ints.data(), 4, 3, 2);
  anari::commitParameters(d, f);
  pass &= expectValid(d, f, false, "INT32 data is invalid");

  anari::setParameterArray3D(d, f, "data", voxels.data(), 4, 3, 2);
  anari::commitParameters(d, f);
  pass &= expectValid(d, f, true, "valid again with FLOAT32 data");

  anari::unsetParameter(d, f, "data");
  anari::commitParameters(d, f);
  pass &= expectValid(d, f, false, "invalid after unsetting data");

  anari::release(d, f);
  return pass;
}

} // namespace

int main()
{
  bool pass = true;
  pass &= testSampling();
  pass &= testInvalid();

  auto library = anari::loadLibrary("halcyon", statusFunc);
  if (!library) {
    std::fprintf(stderr, "failed to load halcyon library\n");
    return 1;
  }
  auto d = anari::newDevice(library, "default");
  pass &= testObjects(d);
  anari::release(d, d);
  anari::unloadLibrary(library);

  if (g_deviceError)
    std::printf("FAIL: device reported errors\n");

  return pass && !g_deviceError ? 0 : 1;
}
