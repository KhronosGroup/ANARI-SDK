// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "ArtifactPublication.h"
// std
#include <atomic>
#include <fstream>
#include <set>
#include <string>
#include <system_error>
#include <utility>

namespace anari {
namespace cts {

namespace {

std::filesystem::path temporarySibling(
    const std::filesystem::path &path, const char *purpose)
{
  static std::atomic_uint64_t nextId{0};
  return path.parent_path()
      / ("." + path.filename().string() + "." + purpose + "."
          + std::to_string(nextId.fetch_add(1, std::memory_order_relaxed)));
}

bool createParentDirectory(const std::filesystem::path &path)
{
  if (!path.has_parent_path())
    return true;
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  return !ec;
}

bool removeIfPresent(const std::filesystem::path &path)
{
  std::error_code ec;
  const bool exists = std::filesystem::exists(path, ec);
  if (ec)
    return false;
  if (!exists)
    return true;
  if (!std::filesystem::is_regular_file(path, ec) || ec)
    return false;
  return std::filesystem::remove(path, ec) && !ec;
}

bool readableFile(const std::filesystem::path &path)
{
  std::ifstream in(path, std::ios::binary);
  return in && in.peek() != std::ifstream::traits_type::eof();
}

bool readableImage(const std::filesystem::path &path)
{
  return loadPNG(path.string()).valid();
}

struct FilesystemArtifactWriter : public ArtifactWriter
{
  bool writeImage(
      const std::filesystem::path &path, const Image &image) override
  {
    return savePNG(path.string(), image);
  }

  bool writeSidecar(
      const std::filesystem::path &path, const CaseResult &result) override
  {
    return cts::writeSidecar(path, result);
  }
};

struct StagedImage
{
  std::filesystem::path finalPath;
  std::filesystem::path stagedPath;
  std::filesystem::path backupPath;
  bool installed{false};
};

struct ImageTransaction
{
  explicit ImageTransaction(ArtifactWriter &writer) : m_writer(writer) {}

  ~ImageTransaction()
  {
    if (!m_finished)
      rollback();
  }

  bool stage(const std::vector<ImageArtifact> &images)
  {
    std::set<std::filesystem::path> finalPaths;
    for (const auto &artifact : images) {
      const auto finalPath = artifact.path.lexically_normal();
      if (!finalPaths.insert(finalPath).second || !artifact.image.valid()
          || !createParentDirectory(finalPath))
        return false;

      std::error_code ec;
      if (std::filesystem::exists(finalPath, ec)
          && (!std::filesystem::is_regular_file(finalPath, ec) || ec))
        return false;
      if (ec)
        return false;

      StagedImage staged;
      staged.finalPath = finalPath;
      staged.stagedPath = temporarySibling(finalPath, "stage");
      m_images.push_back(staged);
      if (!m_writer.writeImage(staged.stagedPath, artifact.image)
          || !readableImage(staged.stagedPath))
        return false;
    }
    return true;
  }

  bool commit()
  {
    for (auto &image : m_images) {
      std::error_code ec;
      if (!std::filesystem::exists(image.finalPath, ec)) {
        if (ec)
          return false;
        continue;
      }
      image.backupPath = temporarySibling(image.finalPath, "backup");
      std::filesystem::rename(image.finalPath, image.backupPath, ec);
      if (ec)
        return false;
    }

    for (auto &image : m_images) {
      std::error_code ec;
      std::filesystem::rename(image.stagedPath, image.finalPath, ec);
      if (ec)
        return false;
      image.installed = true;
    }

    for (const auto &image : m_images) {
      if (!readableImage(image.finalPath))
        return false;
    }
    return true;
  }

  void finish()
  {
    for (const auto &image : m_images) {
      if (image.backupPath.empty())
        continue;
      std::error_code ec;
      std::filesystem::remove(image.backupPath, ec);
    }
    m_finished = true;
  }

  void rollback()
  {
    for (auto &image : m_images) {
      std::error_code ec;
      if (image.installed)
        std::filesystem::remove(image.finalPath, ec);
    }
    for (auto &image : m_images) {
      if (image.backupPath.empty())
        continue;
      std::error_code ec;
      std::filesystem::rename(image.backupPath, image.finalPath, ec);
    }
    for (const auto &image : m_images) {
      std::error_code ec;
      std::filesystem::remove(image.stagedPath, ec);
    }
    m_finished = true;
  }

 private:
  ArtifactWriter &m_writer;
  std::vector<StagedImage> m_images;
  bool m_finished{false};
};

std::filesystem::path resolveSidecarPath(
    const Workdir &workdir, const std::string &path)
{
  const std::filesystem::path p(path);
  return p.is_absolute() ? p : workdir.root() / p;
}

bool referencedImagesReadable(const Workdir &workdir, const CaseResult &result)
{
  for (const auto &channel : result.channels) {
    for (const std::string *path : {&channel.resultImage,
             &channel.groundTruthImage,
             &channel.diffImage,
             &channel.thresholdImage}) {
      if (!path->empty() && !readableImage(resolveSidecarPath(workdir, *path)))
        return false;
    }
  }
  return true;
}

bool resultArtifactsMatch(const Workdir &workdir,
    const CaseResult &result,
    const std::vector<ImageArtifact> &images)
{
  std::set<std::filesystem::path> artifactPaths;
  for (const auto &image : images)
    artifactPaths.insert(image.path.lexically_normal());

  std::set<std::filesystem::path> referencedResultPaths;
  for (const auto &channel : result.channels) {
    for (const std::string *path :
        {&channel.resultImage, &channel.diffImage, &channel.thresholdImage}) {
      if (!path->empty())
        referencedResultPaths.insert(
            resolveSidecarPath(workdir, *path).lexically_normal());
    }
  }
  return artifactPaths == referencedResultPaths;
}

} // namespace

ArtifactPublisher::ArtifactPublisher(
    Workdir workdir, std::shared_ptr<ArtifactWriter> writer)
    : m_workdir(std::move(workdir)),
      m_writer(writer ? std::move(writer)
                      : std::make_shared<FilesystemArtifactWriter>())
{}

bool ArtifactPublisher::publishGroundTruth(
    const std::vector<ImageArtifact> &images) const
{
  ImageTransaction transaction(*m_writer);
  if (!transaction.stage(images) || !transaction.commit())
    return false;
  transaction.finish();
  return true;
}

bool ArtifactPublisher::publishResult(const Case &c,
    const CaseResult &result,
    const std::vector<ImageArtifact> &images) const
{
  const auto sidecarPath = m_workdir.sidecarPath(c);
  if (!createParentDirectory(sidecarPath) || !removeIfPresent(sidecarPath))
    return false;
  if (!resultArtifactsMatch(m_workdir, result, images))
    return false;

  const auto stagedSidecar = temporarySibling(sidecarPath, "stage");
  if (!m_writer->writeSidecar(stagedSidecar, result)
      || !readableFile(stagedSidecar)) {
    std::error_code ec;
    std::filesystem::remove(stagedSidecar, ec);
    return false;
  }

  ImageTransaction transaction(*m_writer);
  if (!transaction.stage(images) || !transaction.commit()
      || !referencedImagesReadable(m_workdir, result)) {
    std::error_code ec;
    std::filesystem::remove(stagedSidecar, ec);
    return false;
  }

  std::error_code ec;
  std::filesystem::rename(stagedSidecar, sidecarPath, ec);
  if (ec || !readableFile(sidecarPath)) {
    std::filesystem::remove(stagedSidecar, ec);
    std::filesystem::remove(sidecarPath, ec);
    return false;
  }

  transaction.finish();
  return true;
}

} // namespace cts
} // namespace anari
