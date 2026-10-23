// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "PathTracer.h"

#include "ColorManagement.h"
#include "Light_ispc.h"
#include "PathTracer_ispc.h"
#include "frame/Frame.h"
// std
#include <algorithm>
#include <atomic>
#include <cmath>
#include <string_view>
#include <utility>

namespace halcyon {

// The 'mode' parameter value 'name' as a PathTracerMode; unknown names (and
// "default") path trace
static ispc::PathTracerMode pathTracerModeFromString(std::string_view name)
{
  static const std::pair<std::string_view, ispc::PathTracerMode> modes[] = {
      {"position", ispc::PATH_TRACER_MODE_POSITION},
      {"normal", ispc::PATH_TRACER_MODE_NORMAL},
      {"tangent", ispc::PATH_TRACER_MODE_TANGENT},
      {"bitangent", ispc::PATH_TRACER_MODE_BITANGENT},
      {"texcoord", ispc::PATH_TRACER_MODE_TEXCOORD},
      {"displayColor", ispc::PATH_TRACER_MODE_DISPLAY_COLOR},
      {"dPdu", ispc::PATH_TRACER_MODE_DPDU},
      {"dPdv", ispc::PATH_TRACER_MODE_DPDV},
      {"dndu", ispc::PATH_TRACER_MODE_DNDU},
      {"dndv", ispc::PATH_TRACER_MODE_DNDV},
      {"dPdx", ispc::PATH_TRACER_MODE_DPDX},
      {"dPdy", ispc::PATH_TRACER_MODE_DPDY},
      {"texcoordDerivatives", ispc::PATH_TRACER_MODE_TEXCOORD_DERIVATIVES},
  };
  for (const auto &[n, mode] : modes) {
    if (n == name)
      return mode;
  }
  return ispc::PATH_TRACER_MODE_DEFAULT;
}

// PathTracerFrameState definitions ///////////////////////////////////////////

// A frame's refinement since the last change (Typhoon's adaptive sampling,
// AdaptiveSampling.isph): the per-pixel running means and adaptive sampling
// buffers, and their ISPC struct (struct PathTracerFrameState in
// PathTracer.ispc), which the shading pass takes; the passes of progressive
// resolution still to come; and the 'numSamples' and 'refinementProgress'
// properties of the frame's last render
struct PathTracerFrameState : public RendererFrameState
{
  PathTracerFrameState();

  bool getProperty(const std::string_view &name,
      ANARIDataType type,
      void *ptr,
      uint64_t size,
      uint32_t flags) override;

  // Sizes the buffers for 'numPixels', the albedo and normal running means
  // only where the frame has those channels (frees them where it doesn't)
  void resize(size_t numPixels, bool albedo, bool normal);
  // After a render of 'pixelSamples' samples per pixel: whether the image is
  // done refining (every pixel converged, or 'sampleLimit' samples taken; 0
  // for no limit), and the properties to report
  void updateRefinement(int pixelSamples, int sampleLimit);
  void *ispc();

  std::vector<uint8_t> m_ispc;
  // running mean colour, and its Welford sum of squared deviations (packed
  // float3) and whether it is converged, per pixel
  std::vector<float4> m_accum;
  std::vector<float> m_varianceM2;
  std::vector<uint8_t> m_converged;
  // the albedo and normal channels' running means (packed float3), empty
  // for channels the frame doesn't have
  std::vector<float> m_albedoAccum;
  std::vector<float> m_normalAccum;
  // renders accumulated since the last change, before the next one
  int m_accumId{0};
  // the samples every pixel not yet converged took since the last change,
  // and whether the image is done (every pixel converged, or the sample
  // limit reached), after which renders with nothing changed are skipped
  int m_samplesTaken{0};
  bool m_refined{false};
  // Progressive resolution: the first render after a change, its
  // 'pixelSamples' samples in every pixel, is spread over renders of ever
  // finer grids, each tracing the pixels on the grid of every 'm_stride'-th
  // pixel that the last didn't (on that of every 'm_tracedStride'-th, 0
  // before the first) and halving the stride, down to 1. Renders that
  // accumulate have 'm_stride' 0.
  int m_stride{0};
  int m_tracedStride{0};
  // 'numSamples' and 'refinementProgress' as of the last render, -1 (not
  // reported) before the first
  std::atomic<int> m_numSamples{-1};
  std::atomic<float> m_refinementProgress{0.f};
};

PathTracerFrameState::PathTracerFrameState()
    : m_ispc(ispc::sizeof_PathTracerFrameState())
{
  ispc::PathTracerFrameState_setBuffers(
      ispc(), nullptr, nullptr, nullptr, nullptr, nullptr);
  ispc::PathTracerFrameState_setAccumulation(ispc(), 0, 0);
  ispc::PathTracerFrameState_setPass(ispc(), 1, 0);
}

void *PathTracerFrameState::ispc()
{
  return m_ispc.data();
}

bool PathTracerFrameState::getProperty(const std::string_view &name,
    ANARIDataType type,
    void *ptr,
    uint64_t /*size*/,
    uint32_t /*flags*/)
{
  if (type == ANARI_INT32 && name == "numSamples") {
    const int numSamples = m_numSamples.load();
    if (numSamples < 0)
      return false;
    helium::writeToVoidP(ptr, numSamples);
    return true;
  }
  if (type == ANARI_FLOAT32 && name == "refinementProgress") {
    if (m_numSamples.load() < 0)
      return false;
    helium::writeToVoidP(ptr, m_refinementProgress.load());
    return true;
  }
  return false;
}

void PathTracerFrameState::resize(size_t numPixels, bool albedo, bool normal)
{
  m_accum.resize(numPixels);
  m_varianceM2.resize(3 * numPixels);
  m_converged.resize(numPixels);
  // free the memory, not only the elements
  const auto resize = [](std::vector<float> &buffer, size_t size) {
    if (size == 0)
      buffer = {};
    else
      buffer.resize(size);
  };
  resize(m_albedoAccum, albedo ? 3 * numPixels : 0);
  resize(m_normalAccum, normal ? 3 * numPixels : 0);
  const auto ptr = [](auto &buffer) -> auto * {
    return buffer.empty() ? nullptr : buffer.data();
  };
  ispc::PathTracerFrameState_setBuffers(ispc(),
      ptr(m_accum),
      ptr(m_varianceM2),
      ptr(m_converged),
      ptr(m_albedoAccum),
      ptr(m_normalAccum));
}

void PathTracerFrameState::updateRefinement(int pixelSamples, int sampleLimit)
{
  m_samplesTaken += pixelSamples;
  const size_t numConverged = std::count_if(
      m_converged.begin(), m_converged.end(), [](uint8_t c) { return c != 0; });
  const bool allConverged = numConverged == m_converged.size();
  if (sampleLimit <= 0) {
    // no limit: done once every pixel converged, progress is the fraction
    // of converged pixels
    m_refined = allConverged;
    m_refinementProgress = allConverged
        ? 1.f
        : float(double(numConverged) / double(m_converged.size()));
    m_numSamples = m_samplesTaken;
    return;
  }
  m_refined = allConverged || m_samplesTaken >= sampleLimit;
  // An image whose every pixel converged early counts as having the samples
  // renders stop at, so that clients comparing 'numSamples' with that
  // number (hdAnari, with 'sampleLimit') see it done
  const int numSamples =
      allConverged ? std::max(m_samplesTaken, sampleLimit) : m_samplesTaken;
  m_refinementProgress = std::min(float(numSamples) / float(sampleLimit), 1.f);
  m_numSamples = numSamples;
}

// PathTracer definitions /////////////////////////////////////////////////////

PathTracer::PathTracer(HalcyonDeviceGlobalState *s) : Renderer(s)
{
  // helium only finalizes objects with changed parameters
  updatePathTracerISPC();
}

void PathTracer::commitParameters()
{
  Renderer::commitParameters();
  m_mode = pathTracerModeFromString(getParamString("mode", "default"));
  m_pixelSamples = std::max(getParam<int32_t>("pixelSamples", 1), 1);
  m_maxBounces =
      std::max(getParam<int32_t>("maxBounces", DEFAULT_MAX_BOUNCES), 0);
  // negative values clamped to 0, as the lights' 'color' and brightness
  m_ambientColor =
      linalg::max(getParam<float3>("ambientColor", float3(1.f)), float3(0.f));
  // khr_renderer_ambient_light's default
  m_ambientRadiance = std::max(getParam<float>("ambientRadiance", 0.f), 0.f);
  m_randomNumberSeed = getParam<int32_t>("randomNumberSeed", -1);
  m_lightSamplesPerHit = std::max(
      getParam<int32_t>("lightSamplesPerHit", DEFAULT_LIGHT_SAMPLES_PER_HIT),
      1);
  m_minBouncesBeforeRR = std::max(
      getParam<int32_t>("minBouncesBeforeRR", DEFAULT_MIN_BOUNCES_BEFORE_RR),
      0);
  m_fireflyClampThreshold =
      getParam<float>("fireflyClampThreshold", DEFAULT_FIREFLY_CLAMP_THRESHOLD);
  m_disableShadows = getParam<bool>("disableShadows", false);
  m_enableCaustics = getParam<bool>("enableCaustics", false);
  // not clamped: 0 or less doesn't clamp, as in Typhoon
  m_causticsClampThreshold = getParam<float>(
      "causticsClampThreshold", DEFAULT_CAUSTICS_CLAMP_THRESHOLD);
  m_nativeMaterials = getParam<bool>("nativeMaterials", true);
  m_convergedSamplesPerPixel =
      std::max(getParam<int32_t>("convergedSamplesPerPixel",
                   DEFAULT_CONVERGED_SAMPLES_PER_PIXEL),
          1);
  commitSampleLimit();
  // Typhoon clamps a negative threshold to 0 where it reads it
  m_adaptiveThreshold = std::max(
      getParam<float>("adaptiveThreshold", DEFAULT_ADAPTIVE_THRESHOLD), 0.f);
  // not clamped: a negative value turns adaptive sampling off, as Typhoon
  // compares it as unsigned
  m_minSamplesBeforeAdaptive = getParam<int32_t>(
      "minSamplesBeforeAdaptive", DEFAULT_MIN_SAMPLES_BEFORE_ADAPTIVE);
  m_resolutionDivider = std::max(
      getParam<int32_t>("resolutionDivider", DEFAULT_RESOLUTION_DIVIDER), 0);
}

void PathTracer::commitSampleLimit()
{
  // VisRTX's alias, which decides when set (an INT32); negative values
  // clamped to 0 (no limit). hdAnari forwards it always, and waits for
  // 'numSamples' to reach it, so a 'convergedSamplesPerPixel' that stopped
  // refinement before it would leave hdAnari waiting forever
  const bool limitSet = hasParam("sampleLimit", ANARI_INT32);
  const int sampleLimit =
      limitSet ? std::max(getParam<int32_t>("sampleLimit", 0), 0) : -1;
  m_sampleLimit = limitSet ? sampleLimit : m_convergedSamplesPerPixel;
  // hdAnari also forwards 'convergedSamplesPerPixel' at its default unless
  // the user changed it: only a changed one that loses warns
  if (!limitSet || sampleLimit == m_convergedSamplesPerPixel
      || m_convergedSamplesPerPixel == DEFAULT_CONVERGED_SAMPLES_PER_PIXEL) {
    m_warnedLimits = {-1, -1};
    return;
  }
  const std::pair<int, int> limits{sampleLimit, m_convergedSamplesPerPixel};
  if (limits == m_warnedLimits)
    return;
  m_warnedLimits = limits;
  reportMessage(ANARI_SEVERITY_WARNING,
      "'sampleLimit' (%d) is set, so it is the sample limit; "
      "'convergedSamplesPerPixel' (%d) doesn't stop refinement",
      sampleLimit,
      m_convergedSamplesPerPixel);
}

void PathTracer::finalize()
{
  Renderer::finalize();
  updatePathTracerISPC();
}

void PathTracer::updatePathTracerISPC()
{
  const float3 ambient = m_ambientColor * m_ambientRadiance;
  const bool ambientEmits =
      (ambient.x > 0.f || ambient.y > 0.f || ambient.z > 0.f)
      && std::isfinite(ambient.x) && std::isfinite(ambient.y)
      && std::isfinite(ambient.z);
  if (ambientEmits) {
    m_ambientLight.resize(ispc::sizeof_Light());
    ispc::Light_setAmbient(m_ambientLight.data(), &ambient.x);
  } else {
    m_ambientLight.clear();
  }
  // what the C++ materials' closures weigh lobes by, for native kinds
  const mxcpp::Vec3f luminance = pathtracer::luminanceCoefficients();
  ispc::PathTracer_setParameters(getIE(),
      ispc::PathTracerMode(m_mode),
      m_pixelSamples,
      m_maxBounces,
      m_ambientLight.empty() ? nullptr : m_ambientLight.data(),
      m_randomNumberSeed,
      m_lightSamplesPerHit,
      m_minBouncesBeforeRR,
      m_fireflyClampThreshold,
      m_disableShadows,
      m_enableCaustics,
      m_causticsClampThreshold,
      m_nativeMaterials,
      &luminance[0],
      // the differentials' footprint is that of one of the samples renders
      // stop at; without a limit, of 'convergedSamplesPerPixel'
      m_sampleLimit > 0 ? m_sampleLimit : m_convergedSamplesPerPixel,
      m_adaptiveThreshold,
      m_minSamplesBeforeAdaptive);
}

bool PathTracer::wantsRender(
    const RendererFrameState *state, bool changed) const
{
  // whatever the frame's 'accumulation' says (like VisRTX; a deliberate
  // difference from KHR_FRAME_ACCUMULATION's opt-in, see AGENTS.md)
  return changed
      || !static_cast<const PathTracerFrameState *>(state)->m_refined;
}

int PathTracer::startStride(uint2 size) const
{
  const uint32_t limit =
      std::min(uint32_t(m_resolutionDivider), std::max(size.x, size.y));
  int stride = 1;
  while (2u * uint32_t(stride) <= limit)
    stride *= 2;
  return stride;
}

std::unique_ptr<RendererFrameState> PathTracer::newFrameState() const
{
  return std::make_unique<PathTracerFrameState>();
}

void PathTracer::render(
    Frame &frame, RendererFrameState *state, bool changed) const
{
  auto &refinement = *static_cast<PathTracerFrameState *>(state);
  // a change starts the accumulation over, with the frame's size and
  // channels as of the change, from the coarsest grid
  if (changed) {
    refinement.m_samplesTaken = 0;
    refinement.m_refined = false;
    const uint2 size = frame.size();
    refinement.resize(size_t(size.x) * size.y,
        frame.hasAlbedoChannel(),
        frame.hasNormalChannel());
    refinement.m_stride = startStride(size);
    refinement.m_tracedStride = 0;
  }
  // every pass of the first render is the first render to the kernel
  const bool firstRender = refinement.m_stride > 0;
  refinement.m_accumId = firstRender ? 0 : refinement.m_accumId + 1;
  const int stride = firstRender ? refinement.m_stride : 1;
  ispc::PathTracerFrameState_setAccumulation(
      refinement.ispc(), refinement.m_accumId, refinement.m_samplesTaken);
  ispc::PathTracerFrameState_setPass(
      refinement.ispc(), stride, refinement.m_tracedStride);

  frame.runPass(
      [&](int x, int y) {
        ispc::PathTracer_renderFrame(frame.getIE(), refinement.ispc(), x, y);
      },
      stride);

  if (stride > 1) {
    // a coarse pass: the image at its resolution, and no samples in every
    // pixel yet
    frame.runPass([&](int x, int y) {
      ispc::PathTracer_fillFrame(frame.getIE(), refinement.ispc(), x, y);
    });
    refinement.m_tracedStride = stride;
    refinement.m_stride = stride / 2;
    refinement.m_numSamples = 0;
    refinement.m_refinementProgress = 0.f;
    return;
  }

  refinement.m_stride = 0;
  refinement.m_tracedStride = 0;
  refinement.updateRefinement(m_pixelSamples, m_sampleLimit);
}

const char *PathTracer::extensionSubtype() const
{
  return "quality";
}

} // namespace halcyon
