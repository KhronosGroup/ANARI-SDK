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

#include "helium/utility/ParameterizedObject.h"

namespace {

SCENARIO(
    "helium::ParameterizedObject interface", "[helium_ParameterizedObject]")
{
  GIVEN("A ParameterizedObject with a parameter set")
  {
    helium::ParameterizedObject obj;

    auto name = "test_int";

    THEN("The parameter should not be stored")
    {
      REQUIRE(!obj.hasParam(name));
    }

    int v = 5;
    obj.setParam(name, ANARI_INT32, &v);

    THEN("The named parameter should have the correct type and value")
    {
      REQUIRE(obj.hasParam(name));
      REQUIRE(obj.hasParam(name, ANARI_INT32));
      REQUIRE(obj.getParam<int>(name, 4) == v);
      REQUIRE(obj.getParam<short>(name, 4) == 4);

      int v2 = 0;
      bool success = obj.getParam(name, ANARI_INT32, &v2);
      REQUIRE(success == true);
      REQUIRE(v2 == v);
    }

    WHEN("The parameter is removed")
    {
      obj.removeParam(name);

      THEN("The paramter should no longer exist on the object")
      {
        REQUIRE(!obj.hasParam(name));
        REQUIRE(!obj.hasParam(name, ANARI_INT32));
        REQUIRE(obj.getParam<int>(name, 4) == 4);
        REQUIRE(obj.getParam<short>(name, 4) == 4);

        int v2 = 0;
        bool success = obj.getParam(name, ANARI_INT32, &v2);
        REQUIRE(success == false);
        REQUIRE(v2 == 0);
      }
    }
  }

  GIVEN("A ParameterizedObject with a string parameter")
  {
    helium::ParameterizedObject obj;

    auto name = "test_string";

    THEN("The parameter should not be stored")
    {
      REQUIRE(!obj.hasParam(name));
    }

    const char *testStr = "test";
    obj.setParam(name, ANARI_STRING, testStr);

    THEN("The named parameter should have the correct type and value")
    {
      REQUIRE(obj.hasParam(name));
      REQUIRE(obj.hasParam(name, ANARI_STRING));
      REQUIRE(obj.getParamString(name, "") == testStr);
      REQUIRE(obj.getParam<short>(name, 4) == 4);
    }

    WHEN("The parameter is removed")
    {
      obj.removeParam(name);

      THEN("The paramter should no longer exist on the object")
      {
        REQUIRE(!obj.hasParam(name));
        REQUIRE(!obj.hasParam(name, ANARI_STRING));
        REQUIRE(obj.getParamString(name, "") == "");
        REQUIRE(obj.getParam<short>(name, 4) == 4);
      }
    }
  }
}

} // namespace
