// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Workdir.h"

namespace anari {
namespace cts {

Workdir::Workdir(std::filesystem::path root) : m_root(std::move(root)) {}

const std::filesystem::path &Workdir::root() const
{
  return m_root;
}

std::filesystem::path Workdir::resultsDir() const
{
  return m_root / "results";
}

std::filesystem::path Workdir::groundTruthDir() const
{
  return m_root / "ground_truth";
}

std::filesystem::path Workdir::assetsDir() const
{
  return m_root / "assets";
}

std::filesystem::path Workdir::sidecarPath(const Case &c) const
{
  return resultsDir() / c.category / c.testName / (c.id() + ".json");
}

std::filesystem::path Workdir::resultImagePath(
    const Case &c, Channel channel) const
{
  return resultsDir() / c.category / c.testName
      / (c.id() + "." + channelName(channel) + ".png");
}

std::filesystem::path Workdir::groundTruthImagePath(
    const Case &c, Channel channel) const
{
  return groundTruthDir() / c.category / c.testName
      / (c.groundTruthKey() + "." + channelName(channel) + ".png");
}

std::string Workdir::relativeToRoot(const std::filesystem::path &p) const
{
  std::error_code ec;
  auto rel = std::filesystem::relative(p, m_root, ec);
  return ec ? p.generic_string() : rel.generic_string();
}

} // namespace cts
} // namespace anari
