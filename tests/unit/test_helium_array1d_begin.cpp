// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// A 1D array's elements are [begin, end) of its buffer. helium's element
// accessors (valueAt(), readAsAttributeValue(), readAttributeValue(),
// valueAtLinear(), valueAtClosest()) must read from that window, whether they
// are called through the Array1D or through its base Array, after a commit
// that only moves the range, and after the array is privatized.

#include "catch.hpp"
#include "helium_test_device.h"

#include "helium/array/Array1D.h"

#include <algorithm>
#include <memory>
#include <vector>

namespace {

using helium_test::newSharedArray;
using helium_test::setRange;
using helium_test::TestDevice;

// Element 'i' of the app's buffer holds float(i), so an element read as an
// attribute value is (i, 0, 0, 1).
std::vector<float> iota(size_t n)
{
  std::vector<float> v(n);
  for (size_t i = 0; i < n; i++)
    v[i] = float(i);
  return v;
}

// Checks every element accessor of 'a' reads buffer elements
// [first, first + n).
void checkWindow(const helium::Array1D *a, size_t first, size_t n)
{
  REQUIRE(a->size() == n);
  const helium::Array *base = a;
  const float f = float(first);

  for (size_t i = 0; i < n; i++) {
    CHECK(*a->valueAt<float>(i) == f + i);
    CHECK(*base->valueAt<float>(i) == f + i);
  }

  const int32_t sn = int32_t(n);
  for (auto wrap : {helium::WrapMode::CLAMP_TO_EDGE,
           helium::WrapMode::REPEAT,
           helium::WrapMode::MIRROR_REPEAT,
           helium::WrapMode::DEFAULT}) {
    for (int32_t i = -sn - 2; i < 2 * sn + 2; i++) {
      const float expected = f + helium::calculateWrapIndex(i, n, wrap);
      CHECK(a->readAsAttributeValue(i, wrap).x == expected);
      CHECK(base->readAsAttributeValue(i, wrap).x == expected);
    }
  }

  for (uint32_t i = 0; i < n; i++)
    CHECK(helium::readAttributeValue(base, i).x == f + i);

  CHECK(a->valueAtLinear<float>(0.f) == Approx(f));
  CHECK(a->valueAtLinear<float>(0.5f) == Approx(f + (n - 1) * 0.5f));
  CHECK(a->valueAtLinear<float>(1.f) == Approx(f + n - 1));
  CHECK(base->valueAtLinear<float>(1.f) == Approx(f + n - 1));

  CHECK(a->valueAtClosest<float>(0.f) == f);
  CHECK(a->valueAtClosest<float>(1.f) == f + n - 1);
  CHECK(base->valueAtClosest<float>(1.f) == f + n - 1);
}

} // namespace

SCENARIO(
    "1D array element accessors read [begin, end)", "[helium_array1d_begin]")
{
  auto *device = new TestDevice;
  auto *state = device->state();
  auto appData = std::make_unique<std::vector<float>>(iota(8));
  auto *array = newSharedArray(state, *appData);
  array->refInc(helium::RefType::INTERNAL);
  bool released = false;

  GIVEN("a shared array with range [3, 6)")
  {
    setRange(array, 3, 6);

    THEN("the accessors read elements 3 to 5")
    {
      checkWindow(array, 3, 3);
    }

    WHEN("a commit changes only the range, to [1, 8)")
    {
      setRange(array, 1, 8);

      THEN("the accessors read elements 1 to 7")
      {
        checkWindow(array, 1, 7);
      }
    }

    WHEN("the app releases the array and frees its buffer")
    {
      device->release((ANARIObject)array);
      released = true;
      std::fill(appData->begin(), appData->end(), -1.f);
      appData.reset();
      REQUIRE(array->wasPrivatized());

      THEN("the accessors read the private copy of elements 3 to 5")
      {
        checkWindow(array, 3, 3);
      }

      THEN("after a commit to [2, 7) they read elements 2 to 6")
      {
        setRange(array, 2, 7);
        checkWindow(array, 2, 5);
      }
    }
  }

  if (!released)
    device->release((ANARIObject)array);
  array->refDec(helium::RefType::INTERNAL);
  state->commitBuffer.clear();
  delete device;
}
