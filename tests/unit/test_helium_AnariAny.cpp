/*
 * Copyright (c) 2019-2022 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "catch.hpp"
// helium
#include "helium/utility/AnariAny.h"
// std
#include <string>

namespace anari {
ANARI_TYPEFOR_SPECIALIZATION(helium::RefCounted *, ANARI_OBJECT);
ANARI_TYPEFOR_DEFINITION(helium::RefCounted *);
} // namespace anari

namespace {

using helium::AnariAny;

template <typename T>
inline void verify_value(const AnariAny &v, T correctValue)
{
  REQUIRE(v.valid());
  REQUIRE(v.is<T>());
  REQUIRE(v.get<T>() == correctValue);
}

template <>
inline void verify_value(const AnariAny &v, const char *correctValue)
{
  REQUIRE(v.valid());
  REQUIRE(v.type() == ANARI_STRING);
  REQUIRE(v.getString() == correctValue);
}

template <typename T>
inline void test_interface(T testValue, T testValue2)
{
  AnariAny v;
  REQUIRE(!v.valid());

  SECTION("Can make valid by C++ construction")
  {
    AnariAny v2(testValue);
    verify_value<T>(v2, testValue);
  }

  if (!std::is_same_v<bool, T>) {
    SECTION("Can make valid by C construction")
    {
      AnariAny v2(anari::ANARITypeFor<T>::value, &testValue);
      verify_value<T>(v2, testValue);
    }
  }

  SECTION("Can make valid by calling operator=()")
  {
    v = testValue;
    verify_value<T>(v, testValue);
  }

  SECTION("Can make valid by copy construction")
  {
    v = testValue;
    AnariAny v2(v);
    verify_value<T>(v2, testValue);
  }

  SECTION("Two objects with same value are equal if constructed the same")
  {
    v = testValue;
    AnariAny v2 = testValue;
    REQUIRE(v.type() == v2.type());
    REQUIRE(v == v2);
  }

  SECTION("Two objects with same value are equal if assigned from another")
  {
    v = testValue;
    AnariAny v2 = testValue2;
    v = v2;
    REQUIRE(v == v2);
  }

  SECTION("Two objects with different values are not equal")
  {
    v = testValue;
    AnariAny v2 = testValue2;
    REQUIRE(v != v2);
  }
}

template <>
inline void test_interface(const char *testValue, const char *testValue2)
{
  AnariAny v;
  REQUIRE(!v.valid());

  SECTION("Can make valid by C++ construction")
  {
    AnariAny v2(testValue);
    verify_value(v2, testValue);
  }

  SECTION("Can make valid by C construction")
  {
    AnariAny v2(ANARI_STRING, testValue);
    verify_value(v2, testValue);
  }

  SECTION("Can make valid by calling operator=()")
  {
    v = testValue;
    verify_value(v, testValue);
  }

  SECTION("Can make valid by copy construction")
  {
    v = testValue;
    AnariAny v2(v);
    verify_value(v2, testValue);
  }

  SECTION("Two objects with same value are equal if constructed the same")
  {
    v = testValue;
    AnariAny v2 = testValue;
    REQUIRE(v.type() == v2.type());
    REQUIRE(v == v2);
  }

  SECTION("Two objects with same value are equal if assigned from another")
  {
    v = testValue;
    AnariAny v2 = testValue2;
    v = v2;
    REQUIRE(v == v2);
  }

  SECTION("Two objects with different values are not equal")
  {
    v = testValue;
    AnariAny v2 = testValue2;
    REQUIRE(v != v2);
  }
}

// Value Tests ////////////////////////////////////////////////////////////////

TEST_CASE("helium::AnariAny 'int' type behavior", "[helium_AnariAny]")
{
  test_interface<int>(5, 7);
}

TEST_CASE("helium::AnariAny 'float' type behavior", "[helium_AnariAny]")
{
  test_interface<float>(1.f, 2.f);
}

TEST_CASE("helium::AnariAny 'bool' type behavior", "[helium_AnariAny]")
{
  test_interface<bool>(true, false);
}

TEST_CASE("helium::AnariAny 'string' type behavior", "[helium_AnariAny]")
{
  test_interface<const char *>("test1", "test2");
}

// Object Tests ///////////////////////////////////////////////////////////////

SCENARIO("helium::AnariAny object behavior", "[helium_AnariAny]")
{
  GIVEN("A ref counted object")
  {
    auto *obj = new helium::RefCounted();

    THEN("Object use count starts at 1")
    {
      REQUIRE(obj->useCount() == 1);
    }

    WHEN("Placing the object in AnariAny")
    {
      AnariAny v = obj;

      THEN("The ref count increments")
      {
        REQUIRE(obj->useCount() == 2);
      }

      THEN("Copying AnariAny also increments the object ref count")
      {
        AnariAny v2 = v;
        REQUIRE(obj->useCount() == 3);
      }

      THEN("Moving AnariAny keeps the object ref count the same")
      {
        AnariAny v2 = std::move(v);
        REQUIRE(obj->useCount() == 2);
      }
    }

    REQUIRE(obj->useCount() == 1);

    obj->refDec();
  }
}

} // namespace
