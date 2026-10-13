// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Authors an HdInstancer prim for one (world, group): the group's surface
// rprims are the instancer's prototypes, each drawn once per instance transform
// (SPEC §4.3). Replaces the baked-transform rprim expansion so N instances of a
// group cost the prototypes + N transforms instead of N rprim copies.

#pragma once

#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/value.h>
#include <pxr/imaging/hd/dataSource.h>
#include <pxr/usd/sdf/path.h>

#include <utility>
#include <vector>

namespace anari_hydra {

// A per-instance attribute override authored as an instance-rate primvar
// (SPEC §6.8): `name` matches the primvar the material's UsdPrimvarReader reads
// (`color`, `attribute0..3`), `value` holds the instance-rate array (one entry
// per instance) as a VtVec3fArray (color) or VtVec4fArray (attributes).
using InstancerAttribute = std::pair<pxr::TfToken, pxr::VtValue>;

// Build the instancer container (prim type HdPrimTypeTokens->instancer). Every
// prototype rprim is drawn for every instance transform. Each attribute in
// `attributes` becomes an `instance`-interpolation primvar the prototype
// materials read per instance.
pxr::HdContainerDataSourceHandle BuildInstancerPrim(
    const std::vector<pxr::SdfPath> &prototypes,
    const std::vector<pxr::GfMatrix4d> &transforms,
    const std::vector<InstancerAttribute> &attributes);

} // namespace anari_hydra
