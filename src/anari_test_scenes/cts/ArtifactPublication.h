// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Image.h"
#include "Sidecar.h"
#include "Workdir.h"
// std
#include <filesystem>
#include <memory>
#include <vector>

namespace anari {
namespace cts {

// One image and the final path at which it becomes visible to CTS consumers.
struct ImageArtifact
{
  std::filesystem::path path;
  Image image;
};

// The serialization seam used by ArtifactPublisher. Tests can inject an
// adapter that fails a selected write without relying on file permissions.
struct ArtifactWriter
{
  virtual ~ArtifactWriter() = default;

  virtual bool writeImage(
      const std::filesystem::path &path, const Image &image) = 0;
  virtual bool writeSidecar(
      const std::filesystem::path &path, const CaseResult &result) = 0;
};

// Transactional publication for one Case. Images are staged and validated
// before any final path is replaced. Result sidecars are commit markers: they
// are staged with the images and renamed into place only after every path they
// reference is present and readable.
struct ArtifactPublisher
{
  explicit ArtifactPublisher(
      Workdir workdir, std::shared_ptr<ArtifactWriter> writer = nullptr);

  bool publishGroundTruth(const std::vector<ImageArtifact> &images) const;

  bool publishResult(const Case &c,
      const CaseResult &result,
      const std::vector<ImageArtifact> &images = {}) const;

 private:
  Workdir m_workdir;
  std::shared_ptr<ArtifactWriter> m_writer;
};

} // namespace cts
} // namespace anari
