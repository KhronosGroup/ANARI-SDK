// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// helide_gpu's GPUBuffer::uploadArray() sends arrayUploadData()'s bytes to the
// GPU. For a 1D array those must be elements [begin, end) of its buffer on
// every path (float as stored, other types as stored, sRGB decoded, other
// types converted), including after a commit that only moves the range.
// arrayUploadData() needs no GPU device, so this runs headless.

#include "catch.hpp"
#include "helium_test_device.h"

#include "HelideGPUSRGB.h"
#include "gpu/ArrayUpload.h"

#include "helium/array/Array1D.h"
#include "helium/array/Array3D.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

using helium_test::newSharedArray;
using helium_test::setRange;
using helium_test::TestDevice;

template <typename T>
std::vector<T> uploadedAs(const helide_gpu::ArrayUploadData &u)
{
  REQUIRE(u.sizeInBytes() % sizeof(T) == 0);
  std::vector<T> v(u.sizeInBytes() / sizeof(T));
  if (!v.empty())
    std::memcpy(v.data(), u.bytes(), u.sizeInBytes());
  return v;
}

// The app buffer is 8 elements; element i is recognizable from its index.
constexpr size_t kCapacity = 8;

std::vector<float> floatElements()
{
  std::vector<float> v(kCapacity);
  for (size_t i = 0; i < kCapacity; i++)
    v[i] = float(i);
  return v;
}

std::vector<uint32_t> uintElements()
{
  std::vector<uint32_t> v(kCapacity);
  for (size_t i = 0; i < kCapacity; i++)
    v[i] = uint32_t(100 + i);
  return v;
}

// kCapacity RGBA8 elements; element i is (10i, 10i + 1, 10i + 2, 20i).
std::vector<uint8_t> rgba8Elements()
{
  std::vector<uint8_t> v(4 * kCapacity);
  for (size_t i = 0; i < kCapacity; i++) {
    v[4 * i + 0] = uint8_t(10 * i);
    v[4 * i + 1] = uint8_t(10 * i + 1);
    v[4 * i + 2] = uint8_t(10 * i + 2);
    v[4 * i + 3] = uint8_t(20 * i);
  }
  return v;
}

// Elements [begin, end) of 'app'.
template <typename T>
std::vector<T> window(const std::vector<T> &app, size_t begin, size_t end)
{
  return std::vector<T>(app.begin() + begin, app.begin() + end);
}

std::vector<float> expectedSRGB(
    const std::vector<uint8_t> &app, size_t begin, size_t end)
{
  std::vector<float> v;
  for (size_t i = begin; i < end; i++) {
    float c[4];
    helide_gpu::srgbBytesToLinear(&app[4 * i], 4, c);
    v.insert(v.end(), c, c + 4);
  }
  return v;
}

std::vector<float> expectedUnorm(
    const std::vector<uint8_t> &app, size_t begin, size_t end)
{
  std::vector<float> v;
  for (size_t i = 4 * begin; i < 4 * end; i++)
    v.push_back(app[i] / 255.f);
  return v;
}

void checkApprox(
    const std::vector<float> &actual, const std::vector<float> &expected)
{
  REQUIRE(actual.size() == expected.size());
  for (size_t i = 0; i < actual.size(); i++)
    CHECK(actual[i] == Approx(expected[i]));
}

// One shared array per upload path, over the app buffers it reads.
struct Arrays
{
  explicit Arrays(helium::BaseGlobalDeviceState *s)
      : floats(newSharedArray(s, floatApp)),
        uints(newSharedArray(s, uintApp)),
        srgb(newSharedArray(
            s, rgba8App.data(), ANARI_UFIXED8_RGBA_SRGB, kCapacity)),
        unorm(newSharedArray(s, rgba8App.data(), ANARI_UFIXED8_VEC4, kCapacity))
  {}

  const std::vector<float> floatApp = floatElements();
  const std::vector<uint32_t> uintApp = uintElements();
  const std::vector<uint8_t> rgba8App = rgba8Elements();

  helium::Array1D *floats; // float: uploaded as stored
  helium::Array1D *uints; // uint32 without conversion: uploaded as stored
  helium::Array1D *srgb; // RGBA8 sRGB: decoded to linear floats
  helium::Array1D *unorm; // RGBA8 unorm: converted via readAsAttributeValue()

  std::vector<helium::Array1D *> all() const
  {
    return {floats, uints, srgb, unorm};
  }

  void setRanges(size_t begin, size_t end)
  {
    for (auto *a : all())
      setRange(a, begin, end);
  }

  // Checks every path uploads buffer elements [begin, end).
  void checkUploads(size_t begin, size_t end) const
  {
    using helide_gpu::arrayUploadData;

    for (bool convert : {false, true}) {
      INFO("convertToFloat = " << convert);
      CHECK(uploadedAs<float>(arrayUploadData(floats, convert))
          == window(floatApp, begin, end));
    }
    CHECK(uploadedAs<uint32_t>(arrayUploadData(uints, false))
        == window(uintApp, begin, end));
    checkApprox(uploadedAs<float>(arrayUploadData(srgb, true)),
        expectedSRGB(rgba8App, begin, end));
    checkApprox(uploadedAs<float>(arrayUploadData(unorm, true)),
        expectedUnorm(rgba8App, begin, end));
  }
};

} // namespace

SCENARIO("helide_gpu uploads a 1D array's elements [begin, end)",
    "[helide_gpu_array_upload]")
{
  auto *device = new TestDevice;
  auto *state = device->state();

  Arrays arrays(state);

  GIVEN("arrays with the full range")
  {
    THEN("every element is uploaded")
    {
      arrays.checkUploads(0, kCapacity);
    }
  }

  GIVEN("arrays with range [3, 6)")
  {
    arrays.setRanges(3, 6);

    THEN("elements 3 to 5 are uploaded")
    {
      arrays.checkUploads(3, 6);
    }

    WHEN("a commit changes only the range, to [1, 7)")
    {
      std::vector<helium::TimeStamp> before;
      for (auto *a : arrays.all())
        before.push_back(a->lastDataModified());

      arrays.setRanges(1, 7);

      THEN("the arrays read as modified, so GPUBuffer re-uploads them")
      {
        const auto all = arrays.all();
        for (size_t i = 0; i < all.size(); i++)
          CHECK(all[i]->lastDataModified() > before[i]);
      }

      THEN("elements 1 to 6 are uploaded")
      {
        arrays.checkUploads(1, 7);
      }
    }
  }

  for (auto *a : arrays.all())
    device->release((ANARIObject)a);
  state->commitBuffer.clear();
  delete device;
}

// structuredRegular spatial fields upload their 3D data as one float per
// voxel: writeScalarFieldVoxels() converts it in bulk, and must give exactly
// the first component of readAsAttributeValue() for every element type.

namespace {

helium::Array3D *newArray3D(helium::BaseGlobalDeviceState *s,
    const void *appMemory,
    ANARIDataType elementType,
    anari::math::uint3 dims)
{
  helium::Array3DMemoryDescriptor md;
  md.appMemory = appMemory;
  md.elementType = elementType;
  md.numItems1 = dims.x;
  md.numItems2 = dims.y;
  md.numItems3 = dims.z;
  auto *array = new helium::Array3D(s, md);
  array->commitParameters();
  return array;
}

// What the device converted per voxel before the bulk conversion.
std::vector<float> perVoxelReference(const helium::Array3D *a)
{
  const auto d = a->size();
  std::vector<float> v;
  for (uint32_t z = 0; z < d.z; z++)
    for (uint32_t y = 0; y < d.y; y++)
      for (uint32_t x = 0; x < d.x; x++)
        v.push_back(a->readAsAttributeValue(
                         anari::math::int3(int32_t(x), int32_t(y), int32_t(z)))
                        .x);
  return v;
}

template <typename T>
std::vector<T> voxelValues(size_t n, T scale)
{
  std::vector<T> v(n);
  for (size_t i = 0; i < n; i++)
    v[i] = T((i * 37 + 11) % 256) * scale;
  return v;
}

} // namespace

SCENARIO("helide_gpu converts structured field voxels to float in bulk",
    "[helide_gpu_array_upload]")
{
  auto *device = new TestDevice;
  auto *state = device->state();

  const anari::math::uint3 dims(5, 4, 3);
  const size_t n = size_t(dims.x) * dims.y * dims.z;

  const auto f32 = voxelValues<float>(n, 0.25f);
  const auto f32v2 = voxelValues<float>(2 * n, 0.5f);
  const auto u8 = voxelValues<uint8_t>(n, 1);
  const auto u16 = voxelValues<uint16_t>(n, 251);
  const auto u32 = voxelValues<uint32_t>(n, 16777259u);
  const auto i16 = voxelValues<int16_t>(n, 100);

  struct Case
  {
    const char *name;
    const void *data;
    ANARIDataType type;
  };
  const Case cases[] = {{"FLOAT32", f32.data(), ANARI_FLOAT32},
      {"FLOAT32_VEC2", f32v2.data(), ANARI_FLOAT32_VEC2},
      {"UFIXED8", u8.data(), ANARI_UFIXED8},
      {"UFIXED8_R_SRGB", u8.data(), ANARI_UFIXED8_R_SRGB},
      {"UFIXED16", u16.data(), ANARI_UFIXED16},
      {"UFIXED32", u32.data(), ANARI_UFIXED32},
      {"INT16", i16.data(), ANARI_INT16}};

  for (const auto &c : cases) {
    GIVEN(std::string("a 5x4x3 ") + c.name + " array")
    {
      auto *a = newArray3D(state, c.data, c.type, dims);

      THEN("each voxel matches the per-voxel attribute read exactly")
      {
        std::vector<float> voxels(n, -1.f);
        helide_gpu::writeScalarFieldVoxels(a, voxels.data());
        CHECK(voxels == perVoxelReference(a));
      }

      device->release((ANARIObject)a);
    }
  }

  state->commitBuffer.clear();
  delete device;
}
