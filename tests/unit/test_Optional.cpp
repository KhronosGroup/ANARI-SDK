// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "catch.hpp"

#include "anari/backend/utilities/Optional.h"

using anari::Optional;

// Helper functions ///////////////////////////////////////////////////////////

template <typename T>
inline void verify_no_value(const Optional<T> &opt)
{
  // Implicitly tests 'operator bool()' and 'has_value()' functions
  REQUIRE(!opt);
  REQUIRE(!opt.has_value());
}

template <typename T>
inline void verify_value(const Optional<T> &opt, const T &value)
{
  // Implicitly tests 'operator bool()', 'has_value()', and 'operator*()'
  REQUIRE(opt);
  REQUIRE(opt.has_value());
  REQUIRE(opt.value() == value);
  REQUIRE(*opt == value);
}

// Tests //////////////////////////////////////////////////////////////////////

SCENARIO("Optional<> construction", "[Optional]")
{
  GIVEN("A default constructed Optional<>")
  {
    Optional<int> default_opt;
    THEN("The Optional<> should not have a value")
    {
      verify_no_value(default_opt);
    }

    WHEN("Used to copy construct from an Optional<> without a value")
    {
      Optional<int> copy_opt(default_opt);
      THEN("The new Optional<> should also not have a value")
      {
        verify_no_value(copy_opt);
      }
    }

    WHEN("Used to move construct from Optional<> without value")
    {
      Optional<int> copy_opt(std::move(default_opt));
      THEN("The new Optional<> should also not have a value")
      {
        verify_no_value(copy_opt);
      }
    }

    WHEN("Assigned a value with operator=()")
    {
      default_opt = 5;
      THEN("The Optional<> should take on the new value")
      {
        verify_value(default_opt, 5);
      }
    }
  }

  GIVEN("An Optional<> constructed with a value")
  {
    Optional<int> value_opt(5);

    THEN("The stored value should match the value passed on construction")
    {
      verify_value(value_opt, 5);
    }

    WHEN("Used to copy construct from an Optional<> with a value")
    {
      Optional<int> copy_opt(value_opt);
      THEN("The new Optional<> should have the same value")
      {
        verify_value(copy_opt, 5);
      }
    }

    WHEN("Used to copy construct from Optional<> with value and different type")
    {
      Optional<float> copy_opt(value_opt);
      THEN("The new Optional<> should have the right value")
      {
        verify_value(copy_opt, 5.f);
      }
    }

    WHEN("Used to move construct from Optional<> with value")
    {
      Optional<int> copy_opt(std::move(value_opt));
      THEN("The new Optional<> should have the same value")
      {
        verify_value(copy_opt, 5);
      }
    }

    WHEN("Used to move construct from Optional<> with value and different type")
    {
      Optional<float> copy_opt(std::move(value_opt));
      THEN("The new Optional<> should have the right value")
      {
        verify_value(copy_opt, 5.f);
      }
    }
  }
}

TEST_CASE("Optional<>::operator=(Optional<>)", "[Optional]")
{
  Optional<int> opt;
  opt = 5;

  Optional<int> otherOpt = opt;
  verify_value(otherOpt, 5);

  Optional<float> movedFromOpt;
  movedFromOpt = std::move(opt);
  verify_value(movedFromOpt, 5.f);
}

SCENARIO("Optional<>::value_or()", "[Optional]")
{
  Optional<int> opt;

  SECTION("Use default value in 'value_or()' if no value is present")
  {
    REQUIRE(opt.value_or(10) == 10);
  }

  SECTION("Use correct value in 'value_or()' if value is present")
  {
    opt = 5;
    REQUIRE(opt.value_or(10) == 5);
  }
}

TEST_CASE("Optional<>::reset()", "[Optional]")
{
  Optional<int> opt(5);
  opt.reset();
  verify_no_value(opt);
}

TEST_CASE("Optional<>::emplace()", "[Optional]")
{
  Optional<int> opt;
  opt.emplace(5);
  verify_value(opt, 5);
}

TEST_CASE("logical operators", "[Optional]")
{
  Optional<int> opt1(5);

  SECTION("operator==()")
  {
    Optional<int> opt2(5);
    REQUIRE(opt1 == opt2);
  }

  SECTION("operator!=()")
  {
    Optional<int> opt2(1);
    REQUIRE(opt1 != opt2);
  }

  SECTION("operator<()")
  {
    Optional<int> opt2(1);
    REQUIRE(opt2 < opt1);
  }

  SECTION("operator<=()")
  {
    Optional<int> opt2(5);
    REQUIRE(opt2 <= opt1);

    Optional<int> opt3(4);
    REQUIRE(opt3 <= opt2);
  }

  SECTION("operator>()")
  {
    Optional<int> opt2(1);
    REQUIRE(opt1 > opt2);
  }

  SECTION("operator>=()")
  {
    Optional<int> opt2(5);
    REQUIRE(opt1 >= opt2);

    Optional<int> opt3(4);
    REQUIRE(opt2 >= opt3);
  }
}

TEST_CASE("make_optional<>()", "[Optional]")
{
  Optional<int> opt1(5);
  auto opt2 = anari::make_optional<int>(5);
  REQUIRE(opt1 == opt2);
}
