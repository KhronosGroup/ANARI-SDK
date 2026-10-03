// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// runProcess() runs this test binary again as the child, choosing one of the
// hidden "child" test cases below by name.

#include "catch.hpp"
// cts
#include "cts/Isolation.h"
// std
#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>

using namespace anari::cts;
using namespace std::chrono_literals;

TEST_CASE("cts isolation child: passes", "[.]") {}

TEST_CASE("cts isolation child: fails", "[.]")
{
  FAIL("this child fails on purpose");
}

TEST_CASE("cts isolation child: hangs", "[.]")
{
  std::this_thread::sleep_for(60s);
}

TEST_CASE("cts isolation child: crashes", "[.]")
{
  std::abort();
}

namespace {

ProcessResult runChild(const std::string &name, std::chrono::milliseconds t)
{
  return runProcess(currentExecutable(), {name}, t);
}

} // namespace

TEST_CASE("runProcess reports how a child process ended", "[cts][isolation]")
{
  REQUIRE_FALSE(currentExecutable().empty());

  SECTION("a clean exit")
  {
    const auto r = runChild("cts isolation child: passes", 30s);
    CHECK(r.status == ProcessResult::Status::Exited);
    CHECK(r.exitCode == 0);
  }

  SECTION("an exit code")
  {
    const auto r = runChild("cts isolation child: fails", 30s);
    CHECK(r.status == ProcessResult::Status::Exited);
    CHECK(r.exitCode != 0);
    CHECK(r.detail.find("exited with code") != std::string::npos);
  }

  SECTION("a hang is killed at the timeout")
  {
    const auto start = std::chrono::steady_clock::now();
    const auto r = runChild("cts isolation child: hangs", 500ms);
    const auto took = std::chrono::steady_clock::now() - start;
    CHECK(r.status == ProcessResult::Status::TimedOut);
    CHECK(took < 30s);
  }

#ifndef _WIN32
  // (abort() on Windows exits with code 3, not an exception status.)
  SECTION("a crash")
  {
    const auto r = runChild("cts isolation child: crashes", 30s);
    CHECK(r.status == ProcessResult::Status::Crashed);
    CHECK(r.detail.find("signal") != std::string::npos);
  }
#endif

  SECTION("a missing executable")
  {
    const auto r = runProcess("/no/such/executable", {}, 5s);
    CHECK_FALSE((r.status == ProcessResult::Status::Exited && r.exitCode == 0));
  }
}

TEST_CASE(
    "durationText prints whole seconds or milliseconds", "[cts][isolation]")
{
  CHECK(durationText(30s) == "30 s");
  CHECK(durationText(250ms) == "250 ms");
}
