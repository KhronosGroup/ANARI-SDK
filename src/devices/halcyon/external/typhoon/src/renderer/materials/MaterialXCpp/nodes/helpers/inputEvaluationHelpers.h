//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef MXCPP_NODES_INPUT_EVALUATION_HELPERS_H
#define MXCPP_NODES_INPUT_EVALUATION_HELPERS_H

#include <renderer/materials/MaterialXCpp/paramMap.h>

namespace mxcpp {

template<typename T>
inline T
EvaluateInput(const ParamMap& inputs,
              const SlotName& slot,
              const ShadingContext& ctx,
              const T& defaultValue)
{
    Value value;
    if (inputs.Evaluate(slot, ctx, &value) && ValueHolds<T>(value)) {
        return ValueGet<T>(value);
    }
    return Get<T>(inputs, slot, defaultValue);
}

}  // namespace mxcpp

#endif
