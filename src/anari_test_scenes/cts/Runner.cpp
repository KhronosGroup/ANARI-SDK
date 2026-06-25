// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Runner.h"
#include "BuildContext.h"
#include "Expansion.h"
#include "Metrics.h"
#include "Sidecar.h"
#include "Value.h"
#include "WorldBuilder.h"
#include "generators/ColorPalette.h"
// std
#include <chrono>
#include <cmath>
#include <cstring>

namespace anari {
namespace cts {

namespace {

const char *channelFrameParam(Channel ch)
{
  switch (ch) {
  case Channel::Color:
    return "channel.color";
  case Channel::Depth:
    return "channel.depth";
  case Channel::Albedo:
    return "channel.albedo";
  case Channel::Normal:
    return "channel.normal";
  case Channel::PrimitiveId:
    return "channel.primitiveId";
  case Channel::ObjectId:
    return "channel.objectId";
  case Channel::InstanceId:
    return "channel.instanceId";
  }
  return "channel.color";
}

ANARIDataType channelFormat(Channel ch)
{
  switch (ch) {
  case Channel::Color:
    return ANARI_UFIXED8_RGBA_SRGB;
  case Channel::Depth:
    return ANARI_FLOAT32;
  case Channel::Albedo:
  case Channel::Normal:
    return ANARI_FLOAT32_VEC3;
  case Channel::PrimitiveId:
  case Channel::ObjectId:
  case Channel::InstanceId:
    return ANARI_UINT32;
  }
  return ANARI_UFIXED8_RGBA_SRGB;
}

uint8_t toU8(float v)
{
  const float s = v * 255.0f;
  return static_cast<uint8_t>(s < 0.f ? 0.f : (s > 255.f ? 255.f : s));
}

Image colorToImage(anari::Device d, anari::Frame frame, uint32_t w, uint32_t h)
{
  Image image;
  image.width = w;
  image.height = h;
  image.rgba.resize(static_cast<size_t>(w) * h * 4);
  auto fb = anari::map<uint32_t>(d, frame, "channel.color");
  if (fb.data != nullptr)
    std::memcpy(image.rgba.data(), fb.data, image.rgba.size());
  anari::unmap(d, frame, "channel.color");
  return image;
}

// Encode depth to grayscale with a fixed, scene-derived scale (not the
// per-frame maximum, which would differ between candidate and reference and
// make the comparison non-deterministic). depthScale is the same for both
// because both render the same world with the same bounds-framed camera.
Image depthToImage(anari::Device d,
    anari::Frame frame,
    uint32_t w,
    uint32_t h,
    float depthScale)
{
  Image image;
  image.width = w;
  image.height = h;
  image.rgba.assign(static_cast<size_t>(w) * h * 4, 255);
  auto fb = anari::map<float>(d, frame, "channel.depth");
  if (fb.data != nullptr) {
    const size_t n = static_cast<size_t>(w) * h;
    const float scale = depthScale > 0.f ? 1.f / depthScale : 0.f;
    for (size_t i = 0; i < n; ++i) {
      const float v = fb.data[i];
      const uint8_t g = std::isfinite(v) ? toU8(v * scale) : 255;
      image.rgba[i * 4 + 0] = g;
      image.rgba[i * 4 + 1] = g;
      image.rgba[i * 4 + 2] = g;
      image.rgba[i * 4 + 3] = 255;
    }
  }
  anari::unmap(d, frame, "channel.depth");
  return image;
}

Image vec3ToImage(anari::Device d,
    anari::Frame frame,
    const char *channel,
    uint32_t w,
    uint32_t h,
    bool signedRange)
{
  Image image;
  image.width = w;
  image.height = h;
  image.rgba.assign(static_cast<size_t>(w) * h * 4, 0);
  auto fb = anari::map<float>(d, frame, channel);
  if (fb.data != nullptr) {
    const size_t n = static_cast<size_t>(w) * h;
    for (size_t i = 0; i < n; ++i) {
      for (int c = 0; c < 3; ++c) {
        float v = fb.data[i * 3 + c];
        if (signedRange) // map [-1,1] -> [0,1] (normals)
          v = v * 0.5f + 0.5f;
        image.rgba[i * 4 + c] = toU8(v);
      }
      image.rgba[i * 4 + 3] = 255;
    }
  }
  anari::unmap(d, frame, channel);
  return image;
}

Image idToImage(anari::Device d,
    anari::Frame frame,
    const char *channel,
    uint32_t w,
    uint32_t h)
{
  Image image;
  image.width = w;
  image.height = h;
  image.rgba.assign(static_cast<size_t>(w) * h * 4, 0);
  auto fb = anari::map<uint32_t>(d, frame, channel);
  if (fb.data != nullptr) {
    const size_t n = static_cast<size_t>(w) * h;
    for (size_t i = 0; i < n; ++i) {
      const auto color = scenes::colors::getColorFromPalette(fb.data[i]);
      image.rgba[i * 4 + 0] = toU8(color.x);
      image.rgba[i * 4 + 1] = toU8(color.y);
      image.rgba[i * 4 + 2] = toU8(color.z);
      image.rgba[i * 4 + 3] = 255;
    }
  }
  anari::unmap(d, frame, channel);
  return image;
}

Image renderChannel(
    anari::Device d, anari::World world, Channel ch, uint32_t w, uint32_t h)
{
  const auto bounds = worldBounds(d, world);
  const auto camDesc = cameraFromBounds(bounds);
  PerspectiveCameraOptions camOpts;
  if (h != 0)
    camOpts.aspect = static_cast<float>(w) / static_cast<float>(h);
  auto camera = makePerspectiveCamera(d, camDesc, camOpts);

  // Deterministic depth scale derived from the (shared) scene bounds: the
  // camera sits ~one diagonal from the center, so 2x covers near..far.
  const float depthScale =
      2.0f * anari::math::length(bounds[1] - bounds[0]);

  auto renderer = anari::newObject<anari::Renderer>(d, "default");
  anari::commitParameters(d, renderer);

  auto frame = anari::newObject<anari::Frame>(d);
  anari::setParameter(d, frame, "size", anari::math::vec<uint32_t, 2>(w, h));
  anari::setParameter(d, frame, "channel.color", ANARI_UFIXED8_RGBA_SRGB);
  if (ch != Channel::Color)
    anari::setParameter(d, frame, channelFrameParam(ch), channelFormat(ch));
  anari::setParameter(d, frame, "renderer", renderer);
  anari::setParameter(d, frame, "camera", camera);
  anari::setParameter(d, frame, "world", world);
  anari::commitParameters(d, frame);

  anari::render(d, frame);
  anari::wait(d, frame);

  Image image;
  switch (ch) {
  case Channel::Color:
    image = colorToImage(d, frame, w, h);
    break;
  case Channel::Depth:
    image = depthToImage(d, frame, w, h, depthScale);
    break;
  case Channel::Albedo:
    image = vec3ToImage(d, frame, "channel.albedo", w, h, false);
    break;
  case Channel::Normal:
    image = vec3ToImage(d, frame, "channel.normal", w, h, true);
    break;
  case Channel::PrimitiveId:
  case Channel::ObjectId:
  case Channel::InstanceId:
    image = idToImage(d, frame, channelFrameParam(ch), w, h);
    break;
  }

  anari::release(d, frame);
  anari::release(d, camera);
  anari::release(d, renderer);
  return image;
}

CaseResult baseResult(const Case &c)
{
  CaseResult r;
  r.category = c.category;
  r.test = c.testName;
  r.caseId = c.id();
  r.groundTruthKey = c.groundTruthKey();
  for (const auto &cv : c.values)
    r.axisValues.emplace_back(cv.axisName, anyToString(cv.value));
  return r;
}

} // namespace

Runner::Runner(anari::Device device, Workdir workdir, RunOptions options)
    : m_device(device), m_workdir(std::move(workdir)), m_options(options)
{}

std::vector<Image> Runner::renderCase(const TestDef &test, const Case &c)
{
  if (!test.build)
    return {};

  BuildContext ctx(m_device);
  for (const auto &cv : c.values)
    ctx.set(cv.axisName, cv.value);

  anari::World world = test.build(ctx);
  if (world == nullptr)
    return {};

  std::vector<Image> images;
  images.reserve(test.channels.size());
  for (Channel ch : test.channels)
    images.push_back(
        renderChannel(m_device, world, ch, m_options.width, m_options.height));

  anari::release(m_device, world);
  return images;
}

RunSummary Runner::generate(const Catalog &catalog,
    const Filter &filter,
    const std::set<std::string> &referenceFeatures)
{
  m_workdir.writeGitignore();
  RunSummary summary;
  for (const TestDef *test : catalog.filter(filter)) {
    for (const Case &c : expand(*test)) {
      summary.total++;
      if (!isSupported(*test, referenceFeatures)) {
        summary.skipped++;
        continue;
      }
      auto images = renderCase(*test, c);
      if (images.size() != test->channels.size()) {
        summary.failed++;
        continue;
      }
      bool ok = true;
      for (size_t i = 0; i < test->channels.size(); ++i) {
        if (!savePNG(m_workdir.groundTruthImagePath(c, test->channels[i]).string(),
                images[i]))
          ok = false;
      }
      ok ? summary.passed++ : summary.failed++;
    }
  }
  return summary;
}

RunSummary Runner::run(const Catalog &catalog,
    const Filter &filter,
    const std::set<std::string> &candidateFeatures)
{
  m_workdir.writeGitignore();
  RunSummary summary;
  for (const TestDef *test : catalog.filter(filter)) {
    const double ssimThreshold = test->thresholdOr("ssim", m_options.ssimThreshold);
    const double psnrThreshold = test->thresholdOr("psnr", m_options.psnrThreshold);

    for (const Case &c : expand(*test)) {
      summary.total++;
      CaseResult result = baseResult(c);

      if (!isSupported(*test, candidateFeatures)) {
        result.verdict = Verdict::Skipped;
        result.skipReason = "device is missing a required feature";
        writeSidecar(m_workdir.sidecarPath(c), result);
        summary.skipped++;
        continue;
      }

      // Need ground truth for every channel before we can compare.
      bool haveGroundTruth = true;
      std::vector<Image> groundTruth;
      for (Channel ch : test->channels) {
        auto gt = loadPNG(m_workdir.groundTruthImagePath(c, ch).string());
        if (!gt.valid()) {
          haveGroundTruth = false;
          break;
        }
        groundTruth.push_back(std::move(gt));
      }
      if (!haveGroundTruth) {
        result.verdict = Verdict::Skipped;
        result.skipReason = "no ground truth (run generate first)";
        writeSidecar(m_workdir.sidecarPath(c), result);
        summary.skipped++;
        continue;
      }

      const auto start = std::chrono::steady_clock::now();
      auto images = renderCase(*test, c);
      const auto end = std::chrono::steady_clock::now();
      result.durationMs =
          std::chrono::duration<double, std::milli>(end - start).count();

      if (images.size() != test->channels.size()) {
        result.verdict = Verdict::Failed;
        result.skipReason = "render produced no image";
        writeSidecar(m_workdir.sidecarPath(c), result);
        summary.failed++;
        continue;
      }

      bool allPassed = true;
      for (size_t i = 0; i < test->channels.size(); ++i) {
        const Channel ch = test->channels[i];
        savePNG(m_workdir.resultImagePath(c, ch).string(), images[i]);

        ChannelResult cr;
        cr.channel = ch;
        const double ssimScore = ssim(groundTruth[i], images[i]);
        const double psnrScore = psnr(groundTruth[i], images[i]);
        cr.metrics = {{"ssim", ssimScore}, {"psnr", psnrScore}};
        cr.thresholds = {{"ssim", ssimThreshold}, {"psnr", psnrThreshold}};
        cr.passed = metricPassed(ssimScore, ssimThreshold)
            && metricPassed(psnrScore, psnrThreshold);
        cr.resultImage =
            m_workdir.relativeToRoot(m_workdir.resultImagePath(c, ch));
        cr.groundTruthImage =
            m_workdir.relativeToRoot(m_workdir.groundTruthImagePath(c, ch));
        allPassed = allPassed && cr.passed;
        result.channels.push_back(std::move(cr));
      }

      result.verdict = allPassed ? Verdict::Passed : Verdict::Failed;
      writeSidecar(m_workdir.sidecarPath(c), result);
      allPassed ? summary.passed++ : summary.failed++;
    }
  }
  return summary;
}

} // namespace cts
} // namespace anari
