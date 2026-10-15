// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "InstancerBuilder.h"

#include <pxr/imaging/hd/instancerTopologySchema.h>
#include <pxr/imaging/hd/primvarSchema.h>
#include <pxr/imaging/hd/primvarsSchema.h>
#include <pxr/imaging/hd/retainedDataSource.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hd/xformSchema.h>

#include <pxr/base/vt/array.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace anari_hydra {

// Wrap a per-instance values array (held in a VtValue) as an `instance`-
// interpolation primvar the prototype rprims read per instance.
static HdContainerDataSourceHandle InstancePrimvar(const VtValue &values)
{
  return HdPrimvarSchema::BuildRetained(
      HdRetainedSampledDataSource::New(values),
      nullptr,
      nullptr,
      HdRetainedTypedSampledDataSource<TfToken>::New(
          HdPrimvarSchemaTokens->instance),
      /* role = */ nullptr,
      nullptr,
      nullptr);
}

HdContainerDataSourceHandle BuildInstancerPrim(
    const std::vector<SdfPath> &prototypes,
    const std::vector<GfMatrix4d> &transforms,
    const std::vector<InstancerAttribute> &attributes)
{
  const size_t n = transforms.size();

  // instancerTopology: each group surface is a prototype, and every instance
  // [0..N-1] draws every prototype, so each prototype gets the same
  // instanceIndices bucket.
  VtArray<SdfPath> protoPaths(prototypes.begin(), prototypes.end());
  VtIntArray indices(n);
  for (size_t i = 0; i < n; ++i)
    indices[i] = int(i);
  HdDataSourceBaseHandle indicesDS =
      HdRetainedTypedSampledDataSource<VtIntArray>::New(indices);
  std::vector<HdDataSourceBaseHandle> buckets(prototypes.size(), indicesDS);
  auto instanceIndices =
      HdRetainedSmallVectorDataSource::New(buckets.size(), buckets.data());

  auto topology = HdInstancerTopologySchema::BuildRetained(
      HdRetainedTypedSampledDataSource<VtArray<SdfPath>>::New(protoPaths),
      instanceIndices,
      /* mask = */ nullptr,
      /* instanceLocations = */ nullptr);

  // Per-instance transforms as an 'instance'-interpolation matrix primvar.
  VtMatrix4dArray xforms(n);
  for (size_t i = 0; i < n; ++i)
    xforms[i] = transforms[i];
  auto xformPrimvar = HdPrimvarSchema::BuildRetained(
      HdRetainedTypedSampledDataSource<VtMatrix4dArray>::New(xforms),
      nullptr,
      nullptr,
      HdRetainedTypedSampledDataSource<TfToken>::New(
          HdPrimvarSchemaTokens->instance),
      /* role = */ nullptr,
      nullptr,
      nullptr);

  // Primvars container: the transforms Hydra consumes to place instances, plus
  // any per-instance attribute overrides the prototype materials read.
  std::vector<TfToken> pvNames{HdInstancerTokens->instanceTransforms};
  std::vector<HdDataSourceBaseHandle> pvValues{xformPrimvar};
  for (const auto &attr : attributes) {
    pvNames.push_back(attr.first);
    pvValues.push_back(InstancePrimvar(attr.second));
  }
  auto primvars = HdRetainedContainerDataSource::New(
      pvNames.size(), pvNames.data(), pvValues.data());

  return HdRetainedContainerDataSource::New(
      HdInstancerTopologySchemaTokens->instancerTopology,
      topology,
      HdPrimvarsSchemaTokens->primvars,
      primvars,
      HdXformSchemaTokens->xform,
      HdXformSchema::BuildRetained(
          HdRetainedTypedSampledDataSource<GfMatrix4d>::New(GfMatrix4d(1)),
          HdRetainedTypedSampledDataSource<bool>::New(false)));
}

} // namespace anari_hydra
