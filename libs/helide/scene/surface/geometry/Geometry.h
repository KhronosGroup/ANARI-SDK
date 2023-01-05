// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "array/Array1D.h"

namespace helide {

struct Geometry : public Object
{
  Geometry(HelideGlobalState *s);
  ~Geometry() override;

  static Geometry *createInstance(
      std::string_view subtype, HelideGlobalState *s);

  RTCGeometry embreeGeometry() const;

  void commit() override;
  void markCommitted() override;

 protected:
  RTCGeometry m_embreeGeometry{nullptr};
};

} // namespace helide

HELIDE_ANARI_TYPEFOR_SPECIALIZATION(helide::Geometry *, ANARI_GEOMETRY);
