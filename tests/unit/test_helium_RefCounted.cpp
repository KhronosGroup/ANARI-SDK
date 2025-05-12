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

#include "helium/BaseObject.h"
#include "helium/utility/AnariAny.h"
#include "helium/utility/IntrusivePtr.h"

struct TestObject : public helium::RefCounted
{
  bool on_NoPublicReferences_triggered{false};
  bool on_NoInternalReferences_triggered{false};

 private:
  void on_NoPublicReferences() override
  {
    on_NoPublicReferences_triggered = true;
  }
  void on_NoInternalReferences() override
  {
    on_NoInternalReferences_triggered = true;
  }
};

// NOTE(jda) -- This specialization is *only* for this test so AnariAny can be
//              used.
HELIUM_ANARI_TYPEFOR_SPECIALIZATION(TestObject *, ANARI_OBJECT);
HELIUM_ANARI_TYPEFOR_DEFINITION(TestObject *);

namespace {

using helium::AnariAny;
using helium::BaseObject;
using helium::IntrusivePtr;
using helium::RefCounted;
using helium::RefType;

SCENARIO("helium::RefCounted interface", "[helium_RefCounted]")
{
  GIVEN("A default constructed RefCounted object")
  {
    auto *obj = new TestObject();

    THEN("The total ref count is initially 1")
    {
      REQUIRE(obj->useCount(RefType::ALL) == 1);
    }

    THEN("The public ref count is initially 1")
    {
      REQUIRE(obj->useCount(RefType::PUBLIC) == 1);
    }

    THEN("The internal ref count is initially 0")
    {
      REQUIRE(obj->useCount(RefType::INTERNAL) == 0);
    }

    WHEN("The public ref count is incremented")
    {
      obj->refInc(RefType::PUBLIC);

      THEN("The ref callbacks have not triggered")
      {
        REQUIRE(obj->on_NoPublicReferences_triggered == false);
        REQUIRE(obj->on_NoInternalReferences_triggered == false);
      }

      THEN("The total ref count is now 2")
      {
        REQUIRE(obj->useCount(RefType::ALL) == 2);
      }

      THEN("The public ref count is now 2")
      {
        REQUIRE(obj->useCount(RefType::PUBLIC) == 2);
      }

      THEN("The internal ref count is still 0")
      {
        REQUIRE(obj->useCount(RefType::INTERNAL) == 0);
      }

      WHEN("The public ref is decremented again")
      {
        obj->refDec(RefType::PUBLIC);

        THEN("It is restored back to its previous state")
        {
          REQUIRE(obj->useCount(RefType::ALL) == 1);
          REQUIRE(obj->useCount(RefType::PUBLIC) == 1);
          REQUIRE(obj->useCount(RefType::INTERNAL) == 0);
        }

        THEN("The ref callbacks still have not triggered")
        {
          REQUIRE(obj->on_NoPublicReferences_triggered == false);
          REQUIRE(obj->on_NoInternalReferences_triggered == false);
        }
      }
    }

    WHEN("The internal ref count is incremented")
    {
      obj->refInc(RefType::INTERNAL);

      THEN("The ref callbacks have not triggered")
      {
        REQUIRE(obj->on_NoPublicReferences_triggered == false);
        REQUIRE(obj->on_NoInternalReferences_triggered == false);
      }

      THEN("The total ref count is now 2")
      {
        REQUIRE(obj->useCount(RefType::ALL) == 2);
      }

      THEN("The public ref count is still 1")
      {
        REQUIRE(obj->useCount(RefType::PUBLIC) == 1);
      }

      THEN("The internal ref count is now 1")
      {
        REQUIRE(obj->useCount(RefType::INTERNAL) == 1);
      }

      WHEN("The internal ref is decremented again")
      {
        obj->refDec(RefType::INTERNAL);
        THEN("Decrementing it again restores it back to its previous state")
        {
          REQUIRE(obj->useCount(RefType::ALL) == 1);
          REQUIRE(obj->useCount(RefType::PUBLIC) == 1);
          REQUIRE(obj->useCount(RefType::INTERNAL) == 0);
        }

        THEN("Then only on_NoInternalReferences() should be triggered")
        {
          REQUIRE(obj->on_NoPublicReferences_triggered == false);
          REQUIRE(obj->on_NoInternalReferences_triggered == true);
        }
      }
    }

    WHEN("The the object is placed inside an AnariAny")
    {
      AnariAny any = obj;

      THEN("The ref callbacks have not triggered")
      {
        REQUIRE(obj->on_NoPublicReferences_triggered == false);
        REQUIRE(obj->on_NoInternalReferences_triggered == false);
      }

      THEN("The total ref count is now 2")
      {
        REQUIRE(obj->useCount(RefType::ALL) == 2);
      }

      THEN("The public ref count is still 1")
      {
        REQUIRE(obj->useCount(RefType::PUBLIC) == 1);
      }

      THEN("The internal ref count is still 0")
      {
        REQUIRE(obj->useCount(RefType::INTERNAL) == 1);
      }

      WHEN("The AnariAny is cleared")
      {
        any.reset();
        THEN("The ref count is restored it back to its previous state")
        {
          REQUIRE(obj->useCount(RefType::ALL) == 1);
          REQUIRE(obj->useCount(RefType::PUBLIC) == 1);
          REQUIRE(obj->useCount(RefType::INTERNAL) == 0);
        }

        THEN("Then only on_NoInternalReferences() should be triggered")
        {
          REQUIRE(obj->on_NoPublicReferences_triggered == false);
          REQUIRE(obj->on_NoInternalReferences_triggered == true);
        }
      }
    }

    WHEN("An IntrusivePtr points to the object")
    {
      IntrusivePtr ptr = obj;

      THEN("The ref callbacks have not triggered")
      {
        REQUIRE(obj->on_NoPublicReferences_triggered == false);
        REQUIRE(obj->on_NoInternalReferences_triggered == false);
      }

      THEN("The object internal ref count is incremented")
      {
        REQUIRE(obj->useCount(RefType::ALL) == 2);
        REQUIRE(obj->useCount(RefType::PUBLIC) == 1);
        REQUIRE(obj->useCount(RefType::INTERNAL) == 1);

        WHEN("The IntrusivePtr<> is removed")
        {
          ptr = nullptr;

          THEN("The object ref count is restored to its previous state")
          {
            REQUIRE(obj->useCount(RefType::ALL) == 1);
            REQUIRE(obj->useCount(RefType::PUBLIC) == 1);
            REQUIRE(obj->useCount(RefType::INTERNAL) == 0);
          }

          THEN("Then only on_NoInternalReferences() should be triggered")
          {
            REQUIRE(obj->on_NoPublicReferences_triggered == false);
            REQUIRE(obj->on_NoInternalReferences_triggered == true);
          }
        }
      }
    }

    WHEN("The object is changed to be an internal-only reference")
    {
      obj->refInc(RefType::INTERNAL);
      obj->refDec(RefType::PUBLIC);

      THEN("Then only on_NoPublicReferences() should be triggered")
      {
        REQUIRE(obj->on_NoPublicReferences_triggered == true);
        REQUIRE(obj->on_NoInternalReferences_triggered == false);
      }

      obj->refInc(RefType::PUBLIC);
      obj->refDec(RefType::INTERNAL);
    }

    obj->refDec(RefType::PUBLIC);
  }
}

} // namespace
