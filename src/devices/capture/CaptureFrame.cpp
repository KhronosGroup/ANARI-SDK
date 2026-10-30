// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "CaptureFrame.h"

#include "CaptureWriter.h"
#include "helium/helium_math.h"
// std
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

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

  m_size = getParam<anari::math::uint2>("size", anari::math::uint2(0u, 0u));
  m_channels.clear();
  for (const auto &p : committedParams()) {
    if (p.first.rfind("channel.", 0) == 0
        && p.second.type() == ANARI_DATA_TYPE) {
      ANARIDataType type;
      std::memcpy(&type, p.second.data(), sizeof(type));
      m_channels[p.first] = type;
    }
  }

  m_pattern = Pattern();
  m_pattern.solid = getParamString("capture.mode", "checkerboard") == "solid";
  m_pattern.cellSize = std::max(1u, getParam<uint32_t>("capture.cellSize", 8u));
  m_pattern.color0 =
      getParam<anari::math::float3>("capture.color0", m_pattern.color0);
  m_pattern.color1 =
      getParam<anari::math::float3>("capture.color1", m_pattern.color1);
  m_pattern.alpha = getParam<float>("capture.alpha", 1.f);
  m_pattern.depthNear = getParam<float>("capture.depthNear", 0.f);
  m_pattern.depthFar = getParam<float>("capture.depthFar", 1.f);
}

void Frame::finalize() {}

// Writes the last capture and its blobs to the capture directory, if there is
// one: the device's, else ANARI_CAPTURE_DIR
void Frame::writeFiles()
{
  std::string dir = state()->captureDir;
  if (dir.empty()) {
    const char *fromEnv = std::getenv("ANARI_CAPTURE_DIR");
    dir = fromEnv ? fromEnv : "";
  }
  if (dir.empty())
    return;

  namespace fs = std::filesystem;
  auto write = [&](const fs::path &path, const void *data, size_t size) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write((const char *)data, std::streamsize(size));
    out.close();
    if (!out) {
      reportMessage(
          ANARI_SEVERITY_ERROR, "cannot write '%s'", path.string().c_str());
      return false;
    }
    return true;
  };

  try {
    const fs::path root(dir);
    fs::create_directories(root);

    char name[64];
    std::snprintf(
        name, sizeof(name), "frame%04u_render%04u.capture", m_index, m_renders);
    write(root / name, m_captureText.data(), m_captureText.size());

    if (!m_blobs.empty())
      fs::create_directories(root / "blobs");
    for (const auto &blob : m_blobs) {
      // Named by their content: one that exists is the same
      const auto path = root / "blobs" / (blob.first + ".bin");
      if (!fs::exists(path))
        write(path, blob.second.data(), blob.second.size());
    }
  } catch (const fs::filesystem_error &e) {
    reportMessage(ANARI_SEVERITY_ERROR,
        "cannot write captures to '%s': %s",
        dir.c_str(),
        e.what());
  }
}

void Frame::renderFrame()
{
  state()->commitBuffer.flush();

  WriterOptions options;
  options.inlineArrayLimit = state()->inlineArrayLimit;
  options.extensions = state()->extensions;
  Capture capture = writeCapture(*this, options);
  m_captureText = std::move(capture.text);
  m_blobs = std::move(capture.blobs);
  writeFiles();
  ++m_renders;
  for (const auto &warning : capture.warnings)
    reportMessage(ANARI_SEVERITY_WARNING, "%s", warning.c_str());

  if (m_callback)
    invokeCompletionCallback(
        m_callback, m_callbackUserPtr, state()->anariDevice);
}

std::vector<uint8_t> Frame::channelPixels(
    const std::string &channel, ANARIDataType type) const
{
  const size_t width = m_size.x;
  const size_t height = m_size.y;
  std::vector<uint8_t> bytes(width * height * anari::sizeOf(type), 0);

  const auto &pattern = m_pattern;
  auto colorAt = [&](size_t x, size_t y) {
    const bool odd = !pattern.solid
        && (((x / pattern.cellSize) + (y / pattern.cellSize)) % 2) == 1;
    const auto &c = odd ? pattern.color1 : pattern.color0;
    return anari::math::float4(c.x, c.y, c.z, pattern.alpha);
  };

  if (channel == "channel.color" && type == ANARI_FLOAT32_VEC4) {
    for (size_t y = 0; y < height; ++y) {
      for (size_t x = 0; x < width; ++x) {
        const auto c = colorAt(x, y);
        std::memcpy(&bytes[(y * width + x) * sizeof(c)], &c, sizeof(c));
      }
    }
  } else if (channel == "channel.color"
      && (type == ANARI_UFIXED8_VEC4 || type == ANARI_UFIXED8_RGBA_SRGB)) {
    for (size_t y = 0; y < height; ++y) {
      for (size_t x = 0; x < width; ++x) {
        const auto c = colorAt(x, y);
        const uint32_t rgba = type == ANARI_UFIXED8_VEC4
            ? helium::math::cvt_color_to_uint32(c)
            : helium::math::cvt_color_to_uint32_srgb(c);
        std::memcpy(
            &bytes[(y * width + x) * sizeof(rgba)], &rgba, sizeof(rgba));
      }
    }
  } else if (channel == "channel.depth" && type == ANARI_FLOAT32) {
    for (size_t y = 0; y < height; ++y) {
      for (size_t x = 0; x < width; ++x) {
        const float depth = pattern.depthNear
            + (pattern.depthFar - pattern.depthNear)
                * ((float(x) + 0.5f) / float(width));
        std::memcpy(
            &bytes[(y * width + x) * sizeof(depth)], &depth, sizeof(depth));
      }
    }
  }
  return bytes;
}

void *Frame::map(std::string_view channel,
    uint32_t *width,
    uint32_t *height,
    ANARIDataType *pixelType)
{
  *width = 0;
  *height = 0;
  *pixelType = ANARI_UNKNOWN;

  state()->commitBuffer.flush();

  const auto declared = m_channels.find(std::string(channel));
  if (declared == m_channels.end() || m_size.x == 0 || m_size.y == 0)
    return nullptr;

  m_mapped = channelPixels(declared->first, declared->second);
  *width = m_size.x;
  *height = m_size.y;
  *pixelType = declared->second;
  return m_mapped.data();
}

void Frame::unmap(std::string_view) {}

int Frame::frameReady(ANARIWaitMask)
{
  return 1;
}

void Frame::discard() {}

} // namespace capture_device
