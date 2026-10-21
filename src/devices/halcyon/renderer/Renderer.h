// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"
#include "array/Array2D.h"
// std
#include <memory>
#include <vector>

namespace halcyon {

struct Frame;

// A renderer's state for one frame that uses it, which the frame owns (a
// renderer may serve several frames): what its passes keep between them or
// across renders, such as per-pixel buffers
struct RendererFrameState
{
  virtual ~RendererFrameState();
  // A frame property this state reports as of the frame's last render (none
  // by default). An app thread may ask while the worker renders, so what it
  // reads must be safe to read then.
  virtual bool getProperty(const std::string_view &name,
      ANARIDataType type,
      void *ptr,
      uint64_t size,
      uint32_t flags);
};

// The base of the renderer subtypes, 'preview' (renderer/preview/Preview.h)
// and 'quality' (renderer/pathtracer/PathTracer.h): what a frame asks of its
// renderer, which runs every pass of a render itself, and the parameters every
// subtype has ('background', 'taskGrainSizeWidth' and 'taskGrainSizeHeight'),
// which fill the common part of the ISPC struct all subtypes share
struct Renderer : public Object
{
  Renderer(HalcyonDeviceGlobalState *s);
  ~Renderer() override;

  // 'quality' or the 'preview' renderer, which 'default' (the spec's
  // alias of the first renderer listed) gets too; unknown subtypes get an
  // object that is never valid
  static Renderer *createInstance(
      std::string_view subtype, HalcyonDeviceGlobalState *d);

  // Reads the parameters every subtype has; subtypes call it first
  void commitParameters() override;
  // Writes the parameters every subtype has to the ISPC struct; subtypes
  // call it first
  void finalize() override;
  // 'extension': the extensions this renderer honours, as introspection
  // reports them for its subtype (HalcyonDefinitions.json)
  bool getProperty(const std::string_view &name,
      ANARIDataType type,
      void *ptr,
      uint64_t size,
      uint32_t flags) override;

  // Whether the frame renders: by default only if something changed since
  // its last render ('changed', which includes a new 'state'); a renderer that
  // refines its image may go on rendering without a change, whatever the
  // frame's 'accumulation' says, until 'state' is done
  virtual bool wantsRender(const RendererFrameState *state, bool changed) const;
  // A new state for a frame that renders with this renderer (none by
  // default); the frame makes one when it first renders with it
  virtual std::unique_ptr<RendererFrameState> newFrameState() const;
  // Renders 'frame', whose world's scene is up to date: every pass over its
  // pixels (Frame::runPass()). 'state' is what newFrameState() gave the frame;
  // 'changed' says whether anything changed since the frame's last render
  // (or 'state' is new).
  virtual void render(
      Frame &frame, RendererFrameState *state, bool changed) const = 0;
  // Pixels per render task along the frame's width and height
  int2 taskGrainSize() const;

 protected:
  // The subtype whose declared extensions this renderer reports
  virtual const char *extensionSubtype() const = 0;

 private:
  void updateISPC();

  float4 m_background{0.f, 0.f, 0.f, 1.f};
  helium::ChangeObserverPtr<Array2D> m_backgroundImage{this};
  // 'm_backgroundImage' as float4 texels read by ISPC, empty if not set
  std::vector<float4> m_backgroundImageTexels;
  uint2 m_backgroundImageSize{0u, 0u};
  int2 m_taskGrainSize{4, 4};
};

} // namespace halcyon

HALCYON_ANARI_TYPEFOR_SPECIALIZATION(halcyon::Renderer *, ANARI_RENDERER);
