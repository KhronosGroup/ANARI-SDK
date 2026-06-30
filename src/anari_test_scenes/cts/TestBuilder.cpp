// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "TestBuilder.h"

namespace anari {
namespace cts {

TestBuilder::TestBuilder(std::string category, std::string name)
{
  m_def.category = std::move(category);
  m_def.name = std::move(name);
}

TestBuilder &TestBuilder::description(std::string text)
{
  m_def.description = std::move(text);
  return *this;
}

TestBuilder &TestBuilder::build(BuildFn fn)
{
  m_def.build = std::move(fn);
  return *this;
}

TestBuilder &TestBuilder::camera(CameraFn fn)
{
  m_def.cameraBuild = std::move(fn);
  return *this;
}

TestBuilder &TestBuilder::renderer(RendererFn fn)
{
  m_def.rendererBuild = std::move(fn);
  return *this;
}

TestBuilder &TestBuilder::behavior(BehaviorFn fn)
{
  m_def.behaviorCheck = std::move(fn);
  return *this;
}

TestBuilder &TestBuilder::permute(std::string axis, std::vector<Any> values)
{
  m_def.axes.push_back(
      Axis{std::move(axis), std::move(values), AxisKind::Permutation});
  return *this;
}

TestBuilder &TestBuilder::variant(std::string axis, std::vector<Any> values)
{
  m_def.axes.push_back(
      Axis{std::move(axis), std::move(values), AxisKind::Variant});
  return *this;
}

TestBuilder &TestBuilder::simplified(bool on)
{
  m_def.simplified = on;
  return *this;
}

TestBuilder &TestBuilder::requireFeatures(std::vector<std::string> features)
{
  for (auto &f : features)
    m_def.requiredFeatures.push_back(std::move(f));
  return *this;
}

TestBuilder &TestBuilder::requireFeature(std::string feature)
{
  m_def.requiredFeatures.push_back(std::move(feature));
  return *this;
}

TestBuilder &TestBuilder::threshold(std::string metric, double value)
{
  m_def.thresholds[std::move(metric)] = value;
  return *this;
}

TestBuilder &TestBuilder::threshold(
    Channel channel, std::string metric, double value)
{
  m_def.channelThresholds[channel][std::move(metric)] = value;
  return *this;
}

TestBuilder &TestBuilder::boundsTolerance(double tolerance)
{
  m_def.boundsTolerance = tolerance;
  return *this;
}

TestBuilder &TestBuilder::channels(std::vector<Channel> ch)
{
  m_def.channels = std::move(ch);
  return *this;
}

const TestDef &TestBuilder::registerInto(Catalog &catalog)
{
  return catalog.add(m_def);
}

const TestDef &TestBuilder::registerInto()
{
  return globalCatalog().add(m_def);
}

} // namespace cts
} // namespace anari
