// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "renderer/Renderer.h"
#include "world/World.h"

namespace halcyon {

// The 'preview' renderer (which 'default' gets too): helide's eye-lit
// shading, ray-marched volumes and debug modes (Preview.ispc), with
// screen-space ambient occlusion (docs/adr/0001) and compositing through
// surfaces that aren't opaque. Not progressive: a render with nothing changed
// is skipped.
struct Preview : public Renderer
{
  Preview(HalcyonDeviceGlobalState *s);

  void commitParameters() override;
  void finalize() override;

  // A frame's ambient occlusion buffers (PreviewFrameState)
  std::unique_ptr<RendererFrameState> newFrameState() const override;
  // The ambient occlusion passes where the image reads the occlusion factor
  // and the world has surfaces to occlude (each over the whole frame before
  // the next starts), then the shading pass
  void render(
      Frame &frame, RendererFrameState *state, bool changed) const override;

 protected:
  const char *extensionSubtype() const override;

 private:
  void updatePreviewISPC();
  // Whether the image reads the occlusion factor: the mode shows it, or is
  // 'default' with 'ambientOcclusion' set; never at an
  // 'ambientOcclusionIntensity' of 0, which gives a factor of 1
  bool readsOcclusionFactor() const;
  // The occlusion radius: 'ambientOcclusionRadius', or if that is 0, a
  // fraction of the diagonal of the world's bounds as of its last scene
  // update; 0 (no occlusion) for a world with empty bounds
  float occlusionRadius(const World &world) const;

  // 1 until committed: 'ignoreAmbientLighting' defaults to true
  float m_ambientRadiance{1.f};
  float m_eyeLightBlendRatio{0.5f};
  float m_volumeSamplingRate{0.5f};
  // 'mode' as an ispc::RenderMode
  int m_mode{0};
  bool m_ambientOcclusion{false};
  // negative values clamp to 0, which derives the radius from the world
  float m_ambientOcclusionRadius{0.f};
  // 'ambientOcclusionEstimator' as an ispc::AmbientOcclusionEstimator
  int m_ambientOcclusionEstimator{0};
  // negative values clamp to 0, which gives an occlusion factor of 1
  float m_ambientOcclusionIntensity{1.f};
  // values outside [1, 5] clamp into it without a warning
  int m_ambientOcclusionBlurPasses{1};
};

} // namespace halcyon
