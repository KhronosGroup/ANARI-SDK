// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Catalog.h"
#include "TestDef.h"
#include "Value.h"
#include "anari_test_scenes_export.h"
// std
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace anari {
namespace cts {

// Fluent builder for a TestDef. Note: the conformance verb is requireFeatures()
// rather than `requires`, which is a reserved keyword in C++20.
class ANARI_TEST_SCENES_INTERFACE TestBuilder
{
 public:
  TestBuilder(std::string category, std::string name);

  TestBuilder &build(BuildFn fn);

  // Supply a custom render camera / renderer instead of the runner defaults
  // (bounds-framed camera, parameterless "default" renderer).
  TestBuilder &camera(CameraFn fn);
  TestBuilder &renderer(RendererFn fn);

  // Permutation axis: values produce different output (distinct ground truth).
  TestBuilder &permute(std::string axis, std::vector<Any> values);
  template <typename T>
  TestBuilder &permute(std::string axis, std::initializer_list<T> values)
  {
    return permute(std::move(axis), toAnyVector(values));
  }

  // Variant axis: values must produce identical output (shared ground truth).
  TestBuilder &variant(std::string axis, std::vector<Any> values);
  template <typename T>
  TestBuilder &variant(std::string axis, std::initializer_list<T> values)
  {
    return variant(std::move(axis), toAnyVector(values));
  }

  TestBuilder &simplified(bool on = true);
  TestBuilder &requireFeatures(std::vector<std::string> features);
  TestBuilder &requireFeature(std::string feature);
  TestBuilder &threshold(std::string metric, double value);
  TestBuilder &boundsTolerance(double tolerance);
  TestBuilder &channels(std::vector<Channel> ch);

  const TestDef &def() const
  {
    return m_def;
  }

  TestDef take()
  {
    return std::move(m_def);
  }

  // Register the finished Test into a catalog (the global one by default).
  const TestDef &registerInto(Catalog &catalog);
  const TestDef &registerInto();

 private:
  template <typename T>
  static std::vector<Any> toAnyVector(std::initializer_list<T> values)
  {
    std::vector<Any> out;
    out.reserve(values.size());
    for (const auto &v : values)
      out.emplace_back(Any(v));
    return out;
  }

  TestDef m_def;
};

inline TestBuilder makeTest(std::string category, std::string name)
{
  return TestBuilder(std::move(category), std::move(name));
}

} // namespace cts
} // namespace anari
