// Copyright 2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <pxr/pxr.h>
#include <pxr/usd/ndr/parserPlugin.h>

PXR_NAMESPACE_OPEN_SCOPE

class HdAnariMdlParserPlugin : public NdrParserPlugin
{
 public:
  NdrNodeUniquePtr Parse(const NdrNodeDiscoveryResult &discoveryRes) override;

  const NdrTokenVec &GetDiscoveryTypes() const override;

  const TfToken &GetSourceType() const override;

 private:
};

PXR_NAMESPACE_CLOSE_SCOPE
