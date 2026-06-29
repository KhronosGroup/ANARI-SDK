// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Runner.h"
#include "BuildContext.h"
#include "Expansion.h"
#include "FrameFormats.h"
#include "FrameReadback.h"
#include "Metrics.h"
#include "Sidecar.h"
#include "Value.h"
#include "WorldBuilder.h"
// std
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <stdexcept>
#include <string>
#include <utility>

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

// Render one channel of an already-assembled world with a caller-owned camera
// and renderer. chFmt is the buffer format the Case requested for this channel
// (the comparison image is always 8-bit RGBA). depthScale deterministically
// maps depth to gray.
Image renderChannel(anari::Device d,
    anari::World world,
    anari::Camera camera,
    anari::Renderer renderer,
    Channel ch,
    uint32_t w,
    uint32_t h,
    float depthScale,
    ANARIDataType chFmt,
    uint32_t accumulationFrames)
{
  UniqueAnariObject<anari::Frame> frame(d, anari::newObject<anari::Frame>(d));
  const auto f = frame.get();
  anari::setParameter(d, f, "size", anari::math::vec<uint32_t, 2>(w, h));
  // The frame always carries a color buffer; only the channel being read needs
  // the Case's requested format, so non-color channels keep a plain color
  // buffer.
  anari::setParameter(d,
      f,
      "channel.color",
      ch == Channel::Color ? chFmt : ANARI_UFIXED8_RGBA_SRGB);
  if (ch != Channel::Color)
    anari::setParameter(d, f, channelFrameParam(ch), chFmt);
  anari::setParameter(d, f, "renderer", renderer);
  anari::setParameter(d, f, "camera", camera);
  anari::setParameter(d, f, "world", world);
  // Accumulate only the color channel: depth/normal/albedo/*Id are
  // deterministic, so accumulating them is wasted work (and could alias the id
  // channels). Only set the param when gated on; never set it false, so the
  // disabled path is byte-for-byte identical to before and we never poke the
  // param on a device that does not support accumulation.
  const bool accumulate = accumulationFrames > 1 && ch == Channel::Color;
  if (accumulate)
    anari::setParameter(d, f, "accumulation", true);
  anari::commitParameters(d, f);

  // The frame is created fresh each call, so accumulation starts at sample 0;
  // rendering the same frame N times refines the image (matches
  // tests/frame.cpp's progressive-rendering check).
  if (accumulate) {
    for (uint32_t i = 0; i < accumulationFrames; ++i) {
      anari::render(d, f);
      anari::wait(d, f);
    }
  } else {
    anari::render(d, f);
    anari::wait(d, f);
  }

  auto readback = readFrameChannel(d, f, ch, w, h, depthScale);
  if (!readback)
    throw std::runtime_error(readback.detail);
  return std::move(readback.image);
}

// The default render camera: a perspective camera framing the world bounds.
anari::Camera defaultCamera(
    anari::Device d, const scenes::Bounds &bounds, uint32_t w, uint32_t h)
{
  const auto camDesc = cameraFromBounds(bounds);
  PerspectiveCameraOptions camOpts;
  if (h != 0)
    camOpts.aspect = static_cast<float>(w) / static_cast<float>(h);
  return makePerspectiveCamera(d, camDesc, camOpts);
}

// The default renderer: parameterless "default".
anari::Renderer defaultRenderer(anari::Device d)
{
  UniqueAnariObject<anari::Renderer> renderer(
      d, anari::newObject<anari::Renderer>(d, "default"));
  anari::commitParameters(d, renderer.get());
  return renderer.release();
}

CaseResult baseResult(const Case &c, const DeviceSpec &device)
{
  CaseResult r;
  r.category = c.category;
  r.test = c.testName;
  r.caseId = c.id();
  r.groundTruthKey = c.groundTruthKey();
  r.device = device;
  for (const auto &cv : c.values)
    r.axisValues.emplace_back(cv.axisName, anyToString(cv.value));
  return r;
}

// Per-pixel absolute RGB difference between two equal-sized images, with opaque
// alpha so the result is viewable. Returns an invalid Image if the sizes differ
// (then the caller skips the debug images).
Image makeDiffImage(const Image &reference, const Image &candidate)
{
  if (!reference.valid() || reference.width != candidate.width
      || reference.height != candidate.height)
    return {};
  Image diff;
  diff.width = reference.width;
  diff.height = reference.height;
  diff.rgba.resize(reference.rgba.size());
  const size_t n = reference.pixelCount();
  for (size_t i = 0; i < n; ++i) {
    for (int c = 0; c < 3; ++c) {
      const int d =
          int(reference.rgba[i * 4 + c]) - int(candidate.rgba[i * 4 + c]);
      diff.rgba[i * 4 + c] = static_cast<uint8_t>(d < 0 ? -d : d);
    }
    diff.rgba[i * 4 + 3] = 255;
  }
  return diff;
}

// A black/white mask of a diff image: white where any RGB channel differs by
// more than `threshold` (0..255), black otherwise.
Image makeThresholdImage(const Image &diff, uint8_t threshold)
{
  Image out;
  out.width = diff.width;
  out.height = diff.height;
  out.rgba.resize(diff.rgba.size());
  const size_t n = diff.pixelCount();
  for (size_t i = 0; i < n; ++i) {
    const bool over = diff.rgba[i * 4 + 0] > threshold
        || diff.rgba[i * 4 + 1] > threshold || diff.rgba[i * 4 + 2] > threshold;
    const uint8_t v = over ? 255 : 0;
    out.rgba[i * 4 + 0] = v;
    out.rgba[i * 4 + 1] = v;
    out.rgba[i * 4 + 2] = v;
    out.rgba[i * 4 + 3] = 255;
  }
  return out;
}

// Diff intensity (0..255) above which the threshold mask marks a pixel: ~0.05
// of full range, matching the legacy debug-image cutoff.
constexpr uint8_t kDiffThreshold8 = 13;

} // namespace

Runner::Runner(anari::Device device, Workdir workdir, RunOptions options)
    : m_device(device), m_workdir(std::move(workdir)), m_options(options)
{}

void Runner::resolveCapabilities(const std::set<std::string> &features)
{
  m_effectiveAccumulationFrames =
      (m_options.accumulationFrames > 1
          && features.count("ANARI_KHR_FRAME_ACCUMULATION"))
      ? m_options.accumulationFrames
      : 1;
  // Denoise is applied whenever requested, even if the device does not report
  // ANARI_KHR_RENDERER_DENOISE: setting an unadvertised renderer parameter is
  // harmless (an unsupporting device ignores it), and the CLI warns about the
  // mismatch. Unlike accumulation, it is not gated on the feature set.
  m_denoiseEnabled = m_options.denoise;
}

Runner::SceneObjects Runner::buildScene(const TestDef &test, const Case &c)
{
  SceneObjects scene;
  if (!test.build)
    return scene;

  BuildContext ctx(m_device);
  for (const auto &cv : c.values)
    ctx.set(cv.axisName, cv.value);

  scene.world = UniqueAnariObject<anari::World>(m_device, test.build(ctx));
  if (!scene.world)
    return scene;

  scene.bounds = worldBounds(m_device, scene.world.get());
  scene.camera = UniqueAnariObject<anari::Camera>(m_device,
      test.cameraBuild
          ? test.cameraBuild(ctx, scene.bounds)
          : defaultCamera(
                m_device, scene.bounds, m_options.width, m_options.height));
  scene.renderer = UniqueAnariObject<anari::Renderer>(m_device,
      test.rendererBuild ? test.rendererBuild(ctx) : defaultRenderer(m_device));
  // Denoising is additive: set one extra bool on the already-built+committed
  // renderer (test's or default) and re-commit, without disturbing its other
  // params. Set whenever --denoise was requested, even on a device that does
  // not report ANARI_KHR_RENDERER_DENOISE (it ignores the unknown param; the
  // CLI warns about the mismatch).
  if (m_denoiseEnabled && scene.renderer) {
    anari::setParameter(m_device, scene.renderer.get(), "denoise", true);
    anari::commitParameters(m_device, scene.renderer.get());
  }
  return scene;
}

void Runner::writeFeatureSkip(const Case &c, RunSummary &summary)
{
  CaseResult result = baseResult(c, m_options.device);
  result.verdict = Verdict::Skipped;
  result.skipReason = "device is missing a required feature";
  writeSidecar(m_workdir.sidecarPath(c), result);
  summary.skipped++;
}

void Runner::writeCaseFailure(
    const Case &c, const std::string &detail, RunSummary &summary)
{
  CaseResult result = baseResult(c, m_options.device);
  result.verdict = Verdict::Failed;
  result.detail = detail;
  writeSidecar(m_workdir.sidecarPath(c), result);
  summary.failed++;
}

std::vector<Image> Runner::renderCase(const TestDef &test, const Case &c)
{
  SceneObjects scene = buildScene(test, c);
  if (!scene.valid())
    return {};

  // Deterministic depth scale derived from the (shared) scene bounds: the
  // default camera sits within one bounds diagonal of the center, so 2x is a
  // conservative bound that covers the whole near..far range.
  const float depthScale =
      2.0f * anari::math::length(scene.bounds[1] - scene.bounds[0]);

  std::vector<Image> images;
  images.reserve(test.channels.size());
  for (Channel ch : test.channels) {
    const ANARIDataType chFmt = caseChannelFormat(c, ch);
    images.push_back(renderChannel(m_device,
        scene.world.get(),
        scene.camera.get(),
        scene.renderer.get(),
        ch,
        m_options.width,
        m_options.height,
        depthScale,
        chFmt,
        m_effectiveAccumulationFrames));
  }
  return images;
}

RunSummary Runner::generate(const Catalog &catalog,
    const Filter &filter,
    const std::set<std::string> &referenceFeatures)
{
  resolveCapabilities(referenceFeatures);
  m_workdir.writeGitignore();
  RunSummary summary;
  for (const TestDef *test : catalog.filter(filter)) {
    // Behavioral tests verify themselves at run time and have no ground truth.
    if (test->behaviorCheck) {
      const int n = static_cast<int>(expand(*test).size());
      summary.total += n;
      summary.skipped += n;
      continue;
    }
    for (const Case &c : expand(*test)) {
      summary.total++;
      if (!isSupported(*test, referenceFeatures)) {
        summary.skipped++;
        continue;
      }
      // A throwing build helper (or fatal) loses only this Case's ground
      // truth; the rest of the generate run continues (ADR-0003).
      try {
        auto images = renderCase(*test, c);
        if (images.size() != test->channels.size()) {
          summary.failed++;
          continue;
        }
        bool ok = true;
        for (size_t i = 0; i < test->channels.size(); ++i) {
          if (!savePNG(
                  m_workdir.groundTruthImagePath(c, test->channels[i]).string(),
                  images[i]))
            ok = false;
        }
        ok ? summary.passed++ : summary.failed++;
      } catch (...) {
        summary.failed++;
      }
    }
  }
  return summary;
}

RunSummary Runner::run(const Catalog &catalog,
    const Filter &filter,
    const std::set<std::string> &candidateFeatures)
{
  resolveCapabilities(candidateFeatures);
  m_workdir.writeGitignore();
  RunSummary summary;
  for (const TestDef *test : catalog.filter(filter)) {
    if (test->behaviorCheck) {
      runBehaviorTest(*test, candidateFeatures, summary);
      continue;
    }
    for (const Case &c : expand(*test)) {
      summary.total++;

      // Per-case crash isolation (ADR-0003): a throwing build helper or fatal
      // becomes a failed sidecar for this Case and the run continues. Handles
      // built mid-case are released by buildScene/renderCase as they unwind.
      try {
        if (!isSupported(*test, candidateFeatures)) {
          writeFeatureSkip(c, summary);
          continue;
        }

        CaseResult result = baseResult(c, m_options.device);

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
          const double ssimThreshold =
              test->thresholdFor(ch, "ssim", m_options.ssimThreshold);
          const double psnrThreshold =
              test->thresholdFor(ch, "psnr", m_options.psnrThreshold);
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

          // Debug images alongside the result: the absolute per-pixel
          // difference and a thresholded mask of it, to localize a mismatch.
          const Image diffImg = makeDiffImage(groundTruth[i], images[i]);
          if (diffImg.valid()) {
            savePNG(m_workdir.diffImagePath(c, ch).string(), diffImg);
            savePNG(m_workdir.thresholdImagePath(c, ch).string(),
                makeThresholdImage(diffImg, kDiffThreshold8));
            cr.diffImage =
                m_workdir.relativeToRoot(m_workdir.diffImagePath(c, ch));
            cr.thresholdImage =
                m_workdir.relativeToRoot(m_workdir.thresholdImagePath(c, ch));
          }

          allPassed = allPassed && cr.passed;
          result.channels.push_back(std::move(cr));
        }

        // Record the effective render settings additively in the existing
        // detail field (no schema bump). generate writes no sidecars
        // (ADR-0005), so this note is run-only; it makes a mixed-capability
        // diff (e.g. an accumulating candidate vs. a single-render ground
        // truth) interpretable.
        std::string note =
            "accumulation=" + std::to_string(m_effectiveAccumulationFrames)
            + ", denoise=" + (m_denoiseEnabled ? "on" : "off");
        result.detail =
            result.detail.empty() ? note : result.detail + "; " + note;

        result.verdict = allPassed ? Verdict::Passed : Verdict::Failed;
        writeSidecar(m_workdir.sidecarPath(c), result);
        allPassed ? summary.passed++ : summary.failed++;
      } catch (const std::exception &e) {
        writeCaseFailure(
            c, std::string("exception during run: ") + e.what(), summary);
      } catch (...) {
        writeCaseFailure(c, "unknown exception during run", summary);
      }
    }
  }
  return summary;
}

void Runner::runBehaviorTest(const TestDef &test,
    const std::set<std::string> &candidateFeatures,
    RunSummary &summary)
{
  for (const Case &c : expand(test)) {
    summary.total++;

    if (!isSupported(test, candidateFeatures)) {
      writeFeatureSkip(c, summary);
      continue;
    }

    // Per-case crash isolation (ADR-0003): a throwing build hook or behavior
    // check becomes a failed sidecar and the run continues; SceneObjects owns
    // every handle and releases partial or complete builds on scope exit.
    try {
      CaseResult result = baseResult(c, m_options.device);

      SceneObjects scene = buildScene(test, c);
      if (!scene.valid()) {
        result.verdict = Verdict::Failed;
        result.detail = "world/camera/renderer build failed";
        writeSidecar(m_workdir.sidecarPath(c), result);
        summary.failed++;
      } else {
        const auto start = std::chrono::steady_clock::now();
        const BehaviorResult br = test.behaviorCheck(m_device,
            scene.world.get(),
            scene.camera.get(),
            scene.renderer.get(),
            m_options.width,
            m_options.height);
        const auto end = std::chrono::steady_clock::now();
        result.durationMs =
            std::chrono::duration<double, std::milli>(end - start).count();
        result.verdict = br.passed ? Verdict::Passed : Verdict::Failed;
        result.detail = br.detail;
        writeSidecar(m_workdir.sidecarPath(c), result);
        br.passed ? summary.passed++ : summary.failed++;
      }
    } catch (const std::exception &e) {
      writeCaseFailure(c,
          std::string("exception during behavior test: ") + e.what(),
          summary);
    } catch (...) {
      writeCaseFailure(c, "unknown exception during behavior test", summary);
    }
  }
}

} // namespace cts
} // namespace anari
