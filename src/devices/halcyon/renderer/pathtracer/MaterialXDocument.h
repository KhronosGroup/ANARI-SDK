// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// MaterialX documents as MaterialXCpp graphs, for the 'materialx' material
// subtype (HALCYON_MATERIAL_MATERIALX, VisRTX's contract;
// docs/adr/0006-materialx-documents.md). Typhoon never reads a document:
// usdMtlx and Hydra flatten it into a material network, which its
// mxcppAdapter.cpp converts. Halcyon does both steps here, with the MaterialX
// library (whose types this header doesn't show, so the device's own sources
// don't instantiate its symbols):
//
// 1. readMaterialX(): read the document against the embedded data library
//    (MaterialXLibrary.h), select a material, inline the nodes MaterialXCpp
//    lacks (keeping the shading models native), and convert the material's
//    shaders and what they read into a graph, as mxcppAdapter.cpp would
//    (node and input renames, values, colour spaces), with file names
//    resolved into absolute paths.
// 2. applyMaterialXOverrides(): set shader inputs and texture files from the
//    material's other parameters, on the graph, without reading the document
//    again.
//
// Neither throws: problems become diagnostics.

#include "MaterialConverter.h"
// typhoon
#include <renderer/materials/MaterialXCpp/graphTypes.h>
// std
#include <map>
#include <string>
#include <vector>

namespace halcyon::pathtracer {

struct MaterialXDiagnostic
{
  enum class Severity
  {
    ERROR,
    WARNING,
    DEBUG
  };

  Severity severity;
  std::string message;
};

// What 'source' is (the 'sourceType' parameter)
enum class MaterialXSourceType
{
  DOCUMENT_FILE, // a path
  DOCUMENT_INLINE // the document's XML
};

// An input of the material's surface shader (its nodedef's), by name
struct MaterialXShaderInput
{
  // Its MaterialX type: "float", "color3", "vector3", "integer", ...
  std::string type;
  // The nodedef's default, empty if it has none
  mxcpp::Value defaultValue;
};

// A filename input of a node in the graph, which a texture can replace
struct MaterialXTextureInput
{
  // The node's key in the graph and the input's name
  std::string node;
  std::string input;
  // Where a relative replacement path resolves: the directory of the
  // document (or included file) the input is in, the working directory for
  // inline documents
  std::string directory;
};

// A document's selected material, converted
struct MaterialXMaterial
{
  // Whether a material was converted; if not, 'diagnostics' hold an ERROR
  // saying why. A converted graph may still not compile (CppMaterial).
  bool valid{false};
  // Terminals: 'surface' (the surface shader node), 'volume' (a volume
  // shader, which MaterialXCpp makes the surface's interior medium) and
  // 'displacement' (an ND_displacement_float shader, which nothing uses
  // yet); one of 'surface' and 'volume' is there
  mxcpp::MaterialGraph graph;
  // The surface shader's node key in 'graph' (empty for a volume-only
  // material), its nodedef and that nodedef's inputs
  std::string shaderNode;
  std::string shaderNodeDef;
  std::map<std::string, MaterialXShaderInput> shaderInputs;
  // The material nodes of the document that have a surface or volume shader,
  // by name path (the 'materialNames' property), and the one selected
  std::vector<std::string> materialNames;
  std::string materialName;
  // The graph's filename inputs, by their element path in the document,
  // "nodegraph/node/input" (the 'textureInputs' property, in key order)
  std::map<std::string, MaterialXTextureInput> textureInputs;
  std::vector<MaterialXDiagnostic> diagnostics;
};

// Reads 'source' and converts material 'materialName' (its name path or its
// name; empty selects the first), as described above
MaterialXMaterial readMaterialX(MaterialXSourceType type,
    const std::string &source,
    const std::string &materialName);

// One parameter of a 'materialx' material other than sourceType, source and
// materialName: a shader input (by name) or a texture input (by path) to set
struct MaterialXOverride
{
  enum class Kind
  {
    VALUE, // 'value': a float, int, bool, Vec2f, Vec3f or Vec4f
    STRING, // 'string': a path, a string value, or an attribute's name
    SAMPLER // 'sampler', an ISPC Sampler object its owner keeps alive
  };

  std::string name;
  Kind kind{Kind::VALUE};
  mxcpp::Value value;
  std::string string;
  const void *sampler{nullptr};
};

// 'material''s graph with 'overrides' applied, and the samplers it names:
// - on a texture input, a STRING replaces the file (a relative path resolves
//   against the input's document), a SAMPLER the file with its
//   "anari:sampler/<index>" key, which the image node samples with its own
//   texture coordinates (HalcyonTextureSystem::Sample2D());
// - on a shader input, a VALUE of the input's type (FLOAT32 for float,
//   FLOAT32_VEC3 for color3 and vector3, ..., INT32 for integer, BOOL for
//   boolean) or a STRING on a string input replaces its value or
//   connection; a SAMPLER or a STRING naming an attribute ("attribute0",
//   "color", ...) on a float, vector2, vector3, color3, vector4 or color4
//   input connects halcyon's sampler node or the attribute's geomprop node
//   through the components the input takes, as for physicallyBased
//   (MaterialConverter.h), the input's value being the sampler's default;
// - anything else is reported (a WARNING) and ignored.
ConvertedMaterial applyMaterialXOverrides(const MaterialXMaterial &material,
    const std::vector<MaterialXOverride> &overrides,
    std::vector<MaterialXDiagnostic> &diagnostics);

// The OpenUSD colour-space name (ColorManagement.h) MaterialX colour space
// 'name' means: "lin_rec709" is "lin_rec709_scene", "srgb_texture"
// "srgb_rec709_scene", "acescg" "lin_ap1_scene", ...; "" for "none" and
// "raw" (no transform), and 'name' itself for names MaterialX doesn't define
// (OpenUSD's own, say)
std::string openUsdColorSpace(const std::string &name);

} // namespace halcyon::pathtracer
