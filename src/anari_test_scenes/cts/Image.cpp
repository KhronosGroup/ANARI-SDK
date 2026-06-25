// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Image.h"
// stb (implementation lives in the stb_image static library)
#include "stb_image.h"
#include "stb_image_write.h"
// std
#include <filesystem>

namespace anari {
namespace cts {

Image loadPNG(const std::string &path)
{
  int w = 0, h = 0, channels = 0;
  // Files are stored top-left; flip on load so row 0 lands at the bottom again,
  // keeping the in-memory Image in ANARI's bottom-left convention. Restore the
  // global flag afterward so we don't leak it into the scene texture loader,
  // which manages this flag itself.
  stbi_set_flip_vertically_on_load(1);
  // Force 4 channels (RGBA) regardless of the file's native channel count.
  unsigned char *pixels = stbi_load(path.c_str(), &w, &h, &channels, 4);
  stbi_set_flip_vertically_on_load(0);
  if (pixels == nullptr || w <= 0 || h <= 0) {
    if (pixels)
      stbi_image_free(pixels);
    return {};
  }

  Image image;
  image.width = static_cast<uint32_t>(w);
  image.height = static_cast<uint32_t>(h);
  const size_t count = static_cast<size_t>(w) * h * 4;
  image.rgba.assign(pixels, pixels + count);
  stbi_image_free(pixels);
  return image;
}

bool savePNG(const std::string &path, const Image &image)
{
  if (!image.valid())
    return false;

  const std::filesystem::path p(path);
  if (p.has_parent_path()) {
    std::error_code ec;
    std::filesystem::create_directories(p.parent_path(), ec);
    if (ec)
      return false;
  }

  // Image rows run bottom-to-top (ANARI convention); flip on write so the PNG
  // file is stored top-left and views right-side-up. Restore the global flag
  // afterward to keep this self-contained.
  const int stride = static_cast<int>(image.width) * 4;
  stbi_flip_vertically_on_write(1);
  const int ok = stbi_write_png(path.c_str(),
      static_cast<int>(image.width),
      static_cast<int>(image.height),
      4,
      image.rgba.data(),
      stride);
  stbi_flip_vertically_on_write(0);
  return ok != 0;
}

} // namespace cts
} // namespace anari
