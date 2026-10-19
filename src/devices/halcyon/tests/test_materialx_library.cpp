// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// MaterialX's data library as the build embeds it
// (renderer/pathtracer/MaterialXLibrary.*, external/materialx): every
// definition file but the shader generators', read without any search path or
// environment variable, holding the nodedefs and node graph implementations
// the 'materialx' subtype's documents need.

#include "renderer/pathtracer/MaterialXDefinitions.h"
#include "renderer/pathtracer/MaterialXLibrary.h"
// std
#include <cstdio>
#include <cstdlib>
#include <string>

namespace mx = MaterialX;
using namespace halcyon::pathtracer;

namespace {

int g_failures = 0;

void check(bool ok, const std::string &name)
{
  std::printf("%s %s\n", ok ? "PASS" : "FAIL", name.c_str());
  if (!ok)
    g_failures++;
}

} // namespace

int main()
{
  // MaterialX's own search falls back on this; the embedded library must not
  // need it
  setenv("MATERIALX_SEARCH_PATH", "/nonexistent", 1);

  bool noGenerators = MATERIALX_DEFINITION_FILE_COUNT > 0;
  bool stdlib = false;
  for (int i = 0; i < MATERIALX_DEFINITION_FILE_COUNT; i++) {
    const std::string path = MATERIALX_DEFINITION_FILES[i].path;
    noGenerators = noGenerators && path.find("/gen") == std::string::npos
        && path.rfind("targets/", 0) != 0;
    stdlib = stdlib || path == "stdlib/stdlib_defs.mtlx";
  }
  std::printf(
      "  %d embedded definition files\n", MATERIALX_DEFINITION_FILE_COUNT);
  check(noGenerators && stdlib,
      "the embedded files are the definitions and graphs, no generators'");

  mx::ConstDocumentPtr library;
  try {
    library = materialXDataLibrary();
  } catch (const std::exception &e) {
    std::printf("  %s\n", e.what());
  }
  check(library != nullptr, "the embedded library reads");
  if (!library)
    return 1;

  const size_t nodeDefs = library->getNodeDefs().size();
  std::printf("  %zu nodedefs, %zu node graphs\n",
      nodeDefs,
      library->getNodeGraphs().size());
  check(nodeDefs > 800, "the library holds MaterialX's nodedefs");

  bool models = true;
  for (const char *name : {"ND_standard_surface_surfaceshader",
           "ND_open_pbr_surface_surfaceshader",
           "ND_gltf_pbr_surfaceshader",
           "ND_UsdPreviewSurface_surfaceshader",
           "ND_disney_principled",
           "ND_image_color3",
           "ND_tiledimage_color3",
           "ND_UsdPrimvarReader_vector2"}) {
    const bool found = library->getNodeDef(name) != nullptr;
    if (!found)
      std::printf("  missing %s\n", name);
    models = models && found;
  }
  check(models, "the shading models' and texture nodes' nodedefs");

  // node graph implementations, which flattening inlines
  check(
      library->getNodeGraph("NG_standard_surface_surfaceshader_100") != nullptr
          && library->getNodeGraph("NG_tiledimage_color3") != nullptr,
      "the node graph implementations");

  check(materialXDataLibrary() == library, "the library is read once");

  std::string message;
  check(library->validate(&message), "the library validates");
  if (!message.empty())
    std::printf("  %s\n", message.substr(0, 400).c_str());

  return g_failures == 0 ? 0 : 1;
}
