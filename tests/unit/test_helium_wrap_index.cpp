// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// helium's repeat wrap mode maps any texel index, including negative ones
// (linear filtering at the lower edge asks for texel -1), into [0, size).

#include "catch.hpp"

#include "helium/helium_math.h"

#include <cstdint>

namespace {

int32_t repeat(int32_t i, size_t size)
{
  return helium::math::calculateWrapIndex(i, size, helium::WrapMode::REPEAT);
}

} // namespace

SCENARIO(
    "repeat wrap mode maps negative indices into range", "[helium_wrap_index]")
{
  GIVEN("a size that is not a power of two (3)")
  {
    THEN("indices -1, -s, -s-1, 0, s wrap to 2, 0, 2, 0, 0")
    {
      CHECK(repeat(-1, 3) == 2);
      CHECK(repeat(-3, 3) == 0);
      CHECK(repeat(-4, 3) == 2);
      CHECK(repeat(0, 3) == 0);
      CHECK(repeat(3, 3) == 0);
    }
  }

  GIVEN("a size that is a power of two (4)")
  {
    THEN("indices -1, -s, -s-1, 0, s wrap to 3, 0, 3, 0, 0")
    {
      CHECK(repeat(-1, 4) == 3);
      CHECK(repeat(-4, 4) == 0);
      CHECK(repeat(-5, 4) == 3);
      CHECK(repeat(0, 4) == 0);
      CHECK(repeat(4, 4) == 0);
    }
  }

  GIVEN("a size that is not a power of two (5)")
  {
    THEN("indices -1, -s, -s-1, 0, s wrap to 4, 0, 4, 0, 0")
    {
      CHECK(repeat(-1, 5) == 4);
      CHECK(repeat(-5, 5) == 0);
      CHECK(repeat(-6, 5) == 4);
      CHECK(repeat(0, 5) == 0);
      CHECK(repeat(5, 5) == 0);
    }
  }
}
