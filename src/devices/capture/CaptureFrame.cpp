// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "CaptureFrame.h"

#include "CaptureWriter.h"
// std
#include <algorithm>
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
  if (writeString(m_captureText, name, "capture", type, ptr, size))
    return true;

  // The blobs of the last capture, for a capture that is compared in memory
  if (name == "capture.blobs" || name == "capture.blobs.size") {
    std::string names;
    for (const auto &blob : m_blobs)
      names += blob.first + "\n";
    return writeString(names, name, "capture.blobs", type, ptr, size);
  }
  constexpr std::string_view blobPrefix = "capture.blob.";
  if (name.substr(0, blobPrefix.size()) == blobPrefix) {
    const bool wantsSize =
        name.size() > 5 && name.substr(name.size() - 5) == ".size";
    const auto blobName = std::string(name.substr(blobPrefix.size(),
        name.size() - blobPrefix.size() - (wantsSize ? 5 : 0)));
    auto blob = m_blobs.find(blobName);
    if (blob == m_blobs.end())
      return false;
    // Raw bytes, not a C string: the size is the number of bytes, exactly
    if (wantsSize && type == ANARI_UINT64) {
      helium::writeToVoidP(ptr, uint64_t(blob->second.size()));
      return true;
    }
    if (!wantsSize && type == ANARI_STRING) {
      std::memcpy(ptr,
          blob->second.data(),
          std::min<uint64_t>(blob->second.size(), size));
      return true;
    }
  }
  return false;
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
  m_blobs = std::move(capture.blobs);
  ++m_renders;
  for (const auto &warning : capture.warnings)
    reportMessage(ANARI_SEVERITY_WARNING, "%s", warning.c_str());

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
