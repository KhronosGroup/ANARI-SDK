// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "CaptureFrame.h"

#include "CaptureWriter.h"
// std
#include <cstring>

namespace capture_device {

Frame::Frame(CaptureState *s) : helium::BaseFrame(s)
{
  m_index = s->framesCreated++;
}

bool Frame::isValid() const
{
  return true;
}

CaptureState *Frame::state() const
{
  return asCaptureState(deviceState());
}

// Writes 'text' as a string property: the size (with the NUL) as '<name>.size',
// then the string itself as '<name>'
static bool writeString(const std::string &text,
    std::string_view name,
    std::string_view property,
    ANARIDataType type,
    void *ptr,
    uint64_t size)
{
  if (name == property && type == ANARI_STRING) {
    if (size == 0)
      return true;
    std::memset(ptr, 0, size);
    std::memcpy(ptr, text.data(), std::min<uint64_t>(text.size(), size - 1));
    return true;
  }
  if (name.size() == property.size() + 5
      && name.substr(0, property.size()) == property
      && name.substr(property.size()) == ".size" && type == ANARI_UINT64) {
    helium::writeToVoidP(ptr, uint64_t(text.size() + 1));
    return true;
  }
  return false;
}

bool Frame::getProperty(const std::string_view &name,
    ANARIDataType type,
    void *ptr,
    uint64_t size,
    uint32_t)
{
  if (type == ANARI_FLOAT32 && name == "duration") {
    helium::writeToVoidP(ptr, 0.f);
    return true;
  }
  return writeString(m_captureText, name, "capture", type, ptr, size);
}

void Frame::commitParameters()
{
  readCommitted(*this);
  m_callback = getParam<ANARIFrameCompletionCallback>(
      "frameCompletionCallback", nullptr);
  m_callbackUserPtr =
      getParam<const void *>("frameCompletionCallbackUserData", nullptr);
}

void Frame::finalize() {}

void Frame::renderFrame()
{
  state()->commitBuffer.flush();

  WriterOptions options;
  options.inlineArrayLimit = state()->inlineArrayLimit;
  options.extensions = state()->extensions;
  Capture capture = writeCapture(*this, options);
  m_captureText = std::move(capture.text);
  ++m_renders;

  if (m_callback)
    invokeCompletionCallback(
        m_callback, m_callbackUserPtr, state()->anariDevice);
}

void *Frame::map(std::string_view,
    uint32_t *width,
    uint32_t *height,
    ANARIDataType *pixelType)
{
  *width = 0;
  *height = 0;
  *pixelType = ANARI_UNKNOWN;
  return nullptr;
}

void Frame::unmap(std::string_view) {}

int Frame::frameReady(ANARIWaitMask)
{
  return 1;
}

void Frame::discard() {}

} // namespace capture_device
