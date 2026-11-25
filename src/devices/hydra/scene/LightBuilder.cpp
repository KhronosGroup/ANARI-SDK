// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "LightBuilder.h"

#include <pxr/imaging/hd/light.h>
#include <pxr/imaging/hd/lightSchema.h>
#include <pxr/imaging/hd/retainedDataSource.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hd/visibilitySchema.h>
#include <pxr/imaging/hd/xformSchema.h>

#include <map>

PXR_NAMESPACE_USING_DIRECTIVE

namespace anari_hydra {

HdContainerDataSourceHandle BuildLightPrim(const LightData &data)
{
  // The `light` container the delegate reads via GetLightParamValue. Start from
  // the common params every UsdLux light carries — Storm's light task reads
  // these unconditionally and errors on an empty VtValue — then let the subtype
  // override (exposure pinned 0).
  std::map<TfToken, VtValue> params{{HdLightTokens->exposure, VtValue(0.f)},
      {HdLightTokens->diffuse, VtValue(1.f)},
      {HdLightTokens->specular, VtValue(1.f)},
      {HdLightTokens->normalize, VtValue(true)},
      {HdLightTokens->enableColorTemperature, VtValue(false)}};
  for (const auto &p : data.params)
    params[p.first] = p.second;

  std::vector<TfToken> names;
  std::vector<HdDataSourceBaseHandle> values;
  names.reserve(params.size());
  values.reserve(params.size());
  for (const auto &p : params) {
    names.push_back(p.first);
    values.push_back(HdRetainedSampledDataSource::New(p.second));
  }
  auto light = HdRetainedContainerDataSource::New(
      names.size(), names.data(), values.data());

  return HdRetainedContainerDataSource::New(HdLightSchemaTokens->light,
      light,
      HdXformSchemaTokens->xform,
      HdXformSchema::BuildRetained(
          HdRetainedTypedSampledDataSource<GfMatrix4d>::New(data.xform),
          HdRetainedTypedSampledDataSource<bool>::New(false)),
      HdVisibilitySchemaTokens->visibility,
      HdVisibilitySchema::BuildRetained(
          HdRetainedTypedSampledDataSource<bool>::New(true)));
}

} // namespace anari_hydra
