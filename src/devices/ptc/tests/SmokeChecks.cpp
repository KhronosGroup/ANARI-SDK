// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Pixel checks of ptcSmoke: what rank 0 of a frame's communicator finds in the
// composited channels.

// ptc
#include "SmokeHarness.h"
// std
#include <cmath>
#include <cstdio>
#include <cstring>

namespace ptcsmoke {

namespace {

// IEC 61966-2-1 (the curve helide uses for UFIXED8_RGBA_SRGB)
float linearToSrgb(float v)
{
  return v <= 0.0031308f ? 12.92f * v
                         : 1.055f * std::pow(v, 1.f / 2.4f) - 0.055f;
}

int toUnorm8(float v)
{
  return int(std::fmin(std::fmax(v, 0.f), 1.f) * 255.f + 0.5f);
}

// What ptc must composite where every rank of the layout misses: the ranks'
// backgrounds front to back in rank order, with premultiplied "over"
float4 expectedBackground(Background bg, const Layout &l)
{
  float4 result(0.f);
  for (int r = 0; r < l.size; r++) {
    const float4 b = background(bg, l.worldRanks[r]);
    result = result + (1.f - result.w) * b;
  }
  return result;
}

} // namespace

// Rank 0 only: the frame has no channel.color, or its last render failed
// ('failed'), so mapping color returns nothing and reports why through the
// status callback; after a failure, the warning must say so.
int checkColorRefused(
    anari::Device d, anari::Frame frame, const char *which, bool failed)
{
  const int warningsBefore = g_colorWarnings;
  auto fb = anari::map<void>(d, frame, "channel.color");
  const bool warned = g_colorWarnings > warningsBefore
      && (!failed || g_lastWarning.find("failed") != std::string::npos);
  const bool ok = !fb.data && fb.width == 0 && fb.height == 0
      && fb.pixelType == ANARI_UNKNOWN && warned;
  printf(
      "%s: %s: %s color map refused (data=%p %ux%u type=%d, "
      "warning %s)\n",
      ok ? "PASS" : "FAIL",
      which,
      failed ? "failed" : "unset",
      fb.data,
      fb.width,
      fb.height,
      int(fb.pixelType),
      warned ? "reported" : "missing or wrong");
  anari::unmap(d, frame, "channel.color");
  return !ok;
}

// Rank 0 only, first of two frames: the color map has that frame's size and
// type (or is refused if it has no color).
int checkFirstFrame(
    anari::Device d, anari::Frame frame, const FrameState &state)
{
  if (state.color == ANARI_UNKNOWN)
    return checkColorRefused(d, frame, "first frame");
  auto fb = anari::map<void>(d, frame, "channel.color");
  const bool ok = fb.data && fb.width == state.size.x
      && fb.height == state.size.y && fb.pixelType == state.color;
  printf(
      "%s: first frame color map (data=%p %ux%u type=%d, want %ux%u "
      "type=%d)\n",
      ok ? "PASS" : "FAIL",
      fb.data,
      fb.width,
      fb.height,
      int(fb.pixelType),
      state.size.x,
      state.size.y,
      int(state.color));
  anari::unmap(d, frame, "channel.color");
  return !ok;
}

// Rank 0 of the layout only: probe the pixel at the center of each rank's
// sphere.
int checkColor(
    anari::Device d, anari::Frame frame, const Options &opts, const Layout &l)
{
  const int size = l.size;
  if (!compositable(opts.colorType))
    return checkColorRefused(d, frame, "final frame", true);
  if (opts.colorType == ANARI_UNKNOWN)
    return checkColorRefused(d, frame, "final frame");
  int failures = 0;
  auto fb = anari::map<void>(d, frame, "channel.color");
  if (!fb.data || fb.width != IMAGE_SIZE.x || fb.height != IMAGE_SIZE.y
      || fb.pixelType != opts.colorType) {
    printf("FAIL: bad color map (data=%p %ux%u type=%d)\n",
        fb.data,
        fb.width,
        fb.height,
        int(fb.pixelType));
    failures++;
  } else {
    for (int r = 0; r < size; r++) {
      const int px = pixelX(sphereX(r, size));
      const int py = IMAGE_SIZE.y / 2;
      const size_t idx = size_t(py) * IMAGE_SIZE.x + px;
      float c[4];
      if (opts.colorType == ANARI_FLOAT32_VEC4) {
        memcpy(c, (const float *)fb.data + 4 * idx, sizeof(c));
      } else {
        uint32_t v = ((const uint32_t *)fb.data)[idx];
        for (int i = 0; i < 4; i++)
          c[i] = ((v >> (8 * i)) & 0xff) / 255.f;
      }
      float3 want = sphereColor(l, r);
      if (opts.invertCheck)
        want = float3(1.f) - want;
      bool ok = c[3] > 0.9f;
      for (int i = 0; i < 3; i++)
        ok &= (want[i] > 0.5f) == (c[i] > 0.1f);
      printf("%s: rank %d sphere @ px %d -> (%.2f %.2f %.2f %.2f)\n",
          ok ? "PASS" : "FAIL",
          r,
          px,
          c[0],
          c[1],
          c[2],
          c[3]);
      failures += !ok;
    }
  }
  anari::unmap(d, frame, "channel.color");
  return failures;
}

// Rank 0 of the layout only, with --background: the corner pixel, a miss on
// every rank, is the ranks' backgrounds blended in linear space. 8-bit
// results may be off by 1 LSB from rounding; float ones are held to the same
// 1/255.
int checkBackground(
    anari::Device d, anari::Frame frame, const Options &opts, const Layout &l)
{
  if (opts.background == Background::None || opts.colorType == ANARI_UNKNOWN
      || !compositable(opts.colorType))
    return 0;
  const float4 want = expectedBackground(opts.background, l);
  auto fb = anari::map<void>(d, frame, "channel.color");
  int failures = 0;
  if (!fb.data || fb.pixelType != opts.colorType)
    failures++; // reported by checkColor
  else if (opts.colorType == ANARI_FLOAT32_VEC4) {
    float c[4];
    memcpy(c, fb.data, sizeof(c));
    bool ok = true;
    for (int i = 0; i < 4; i++)
      ok &= std::fabs(c[i] - want[i]) <= 1.f / 255.f;
    printf(
        "%s: background (%.4f %.4f %.4f %.4f), want "
        "(%.4f %.4f %.4f %.4f)\n",
        ok ? "PASS" : "FAIL",
        c[0],
        c[1],
        c[2],
        c[3],
        want.x,
        want.y,
        want.z,
        want.w);
    failures += !ok;
  } else {
    const bool srgb = opts.colorType == ANARI_UFIXED8_RGBA_SRGB;
    const uint32_t v = *(const uint32_t *)fb.data;
    int got[4], expect[4];
    bool ok = true;
    for (int i = 0; i < 4; i++) {
      got[i] = (v >> (8 * i)) & 0xff;
      expect[i] = toUnorm8(srgb && i < 3 ? linearToSrgb(want[i]) : want[i]);
      ok &= std::abs(got[i] - expect[i]) <= 1;
    }
    printf("%s: background (%d %d %d %d), want (%d %d %d %d)%s\n",
        ok ? "PASS" : "FAIL",
        got[0],
        got[1],
        got[2],
        got[3],
        expect[0],
        expect[1],
        expect[2],
        expect[3],
        srgb ? " sRGB" : "");
    failures += !ok;
  }
  anari::unmap(d, frame, "channel.color");
  return failures;
}

// Rank 0 of the layout only, with --depth: the composited depth is the
// nearest hit across all ranks. Each rank's sphere center must show that
// sphere's depth: other ranks either miss there (their +inf must not win) or,
// behind the last rank's sphere, hit farther away. The background corner,
// where no rank hits, must be +inf.
int checkDepth(anari::Device d, anari::Frame frame, int size)
{
  int failures = 0;
  auto db = anari::map<float>(d, frame, "channel.depth");
  if (!db.data || db.width != IMAGE_SIZE.x || db.height != IMAGE_SIZE.y
      || db.pixelType != ANARI_FLOAT32) {
    printf("FAIL: bad depth map (data=%p %ux%u type=%d)\n",
        (const void *)db.data,
        db.width,
        db.height,
        int(db.pixelType));
    failures++;
  } else {
    const float want = CAMERA_Z - SPHERE_RADIUS;
    for (int r = 0; r < size; r++) {
      const int px = pixelX(sphereX(r, size));
      const float hit = db.data[(IMAGE_SIZE.y / 2) * IMAGE_SIZE.x + px];
      const bool ok = std::fabs(hit - want) < 0.05f;
      printf("%s: rank %d sphere depth @ px %d = %f (want %f)\n",
          ok ? "PASS" : "FAIL",
          r,
          px,
          hit,
          want);
      failures += !ok;
    }
    const float corner = db.data[0];
    const bool ok = std::isinf(corner) && corner > 0.f;
    printf(
        "%s: background depth = %g (want inf)\n", ok ? "PASS" : "FAIL", corner);
    failures += !ok;
  }
  anari::unmap(d, frame, "channel.depth");
  return failures;
}

// Rank 0 only, without --depth: the app never enabled channel.depth, so
// mapping it returns nothing and reports why through the status callback.
int checkDepthRefused(anari::Device d, anari::Frame frame)
{
  const int warningsBefore = g_depthWarnings;
  auto db = anari::map<float>(d, frame, "channel.depth");
  const bool ok = !db.data && db.width == 0 && db.height == 0
      && db.pixelType == ANARI_UNKNOWN && g_depthWarnings > warningsBefore;
  printf(
      "%s: unrequested depth map refused (data=%p %ux%u type=%d, "
      "warning %s)\n",
      ok ? "PASS" : "FAIL",
      (const void *)db.data,
      db.width,
      db.height,
      int(db.pixelType),
      g_depthWarnings > warningsBefore ? "reported" : "missing");
  anari::unmap(d, frame, "channel.depth");
  return !ok;
}

// Rank 0 only: mapping `channel` returns nothing and reports a warning that
// names the channel through the status callback.
int checkChannelRefused(
    anari::Device d, anari::Frame frame, const char *channel, const char *why)
{
  g_lastWarning.clear();
  auto fb = anari::map<void>(d, frame, channel);
  const bool warned = g_lastWarning.find(channel) != std::string::npos;
  const bool ok = !fb.data && fb.width == 0 && fb.height == 0
      && fb.pixelType == ANARI_UNKNOWN && warned;
  printf("%s: %s %s map refused (data=%p %ux%u type=%d, warning %s)\n",
      ok ? "PASS" : "FAIL",
      why,
      channel,
      fb.data,
      fb.width,
      fb.height,
      int(fb.pixelType),
      warned ? "reported" : "missing");
  anari::unmap(d, frame, channel);
  return !ok;
}

// Rank 0 of the layout only, with --ids: each pixel's ids are the nearest
// fragment's. Each rank's sphere center is primitive 0 of that rank's surface:
// other ranks either miss there or, behind the last rank's sphere, hit their
// primitive 1 farther away. Where no rank hits, the ids are ~0u.
int checkIds(anari::Device d, anari::Frame frame, const Layout &l)
{
  int failures = 0;
  for (const char *channel : ID_CHANNELS) {
    const std::string name = channel;
    auto fb = anari::map<uint32_t>(d, frame, channel);
    if (!fb.data || fb.width != IMAGE_SIZE.x || fb.height != IMAGE_SIZE.y
        || fb.pixelType != ANARI_UINT32) {
      printf("FAIL: bad %s map (data=%p %ux%u type=%d)\n",
          channel,
          (const void *)fb.data,
          fb.width,
          fb.height,
          int(fb.pixelType));
      failures++;
      anari::unmap(d, frame, channel);
      continue;
    }
    // helide gives surfaces outside an instance no instance id
    if (name != "channel.instanceId") {
      for (int r = 0; r < l.size; r++) {
        const int px = pixelX(sphereX(r, l.size));
        const uint32_t got = fb.data[(IMAGE_SIZE.y / 2) * IMAGE_SIZE.x + px];
        const uint32_t want =
            name == "channel.primitiveId" ? 0u : surfaceId(l.worldRanks[r]);
        const bool ok = got == want;
        printf("%s: rank %d sphere %s @ px %d = %u (want %u)\n",
            ok ? "PASS" : "FAIL",
            r,
            channel,
            px,
            got,
            want);
        failures += !ok;
      }
    }
    if (name != "channel.instanceId") {
      // equal depth on every rank: the lowest rank is in front
      const int px = pixelX(TIE_X);
      const uint32_t got = fb.data[(IMAGE_SIZE.y / 2) * IMAGE_SIZE.x + px];
      const uint32_t want = name == "channel.primitiveId"
          ? tiePrimitive(0, l.size)
          : surfaceId(l.worldRanks[0]);
      const bool ok = got == want;
      printf("%s: tie %s @ px %d = %u (want rank 0's %u)\n",
          ok ? "PASS" : "FAIL",
          channel,
          px,
          got,
          want);
      failures += !ok;
    }
    const uint32_t corner = fb.data[0];
    const bool ok = corner == ~0u;
    printf("%s: background %s = %u (want %u)\n",
        ok ? "PASS" : "FAIL",
        channel,
        corner,
        ~0u);
    failures += !ok;
    anari::unmap(d, frame, channel);
  }
  return failures;
}

// Rank 0 only: channels ptc does not composite are always refused, and the
// id channels are refused unless --ids enabled them.
int checkChannelsRefused(anari::Device d, anari::Frame frame, bool ids)
{
  int failures = 0;
  for (const char *channel : UNSUPPORTED_CHANNELS)
    failures += checkChannelRefused(d, frame, channel, "unsupported");
  if (!ids) {
    for (const char *channel : ID_CHANNELS)
      failures += checkChannelRefused(d, frame, channel, "unrequested");
  }
  return failures;
}

// Ranks other than the layout's rank 0: the spec leaves the mapped contents
// undefined there, but mapping and reading what is returned must not crash.
void touchNonRootMaps(anari::Device d, anari::Frame frame)
{
  std::vector<const char *> channels = {"channel.color", "channel.depth"};
  channels.insert(
      channels.end(), std::begin(ID_CHANNELS), std::end(ID_CHANNELS));
  channels.insert(channels.end(),
      std::begin(UNSUPPORTED_CHANNELS),
      std::end(UNSUPPORTED_CHANNELS));
  for (const char *channel : channels) {
    auto fb = anari::map<uint8_t>(d, frame, channel);
    volatile uint8_t sum = 0;
    if (fb.data) {
      const size_t bytes =
          size_t(fb.width) * fb.height * anari::sizeOf(fb.pixelType);
      for (size_t i = 0; i < bytes; i++)
        sum += fb.data[i];
    }
    anari::unmap(d, frame, channel);
  }
}

} // namespace ptcsmoke
