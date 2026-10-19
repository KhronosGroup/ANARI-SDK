// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Preview.h"

#include "Preview_ispc.h"
#include "frame/Frame.h"
// std
#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <utility>

namespace halcyon {

// 'volumeSamplingRate': 0.5, as HalcyonDefinitions.json declares (helide's code
// uses 1, though its definitions say 0.5),
// clamped to the declared range so the ray marching step stays finite
constexpr float DEFAULT_VOLUME_SAMPLING_RATE = 0.5f;
constexpr float MIN_VOLUME_SAMPLING_RATE = 0.001f;
constexpr float MAX_VOLUME_SAMPLING_RATE = 10.f;

// 'ambientOcclusionBlurPasses': 1, the single blur that cancels the occlusion
// pass's rotation pattern; the extra a-trous passes are opt-in, as each costs
// another pass over the frame
constexpr int DEFAULT_AMBIENT_OCCLUSION_BLUR_PASSES = 1;
constexpr int MIN_AMBIENT_OCCLUSION_BLUR_PASSES = 1;
constexpr int MAX_AMBIENT_OCCLUSION_BLUR_PASSES = 5;

// The 'mode' parameter value 'name' as a RenderMode; unknown names (and
// "default") select the default eye-lit shading, as in helide
static ispc::RenderMode renderModeFromString(std::string_view name)
{
  static const std::pair<std::string_view, ispc::RenderMode> modes[] = {
      {"primitiveId", ispc::RENDER_MODE_PRIMITIVE_ID},
      {"objectId", ispc::RENDER_MODE_OBJECT_ID},
      {"instanceId", ispc::RENDER_MODE_INSTANCE_ID},
      {"embreePrimID", ispc::RENDER_MODE_EMBREE_PRIM_ID},
      {"embreeGeomID", ispc::RENDER_MODE_EMBREE_GEOM_ID},
      {"embreeInstID", ispc::RENDER_MODE_EMBREE_INST_ID},
      {"Ng", ispc::RENDER_MODE_NG},
      {"Ng.abs", ispc::RENDER_MODE_NG_ABS},
      {"Ns", ispc::RENDER_MODE_NS},
      {"Ns.abs", ispc::RENDER_MODE_NS_ABS},
      {"Ns.flipped", ispc::RENDER_MODE_NS_FLIPPED},
      {"uvw", ispc::RENDER_MODE_UVW},
      {"backface", ispc::RENDER_MODE_BACKFACE},
      {"hitSurface", ispc::RENDER_MODE_HIT_SURFACE},
      {"hitVolume", ispc::RENDER_MODE_HIT_VOLUME},
      {"geometry.attribute0", ispc::RENDER_MODE_GEOMETRY_ATTRIBUTE_0},
      {"geometry.attribute1", ispc::RENDER_MODE_GEOMETRY_ATTRIBUTE_1},
      {"geometry.attribute2", ispc::RENDER_MODE_GEOMETRY_ATTRIBUTE_2},
      {"geometry.attribute3", ispc::RENDER_MODE_GEOMETRY_ATTRIBUTE_3},
      {"geometry.color", ispc::RENDER_MODE_GEOMETRY_COLOR},
      {"opacityHeatmap", ispc::RENDER_MODE_OPACITY_HEATMAP},
      {"ambientOcclusion", ispc::RENDER_MODE_AMBIENT_OCCLUSION},
      {"ambientOcclusion.unblurred",
          ispc::RENDER_MODE_AMBIENT_OCCLUSION_UNBLURRED},
      {"testFrame", ispc::RENDER_MODE_TEST_FRAME},
  };
  for (const auto &[n, mode] : modes) {
    if (n == name)
      return mode;
  }
  return ispc::RENDER_MODE_DEFAULT;
}

// The 'ambientOcclusionEstimator' value 'name' as an
// AmbientOcclusionEstimator, none if 'name' is unknown
static std::optional<ispc::AmbientOcclusionEstimator>
ambientOcclusionEstimatorFromString(std::string_view name)
{
  if (name == "g3d")
    return ispc::AMBIENT_OCCLUSION_ESTIMATOR_G3D;
  if (name == "sao")
    return ispc::AMBIENT_OCCLUSION_ESTIMATOR_SAO;
  return std::nullopt;
}

// The fraction of the world bounds' diagonal that an
// 'ambientOcclusionRadius' of 0 stands for
constexpr float DERIVED_OCCLUSION_RADIUS_FRACTION = 0.03f;

// PreviewFrameState definitions //////////////////////////////////////////////

// A frame's ambient occlusion buffers and their ISPC struct (struct
// PreviewFrameState in Preview.ispc), which the kernels of the occlusion,
// blur and shading passes take
struct PreviewFrameState : public RendererFrameState
{
  PreviewFrameState();

  // Sizes the ambient occlusion buffers for 'numPixels', or frees them for
  // 0, and the intermediate blur buffer too if 'blurPasses' is above 1 (frees
  // it otherwise); 'reporter' reports a DEBUG message when it allocates or
  // frees either
  void resize(
      const helium::BaseObject &reporter, size_t numPixels, int blurPasses);
  // Runs 'blurPasses' blur passes: the first over the unblurred factor, each
  // later one over the previous one's result, alternating between the
  // intermediate buffer and the blurred one so the last writes the blurred
  // one
  void runBlurPasses(Frame &frame, int blurPasses);
  void *ispc();

  std::vector<uint8_t> m_ispc;
  std::vector<uint8_t> m_hitSurface;
  std::vector<float> m_hitDistance;
  std::vector<float> m_hitNormal; // packed float3
  std::vector<float> m_unblurredOcclusion;
  std::vector<float> m_occlusion;
  // the blur passes' intermediate results, only with more than one pass
  std::vector<float> m_intermediateOcclusion;
};

PreviewFrameState::PreviewFrameState()
    : m_ispc(ispc::sizeof_PreviewFrameState())
{
  ispc::PreviewFrameState_setBuffers(
      ispc(), nullptr, nullptr, nullptr, nullptr, nullptr);
  ispc::PreviewFrameState_setOcclusionRadius(ispc(), 0.f);
}

void *PreviewFrameState::ispc()
{
  return m_ispc.data();
}

void PreviewFrameState::resize(
    const helium::BaseObject &reporter, size_t numPixels, int blurPasses)
{
  const size_t intermediatePixels = blurPasses > 1 ? numPixels : 0;
  if (intermediatePixels == 0 && !m_intermediateOcclusion.empty()) {
    reporter.reportMessage(ANARI_SEVERITY_DEBUG,
        "halcyon::Preview freeing ambient occlusion intermediate blur buffer");
    // free the memory, not only the elements
    m_intermediateOcclusion = {};
  } else if (intermediatePixels > 0) {
    if (m_intermediateOcclusion.empty()) {
      reporter.reportMessage(ANARI_SEVERITY_DEBUG,
          "halcyon::Preview allocating ambient occlusion intermediate blur "
          "buffer");
    }
    m_intermediateOcclusion.resize(intermediatePixels);
  }

  if (numPixels == 0) {
    if (!m_occlusion.empty()) {
      reporter.reportMessage(ANARI_SEVERITY_DEBUG,
          "halcyon::Preview freeing ambient occlusion buffers");
    }
    // free the memory, not only the elements
    m_hitSurface = {};
    m_hitDistance = {};
    m_hitNormal = {};
    m_unblurredOcclusion = {};
    m_occlusion = {};
    ispc::PreviewFrameState_setBuffers(
        ispc(), nullptr, nullptr, nullptr, nullptr, nullptr);
    return;
  }

  if (m_occlusion.empty()) {
    reporter.reportMessage(ANARI_SEVERITY_DEBUG,
        "halcyon::Preview allocating ambient occlusion buffers");
  }
  m_hitSurface.resize(numPixels);
  m_hitDistance.resize(numPixels);
  m_hitNormal.resize(3 * numPixels);
  m_unblurredOcclusion.resize(numPixels);
  m_occlusion.resize(numPixels);
  ispc::PreviewFrameState_setBuffers(ispc(),
      m_hitSurface.data(),
      m_hitDistance.data(),
      m_hitNormal.data(),
      m_unblurredOcclusion.data(),
      m_occlusion.data());
}

void PreviewFrameState::runBlurPasses(Frame &frame, int blurPasses)
{
  // the unblurred factor stays as the occlusion pass left it, for the
  // 'ambientOcclusion.unblurred' mode
  const float *source = m_unblurredOcclusion.data();
  for (int pass = 1; pass <= blurPasses; pass++) {
    // the last pass, and every second one before it, writes the blurred
    // buffer, so no pass reads the buffer it writes
    float *target = (blurPasses - pass) % 2 == 0
        ? m_occlusion.data()
        : m_intermediateOcclusion.data();
    frame.runPass([&](int x, int y) {
      ispc::Preview_renderOcclusionBlur(
          frame.getIE(), ispc(), x, y, pass, source, target);
    });
    source = target;
  }
}

// Preview definitions ////////////////////////////////////////////////////////

Preview::Preview(HalcyonDeviceGlobalState *s) : Renderer(s)
{
  // helium only finalizes objects with changed parameters
  updatePreviewISPC();
}

void Preview::commitParameters()
{
  Renderer::commitParameters();
  // khr_renderer_ambient_light's default (helide's code uses 1)
  m_ambientRadiance = getParam<float>("ambientRadiance", 0.f);
  if (getParam<bool>("ignoreAmbientLighting", true))
    m_ambientRadiance = 1.f;
  m_eyeLightBlendRatio =
      std::clamp(getParam<float>("eyeLightBlendRatio", 0.5f), 0.f, 1.f);
  m_volumeSamplingRate = std::clamp(
      getParam<float>("volumeSamplingRate", DEFAULT_VOLUME_SAMPLING_RATE),
      MIN_VOLUME_SAMPLING_RATE,
      MAX_VOLUME_SAMPLING_RATE);
  m_mode = renderModeFromString(getParamString("mode", "default"));
  m_ambientOcclusion = getParam<bool>("ambientOcclusion", true);
  m_ambientOcclusionRadius =
      std::max(getParam<float>("ambientOcclusionRadius", 0.f), 0.f);
  const std::string estimator =
      getParamString("ambientOcclusionEstimator", "g3d");
  const auto knownEstimator = ambientOcclusionEstimatorFromString(estimator);
  if (!knownEstimator) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "unknown ambientOcclusionEstimator '%s', using 'g3d'",
        estimator.c_str());
  }
  m_ambientOcclusionEstimator =
      knownEstimator.value_or(ispc::AMBIENT_OCCLUSION_ESTIMATOR_G3D);
  m_ambientOcclusionIntensity =
      std::max(getParam<float>("ambientOcclusionIntensity", 1.f), 0.f);
  m_ambientOcclusionBlurPasses =
      std::clamp(getParam<int32_t>("ambientOcclusionBlurPasses",
                     DEFAULT_AMBIENT_OCCLUSION_BLUR_PASSES),
          MIN_AMBIENT_OCCLUSION_BLUR_PASSES,
          MAX_AMBIENT_OCCLUSION_BLUR_PASSES);
}

void Preview::finalize()
{
  Renderer::finalize();
  updatePreviewISPC();
}

void Preview::updatePreviewISPC()
{
  ispc::Preview_setParameters(getIE(),
      m_ambientRadiance,
      m_eyeLightBlendRatio,
      1.f / m_volumeSamplingRate,
      ispc::RenderMode(m_mode),
      ispc::AmbientOcclusionEstimator(m_ambientOcclusionEstimator),
      m_ambientOcclusionIntensity);
}

std::unique_ptr<RendererFrameState> Preview::newFrameState() const
{
  return std::make_unique<PreviewFrameState>();
}

void Preview::render(
    Frame &frame, RendererFrameState *state, bool /*changed*/) const
{
  auto &ao = *static_cast<PreviewFrameState *>(state);
  const uint2 size = frame.size();

  // ambient occlusion: the hit-buffer, occlusion and blur passes, which give
  // the occlusion factor the shading pass reads. They run only if the image
  // reads the factor and the world has surfaces to occlude; otherwise the
  // factor is 1 and the buffers are freed. Whether the world has surfaces,
  // and a derived radius, follow the scene, which the frame has just brought
  // up to date.
  const bool occlusion = readsOcclusionFactor() && frame.world().hasSurfaces();
  ao.resize(*this,
      occlusion ? size_t(size.x) * size.y : 0,
      m_ambientOcclusionBlurPasses);
  if (occlusion) {
    ispc::PreviewFrameState_setOcclusionRadius(
        ao.ispc(), occlusionRadius(frame.world()));
    frame.runPass([&](int x, int y) {
      ispc::Preview_renderHitBuffer(frame.getIE(), ao.ispc(), x, y);
    });
    frame.runPass([&](int x, int y) {
      ispc::Preview_renderOcclusion(frame.getIE(), ao.ispc(), x, y);
    });
    ao.runBlurPasses(frame, m_ambientOcclusionBlurPasses);
  }

  frame.runPass([&](int x, int y) {
    ispc::Preview_renderFrame(frame.getIE(), ao.ispc(), x, y);
  });
}

float Preview::occlusionRadius(const World &world) const
{
  if (m_ambientOcclusionRadius > 0.f)
    return m_ambientOcclusionRadius;

  const auto bounds = world.bounds();
  if (!bounds)
    return 0.f;
  const float3 diagonal = bounds->upper - bounds->lower;
  // an empty world's bounds are empty (Embree's are +inf to -inf)
  if (!(diagonal.x >= 0.f && diagonal.y >= 0.f && diagonal.z >= 0.f))
    return 0.f;
  const float derived =
      DERIVED_OCCLUSION_RADIUS_FRACTION * linalg::length(diagonal);
  return std::isfinite(derived) ? derived : 0.f;
}

bool Preview::readsOcclusionFactor() const
{
  if (!(m_ambientOcclusionIntensity > 0.f))
    return false;
  return (m_ambientOcclusion && m_mode == ispc::RENDER_MODE_DEFAULT)
      || m_mode == ispc::RENDER_MODE_AMBIENT_OCCLUSION
      || m_mode == ispc::RENDER_MODE_AMBIENT_OCCLUSION_UNBLURRED;
}

const char *Preview::extensionSubtype() const
{
  return "preview";
}

} // namespace halcyon
