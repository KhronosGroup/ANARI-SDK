// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "BaseFrame.h"

namespace helium {

BaseFrame::BaseFrame(BaseGlobalDeviceState *state)
    : BaseObject(ANARI_FRAME, state)
{}

} // namespace helium