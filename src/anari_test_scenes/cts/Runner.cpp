// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Runner.h"
#include "BuildContext.h"
#include "Expansion.h"
#include "FrameFormats.h"
#include "Metrics.h"
#include "Sidecar.h"
#include "Value.h"
#include "WorldBuilder.h"
#include "generators/ColorPalette.h"
// std
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <exception>
#include <string>

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
  image.rgba.assign(static_cast<size_t>(w) * h * 4, 0);
  const size_t n = static_cast<size_t>(w) * h;
  // Read by the device-reported pixel type, not the requested one: a device may
  // return a different format than asked for, and reading at the wrong stride
  // would corrupt the comparison image. Each device's own readback is correct,
  // so a candidate and its ground truth stay comparable even across devices.
  auto fb = anari::map<uint8_t>(d, frame, "channel.color");
  if (fb.data != nullptr) {
    if (fb.pixelType == ANARI_FLOAT32_VEC4) {
      // Float color is read componentwise and quantized to 8-bit.
      const auto *p = reinterpret_cast<const float *>(fb.data);
      for (size_t i = 0; i < n; ++i)
        for (int c = 0; c < 4; ++c)
          image.rgba[i * 4 + c] = toU8(p[i * 4 + c]);
    } else {
      // UFIXED8_RGBA_SRGB / UFIXED8_VEC4 are already packed RGBA8.
      std::memcpy(image.rgba.data(), fb.data, image.rgba.size());
    }
  }
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

// Read a 3-component channel (albedo/normal) into the 8-bit comparison image.
// Dispatch on the device-reported pixel type (float, unsigned-normalized 8-bit,
// or signed-normalized 16-bit) rather than the requested format, so a device
// that returns a different format than asked is still read at the right stride.
// signedRange maps a [-1,1] value (normals) into [0,1] before quantizing.
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
  const size_t n = static_cast<size_t>(w) * h;

  // Map raw bytes so we can reinterpret by whatever pixel type the device
  // reports.
  auto fb = anari::map<uint8_t>(d, frame, channel);
  if (fb.data != nullptr) {
    switch (fb.pixelType) {
    case ANARI_UFIXED8_VEC3:
    case ANARI_UFIXED8_RGB_SRGB: {
      // Already 8-bit per component; copy verbatim (sRGB encoding preserved).
      const uint8_t *p = fb.data;
      for (size_t i = 0; i < n; ++i) {
        for (int c = 0; c < 3; ++c)
          image.rgba[i * 4 + c] = p[i * 3 + c];
        image.rgba[i * 4 + 3] = 255;
      }
      break;
    }
    case ANARI_FIXED16_VEC3: {
      // Signed-normalized 16-bit: value = raw / 32767 in [-1, 1].
      const auto *p = reinterpret_cast<const int16_t *>(fb.data);
      for (size_t i = 0; i < n; ++i) {
        for (int c = 0; c < 3; ++c) {
          float v = std::max(p[i * 3 + c] / 32767.0f, -1.0f);
          if (signedRange) // map [-1,1] -> [0,1] (normals)
            v = v * 0.5f + 0.5f;
          image.rgba[i * 4 + c] = toU8(v);
        }
        image.rgba[i * 4 + 3] = 255;
      }
      break;
    }
    default: { // ANARI_FLOAT32_VEC3 (and any unexpected format)
      const auto *p = reinterpret_cast<const float *>(fb.data);
      for (size_t i = 0; i < n; ++i) {
        for (int c = 0; c < 3; ++c) {
          float v = p[i * 3 + c];
          if (signedRange) // map [-1,1] -> [0,1] (normals)
            v = v * 0.5f + 0.5f;
          image.rgba[i * 4 + c] = toU8(v);
        }
        image.rgba[i * 4 + 3] = 255;
      }
      break;
    }
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
  auto frame = anari::newObject<anari::Frame>(d);
  anari::setParameter(d, frame, "size", anari::math::vec<uint32_t, 2>(w, h));
  // The frame always carries a color buffer; only the channel being read needs
  // the Case's requested format, so non-color channels keep a plain color
  // buffer.
  anari::setParameter(d,
      frame,
      "channel.color",
      ch == Channel::Color ? chFmt : ANARI_UFIXED8_RGBA_SRGB);
  if (ch != Channel::Color)
    anari::setParameter(d, frame, channelFrameParam(ch), chFmt);
  anari::setParameter(d, frame, "renderer", renderer);
  anari::setParameter(d, frame, "camera", camera);
  anari::setParameter(d, frame, "world", world);
  // Accumulate only the color channel: depth/normal/albedo/*Id are
  // deterministic, so accumulating them is wasted work (and could alias the id
  // channels). Only set the param when gated on; never set it false, so the
  // disabled path is byte-for-byte identical to before and we never poke the
  // param on a device that does not support accumulation.
  const bool accumulate = accumulationFrames > 1 && ch == Channel::Color;
  if (accumulate)
    anari::setParameter(d, frame, "accumulation", true);
  anari::commitParameters(d, frame);

  // The frame is created fresh each call, so accumulation starts at sample 0;
  // rendering the same frame N times refines the image (matches
  // tests/frame.cpp's progressive-rendering check).
  if (accumulate) {
    for (uint32_t i = 0; i < accumulationFrames; ++i) {
      anari::render(d, frame);
      anari::wait(d, frame);
    }
  } else {
    anari::render(d, frame);
    anari::wait(d, frame);
  }

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
  return image;
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
  auto renderer = anari::newObject<anari::Renderer>(d, "default");
  anari::commitParameters(d, renderer);
  return renderer;
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
        || diff.rgba[i * 4 + 1] > threshold
        || diff.rgba[i * 4 + 2] > threshold;
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
  m_effectiveAccumulationFrames = (m_options.accumulationFrames > 1
                                      && features.count(
                                          "ANARI_KHR_FRAME_ACCUMULATION"))
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

  // A build hook (or a default camera/renderer) may throw after some handles
  // are already created; release the partial scene before letting the
  // exception unwind so a throwing Case leaks nothing (ADR-0003).
  try {
    scene.world = test.build(ctx);
    if (scene.world == nullptr)
      return scene;

    scene.bounds = worldBounds(m_device, scene.world);
    scene.camera = test.cameraBuild
        ? test.cameraBuild(ctx, scene.bounds)
        : defaultCamera(
              m_device, scene.bounds, m_options.width, m_options.height);
    scene.renderer = test.rendererBuild ? test.rendererBuild(ctx)
                                        : defaultRenderer(m_device);
    // Denoising is additive: set one extra bool on the already-built+committed
    // renderer (test's or default) and re-commit, without disturbing its other
    // params. Set whenever --denoise was requested, even on a device that does
    // not report ANARI_KHR_RENDERER_DENOISE (it ignores the unknown param; the
    // CLI warns about the mismatch).
    if (m_denoiseEnabled && scene.renderer) {
      anari::setParameter(m_device, scene.renderer, "denoise", true);
      anari::commitParameters(m_device, scene.renderer);
    }
  } catch (...) {
    releaseScene(scene);
    throw;
  }
  return scene;
}

void Runner::releaseScene(SceneObjects &scene)
{
  if (scene.world)
    anari::release(m_device, scene.world);
  if (scene.camera)
    anari::release(m_device, scene.camera);
  if (scene.renderer)
    anari::release(m_device, scene.renderer);
  scene = {};
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
  if (!scene.valid()) {
    releaseScene(
        scene); // release a partial build (world but no camera/renderer)
    return {};
  }

  // Deterministic depth scale derived from the (shared) scene bounds: the
  // default camera sits within one bounds diagonal of the center, so 2x is a
  // conservative bound that covers the whole near..far range.
  const float depthScale =
      2.0f * anari::math::length(scene.bounds[1] - scene.bounds[0]);

  std::vector<Image> images;
  // Release the scene even if a channel render throws (then let it unwind to
  // the per-case handler, which records the failure and continues the run).
  try {
    images.reserve(test.channels.size());
    for (Channel ch : test.channels) {
      const ANARIDataType chFmt = caseChannelFormat(c, ch);
      images.push_back(renderChannel(m_device,
          scene.world,
          scene.camera,
          scene.renderer,
          ch,
          m_options.width,
          m_options.height,
          depthScale,
          chFmt,
          m_effectiveAccumulationFrames));
    }
  } catch (...) {
    releaseScene(scene);
    throw;
  }

  releaseScene(scene);
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
        // detail field (no schema bump). generate writes no sidecars (ADR-0005),
        // so this note is run-only; it makes a mixed-capability diff (e.g. an
        // accumulating candidate vs. a single-render ground truth) interpretable.
        std::string note = "accumulation="
            + std::to_string(m_effectiveAccumulationFrames)
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
    // check becomes a failed sidecar and the run continues; the scene is
    // released on every path (buildScene releases a partial build on throw).
    SceneObjects scene;
    try {
      CaseResult result = baseResult(c, m_options.device);

      scene = buildScene(test, c);
      if (!scene.valid()) {
        result.verdict = Verdict::Failed;
        result.detail = "world/camera/renderer build failed";
        writeSidecar(m_workdir.sidecarPath(c), result);
        summary.failed++;
      } else {
        const auto start = std::chrono::steady_clock::now();
        const BehaviorResult br = test.behaviorCheck(m_device,
            scene.world,
            scene.camera,
            scene.renderer,
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
    releaseScene(scene);
  }
}

} // namespace cts
} // namespace anari
