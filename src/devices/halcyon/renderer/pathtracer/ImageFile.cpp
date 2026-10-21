// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "ImageFile.h"
// std
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>

// stb_image, private to this file (external/images)
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_FAILURE_USERMSG
#include <stb_image.h>

// tinyexr with miniz (external/images); its symbols stay in halcyon's static
// libraries, which the device excludes from its dynamic symbols
#define TINYEXR_USE_MINIZ 1
#define TINYEXR_USE_THREAD 0
#define TINYEXR_USE_OPENMP 0
#define TINYEXR_IMPLEMENTATION
#include <miniz.h>
#include <tinyexr.h>

namespace halcyon::pathtracer {

namespace {

// 'channels' values per texel, as floats, top row first, into level 0 (RGBA,
// bottom row first)
template <typename T>
ImageLevel toLevel(
    const T *data, int width, int height, int channels, float scale)
{
  ImageLevel level;
  level.width = width;
  level.height = height;
  level.texels.resize(size_t(width) * height * 4);
  for (int y = 0; y < height; y++) {
    const T *row = data + size_t(height - 1 - y) * width * channels;
    float *out = level.texels.data() + size_t(y) * width * 4;
    for (int x = 0; x < width; x++) {
      const T *in = row + size_t(x) * channels;
      float v[4];
      switch (channels) {
      case 1:
        v[0] = v[1] = v[2] = float(in[0]) * scale;
        v[3] = 1.f;
        break;
      case 2:
        v[0] = v[1] = v[2] = float(in[0]) * scale;
        v[3] = float(in[1]) * scale;
        break;
      case 3:
        v[0] = float(in[0]) * scale;
        v[1] = float(in[1]) * scale;
        v[2] = float(in[2]) * scale;
        v[3] = 1.f;
        break;
      default:
        for (int c = 0; c < 4; c++)
          v[c] = float(in[c]) * scale;
        break;
      }
      std::copy(v, v + 4, out + 4 * size_t(x));
    }
  }
  return level;
}

// The next mip level of 'src'
ImageLevel halve(const ImageLevel &src)
{
  ImageLevel dst;
  dst.width = std::max(1, src.width / 2);
  dst.height = std::max(1, src.height / 2);
  dst.texels.resize(size_t(dst.width) * dst.height * 4);
  const auto texel = [&](int x, int y) {
    x = std::min(x, src.width - 1);
    y = std::min(y, src.height - 1);
    return src.texels.data() + 4 * (size_t(y) * src.width + x);
  };
  for (int y = 0; y < dst.height; y++) {
    for (int x = 0; x < dst.width; x++) {
      const float *a = texel(2 * x, 2 * y);
      const float *b = texel(2 * x + 1, 2 * y);
      const float *c = texel(2 * x, 2 * y + 1);
      const float *d = texel(2 * x + 1, 2 * y + 1);
      float *out = dst.texels.data() + 4 * (size_t(y) * dst.width + x);
      for (int i = 0; i < 4; i++)
        out[i] = 0.25f * (a[i] + b[i] + c[i] + d[i]);
    }
  }
  return dst;
}

// The next token of an ASCII PNM header or body, skipping comments
bool pnmToken(std::istream &in, std::string &token)
{
  token.clear();
  char c;
  while (in.get(c)) {
    if (c == '#') {
      std::string comment;
      std::getline(in, comment);
      if (!token.empty())
        return true;
      continue;
    }
    if (std::isspace(static_cast<unsigned char>(c))) {
      if (!token.empty())
        return true;
      continue;
    }
    token.push_back(c);
  }
  return !token.empty();
}

// ASCII PGM (P2) and PPM (P3), which stb_image doesn't read; false (and
// 'error') for other files
bool readAsciiPnm(const std::string &path, ImageFile &image, std::string &error)
{
  std::ifstream in(path, std::ios::binary);
  std::string magic;
  if (!in || !pnmToken(in, magic) || (magic != "P2" && magic != "P3"))
    return false;
  const int channels = magic == "P3" ? 3 : 1;
  std::string token;
  long width = 0, height = 0, maxValue = 0;
  try {
    if (!pnmToken(in, token))
      throw std::runtime_error("");
    width = std::stol(token);
    if (!pnmToken(in, token))
      throw std::runtime_error("");
    height = std::stol(token);
    if (!pnmToken(in, token))
      throw std::runtime_error("");
    maxValue = std::stol(token);
  } catch (...) {
    error = "bad ASCII PNM header";
    return true;
  }
  if (width <= 0 || height <= 0 || maxValue <= 0 || maxValue > 65535
      || width * height > (1l << 28)) {
    error = "bad ASCII PNM header";
    return true;
  }
  std::vector<float> values(size_t(width) * height * channels);
  for (float &v : values) {
    long value = 0;
    try {
      if (!pnmToken(in, token))
        throw std::runtime_error("");
      value = std::stol(token);
    } catch (...) {
      error = "ASCII PNM data ends early";
      return true;
    }
    v = float(value);
  }
  image.levels.push_back(toLevel(
      values.data(), int(width), int(height), channels, 1.f / float(maxValue)));
  image.hasAlpha = false;
  return true;
}

bool readExr(const std::string &path, ImageFile &image, std::string &error)
{
  EXRVersion version;
  if (ParseEXRVersionFromFile(&version, path.c_str()) != TINYEXR_SUCCESS)
    return false;
  // whether it has alpha
  EXRHeader header;
  InitEXRHeader(&header);
  const char *message = nullptr;
  bool alpha = false;
  if (ParseEXRHeaderFromFile(&header, &version, path.c_str(), &message)
      == TINYEXR_SUCCESS) {
    for (int c = 0; c < header.num_channels; c++)
      alpha = alpha || std::string(header.channels[c].name) == "A";
    FreeEXRHeader(&header);
  } else if (message) {
    FreeEXRErrorMessage(message);
    message = nullptr;
  }

  float *rgba = nullptr;
  int width = 0, height = 0;
  if (LoadEXR(&rgba, &width, &height, path.c_str(), &message)
      != TINYEXR_SUCCESS) {
    error = message ? message : "tinyexr can't read it";
    if (message)
      FreeEXRErrorMessage(message);
    return true;
  }
  image.levels.push_back(toLevel(rgba, width, height, 4, 1.f));
  image.hasAlpha = alpha;
  free(rgba);
  return true;
}

} // namespace

std::shared_ptr<const ImageFile> readImageFile(
    const std::string &path, std::string &error)
{
  auto image = std::make_shared<ImageFile>();
  error.clear();

  // each reader declines files that aren't its format
  if (!readExr(path, *image, error) && !readAsciiPnm(path, *image, error)) {
    // stb_image: Radiance HDR as floats, 16-bit PNG and PNM as 16 bits,
    // the rest as 8 bits; values as stored (no gamma)
    int width = 0, height = 0, channels = 0;
    if (stbi_is_hdr(path.c_str())) {
      float *data = stbi_loadf(path.c_str(), &width, &height, &channels, 0);
      if (data) {
        image->levels.push_back(toLevel(data, width, height, channels, 1.f));
        stbi_image_free(data);
      }
    } else if (stbi_is_16_bit(path.c_str())) {
      stbi_us *data = stbi_load_16(path.c_str(), &width, &height, &channels, 0);
      if (data) {
        image->levels.push_back(
            toLevel(data, width, height, channels, 1.f / 65535.f));
        stbi_image_free(data);
      }
    } else {
      stbi_uc *data = stbi_load(path.c_str(), &width, &height, &channels, 0);
      if (data) {
        image->levels.push_back(
            toLevel(data, width, height, channels, 1.f / 255.f));
        stbi_image_free(data);
      }
    }
    if (image->levels.empty()) {
      const char *reason = stbi_failure_reason();
      error = reason ? reason : "stb_image can't read it";
    }
    image->hasAlpha = channels == 2 || channels == 4;
  }
  if (image->levels.empty()) {
    if (error.empty())
      error = "can't read it";
    return nullptr;
  }

  while (image->levels.back().width > 1 || image->levels.back().height > 1)
    image->levels.push_back(halve(image->levels.back()));
  return image;
}

std::shared_ptr<const ImageFile> ImageFileCache::load(
    const std::string &path, std::string *error)
{
  std::error_code ec;
  const auto modified = std::filesystem::last_write_time(path, ec);

  const bool missing = bool(ec);

  std::lock_guard<std::mutex> lock(m_mutex);
  const auto it = m_entries.find(path);
  const bool known = it != m_entries.end();
  Entry &entry = m_entries[path];
  // the file as last seen: missing again, or unmodified
  const bool same = known
      && (missing ? entry.missing
                  : !entry.missing && entry.modified == modified);
  if (same) {
    if (auto image = entry.image.lock())
      return image;
    if (entry.failed)
      return nullptr;
  }

  std::string message = "no such file";
  std::shared_ptr<const ImageFile> image =
      missing ? nullptr : readImageFile(path, message);
  entry.image = image;
  entry.modified = modified;
  entry.missing = missing;
  entry.failed = !image;
  if (!image && error)
    *error = message;
  return image;
}

} // namespace halcyon::pathtracer
