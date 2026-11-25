// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "shading/halcyon_objects.isph"
#include "shading/halcyon_util.isph"

// Exported interface /////////////////////////////////////////////////////////

export uniform ulong sizeof_Renderer()
{
  return sizeof(uniform Renderer);
}

export void Renderer_setBackground(void *uniform _self,
    const uniform float *uniform background, /* float4 */
    const uniform float *uniform backgroundImage, /* packed float4 texels */
    uniform uint32 backgroundImageWidth,
    uniform uint32 backgroundImageHeight)
{
  uniform Renderer *uniform self = object_cast<Renderer>(_self);
  self->background = load_float4(background);
  self->backgroundImage = backgroundImage;
  self->backgroundImageSize =
      make_int2(backgroundImageWidth, backgroundImageHeight);
}
