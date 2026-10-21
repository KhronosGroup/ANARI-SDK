//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "applicationNodes.h"

#include <renderer/materials/MaterialXCpp/nodeRegistry.h>

namespace mxcpp {

static const SlotName _kOut("out");

static void
_EvalFrame(const ParamMap&, const ShadingContext& ctx, NodeOutputMap* outputs)
{
    (*outputs)[_kOut] = Value(ctx.frame);
}

static void
_EvalTime(const ParamMap&, const ShadingContext& ctx, NodeOutputMap* outputs)
{
    (*outputs)[_kOut] = Value(ctx.time);
}

#define _REG(name, fn) reg.Register(name, fn)

void
RegisterApplicationNodes(NodeRegistry& reg)
{
    _REG("ND_frame_float", &_EvalFrame);
    _REG("ND_time_float", &_EvalTime);
}

#undef _REG

}  // namespace mxcpp
