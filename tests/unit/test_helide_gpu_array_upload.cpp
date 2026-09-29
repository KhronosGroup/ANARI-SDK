// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// helide_gpu's GPUBuffer::uploadArray() sends arrayUploadData()'s bytes to the
// GPU. For a 1D array those must be elements [begin, end) of its buffer on
// every path (float as stored, other types as stored, sRGB decoded, other
// types converted), including after a commit that only moves the range.
// arrayUploadData() needs no GPU device, so this runs headless.

#include "catch.hpp"
#include "helium_test_device.h"

#include "HelideGPUColorSpace.h"
#include "gpu/ArrayUpload.h"

#include "helium/array/Array1D.h"

#include <cstdint>
#include <cstring>
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
    const auto c = helide_gpu::srgbBytesToLinear(&app[4 * i], 4);
    v.insert(v.end(), {c.x, c.y, c.z, c.w});
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
