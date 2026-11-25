// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Image files for the pathtracer's texture system (HalcyonTextureSystem.h),
// which MaterialX documents' image nodes name
// (docs/adr/0005-pathtracer-texture-system.md, "File-backed textures"):
// read with stb_image (PNG, JPEG, Radiance HDR, TGA, BMP, binary PNM),
// tinyexr (OpenEXR) or a small reader of ASCII PNM (P2, P3), into float RGBA
// texels with a box-filtered mip chain, and cached per device.

#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace halcyon::pathtracer {

// One mip level: RGBA texels, x fastest, the bottom row first (MaterialX's
// texture coordinates have a lower-left origin, files store the top row
// first)
struct ImageLevel
{
  int width{0};
  int height{0};
  std::vector<float> texels;
};

struct ImageFile
{
  // Level 0 is the image; each next one half the size (at least 1), each
  // texel the mean of the 2 x 2 texels it covers (clamped at odd edges)
  std::vector<ImageLevel> levels;
  // Whether the file has an alpha channel; without one, a texel's alpha is
  // the request's fill value. Grey images are (g, g, g); values are as
  // stored (8 and 16 bits divided by their maximum, no colour conversion,
  // alpha not premultiplied).
  bool hasAlpha{false};
};

// Reads 'path'; nullptr with 'error' set if it can't
std::shared_ptr<const ImageFile> readImageFile(
    const std::string &path, std::string &error);

// The images a device's materials read, by path: an image is read once and
// shared while materials use it (the cache keeps only weak references), and
// read again when its file's modification time changes. Thread-safe.
class ImageFileCache
{
 public:
  // The image of 'path', nullptr if it doesn't read; 'error' then says why,
  // but only the first time for the same file (a warning per file, not per
  // material)
  std::shared_ptr<const ImageFile> load(
      const std::string &path, std::string *error);

 private:
  struct Entry
  {
    std::weak_ptr<const ImageFile> image;
    std::filesystem::file_time_type modified{};
    bool missing{false};
    bool failed{false};
  };

  std::mutex m_mutex;
  std::map<std::string, Entry> m_entries;
};

} // namespace halcyon::pathtracer
