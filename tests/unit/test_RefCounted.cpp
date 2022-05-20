// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "catch.hpp"

#include "anari/backend/utilities/IntrusivePtr.h"

using anari::RefCounted;
using anari::RefType;
using anari::IntrusivePtr;

SCENARIO("RefCounted interface", "[RefCounted]")
{
  GIVEN("A default constructed RefCounted object")
  {
    RefCounted obj;

    THEN("The total ref count is 1")
    {
      REQUIRE(obj.useCount(RefType::ALL) == 1);
    }

    THEN("The public ref count is 1")
    {
      REQUIRE(obj.useCount(RefType::PUBLIC) == 1);
    }

    THEN("The internal ref count is 0")
    {
      REQUIRE(obj.useCount(RefType::INTERNAL) == 0);
    }

    WHEN("The public ref count is incremented")
    {
      obj.refInc(RefType::PUBLIC);

      THEN("The total ref count is 2")
      {
        REQUIRE(obj.useCount(RefType::ALL) == 2);
      }

      THEN("The public ref count is 2")
      {
        REQUIRE(obj.useCount(RefType::PUBLIC) == 2);
      }

      THEN("The internal ref count is still 0")
      {
        REQUIRE(obj.useCount(RefType::INTERNAL) == 0);
      }

      THEN("Decrementing it again restores it back to its previous state")
      {
        obj.refDec(RefType::PUBLIC);
        REQUIRE(obj.useCount(RefType::ALL) == 1);
        REQUIRE(obj.useCount(RefType::PUBLIC) == 1);
        REQUIRE(obj.useCount(RefType::INTERNAL) == 0);
      }
    }

    WHEN("The internal ref count is incremented")
    {
      obj.refInc(RefType::INTERNAL);

      THEN("The total ref count is 2")
      {
        REQUIRE(obj.useCount(RefType::ALL) == 2);
      }

      THEN("The public ref count is still 1")
      {
        REQUIRE(obj.useCount(RefType::PUBLIC) == 1);
      }

      THEN("The internal ref count is 1")
      {
        REQUIRE(obj.useCount(RefType::INTERNAL) == 1);
      }

      THEN("Decrementing it again restores it back to its previous state")
      {
        obj.refDec(RefType::INTERNAL);
        REQUIRE(obj.useCount(RefType::ALL) == 1);
        REQUIRE(obj.useCount(RefType::PUBLIC) == 1);
        REQUIRE(obj.useCount(RefType::INTERNAL) == 0);
      }
    }

    WHEN("An IntrusivePtr points to the object")
    {
      IntrusivePtr<> ptr = &obj;

      THEN("The object internal ref count is incremented")
      {
        REQUIRE(obj.useCount(RefType::ALL) == 2);
        REQUIRE(obj.useCount(RefType::PUBLIC) == 1);
        REQUIRE(obj.useCount(RefType::INTERNAL) == 1);

        WHEN("The IntrusivePtr<> is removed")
        {
          ptr = nullptr;

          THEN("The object ref count is restored to its previous state")
          {
            REQUIRE(obj.useCount(RefType::ALL) == 1);
            REQUIRE(obj.useCount(RefType::PUBLIC) == 1);
            REQUIRE(obj.useCount(RefType::INTERNAL) == 0);
          }
        }
      }
    }
  }
}
