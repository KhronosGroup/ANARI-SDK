// Copyright 2023-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <anari/anari.h>

namespace remote {

struct ObjectDesc
{
  ObjectDesc() = default;

  ObjectDesc(ANARIDevice dev, ANARIObject obj) : device(dev), object(obj) {}

  ObjectDesc(ANARIDevice dev, ANARIObject obj, ANARIDataType t)
      : device(dev), object(obj), type(t)
  {}

  ANARIDevice device{nullptr};
  ANARIObject object{nullptr};
  ANARIDataType type{ANARI_UNKNOWN};
  std::string subtype;
};

} // namespace remote
