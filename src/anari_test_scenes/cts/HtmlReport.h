// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Sidecar.h"
// std
#include <filesystem>
#include <map>
#include <string>

// The HTML report: a single self-contained, interactive document rendered from
// a run's results tree. Every Case's metadata is always present so the client
// can filter by verdict and search without a server; which images travel with
// the file depends on `embed` and the initial filter (a passing Case is not
// embedded in a failures-only report). See Report.h for the shared aggregation.
namespace anari {
namespace cts {

struct HtmlOptions
{
  // The initial verdict filter shows failures only unless includeAll is set;
  // the reader can always toggle passed/skipped back on client-side.
  bool includeAll{false};
  // Inline images as base64 data URIs (a portable single file) instead of
  // referencing the PNGs in the workdir. Only the initially-shown Cases are
  // embedded, so the file stays bounded; `--embed --all` embeds everything.
  bool embed{false};
  // Where the document will be written. Browsers resolve non-embedded image
  // references against this location, so they are emitted relative to it.
  // Empty keeps them workdir-relative (only correct for a document placed in
  // the workdir root).
  std::filesystem::path htmlPath;
};

// Render the full HTML document for a run's results tree.
std::string generateHtml(const std::filesystem::path &workdir,
    const std::map<std::string, CaseResult> &results,
    const HtmlOptions &opts);

// Escape text for HTML text and double-quoted attribute content.
std::string htmlEscape(const std::string &text);

// Standard base64 of arbitrary bytes (no line wrapping).
std::string base64Encode(const std::string &bytes);

} // namespace cts
} // namespace anari
