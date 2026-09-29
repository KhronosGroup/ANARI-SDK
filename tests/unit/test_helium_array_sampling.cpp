// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// helium::Array::valueAtLinear() samples a 1D array at a normalized coordinate
// in [0, 1]. At in == 1 it must read the last element only, not the one past
// it (transfer functions sample there at the top of their value range). Each
// array wraps an exactly sized heap buffer so ASan flags any over-read.

#include "catch.hpp"

#include "helium/array/Array1D.h"

#include <utility>
#include <vector>

namespace {

struct SharedArray
{
  SharedArray(helium::BaseGlobalDeviceState &state, std::vector<float> values)
      : data(std::move(values))
  {
    helium::Array1DMemoryDescriptor md;
    md.appMemory = data.data();
    md.elementType = ANARI_FLOAT32;
    md.numItems = data.size();
    array = new helium::Array1D(&state, md);
  }

  SharedArray(const SharedArray &) = delete;
  SharedArray &operator=(const SharedArray &) = delete;

  ~SharedArray()
  {
    array->refDec(helium::RefType::PUBLIC);
  }

  std::vector<float> data;
  helium::Array1D *array{nullptr};
};

} // namespace

SCENARIO("valueAtLinear samples [0, 1] without reading past the end",
    "[helium_array_sampling]")
{
  helium::BaseGlobalDeviceState state(nullptr);

  GIVEN("an array of size 1")
  {
    SharedArray a(state, {7.f});
    THEN("every coordinate returns the only element")
    {
      CHECK(a.array->valueAtLinear<float>(0.f) == Approx(7.f));
      CHECK(a.array->valueAtLinear<float>(0.5f) == Approx(7.f));
      CHECK(a.array->valueAtLinear<float>(1.f) == Approx(7.f));
    }
  }

  GIVEN("an array of size 2")
  {
    SharedArray a(state, {10.f, 20.f});
    THEN("0, 0.5 and 1 return the first, the midpoint and the last")
    {
      CHECK(a.array->valueAtLinear<float>(0.f) == Approx(10.f));
      CHECK(a.array->valueAtLinear<float>(0.5f) == Approx(15.f));
      CHECK(a.array->valueAtLinear<float>(1.f) == Approx(20.f));
    }
  }

  GIVEN("an array of size 3")
  {
    SharedArray a(state, {0.f, 1.f, 4.f});
    THEN("0, 0.5 and 1 return the first, the middle and the last")
    {
      CHECK(a.array->valueAtLinear<float>(0.f) == Approx(0.f));
      CHECK(a.array->valueAtLinear<float>(0.5f) == Approx(1.f));
      CHECK(a.array->valueAtLinear<float>(1.f) == Approx(4.f));
    }
  }
}
