// Copyright 2023-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <cstring>

#define STB_IMAGE_IMPLEMENTATION 1
#define TINYEXR_IMPLEMENTATION 1
#include "external/stb_image/stb_image.h"
#include "external/stb_image/stb_image_write.h"
#ifndef _WIN32
#include "external/tinyexr/tinyexr.h"
#endif

#include "HDRImage.h"

namespace anari_viewer::importers {

bool HDRImage::load(std::string fileName)
{
  if (fileName.size() < 4)
    return false;

  // check the extension
  std::string extension = std::string(strrchr(fileName.c_str(), '.'));
  std::transform(extension.data(),
      extension.data() + extension.size(),
      std::addressof(extension[0]),
      [](unsigned char c) { return std::tolower(c); });

  if (extension != ".hdr" && extension != ".exr")
    return false;

  if (extension == ".hdr") {
    int w, h, n;
    float *imgData = stbi_loadf(fileName.c_str(), &w, &h, &n, STBI_rgb);
    width = w;
    height = h;
    numComponents = n;
    pixel.resize(w * h * n);
    memcpy(pixel.data(), imgData, w * h * n * sizeof(float));
    stbi_image_free(imgData);
    return width > 0 && height > 0
        && (numComponents == 3 || numComponents == 4);
#ifdef _WIN32
  }
#else
  } else {
    int w, h, n;
    float *imgData;
    const char *err;
    int ret = LoadEXR(&imgData, &w, &h, fileName.c_str(), &err);
    if (ret != 0) {
      printf("Error loading EXR: %s\n", err);
      return false;
    }
    n = 4;

    width = w;
    height = h;
    numComponents = n;
    pixel.resize(w * h * n);
    memcpy(pixel.data(), imgData, w * h * n * sizeof(float));
    return width > 0 && height > 0
        && (numComponents == 3 || numComponents == 4);
  }
#endif

  return false;
}

} // namespace anari_viewer::importers
