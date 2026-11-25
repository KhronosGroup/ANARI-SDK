// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// The C++ side of the pathtracer's material interface (PathMaterial.isph,
// docs/adr/0004-pathtracer-material-interface.md): MATERIAL_KIND_CPP materials
// and the batched calls ISPC makes into them. The records and flags come from
// PathMaterial.isph through the ispc-generated header.

#include "HalcyonTextureSystem.h"
#include "PathMaterial_ispc.h"
// typhoon
#include <renderer/materials/MaterialXCpp/graph.h>
// std
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace halcyon::pathtracer {

// PATH_MATERIAL_MAX_LANES: lanes in each SoA record
constexpr int MAX_LANES =
    int(sizeof(ispc::PathHandleSoA::handle) / sizeof(int));

// The value type a geomprop node reads (its ND_geompropvalue_<type>)
enum class GeomPropType
{
  FLOAT,
  VECTOR2,
  VECTOR3, // also color3
  VECTOR4, // also color4
  INTEGER,
  BOOLEAN,
  UNSUPPORTED // matrix44: no attribute has one
};

// What a geomprop handle of a compiled graph reads: an attribute (an
// ispc::Attribute, -1 for a name that isn't one) as the node's type
struct GeomPropBinding
{
  int attribute{-1};
  GeomPropType type{GeomPropType::UNSUPPORTED};
};

// The ispc::Attribute an attribute name means (helium's
// attributeFromString()), -1 if none
int attributeFromName(const std::string &name);
// The name of ispc::Attribute 'attribute', nullptr for none (-1,
// ATTRIBUTE_NONE)
const char *attributeName(int attribute);

// A material that Typhoon's C++ shades: a compiled MaterialXCpp graph, whose
// closures are BSDL/MaterialXCpp closure trees. An ISPC Material of kind
// MATERIAL_KIND_CPP points at one ('cppMaterial'). It must not change or go
// away while a render may read it, so its owner replaces it only in
// finalize().
//
// Its geomprop nodes (ND_geompropvalue_*) read halcyon attributes: a node's
// geomprop names an attribute ("attribute0", "color", ...), which the
// integrator resolves at the hit like any attribute (getAttribute()) for the
// attributes in attributeMask(), and the shading context's geomprop lookup
// hands it to the node as the node's type (x; xy; xyz; xyzw; int(x);
// x != 0), where the hit has it. Other names, and attributes the hit doesn't
// have, get the node's default.
//
// Its sampler nodes and "anari:sampler/<index>" texture keys name the
// samplers it was compiled with, by index, through its texture system
// (HalcyonTextureSystem.h); their owner keeps them alive.
class CppMaterial
{
 public:
  // nullptr if the graph's surface terminal doesn't compile; 'diagnostic'
  // then says why (and may hold a recoverable authoring problem of a graph
  // that compiled). 'images' are the image files its image nodes read
  // (imageFiles(), HalcyonTextureSystem.h), as read.
  static std::unique_ptr<CppMaterial> compile(const mxcpp::MaterialGraph &graph,
      std::vector<const void *> samplers = {},
      std::string *diagnostic = nullptr,
      ImageFiles images = {});

  const mxcpp::EvalGraph &graph() const
  {
    return *m_graph;
  }

  // Bit (1 << a) for each ispc::Attribute a the graph reads
  uint32_t attributeMask() const
  {
    return m_attributeMask;
  }

  // The parts of the shading context the graph may read
  // (ispc::PathContextGroup bits, contextGroupsRead()): the shading call
  // sends only those; and PATH_CONTEXT_OBJECT_POSITION where the graph reads
  // the object position, which the shading context then interpolates from
  // the primitive's vertices
  uint32_t contextMask() const
  {
    return m_contextMask;
  }

  // By geomprop handle
  const std::vector<GeomPropBinding> &geomProps() const
  {
    return m_geomProps;
  }

  // What its shading contexts sample its samplers with
  const HalcyonTextureSystem &textures() const
  {
    return m_textures;
  }

 private:
  CppMaterial(std::unique_ptr<mxcpp::EvalGraph> graph,
      std::vector<GeomPropBinding> geomProps,
      std::vector<const void *> samplers,
      ImageFiles images,
      uint32_t contextMask);

  std::unique_ptr<mxcpp::EvalGraph> m_graph;
  std::vector<GeomPropBinding> m_geomProps;
  HalcyonTextureSystem m_textures;
  uint32_t m_attributeMask{0};
  uint32_t m_contextMask{0};
};

} // namespace halcyon::pathtracer

// The batched calls ISPC makes (declared in PathMaterial.isph; see there and
// the ADR for what each computes). Closures live in an arena per calling
// thread: PathMaterial_shadeSoA() starts a new generation of it, and the
// handles it returns are refused (zero results) once the next one has run.
extern "C" {
void PathMaterial_shadeSoA(const ispc::PathMaterialContext *context,
    int mask,
    const ispc::PathShadeInSoA *in,
    ispc::PathShadeOutSoA *out);
void PathMaterial_evalSoA(
    int mask, const ispc::PathEvalInSoA *in, ispc::PathEvalOutSoA *out);
void PathMaterial_subsurfaceSoA(int mask,
    const ispc::PathSubsurfaceInSoA *in,
    ispc::PathSubsurfaceOutSoA *out);
void PathMaterial_mediumSoA(
    int mask, const ispc::PathHandleSoA *in, ispc::PathMediumSoA *out);
void PathMaterial_shadowSoA(const ispc::PathMaterialContext *context,
    int mask,
    const ispc::PathShadeInSoA *in,
    ispc::PathShadowOutSoA *out);
}
