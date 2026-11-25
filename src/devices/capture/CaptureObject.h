// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CaptureState.h"
// helium
#include "helium/BaseObject.h"
// std
#include <string>
#include <utility>
#include <vector>

namespace capture_device {

// The parameters an object had at its last commit, in the order they were set
struct CommittedParams
{
  using List = std::vector<std::pair<std::string, helium::AnariAny>>;

  const List &committedParams() const;
  const std::string &subtype() const;

 protected:
  // Copies what 'obj' reads now. Call from commitParameters(), where that is
  // the object's committed snapshot.
  void readCommitted(const helium::ParameterizedObject &obj);

  std::string m_subtype;

 private:
  List m_committed;
};

// Any object but a frame or an array: it keeps its type, its subtype and its
// committed parameters, and nothing else.
struct Object : public helium::BaseObject, public CommittedParams
{
  Object(ANARIDataType type, const char *subtype, CaptureState *state);

  bool isValid() const override;
  bool getProperty(const std::string_view &name,
      ANARIDataType type,
      void *ptr,
      uint64_t size,
      uint32_t flags) override;
  void commitParameters() override;
  void finalize() override;
};

} // namespace capture_device
