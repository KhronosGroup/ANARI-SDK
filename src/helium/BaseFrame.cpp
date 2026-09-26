// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "BaseFrame.h"

namespace helium {

// The frame whose completion callback is running on this thread, if any.
static thread_local const BaseFrame *t_completingFrame = nullptr;

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

bool BaseFrame::completingOnThisThread() const
{
  return t_completingFrame == this;
}

void BaseFrame::invokeCompletionCallback(
    ANARIFrameCompletionCallback cb, const void *userPtr, ANARIDevice device)
{
  if (!cb)
    return;
  const BaseFrame *outer = t_completingFrame;
  t_completingFrame = this;
  cb(userPtr, device, (ANARIFrame)this);
  t_completingFrame = outer;
}

} // namespace helium

HELIUM_ANARI_TYPEFOR_DEFINITION(helium::BaseFrame *);
