// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// glTF asset-scanning factory. Replaces the old ctsGLTF.py +
// createTestCasesFromGltf.py: instead of generating JSON test descriptors, this
// scans the glTF-Sample-Assets tree at registration time and registers one Test
// per asset, loading the asset with the in-tree C++ glTF loader (gltf2anari.h).
//
// Gated on ENABLE_GLTF, the flag that compiles the glTF loader (set by CMake
// when either CTS_ENABLE_GLTF or VIEWER_ENABLE_GLTF is on). When glTF is not
// built, registerGltfTests is a no-op so the default build and catalog are
// unaffected. Encodings that need optional dependencies (Draco/KTX) are only
// offered when those are built.

#include "Categories.h"

#ifdef ENABLE_GLTF
#include "../BuildContext.h"
#include "../TestBuilder.h"
#include "../WorldBuilder.h"
#include "scenes/file/gltf2anari.h"
// std
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>
#include <vector>
#endif

namespace anari {
namespace cts {

#ifdef ENABLE_GLTF

namespace {

namespace fs = std::filesystem;

// The standard glTF encodings that load without optional dependencies. Draco
// and KTX-based encodings are added only when those features are compiled in.
struct Encoding
{
  const char *dir;
  const char *ext;
};

std::vector<Encoding> loadableEncodings()
{
  std::vector<Encoding> e = {
      {"glTF", ".gltf"}, {"glTF-Binary", ".glb"}, {"glTF-Embedded", ".gltf"}};
#ifdef USE_DRACO
  e.push_back({"glTF-Draco", ".gltf"});
#endif
  return e;
}

// The asset root: an env override, else the source tree location baked in at
// build time.
fs::path assetRoot()
{
  if (const char *env = std::getenv("ANARI_CTS_GLTF_ASSETS"))
    return fs::path(env);
#ifdef CTS_GLTF_ASSET_DIR
  return fs::path(CTS_GLTF_ASSET_DIR);
#else
  return {};
#endif
}

// A filesystem-safe Test name (the runner mirrors category/test into paths).
std::string sanitizeName(const std::string &in)
{
  std::string out;
  out.reserve(in.size());
  for (char c : in)
    out.push_back((c == '/' || c == '\\' || c == ' ') ? '_' : c);
  return out;
}

// Assemble a world from a loaded glTF context (mirrors FileGLTF::commit). The
// loader's gltf_data lives in the global namespace (gltf2anari.h declares no
// namespace; glTF.cpp includes it at global scope likewise).
anari::World worldFromGltf(anari::Device d, ::gltf_data &ctx)
{
  auto world = anari::newObject<anari::World>(d);
  if (!ctx.instances.empty()) {
    anari::setParameterArray1D(
        d, world, "instance", ctx.instances[0].data(), ctx.instances[0].size());
  } else if (!ctx.groups.empty()) {
    std::vector<anari::Instance> instances;
    for (auto group : ctx.groups) {
      auto inst = anari::newObject<anari::Instance>(d, "transform");
      anari::setParameter(d, inst, "group", group);
      anari::commitParameters(d, inst);
      instances.push_back(inst);
    }
    anari::setParameterArray1D(
        d, world, "instance", instances.data(), instances.size());
    for (auto inst : instances)
      anari::release(d, inst);
  }
  anari::commitParameters(d, world);
  return world;
}

} // namespace

void registerGltfTests(Catalog &catalog)
{
  const fs::path root = assetRoot();
  std::error_code ec;
  if (root.empty() || !fs::is_directory(root, ec))
    return;

  const auto encodings = loadableEncodings();

  std::vector<fs::path> assets;
  for (const auto &entry : fs::directory_iterator(root, ec)) {
    if (entry.is_directory(ec))
      assets.push_back(entry.path());
  }
  std::sort(assets.begin(), assets.end());

  for (const auto &assetDir : assets) {
    const std::string asset = assetDir.filename().string();

    // Collect the encodings whose file actually exists for this asset.
    std::vector<std::string> available;
    for (const auto &enc : encodings) {
      const fs::path file = assetDir / enc.dir / (asset + enc.ext);
      if (fs::exists(file, ec))
        available.push_back(enc.dir);
    }
    if (available.empty())
      continue;

    const std::string rootStr = root.string();
    // The default encoding when a Case carries no `encoding` axis value (i.e.
    // the asset ships a single encoding, so no axis was registered). It must be
    // the asset's actual encoding, not a hard-coded "glTF": several assets ship
    // only glTF-Binary, and defaulting to "glTF" would look for a .gltf that
    // isn't there.
    const std::string defaultEnc = available.front();
    auto build = [rootStr, asset, defaultEnc](
                     BuildContext &ctx) -> anari::World {
      auto d = ctx.device();
      const std::string enc = ctx.getString("encoding", defaultEnc);
      const std::string ext = (enc == "glTF-Binary") ? ".glb" : ".gltf";
      const std::string file =
          rootStr + "/" + asset + "/" + enc + "/" + asset + ext;
      // The glTF loader throws on malformed/unreadable JSON, missing buffers,
      // or unsupported encodings. Contain it: one bad asset must become a
      // failed Case, not abort the whole sweep (the runner records a build that
      // returns no world as a failure).
      try {
        ::gltf_data gltf(d);
        gltf.open_file(file);
        return worldFromGltf(d, gltf);
      } catch (const std::exception &e) {
        fprintf(stderr,
            "[cts][gltf] failed to load %s (%s): %s\n",
            asset.c_str(),
            enc.c_str(),
            e.what());
        return nullptr;
      }
    };

    auto t = makeTest("gltf", sanitizeName(asset));
    t.build(build);
    // Each encoding (glTF / glTF-Binary / glTF-Embedded / Draco) is scored
    // against its own ground truth: a Permutation, since the encodings are not
    // guaranteed to render pixel-identically (textures, quantization).
    if (available.size() > 1) {
      std::vector<Any> values;
      for (const auto &e : available)
        values.emplace_back(Any(e.c_str()));
      t.permute("encoding", std::move(values));
    }
    t.registerInto(catalog);
  }
}

} // namespace cts

#else // ENABLE_GLTF

void registerGltfTests(Catalog & /*catalog*/) {}

} // namespace cts

#endif

} // namespace anari
