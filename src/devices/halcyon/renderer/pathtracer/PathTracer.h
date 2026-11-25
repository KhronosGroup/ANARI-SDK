// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "renderer/Renderer.h"
// std
#include <cstdint>
#include <utility>
#include <vector>

namespace halcyon {

// The 'quality' renderer: a progressive port of Typhoon's path tracer
// (PathTracer.ispc), shading through the pathtracer material interface
// (PathMaterial.isph): next-event estimation of every light with MIS against
// BSDF sampling, Russian roulette, firefly clamping and transparent shadows.
// Materials are Typhoon's C++ closures of MaterialXCpp graphs
// (MaterialConverter.h), matte natively (PathMatte.isph, or through its
// graph with 'nativeMaterials' false), with volumes, interior media and
// subsurface scattering. Renders refine the image
// progressively, with Typhoon's adaptive sampling (AdaptiveSampling.isph);
// with 'resolutionDivider', the first render after a change is spread over
// renders of ever finer grids of pixels (progressive resolution).
struct PathTracer : public Renderer
{
  PathTracer(HalcyonDeviceGlobalState *s);

  void commitParameters() override;
  void finalize() override;

  // Renders go on without a change until the image is done refining: every
  // pixel converged, or the sample limit reached (PathTracerFrameState)
  bool wantsRender(
      const RendererFrameState *state, bool changed) const override;
  // A frame's refinement state (PathTracerFrameState)
  std::unique_ptr<RendererFrameState> newFrameState() const override;
  // The path tracing pass, adding 'pixelSamples' samples to every pixel
  // that isn't converged (all of them after a change, which starts over;
  // with progressive resolution over several renders, PathTracerFrameState)
  void render(
      Frame &frame, RendererFrameState *state, bool changed) const override;

 protected:
  const char *extensionSubtype() const override;

 private:
  // Sets m_sampleLimit from 'sampleLimit' if it is set, else from the
  // committed 'convergedSamplesPerPixel', warning once per pair of values
  // where 'sampleLimit' is set and 'convergedSamplesPerPixel' differs from
  // both it and the default
  void commitSampleLimit();
  void updatePathTracerISPC();
  // The stride of the first pass after a change for a frame of 'size':
  // 'resolutionDivider' rounded down to a power of two (so that every pass's
  // grid holds the next's) and to at most the frame's longer side (beyond
  // which the first pass is one pixel anyway); 1 (no coarse passes) for 0
  // and 1
  int startStride(uint2 size) const;

  // Typhoon's default (renderSettings.h)
  static constexpr int DEFAULT_MAX_BOUNCES = 16;
  static constexpr int DEFAULT_LIGHT_SAMPLES_PER_HIT = 1;
  static constexpr int DEFAULT_MIN_BOUNCES_BEFORE_RR = 2;
  static constexpr float DEFAULT_FIREFLY_CLAMP_THRESHOLD = 20.f;
  static constexpr float DEFAULT_CAUSTICS_CLAMP_THRESHOLD = 5.f;
  static constexpr int DEFAULT_CONVERGED_SAMPLES_PER_PIXEL = 256;
  static constexpr float DEFAULT_ADAPTIVE_THRESHOLD = 0.01f;
  static constexpr int DEFAULT_MIN_SAMPLES_BEFORE_ADAPTIVE = 64;
  // halcyon's, not Typhoon's
  static constexpr int DEFAULT_RESOLUTION_DIVIDER = 4;

  // 'mode' as an ispc::PathTracerMode
  int m_mode{0};
  int m_pixelSamples{1};
  int m_maxBounces{DEFAULT_MAX_BOUNCES};
  float3 m_ambientColor{1.f, 1.f, 1.f};
  float m_ambientRadiance{0.f};
  // The ambient light's record (struct Light, Light_setAmbient()): a dome
  // of radiance m_ambientColor * m_ambientRadiance that camera rays don't
  // see, sampled and gathered like an hdri light of one white texel without
  // a sampling distribution, so uniformly over the sphere (Typhoon's dome
  // light without a texture); empty where it emits nothing
  std::vector<uint8_t> m_ambientLight;
  // Typhoon's default: the scene frame's bits as the seed
  int m_randomNumberSeed{-1};
  int m_lightSamplesPerHit{DEFAULT_LIGHT_SAMPLES_PER_HIT};
  int m_minBouncesBeforeRR{DEFAULT_MIN_BOUNCES_BEFORE_RR};
  float m_fireflyClampThreshold{DEFAULT_FIREFLY_CLAMP_THRESHOLD};
  bool m_disableShadows{false};
  // Typhoon's enableCaustics: off, caustic-class lobes are pruned after a
  // diffuse-like event and a caustic sample ends the path; on, caustic
  // paths go on, their contributions clamped to m_causticsClampThreshold
  bool m_enableCaustics{false};
  float m_causticsClampThreshold{DEFAULT_CAUSTICS_CLAMP_THRESHOLD};
  // Shade materials with a native kind (matte) natively; false sends them
  // through their C++ material, to validate one against the other
  bool m_nativeMaterials{true};
  // Adaptive sampling (Typhoon's): renders stop refining once every pixel
  // has m_sampleLimit samples or is converged, which a pixel is once it has
  // 'minSamplesBeforeAdaptive' samples and its variance passes the
  // 'adaptiveThreshold' test (AdaptiveSampling.isph)
  int m_convergedSamplesPerPixel{DEFAULT_CONVERGED_SAMPLES_PER_PIXEL};
  // The sample limit: 'sampleLimit' (VisRTX's name, which hdAnari reads) if
  // set, else 'convergedSamplesPerPixel'; 0 (from 'sampleLimit') for none
  int m_sampleLimit{DEFAULT_CONVERGED_SAMPLES_PER_PIXEL};
  // The 'sampleLimit' and 'convergedSamplesPerPixel' last warned about, so
  // that commits of the same values warn once
  std::pair<int, int> m_warnedLimits{-1, -1};
  float m_adaptiveThreshold{DEFAULT_ADAPTIVE_THRESHOLD};
  int m_minSamplesBeforeAdaptive{DEFAULT_MIN_SAMPLES_BEFORE_ADAPTIVE};
  // Progressive resolution: the first pass after a change renders at this
  // fraction of the frame's resolution (1 / m_resolutionDivider along x and
  // y), 0 or 1 for none
  int m_resolutionDivider{DEFAULT_RESOLUTION_DIVIDER};
};

} // namespace halcyon
