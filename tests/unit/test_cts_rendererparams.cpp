// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "catch.hpp"
// cts
#include "cts/RendererParams.h"
// std
#include <cstdint>
#include <cstring>

using namespace anari::cts;

namespace {

template <typename T>
T as(const ParsedParam &p)
{
  T v{};
  std::memcpy(&v, p.bytes.data(), sizeof(T));
  return v;
}

} // namespace

TEST_CASE("parseRendererParam splits on the first '='", "[cts]")
{
  auto p = parseRendererParam("ambientSample=4");
  REQUIRE(p);
  CHECK(p->name == "ambientSample");
  CHECK(p->value == "4");

  SECTION("value may contain '='")
  {
    auto q = parseRendererParam("expr=a=b");
    REQUIRE(q);
    CHECK(q->name == "expr");
    CHECK(q->value == "a=b");
  }

  SECTION("surrounding whitespace on the name is trimmed")
  {
    auto q = parseRendererParam("  name = value");
    REQUIRE(q);
    CHECK(q->name == "name");
    CHECK(q->value == " value");
  }

  SECTION("missing '=' or empty name is rejected")
  {
    CHECK_FALSE(parseRendererParam("noequals"));
    CHECK_FALSE(parseRendererParam("=value"));
  }
}

TEST_CASE("inferParamType picks the narrowest matching type", "[cts]")
{
  CHECK(inferParamType("true") == ANARI_BOOL);
  CHECK(inferParamType("false") == ANARI_BOOL);
  CHECK(inferParamType("4") == ANARI_INT32);
  CHECK(inferParamType("-7") == ANARI_INT32);
  CHECK(inferParamType("1.5") == ANARI_FLOAT32);
  CHECK(inferParamType("1,2") == ANARI_FLOAT32_VEC2);
  CHECK(inferParamType("1,2,3") == ANARI_FLOAT32_VEC3);
  CHECK(inferParamType("1,2,3,4") == ANARI_FLOAT32_VEC4);
  CHECK(inferParamType("red") == ANARI_STRING);
  CHECK(inferParamType("1,2,3,4,5") == ANARI_STRING); // no 5-vector
  CHECK(inferParamType("a,b") == ANARI_STRING); // non-numeric list
}

TEST_CASE("parseParamValue honors the declared type", "[cts]")
{
  SECTION("int declared type parses an integer value")
  {
    auto p = parseParamValue(ANARI_INT32, "4");
    REQUIRE(p);
    CHECK(p->type == ANARI_INT32);
    CHECK(as<int32_t>(*p) == 4);
  }

  SECTION("float declared type accepts an integer-looking value")
  {
    auto p = parseParamValue(ANARI_FLOAT32, "4");
    REQUIRE(p);
    CHECK(p->type == ANARI_FLOAT32);
    CHECK(as<float>(*p) == 4.f);
  }

  SECTION("bool accepts true/false and 1/0")
  {
    CHECK(as<int8_t>(*parseParamValue(ANARI_BOOL, "true")) == 1);
    CHECK(as<int8_t>(*parseParamValue(ANARI_BOOL, "0")) == 0);
    CHECK_FALSE(parseParamValue(ANARI_BOOL, "yes"));
  }

  SECTION("uint rejects negatives")
  {
    CHECK(parseParamValue(ANARI_UINT32, "5"));
    CHECK_FALSE(parseParamValue(ANARI_UINT32, "-1"));
  }

  SECTION("float vector requires the exact component count")
  {
    auto p = parseParamValue(ANARI_FLOAT32_VEC3, "1,2,3");
    REQUIRE(p);
    CHECK(as<float>(*p) == 1.f);
    CHECK_FALSE(parseParamValue(ANARI_FLOAT32_VEC3, "1,2"));
  }

  SECTION("string is NUL-terminated")
  {
    auto p = parseParamValue(ANARI_STRING, "sobol");
    REQUIRE(p);
    CHECK(std::strcmp(reinterpret_cast<const char *>(p->bytes.data()), "sobol")
        == 0);
  }
}

TEST_CASE("parseParamValue infers when type is unknown", "[cts]")
{
  auto p = parseParamValue(ANARI_UNKNOWN, "4");
  REQUIRE(p);
  CHECK(p->type == ANARI_INT32);

  CHECK_FALSE(parseParamValue(ANARI_INT32, "1.5")); // declared int, float text
  CHECK_FALSE(parseParamValue(ANARI_FLOAT32, "abc"));
}
