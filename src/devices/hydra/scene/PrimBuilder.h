// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Authors Hydra prim container data sources from ANARI objects. This is where
// the Hydra schema knowledge lives (mirroring prototype 012's _BuildQuadPrim);
// object classes stay thin. Bulk vertex data flows through zero-copy data
// sources; topology/indices are copied (small, outside zero scope).

#pragma once

#include "InstancerBuilder.h"
#include "surface/Surface.h"
#include "world/Instance.h"

#include <pxr/base/gf/matrix4d.h>
#include <pxr/imaging/hd/dataSource.h>
#include <pxr/imaging/hd/dataSourceLocator.h>
#include <pxr/usd/sdf/path.h>

#include <string>
#include <vector>

namespace anari_hydra {

// How instancing reaches a surface's rprim. Attribute slots are numbered as
// Sampler's: attribute0..3, then color (bit 4).
struct InstanceBinding
{
  // Set for a prototype rprim of an instancer: the rprim carries an
  // `instancedBy` and is authored with an identity xform.
  pxr::SdfPath instancerPath;
  pxr::SdfPath prototypeRoot;
  // Slots whose geometry constant the instancer carries instead (the
  // per-surface instancer), so the prototype leaves the constant out.
  uint32_t constantMask{0};
  // Slots some instance overrides. A baked sampler on such a slot reads the
  // instancer's per-instance values (BuildDerivedInstanceAttributes), so the
  // prototype authors no constant that would shadow them.
  uint32_t overrideMask{0};
  // A per-instance copy (world-space sources): that instance's overrides, which
  // beat the geometry constants as ANARI precedence says.
  const InstanceOverrides *values{nullptr};
};

// Build the rprim container for a surface with the given world-space transform.
// Dispatches on the geometry's Hydra rprim type: mesh (triangle / quad /
// tessellated sphere-cone-cylinder), or basisCurves (curve). When
// `materialPath` is non-empty the rprim is bound to that material sprim; it
// always also carries the constant-color displayColor fallback.
pxr::HdContainerDataSourceHandle BuildSurfacePrim(Surface *surface,
    const pxr::GfMatrix4d &xform,
    const pxr::SdfPath &materialPath,
    const InstanceBinding &instancing = {});

// The per-instance values of the material's baked samplers on an attribute
// source, as instance-rate primvars for the instancer: one array per baked
// primvar whose slot some instance in `overrides` sets, holding the sampler's
// map of each instance's value (ANARI's (0,0,0,1) where an instance leaves it
// unset). A prototype's higher-rate primvar of the same name wins over these,
// as it does for the raw attributes.
std::vector<InstancerAttribute> BuildDerivedInstanceAttributes(
    Material *material, const std::vector<InstanceOverrides> &overrides);

// True when one of the material's samplers reads worldPosition/worldNormal,
// which a shared prototype rprim cannot provide.
bool ReadsWorldSpace(Material *material);

// Locator for incremental invalidation of bulk vertex positions (proto 012):
// points cover mesh/points/basisCurves positions.
pxr::HdDataSourceLocator PointsLocator();

// The primvar name a material's attribute reader reads for ANARI `attributeN`
// (index 0..3): "attribute0".."attribute3". Shared by the per-vertex authoring
// (PrimBuilder) and the instance-rate override authoring (Mirror) so the naming
// lives in one place.
std::string AttributePrimvarName(int index);

} // namespace anari_hydra
