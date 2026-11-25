// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// helide_gpu fits its cameras' near and far clip planes to what the frame
// draws (fitClipRange()), so the depth buffer's precision isn't spread over
// empty space. fitClipRange() takes the drawn bounds' range of distances along
// the view direction, so this needs neither glm nor a GPU.

#include "catch.hpp"

#include "camera/ClipRange.h"

#include <limits>

using helide_gpu::ClipRange;
using helide_gpu::fitClipRange;

namespace {

constexpr float EMPTY_MIN = std::numeric_limits<float>::max();
constexpr float EMPTY_MAX = -std::numeric_limits<float>::max();
const std::optional<float> UNSET;

} // namespace

TEST_CASE("perspective fits the drawn range", "[helide_gpu_clip_range]")
{
  const ClipRange r = fitClipRange(10.f, 20.f, true, UNSET, UNSET);
  CHECK(r.zNear < 10.f);
  CHECK(r.zNear > 9.9f);
  CHECK(r.zFar > 20.f);
  CHECK(r.zFar < 20.1f);
}

TEST_CASE(
    "perspective near is kept at least far / 1000", "[helide_gpu_clip_range]")
{
  // the camera inside the bounds
  const ClipRange r = fitClipRange(-50.f, 100.f, true, UNSET, UNSET);
  CHECK(r.zFar > 100.f);
  CHECK(r.zNear == Approx(r.zFar * 1e-3f));
}

TEST_CASE("orthographic near can reach the camera", "[helide_gpu_clip_range]")
{
  const ClipRange inside = fitClipRange(-50.f, 100.f, false, UNSET, UNSET);
  CHECK(inside.zNear == 0.f);
  CHECK(inside.zFar > 100.f);

  // a flat scene facing the camera still gets a non-empty range
  const ClipRange flat = fitClipRange(5.f, 5.f, false, UNSET, UNSET);
  CHECK(flat.zNear < 5.f);
  CHECK(flat.zFar > 5.f);
}

TEST_CASE("the application's near and far narrow the fitted range",
    "[helide_gpu_clip_range]")
{
  const ClipRange r = fitClipRange(10.f, 20.f, true, 12.f, 15.f);
  CHECK(r.zNear == 12.f);
  CHECK(r.zFar == 15.f);

  // an application near lifts the far / 1000 floor
  const ClipRange close = fitClipRange(-1.f, 1000.f, true, 0.01f, UNSET);
  CHECK(close.zNear == 0.01f);

  // ...but the range never grows past the drawn bounds
  const ClipRange wide = fitClipRange(10.f, 20.f, true, 1.f, 1000.f);
  CHECK(wide.zNear > 9.9f);
  CHECK(wide.zFar < 20.1f);
}

TEST_CASE("nothing drawn in front of the camera falls back to a valid range",
    "[helide_gpu_clip_range]")
{
  for (bool perspective : {true, false}) {
    // empty bounds
    const ClipRange empty =
        fitClipRange(EMPTY_MIN, EMPTY_MAX, perspective, UNSET, UNSET);
    CHECK(empty.zFar > empty.zNear);
    CHECK(empty.zNear >= 0.f);

    // bounds behind the camera
    const ClipRange behind =
        fitClipRange(-20.f, -10.f, perspective, UNSET, UNSET);
    CHECK(behind.zFar > behind.zNear);
    CHECK(behind.zNear >= 0.f);
  }

  // bounds beyond the application's range: that range itself
  const ClipRange beyond = fitClipRange(100.f, 200.f, true, 1.f, 10.f);
  CHECK(beyond.zNear == 1.f);
  CHECK(beyond.zFar == 10.f);

  // only a far: a perspective near of far / 1000
  const ClipRange farOnly = fitClipRange(100.f, 200.f, true, UNSET, 10.f);
  CHECK(farOnly.zNear == Approx(0.01f));
  CHECK(farOnly.zFar == 10.f);
}
