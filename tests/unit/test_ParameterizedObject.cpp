// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "catch.hpp"

#include "anari/backend/utilities/ParameterizedObject.h"

using anari::ParameterizedObject;

struct TestObject : public ParameterizedObject
{
  void test1(const char *name)
  {
    THEN("The parameter should be stored locally")
    {
      auto *p = findParam(name);
      REQUIRE(p);
      REQUIRE(p->locallyStored);
      REQUIRE(p->type == ANARI_INT32);
    }
  }

  void test2(const char *name)
  {
    THEN("The parameter should not be stored locally")
    {
      auto *p = findParam(name);
      REQUIRE(p);
      REQUIRE(!p->locallyStored);
      REQUIRE(p->type == ANARI_UNKNOWN);
    }
  }
};

struct NonTrivialStruct
{
  NonTrivialStruct(int v = 1) : value(v) {}
  ~NonTrivialStruct() {}
  int value{0};
};

static_assert(!std::is_trivially_copyable<NonTrivialStruct>::value,
    "non-trivially copyable struct is actually trivially copyable");

SCENARIO("ParameterizedObject interface", "[ParameterizedObject]")
{
  GIVEN("A ParameterizedObject with a parameter set")
  {
    TestObject obj;

    auto name = "test_int";

    obj.setParam(name, 5);

    obj.test1(name);

    THEN("The named parameter should have the correct type and value")
    {
      REQUIRE(obj.hasParam(name));
      REQUIRE(obj.getParam<int>(name, 4) == 5);
      REQUIRE(obj.getParam<short>(name, 4) == 4);
    }

    WHEN("The parameter is removed")
    {
      obj.removeParam(name);

      THEN("The paramter should no longer exist on the object")
      {
        REQUIRE(!obj.hasParam(name));
        REQUIRE(obj.getParam<int>(name, 4) == 4);
        REQUIRE(obj.getParam<short>(name, 4) == 4);
      }
    }

    GIVEN("A ParameterizedObject with a non-trivially copyable parameter")
    {
      TestObject obj;

      auto name = "test_struct";

      obj.setParam(name, NonTrivialStruct(5));

      obj.test2(name);

      THEN("The named parameter should have the correct type and value")
      {
        REQUIRE(obj.hasParam(name));
        REQUIRE(obj.getParam<NonTrivialStruct>(name, {2}).value == 5);
        REQUIRE(obj.getParam<short>(name, 4) == 4);
      }

      WHEN("The parameter is removed")
      {
        obj.removeParam(name);

        THEN("The paramter should no longer exist on the object")
        {
          REQUIRE(!obj.hasParam(name));
          REQUIRE(obj.getParam<NonTrivialStruct>(name, {2}).value == 2);
          REQUIRE(obj.getParam<short>(name, 4) == 4);
        }
      }
    }
  }
}