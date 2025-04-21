// Copyright 2021-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "BaseFrame.h"

namespace helium {

BaseFrame::BaseFrame(BaseGlobalDeviceState *state)
    : BaseObject(ANARI_FRAME, state)
{}

void BaseFrame::on_NoPublicReferences()
{
  if (!frameReady(ANARI_NO_WAIT)) {
    reportMessage(ANARI_SEVERITY_DEBUG, "discarding released frame in-flight");
    discard();
    frameReady(ANARI_WAIT);
  }
}

} // namespace helium

HELIUM_ANARI_TYPEFOR_DEFINITION(helium::BaseFrame *);
