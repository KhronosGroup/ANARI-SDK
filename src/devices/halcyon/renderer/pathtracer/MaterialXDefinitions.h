// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// MaterialX's data library as the build embedded it
// (external/materialx/CMakeLists.txt generates the definitions from the
// MaterialX the device links): every .mtlx file of its libraries/ directory
// but the shader generators' (gen*/) and targets/.

namespace halcyon::pathtracer {

struct MaterialXDefinitionFile
{
  // Relative to libraries/, e.g. "stdlib/stdlib_defs.mtlx"
  const char *path;
  // The file's XML
  const char *content;
};

extern const MaterialXDefinitionFile MATERIALX_DEFINITION_FILES[];
extern const int MATERIALX_DEFINITION_FILE_COUNT;

} // namespace halcyon::pathtracer
