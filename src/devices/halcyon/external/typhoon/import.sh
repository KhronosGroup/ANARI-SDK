#!/usr/bin/env bash
## Copyright 2026 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

# Copies Typhoon's USD-free material library from the NVIDIA-Omniverse/OpenUSD
# typhoon/main branch into src/, at the commit given as the first argument
# (default: the pin in PROVENANCE.md). Files are copied unmodified; moving the
# pin is: run this with the new commit, update PROVENANCE.md, rebuild, test
# (AGENTS.md, "Moving the Typhoon pin").

set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
commit="${1:-$(sed -n 's/^- Commit: `\([0-9a-f]*\)`.*/\1/p' "$here/PROVENANCE.md")}"
repo="https://github.com/NVIDIA-Omniverse/OpenUSD.git"
ty="pxr/imaging/plugin/hdEmbree"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

git -C "$work" init -q
git -C "$work" remote add origin "$repo"
git -C "$work" sparse-checkout set --no-cone \
  "/$ty/renderer/materials/MaterialXCpp/" \
  "/$ty/renderer/materials/BSDL/" \
  "/$ty/renderer/integrator/medium.*" \
  /LICENSE.txt /NOTICE.txt
git -C "$work" fetch -q --depth 1 --filter=blob:none origin "$commit"
git -C "$work" checkout -q FETCH_HEAD

rm -rf "$here/src"
mkdir -p "$here/src/renderer/materials" "$here/src/renderer/integrator"

# Everything but the parts bound to OpenUSD or OpenImageIO: the adapter and
# texture system tests, the closure classification test (it needs the
# integrator), and their USD/image test data.
rsync -a \
  --exclude 'tests/data/' \
  --exclude 'tests/testMaterialXCppAdapter.cpp' \
  --exclude 'tests/testMaterialXCppTextureSystem.cpp' \
  --exclude 'tests/testClosureClassification.cpp' \
  "$work/$ty/renderer/materials/MaterialXCpp" \
  "$work/$ty/renderer/materials/BSDL" \
  "$here/src/renderer/materials/"
cp "$work/$ty/renderer/integrator/medium.h" \
   "$work/$ty/renderer/integrator/medium.cpp" \
   "$here/src/renderer/integrator/"
cp "$work/LICENSE.txt" "$work/NOTICE.txt" "$here/"

echo "Imported $ty material library at $(git -C "$work" rev-parse HEAD)"
