// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// An ObjectArray's handlesBegin()/handlesEnd() yield exactly the handles in
// [begin, end) followed by any appended handles, however the range, the
// mapped contents and the appended handles changed, and after privatization.

#include "catch.hpp"
#include "helium_test_device.h"

#include "helium/array/ObjectArray.h"

#include <vector>

namespace {

using helium_test::StubObject;
using helium_test::TestDevice;

using Handles = std::vector<helium::BaseObject *>;

Handles liveHandles(const helium::ObjectArray *a)
{
  return Handles(a->handlesBegin(), a->handlesEnd());
}

void setRange(helium::ObjectArray *a, size_t begin, size_t end)
{
  a->setParam("begin", begin);
  a->setParam("end", end);
  a->commitParameters();
  a->finalize();
}

void mapUnmap(helium::ObjectArray *a)
{
  a->map();
  a->unmap();
}

// appHandles[begin, end) followed by 'extra'
Handles expected(const Handles &appHandles,
    size_t begin,
    size_t end,
    const Handles &extra = {})
{
  Handles h(appHandles.begin() + begin, appHandles.begin() + end);
  h.insert(h.end(), extra.begin(), extra.end());
  return h;
}

} // namespace

SCENARIO("an ObjectArray yields the handles in [begin, end)",
    "[helium_object_array]")
{
  auto *device = new TestDevice;
  auto *state = device->state();

  Handles objs;
  for (int i = 0; i < 6; i++)
    objs.push_back(new StubObject(ANARI_SURFACE, state));
  auto *extra = new StubObject(ANARI_SURFACE, state);

  Handles appHandles = objs;
  helium::Array1DMemoryDescriptor md;
  md.appMemory = appHandles.data();
  md.elementType = ANARI_SURFACE;
  md.numItems = appHandles.size();
  auto *array = new helium::ObjectArray(state, md);
  array->commitParameters();
  array->finalize();
  REQUIRE(liveHandles(array) == objs);

  WHEN("'begin' is set and the array is then mapped and unmapped")
  {
    setRange(array, 2, 6);
    mapUnmap(array);
    THEN("the handles are [begin, end)")
    {
      CHECK(liveHandles(array) == expected(objs, 2, 6));
    }
  }

  WHEN("the range is changed without a map/unmap")
  {
    setRange(array, 2, 6);
    mapUnmap(array);
    setRange(array, 1, 4);
    THEN("the handles follow the new range")
    {
      CHECK(liveHandles(array) == expected(objs, 1, 4));
    }
    setRange(array, 3, 5);
    THEN("and again")
    {
      CHECK(liveHandles(array) == expected(objs, 3, 5));
    }
  }

  WHEN("the mapped contents change with a nonzero 'begin'")
  {
    setRange(array, 2, 6);
    auto **mapped = (helium::BaseObject **)array->map();
    mapped[3] = objs[0];
    array->unmap();
    THEN("the handles are the new contents of [begin, end)")
    {
      CHECK(liveHandles(array) == Handles{objs[2], objs[0], objs[4], objs[5]});
    }
  }

  WHEN("a handle is appended with a nonzero 'begin'")
  {
    setRange(array, 2, 6);
    array->appendHandle(extra);
    THEN("the handles are [begin, end) then the appended one")
    {
      CHECK(liveHandles(array) == expected(objs, 2, 6, {extra}));
    }
    setRange(array, 1, 3);
    THEN("a later range change keeps the appended handle after the range")
    {
      CHECK(liveHandles(array) == expected(objs, 1, 3, {extra}));
    }
    array->removeAppendedHandles();
    THEN("removing it leaves [begin, end)")
    {
      CHECK(liveHandles(array) == expected(objs, 1, 3));
    }
  }

  WHEN("the app releases the array (privatize) with a nonzero 'begin'")
  {
    setRange(array, 2, 5);
    array->refInc(helium::RefType::INTERNAL);
    device->release((ANARIObject)array);
    std::fill(appHandles.begin(), appHandles.end(), nullptr);
    THEN("the handles are the original [begin, end)")
    {
      CHECK(liveHandles(array) == expected(objs, 2, 5));
    }
    setRange(array, 0, 6);
    THEN("a later range change reads the original handles")
    {
      CHECK(liveHandles(array) == objs);
    }
    array->appendHandle(extra);
    setRange(array, 4, 6);
    THEN("appending and narrowing after privatization")
    {
      CHECK(liveHandles(array) == expected(objs, 4, 6, {extra}));
    }
    // restore the public reference the cleanup below drops
    array->refInc(helium::RefType::PUBLIC);
    array->refDec(helium::RefType::INTERNAL);
  }

  array->refDec(helium::RefType::PUBLIC);
  for (auto *o : objs)
    o->refDec(helium::RefType::PUBLIC);
  extra->refDec(helium::RefType::PUBLIC);
  state->commitBuffer.clear();
  delete device;
}
