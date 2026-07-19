// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "geometry/Geometry.h"
#include "material/Material.h"

namespace helide_gpu {

struct Surface : public Object
{
  Surface(HelideGPUDeviceGlobalState *s);
  ~Surface() override = default;

  void commitParameters() override;
  void finalize() override;

  uint32_t id() const;
  const Geometry *geometry() const;
  const Material *material() const;

  bool isValid() const override;

  void draw(SDL_GPURenderPass *pass,
      SDL_GPUCommandBuffer *cmd,
      const SurfaceDrawContext &ctx);

 private:
  void pushFragmentUniforms(SDL_GPURenderPass *pass,
      SDL_GPUCommandBuffer *cmd,
      const SurfaceDrawContext &ctx);

  uint32_t m_id{~0u};
  helium::IntrusivePtr<Geometry> m_geometry;
  helium::IntrusivePtr<Material> m_material;
};

} // namespace helide_gpu

HELIDE_GPU_ANARI_TYPEFOR_SPECIALIZATION(helide_gpu::Surface *, ANARI_SURFACE);
