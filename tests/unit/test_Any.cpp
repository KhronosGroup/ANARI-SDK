// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "catch.hpp"

#include "anari/backend/utilities/Any.h"

using namespace anari;

template <typename T>
inline void verify_value(const Any &v, const T &correctValue)
{
  REQUIRE(v.valid());
  REQUIRE(v.is<T>());
  REQUIRE(v.get<T>() == correctValue);
}

template <typename T>
inline void test_interface(T testValue, T testValue2)
{
  Any v;
  REQUIRE(!v.valid());

  SECTION("Can make valid by construction")
  {
    Any v2(testValue);
    verify_value<T>(v2, testValue);
  }

  SECTION("Can make valid by calling operator=()")
  {
    v = testValue;
    verify_value<T>(v, testValue);
  }

  SECTION("Can make valid by copy construction")
  {
    v = testValue;
    Any v2(v);
    verify_value<T>(v2, testValue);
  }

  SECTION("Two objects with same value are equal if constructed the same")
  {
    v      = testValue;
    Any v2 = testValue;
    REQUIRE(v == v2);
  }

  SECTION("Two objects with same value are equal if assigned from another")
  {
    v      = testValue;
    Any v2 = testValue2;
    v      = v2;
    REQUIRE(v == v2);
  }

  SECTION("Two objects with different values are not equal")
  {
    v      = testValue;
    Any v2 = testValue2;
    REQUIRE(v != v2);
  }
}

// Tests //////////////////////////////////////////////////////////////////////

TEST_CASE("Any 'int' type behavior", "[Any]")
{
  test_interface<int>(5, 7);
}

TEST_CASE("Any 'float' type behavior", "[Any]")
{
  test_interface<float>(1.f, 2.f);
}

TEST_CASE("Any 'bool' type behavior", "[Any]")
{
  test_interface<bool>(true, false);
}

TEST_CASE("Any 'string' type behavior", "[Any]")
{
  test_interface<std::string>("Hello", "World");
}
