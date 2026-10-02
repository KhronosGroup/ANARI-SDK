// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Releasing the app's last reference to a shared array the device still uses
// privatizes it: the array copies the app's data, so data() moves. BaseDevice
// runs that release as device work (so it waits for a render in flight) and
// the array notifies its observers, which re-finalize at the next flush and
// pick up the new pointer.

#include "catch.hpp"
#include "helium_test_device.h"

#include "helium/array/Array1D.h"
#include "helium/utility/ChangeObserverPtr.h"

#include <algorithm>
#include <memory>
#include <vector>

namespace {

using helium_test::newSharedArray;
using helium_test::TestDevice;

// Observes an array and records its data() at each finalize().
struct Observer : public helium::BaseObject
{
  Observer(helium::BaseGlobalDeviceState *s, helium::Array1D *a)
      : BaseObject(ANARI_GEOMETRY, s), array(this, a)
  {}

  bool isValid() const override
  {
    return true;
  }
  bool getProperty(const std::string_view &,
      ANARIDataType,
      void *,
      uint64_t,
      uint32_t) override
  {
    return false;
  }
  void commitParameters() override {}
  void finalize() override
  {
    data = array->data();
  }

  helium::ChangeObserverPtr<helium::Array1D> array;
  const void *data{nullptr};
};

// Counts device releases, noting whether 'watched' was privatized before and
// after each run, and device queries.
struct CountingDevice : public TestDevice
{
  void runDeviceRelease(const std::function<void()> &work) override
  {
    runs++;
    privatizedBefore = watched && watched->wasPrivatized();
    work();
    privatizedAfter = watched && watched->wasPrivatized();
  }

  bool runDeviceQuery(const std::function<void(bool)> &work) override
  {
    queries++;
    work(true);
    return true;
  }

  helium::Array1D *watched{nullptr};
  int runs{0};
  int queries{0};
  bool privatizedBefore{false};
  bool privatizedAfter{false};
};

} // namespace

SCENARIO(
    "releasing a shared array the device uses privatizes it as device work",
    "[helium_array_privatize]")
{
  auto *device = new CountingDevice;
  auto *state = device->state();
  std::vector<int> appData = {1, 2, 3, 4};
  auto *array = newSharedArray(state, appData);
  device->watched = array;

  GIVEN("an object observing the array")
  {
    auto *observer = new Observer(state, array);
    observer->markUpdated();
    state->commitBuffer.addObjectToFinalize(observer);
    state->commitBuffer.flush();
    REQUIRE(observer->data == appData.data());

    WHEN("the app releases its last reference to the array")
    {
      device->release((ANARIObject)array);

      THEN("the array privatizes inside a device release, not a query")
      {
        CHECK(device->runs == 1);
        CHECK(device->queries == 0);
        CHECK(!device->privatizedBefore);
        CHECK(device->privatizedAfter);
        CHECK(array->data() != appData.data());
      }

      THEN("the observer picks up the private copy at the next flush")
      {
        state->commitBuffer.flush();
        CHECK(observer->data == array->data());
        CHECK(observer->data != appData.data());
        CHECK(((const int *)observer->data)[3] == 4);
      }
    }

    WHEN("the app releases one of two references to the array")
    {
      device->retain((ANARIObject)array);
      device->release((ANARIObject)array);

      THEN("nothing is privatized or run as device work")
      {
        CHECK(device->runs == 0);
        CHECK(array->data() == appData.data());
      }

      device->release((ANARIObject)array);
    }

    device->watched = nullptr;
    observer->refDec(helium::RefType::PUBLIC);
  }

  GIVEN("no object using the array")
  {
    WHEN("the app releases it")
    {
      device->watched = nullptr;
      device->release((ANARIObject)array);

      THEN("it is deleted without device work")
      {
        CHECK(device->runs == 0);
      }
    }
  }

  state->commitBuffer.clear();
  delete device;
}

SCENARIO("releasing a managed array the device uses runs no device work",
    "[helium_array_privatize]")
{
  auto *device = new CountingDevice;
  auto *state = device->state();
  helium::Array1DMemoryDescriptor md;
  md.elementType = ANARI_INT32;
  md.numItems = 4;
  auto *array = new helium::Array1D(state, md);
  array->commitParameters();
  auto *observer = new Observer(state, array);

  WHEN("the app releases its last reference to the array")
  {
    const void *data = array->data();
    device->release((ANARIObject)array);

    THEN("its memory, already helium's, is kept without device work")
    {
      CHECK(device->runs == 0);
      CHECK(array->data() == data);
    }
  }

  observer->refDec(helium::RefType::PUBLIC);
  state->commitBuffer.clear();
  delete device;
}

SCENARIO("privatizing a shared array with a nonzero 'begin' keeps its range",
    "[helium_array_privatize]")
{
  auto *device = new TestDevice;
  auto *state = device->state();
  auto appData = std::make_unique<std::vector<float>>(
      std::vector<float>{0.f, 1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 7.f});
  auto *array = newSharedArray(state, *appData);
  array->setParam("begin", size_t(3));
  array->setParam("end", size_t(6));
  array->commitParameters();
  array->refInc(helium::RefType::INTERNAL);

  WHEN("the app releases the array and frees its buffer")
  {
    device->release((ANARIObject)array);
    std::fill(appData->begin(), appData->end(), -1.f);
    appData.reset();
    REQUIRE(array->wasPrivatized());

    THEN("the range still reads the originally shared values")
    {
      REQUIRE(array->size() == 3);
      const float *v = array->beginAs<float>();
      CHECK(v[0] == 3.f);
      CHECK(v[1] == 4.f);
      CHECK(v[2] == 5.f);
    }

    THEN("a later commit widening the range reads the shared values")
    {
      array->setParam("begin", size_t(0));
      array->setParam("end", size_t(8));
      array->commitParameters();
      REQUIRE(array->size() == 8);
      const float *v = array->beginAs<float>();
      for (int i = 0; i < 8; i++)
        CHECK(v[i] == float(i));
    }
  }

  array->refDec(helium::RefType::INTERNAL);
  state->commitBuffer.clear();
  delete device;
}
