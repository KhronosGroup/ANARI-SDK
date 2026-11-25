// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Which parts of the shading context a MaterialXCpp graph can read
// (ispc::PathContextGroup, PathMaterial.isph), so that the material interface
// sends each part only to the materials that read it
// (docs/adr/0004-pathtracer-material-interface.md).

#include <renderer/materials/MaterialXCpp/graphTypes.h>
// std
#include <cstdint>

namespace halcyon::pathtracer {

// The PathContextGroup bits the nodes of 'graph' may read, including the
// nodes EvalGraph::Compile() adds for unconnected inputs; all but
// PATH_CONTEXT_OBJECT_POSITION, which CppMaterial::compile() takes from the
// compiled graph (Typhoon's own rule). Over-approximates:
// every node counts, reachable or not, and a node counts whatever its inputs
// do at run time except a constant 'space' that makes a geometric node's
// transform the identity.
//
// The table behind it lists the nodes that read each part, from an audit of
// every read of those fields in the vendored MaterialXCpp and BSDL sources at
// CONTEXT_GROUPS_AUDITED_COMMIT, plus halcyon's own sampler node; a node
// type not listed reads none. Moving the Typhoon pin needs a new audit (the
// material_graph ctest fails until this commit matches
// external/typhoon/PROVENANCE.md's).
uint32_t contextGroupsRead(const mxcpp::MaterialGraph &graph);

// The Typhoon commit whose sources the table was audited against
extern const char *const CONTEXT_GROUPS_AUDITED_COMMIT;

} // namespace halcyon::pathtracer
