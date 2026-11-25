// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Frame completion callback checks of ptcSmoke (--callback, --reenter).

// ptc
#include "SmokeHarness.h"
// std
#include <cstdio>

namespace ptcsmoke {

CallbackContext g_callback;

namespace {

// Called by ptc once the composited frame is ready. The handles must be the
// ones the app holds (not the wrapped device's), and on rank 0 the composited
// color must already be there: mapping it is legal inside the callback.
// --reenter, from inside the callback: rendering or committing the frame is
// refused with an ERROR, and does not call the callback again.
int checkReentryRefused(ANARIDevice d, ANARIFrame f, int rank)
{
  Checker c{rank};
  int before = g_errors;
  anariRenderFrame(d, f);
  c.expect(g_errors == before + 1,
      "anariRenderFrame from inside the frame completion callback was not "
      "refused with one ERROR");
  before = g_errors;
  anariCommitParameters(d, f);
  c.expect(g_errors == before + 1,
      "anariCommitParameters from inside the frame completion callback was "
      "not refused with one ERROR");
  if (!c.failures) {
    printf(
        "PASS: rank %d: render and commit from inside the callback "
        "refused\n",
        rank);
  }
  return c.failures;
}

} // namespace

void onFrameComplete(const void *userData, ANARIDevice d, ANARIFrame f)
{
  if (!userData && g_callback.expectNullUserData) {
    g_callback.calls++;
    g_callback.nullUserDataCalls++;
    return;
  }
  if (userData != &g_callback) {
    printf("FAIL: frame completion callback got the wrong user data\n");
    g_callback.calls++;
    g_callback.failures++;
    return;
  }
  // from here on, everything is reached through the app's user data
  auto &ctx = *(CallbackContext *)userData;
  ctx.calls++;
  Checker c{ctx.rank};
  if (ctx.depth > 0) {
    c.expect(false, "frame completion callback called from inside itself");
    ctx.failures += c.failures;
    return;
  }
  ctx.depth++;
  if (ctx.finalFrame && ctx.rendering && ctx.renderFails) {
    c.expect(g_errors > ctx.errorsBeforeRender,
        "frame completion callback called before the render's ERROR was "
        "reported");
  }
  c.expect(d == ctx.device,
      "frame completion callback got a device handle that is not the ptc "
      "device");
  c.expect(f == ctx.frame,
      "frame completion callback got a frame handle that is not the ptc "
      "frame");
  if (d == ctx.device && f == ctx.frame && ctx.rendering
      && ctx.rendering->layout->rank == 0) {
    printf("callback %d: checking composited color\n", ctx.calls);
    if (!ctx.finalFrame)
      c.failures += checkFirstFrame(ctx.device, ctx.frame, *ctx.rendering);
    else if (ctx.renderFails)
      c.failures +=
          checkColorRefused(ctx.device, ctx.frame, "inside the callback", true);
    else
      c.failures += checkColor(ctx.device, ctx.frame, *ctx.opts, *ctx.layout);
  }
  if (ctx.opts->reenter && !ctx.reentered) {
    ctx.reentered = true;
    c.failures += checkReentryRefused(d, f, ctx.rank);
  }
  ctx.depth--;
  ctx.failures += c.failures;
}

// Every rank, with --callback: one call per render so far; then unset the
// callback, render again, and expect no further call.
int checkCallbackCalls(
    anari::Device d, anari::Frame frame, int rank, int renders)
{
  Checker c{rank};
  c.expect(g_callback.calls == renders,
      "frame completion callback called " + std::to_string(g_callback.calls)
          + " times for " + std::to_string(renders) + " renders");
  c.expect(!g_callback.opts->reenter || g_callback.reentered,
      "no re-entrant render or commit was tried from the callback");
  g_callback.rendering = nullptr;

  // only the user data unset: the callback still runs, with null user data
  g_callback.expectNullUserData = true;
  anari::unsetParameter(d, frame, "frameCompletionCallbackUserData");
  commitFrame(d, frame);
  int before = g_callback.calls;
  renderAndWait(d, frame);
  c.expect(g_callback.calls == before + 1 && g_callback.nullUserDataCalls == 1,
      "frame completion callback not called once with null user data after "
      "unsetting only frameCompletionCallbackUserData");
  const int nullCalls = g_callback.calls - before;

  anari::unsetParameter(d, frame, "frameCompletionCallback");
  commitFrame(d, frame);
  before = g_callback.calls;
  renderAndWait(d, frame);
  c.expect(g_callback.calls == before,
      "frame completion callback called after it was unset");

  if (rank == 0) {
    printf(
        "%s: frame completion callback called %d times for %d renders, "
        "%d times with null user data, then %d times after unset\n",
        c.failures ? "FAIL" : "PASS",
        renders,
        renders,
        nullCalls,
        g_callback.calls - before);
  }
  return c.failures + g_callback.failures;
}

} // namespace ptcsmoke
