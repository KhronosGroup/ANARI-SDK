//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef MXCPP_NODES_SHADING_CONTEXT_HELPERS_H
#define MXCPP_NODES_SHADING_CONTEXT_HELPERS_H

#include <renderer/materials/MaterialXCpp/shadingContext.h>

namespace mxcpp {

inline ShadingContext
OffsetContextDx(const ShadingContext& ctx, float scale = 1.0f)
{
    ShadingContext shifted = ctx;
    shifted.position += ctx.dPositiondx * scale;
    shifted.texcoord += Vec2f(ctx.dudx, ctx.dvdx) * scale;
    return shifted;
}

inline ShadingContext
OffsetContextDy(const ShadingContext& ctx, float scale = 1.0f)
{
    ShadingContext shifted = ctx;
    shifted.position += ctx.dPositiondy * scale;
    shifted.texcoord += Vec2f(ctx.dudy, ctx.dvdy) * scale;
    return shifted;
}

inline ShadingContext
OffsetContextDu(const ShadingContext& ctx, float du)
{
    ShadingContext shifted = ctx;
    shifted.position += ctx.dPositiondu * du;
    shifted.texcoord += Vec2f(du, 0.0f);
    return shifted;
}

inline ShadingContext
OffsetContextDv(const ShadingContext& ctx, float dv)
{
    ShadingContext shifted = ctx;
    shifted.position += ctx.dPositiondv * dv;
    shifted.texcoord += Vec2f(0.0f, dv);
    return shifted;
}

}  // namespace mxcpp

#endif
