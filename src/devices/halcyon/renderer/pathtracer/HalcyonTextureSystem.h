// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// The pathtracer's texture system: halcyon samplers for MaterialXCpp graphs
// (docs/adr/0005-pathtracer-texture-system.md). Both ways in reach the ISPC
// sampling code the preview renderer runs (shading/halcyon_sampler.isph,
// through HalcyonTextureSystem.ispc's single-lane entry points), so the two
// renderers sample alike:
//
// - The sampler node (SAMPLER_NODE_TYPE), which the graphs of halcyon's
//   materials (MaterialConverter.h) use for a sampler-sourced parameter:
//   the whole sampler (inAttribute's value, which a geomprop node resolves,
//   in and out transforms, every subtype, a primitive sampler's primitive),
//   exactly as the preview renderer's hook.
// - Sample2D(), mxcpp's TextureSystem interface, which MaterialX image nodes
//   call with a 'file' key "anari:sampler/<index>": the sampler with the
//   node's st as its inAttribute's value (s, t, 0, 1). A sampler that takes
//   no input there (a primitive sampler, or inAttribute "none") is Missing.
//
// Halcyon samplers have no mip levels, so the request's derivatives, blur,
// address modes and filter type are ignored: the sampler's own wrap modes and
// filter decide, as in the preview renderer. Their values are taken as they
// are, without colour management (ANARI arrays have no colour space), with
// their coordinates as ANARI's: the first texel at coordinate 0, no flip.
//
// Other keys are image files (ImageFile.h), which the material read when its
// graph was built (imageFiles() lists them): sampled with the same ISPC
// filtering code at the mip level the request's derivatives pick (trilinear
// between two), their t flipped (MaterialX's origin is the lower left), the
// request's address modes (Periodic, Clamp, Mirror; Constant and UseMetadata
// give the default outside [0, 1]) and filter (Closest, else linear: no
// cubic), the filtered colour converted from the request's sourceColorSpace
// into the working space where its data role is Color (as Typhoon's OIIO
// backend does). A file the material couldn't read is Error, with the
// request's default value; its blur and frames are ignored.

#include "ImageFile.h"
// typhoon
#include <renderer/materials/MaterialXCpp/graphTypes.h>
#include <renderer/materials/MaterialXCpp/textureSystem.h>
// std
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace halcyon::pathtracer {

// The node type that evaluates a halcyon sampler in a graph. Inputs:
// 'sampler' (integer), the sampler's index in the material's texture system;
// 'in' (vector4), its inAttribute's value, (0, 0, 0, 1) unconnected;
// 'default' (vector4), the output when the context has no halcyon texture
// system or the sampler is invalid (the preview renderer's fallback to the
// parameter's value). Output 'out' (vector4). Registered with MaterialXCpp's
// node registry by registerTextureNodes().
extern const char *const SAMPLER_NODE_TYPE;

// Adds halcyon's nodes to MaterialXCpp's node registry (and its built-in
// nodes first); thread-safe and idempotent. CppMaterial::compile() calls it.
void registerTextureNodes();

// The image files 'graph' reads: its nodes' string 'file' parameters that
// aren't sampler keys
std::vector<std::string> imageFiles(const mxcpp::MaterialGraph &graph);

// Images by path, as a material read them
using ImageFiles = std::map<std::string, std::shared_ptr<const ImageFile>>;

// The ispc::Attribute sampler 'sampler' (its ISPC object) reads its input
// from, -1 if it reads none (an invalid or primitive sampler, or inAttribute
// "none"): what a sampler node's 'in' connects to
int samplerInputAttribute(const void *sampler);

// A material's samplers, by index: its graph's sampler nodes and
// "anari:sampler/<index>" keys name them. They are ISPC Sampler objects,
// which their owner (Material) keeps alive and rewrites only in finalize().
class HalcyonTextureSystem final : public mxcpp::TextureSystem
{
 public:
  explicit HalcyonTextureSystem(
      std::vector<const void *> samplers, ImageFiles images = {});

  // "anari:sampler/<index>"
  static std::string samplerKey(int index);
  // The index key 'key' names, -1 if it doesn't name a sampler
  static int samplerIndex(const std::string &key);

  // Sampler 'index' for its inAttribute's value 'attribute' at Embree
  // primitive 'primId', into 'result'; false (and 'result' untouched) if
  // there is no such sampler or it is invalid
  bool sample(int index,
      const mxcpp::Vec4f &attribute,
      uint32_t primId,
      mxcpp::Vec4f &result) const;

  mxcpp::Texture2DResult Sample2D(
      const mxcpp::Texture2DRequest &request) const override;

 private:
  mxcpp::Texture2DResult sampleFile(const mxcpp::Texture2DRequest &request,
      const ImageFile &image) const;

  std::vector<const void *> m_samplers;
  ImageFiles m_images;
};

} // namespace halcyon::pathtracer
