// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Case.h"
#include "Channel.h"
#include "anari_test_scenes_export.h"
// std
#include <filesystem>
#include <string>

namespace anari {
namespace cts {

// The single root directory a run operates under, with the conventional
// subdirs results/, ground_truth/, and assets/. Files mirror the catalog
// hierarchy: <sub>/<category>/<test>/<leaf>. Result files are keyed by the
// Case id (unique); ground-truth files by the Case's ground-truth key (shared
// by variants). All accessors return absolute paths and create no directories.
class ANARI_TEST_SCENES_INTERFACE Workdir
{
 public:
  explicit Workdir(std::filesystem::path root);

  const std::filesystem::path &root() const;
  std::filesystem::path resultsDir() const;
  std::filesystem::path groundTruthDir() const;
  std::filesystem::path assetsDir() const;

  // results/<category>/<test>/<caseId>.json
  std::filesystem::path sidecarPath(const Case &c) const;
  // results/<category>/<test>/<caseId>.<channel>.png
  std::filesystem::path resultImagePath(const Case &c, Channel channel) const;
  // ground_truth/<category>/<test>/<gtKey>.<channel>.png
  std::filesystem::path groundTruthImagePath(
      const Case &c, Channel channel) const;

  // A path expressed relative to the workdir root (for storing in sidecars).
  std::string relativeToRoot(const std::filesystem::path &p) const;

  // Create the root and drop a ".gitignore" of "*" into it, so the whole
  // workdir (generated ground truth, results, assets) is untracked wherever it
  // lives (ADR-0005). Idempotent; returns false on a filesystem error.
  bool writeGitignore() const;

 private:
  std::filesystem::path m_root;
};

} // namespace cts
} // namespace anari
