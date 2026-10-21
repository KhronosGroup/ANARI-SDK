// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "ContextGroups.h"
#include "HalcyonTextureSystem.h"
#include "PathMaterial_ispc.h"
// typhoon
#include <renderer/materials/MaterialXCpp/nodes/helpers/spaceHelpers.h>
// std
#include <string>

namespace halcyon::pathtracer {

const char *const CONTEXT_GROUPS_AUDITED_COMMIT =
    "70c45e83b08a7b45588cd6cdbd2ae67f5147befb";

namespace {

constexpr uint32_t DISPLAY_COLOR = ispc::PATH_CONTEXT_DISPLAY_COLOR;
constexpr uint32_t PRIMITIVE = ispc::PATH_CONTEXT_PRIMITIVE;
constexpr uint32_t SURFACE_DERIVATIVES = ispc::PATH_CONTEXT_SURFACE_DERIVATIVES;
constexpr uint32_t SCREEN_DERIVATIVES = ispc::PATH_CONTEXT_SCREEN_DERIVATIVES;
constexpr uint32_t TRANSFORMS = ispc::PATH_CONTEXT_TRANSFORMS;

struct NodeReads
{
  // A node type, or the start of node types' names if it ends in '_'
  const char *type;
  uint32_t groups;
  // The node reads them only while this input is unconnected
  const char *unlessConnected{nullptr};
};

// The audit at CONTEXT_GROUPS_AUDITED_COMMIT: every read of the grouped
// fields in external/typhoon/src/renderer/materials (MaterialXCpp and BSDL,
// tests aside) and the node types that reach it. The material models (glTF
// PBR, OpenPBR, standard surface, UsdPreviewSurface, ... in materials/), the
// BSDFs, graph evaluation, ResolveGraphNormal() and the nodes not listed read
// none. The geometric nodes that transform between spaces are in
// geometricReads().
const NodeReads NODE_READS[] = {
    // geometricNodes.cpp
    {"ND_geomcolor_", DISPLAY_COLOR},
    // finite differences of 'height' over du and dv (OffsetContextDu/Dv()),
    // steps from the texture coordinates' screen-space derivatives
    {"ND_bump_", SURFACE_DERIVATIVES | SCREEN_DERIVATIVES},
    // dPdu as the default tangent
    {"ND_normalmap_", SURFACE_DERIVATIVES},
    // procedural2dNodes.cpp: antialiased steps (dudx, dudy; dvdx, dvdy)
    {"ND_splitlr_", SCREEN_DERIVATIVES},
    {"ND_splittb_", SCREEN_DERIVATIVES},
    // convolutionNodes.cpp: the height evaluated one pixel over
    // (OffsetContextDx/Dy()); ND_blur_* only widens textureBlur
    {"ND_heighttonormal_", SCREEN_DERIVATIVES},
    // textureNodes.cpp: texture coordinates one pixel over (OffsetContextDx/
    // Dy()) for the request's derivatives; the glTF normal map's frame from
    // dPdu and dPdv. Its node types all count as reading both.
    {"ND_image_", SURFACE_DERIVATIVES | SCREEN_DERIVATIVES},
    {"ND_tiledimage_", SURFACE_DERIVATIVES | SCREEN_DERIVATIVES},
    {"ND_latlongimage", SURFACE_DERIVATIVES | SCREEN_DERIVATIVES},
    {"ND_hextiledimage_", SURFACE_DERIVATIVES | SCREEN_DERIVATIVES},
    {"ND_hextilednormalmap_", SURFACE_DERIVATIVES | SCREEN_DERIVATIVES},
    {"ND_gltf_colorimage", SURFACE_DERIVATIVES | SCREEN_DERIVATIVES},
    {"ND_gltf_image_", SURFACE_DERIVATIVES | SCREEN_DERIVATIVES},
    {"ND_gltf_iridescence_thickness_",
        SURFACE_DERIVATIVES | SCREEN_DERIVATIVES},
    {"ND_gltf_anisotropy_image", SURFACE_DERIVATIVES | SCREEN_DERIVATIVES},
    {"ND_gltf_normalmap", SURFACE_DERIVATIVES | SCREEN_DERIVATIVES},
    {"ND_gltf_normalmap_", SURFACE_DERIVATIVES | SCREEN_DERIVATIVES},
    {"UsdUVTexture", SURFACE_DERIVATIVES | SCREEN_DERIVATIVES},
    {"ND_UsdUVTexture", SURFACE_DERIVATIVES | SCREEN_DERIVATIVES},
    {"ND_UsdUVTexture_", SURFACE_DERIVATIVES | SCREEN_DERIVATIVES},
    // texture3dNodes.cpp: positions one pixel over, and the world normal in
    // object space (GetDefaultObjectNormal(), and the object-space normal
    // node Compile() adds)
    {"ND_triplanarprojection_", SCREEN_DERIVATIVES | TRANSFORMS},
    // mathNodes.cpp: TransformNamedVec3()
    {"ND_transformpoint_", TRANSFORMS},
    {"ND_transformvector_", TRANSFORMS},
    {"ND_transformnormal_", TRANSFORMS},
    // nprNodes.cpp: the world position (TransformNamedVec3())
    {"ND_viewdirection_", TRANSFORMS},
    {"ND_facingratio_", TRANSFORMS},
    {"ND_gooch_shade", TRANSFORMS},
    // pbrNodes.cpp: the world position (TransformNamedVec3())
    {"ND_generalized_schlick_edf", TRANSFORMS},
    // Compile() adds a world tangent node, which falls back to dPdu, to
    // these when their 'tangent' (the hair's 'curve_direction') is
    // unconnected
    {"ND_dielectric_bsdf", SURFACE_DERIVATIVES, "tangent"},
    {"ND_conductor_bsdf", SURFACE_DERIVATIVES, "tangent"},
    {"ND_generalized_schlick_bsdf", SURFACE_DERIVATIVES, "tangent"},
    {"ND_chiang_hair_bsdf", SURFACE_DERIVATIVES, "curve_direction"},
    // procedural2dNodes.cpp, procedural3dNodes.cpp: the world tangent and
    // bitangent nodes Compile() adds
    {"ND_flake2d", SURFACE_DERIVATIVES},
    {"ND_flake3d", SURFACE_DERIVATIVES},
    // halcyon's (HalcyonTextureSystem.cpp): the primitive sampler's index
    {SAMPLER_NODE_TYPE, PRIMITIVE}};

bool matches(const std::string &type, const char *entry)
{
  const std::string e(entry);
  return !e.empty() && e.back() == '_' ? type.compare(0, e.size(), e) == 0
                                       : type == e;
}

bool connected(const mxcpp::GraphNode &node, const char *input)
{
  const auto it = node.inputConnections.find(input);
  if (it == node.inputConnections.end())
    return false;
  for (const mxcpp::GraphConnection &c : it->second) {
    if (!c.upstreamNode.empty())
      return true;
  }
  return false;
}

// The geometric nodes' own space (geometricNodes.cpp): position is in object
// space, the others in world space, and each transforms to its 'space'
// ("object" if unset) with TransformNamedVec3(), which reads the transforms
// unless the two spaces normalise to the same name. A 'space' that is
// connected or not a string counts as another space.
uint32_t geometricReads(const mxcpp::GraphNode &node)
{
  const std::string &type = node.nodeTypeId;
  const char *native = nullptr;
  uint32_t groups = 0;
  if (type == "ND_position_vector3") {
    native = "object";
  } else if (type == "ND_normal_vector3") {
    native = "world";
  } else if (type == "ND_tangent_vector3" || type == "ND_bitangent_vector3") {
    // dPdu and dPdv where the tangent frame is degenerate
    native = "world";
    groups = SURFACE_DERIVATIVES;
  } else {
    return 0;
  }

  std::string space = "object";
  if (connected(node, "space"))
    return groups | TRANSFORMS;
  const auto it = node.parameters.find("space");
  if (it != node.parameters.end()) {
    if (!mxcpp::ValueHolds<std::string>(it->second))
      return groups | TRANSFORMS;
    space = mxcpp::ValueGet<std::string>(it->second);
  }
  const std::string to = mxcpp::NormalizeSpaceName(
      mxcpp::NormalizeSpaceName(space, "object"), "world");
  return to == native ? groups : groups | TRANSFORMS;
}

uint32_t nodeReads(const mxcpp::GraphNode &node)
{
  uint32_t groups = geometricReads(node);
  for (const NodeReads &r : NODE_READS) {
    if (matches(node.nodeTypeId, r.type)
        && !(r.unlessConnected && connected(node, r.unlessConnected)))
      groups |= r.groups;
  }
  return groups;
}

} // namespace

uint32_t contextGroupsRead(const mxcpp::MaterialGraph &graph)
{
  uint32_t groups = 0;
  for (const auto &entry : graph.nodes)
    groups |= nodeReads(entry.second);
  return groups;
}

} // namespace halcyon::pathtracer
