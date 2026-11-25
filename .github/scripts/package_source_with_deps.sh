#!/usr/bin/env bash
## Copyright 2026 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

# Makes ANARI-SDK-<version>_with_deps.zip: a release's source archive with
# .anari_deps/ pre-populated, so the SDK configures without network access.
#
#   package_source_with_deps.sh <tag | source.zip> [output-dir]
#
# A tag downloads GitHub's source archive for it (the same zip the release
# page offers); a path to a zip uses that instead, for testing. The archive is
# configured to make anari_sdk_fetch_project() download every dependency into
# .anari_deps/, the build directories are deleted, and the tree is zipped.
#
# Needs cmake, a C/C++ compiler, ispc (halcyon), SDL3 development files and
# glslangValidator (helide_gpu), curl, unzip and zip. Not covered: the hydra
# device's NanoVDB headers (that needs OpenUSD to configure). The CTS's glTF
# sample assets are deliberately never included (they are very large).

set -euo pipefail

# Some dependency archives have UTF-8 file names, which CMake cannot extract
# in the POSIX locale of a bare container
export LC_ALL=C.UTF-8

if [[ $# -lt 1 || $# -gt 2 ]]; then
  echo "usage: $0 <tag | source.zip> [output-dir]" >&2
  exit 2
fi

input=$1
out_dir=$(mkdir -p "${2:-$PWD}" && cd "${2:-$PWD}" && pwd)
repo=${GITHUB_REPOSITORY:-KhronosGroup/ANARI-SDK}

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

if [[ -f $input ]]; then
  cp "$input" "$work/source.zip"
else
  curl -fsSL -o "$work/source.zip" \
    "https://github.com/$repo/archive/refs/tags/$input.zip"
fi

mkdir "$work/pkg"
unzip -q "$work/source.zip" -d "$work/pkg"
shopt -s nullglob dotglob
top=("$work"/pkg/*)
if [[ ${#top[@]} -ne 1 || ! -d ${top[0]} ]]; then
  echo "error: expected one top-level directory in the source archive" >&2
  exit 1
fi
src=${top[0]}
name=$(basename "$src")

# Pass 1: everything that fetches without an installed SDL3. Packages that
# would be used instead of a fetch when installed are hidden from find_package
# so their sources are always downloaded.
cmake -S "$src" -B "$work/build_all" \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_HELIDE_DEVICE=ON \
  -DBUILD_HALCYON_DEVICE=ON \
  -DBUILD_EXAMPLES=ON \
  -DBUILD_VIEWER=ON \
  -DBUILD_CAT=ON \
  -DUSE_KOKKOS=ON \
  -DCMAKE_DISABLE_FIND_PACKAGE_SDL3=ON \
  -DCMAKE_DISABLE_FIND_PACKAGE_embree=ON \
  -DCMAKE_DISABLE_FIND_PACKAGE_Imath=ON \
  -DCMAKE_DISABLE_FIND_PACKAGE_MaterialX=ON

# Pass 2: helide_gpu, which requires an installed SDL3
cmake -S "$src" -B "$work/build_gpu" \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_HELIDE_DEVICE=OFF \
  -DBUILD_HELIDE_GPU_DEVICE=ON \
  -DBUILD_EXAMPLES=OFF

rm -rf "$work/build_all" "$work/build_gpu"

echo "Downloaded dependencies:"
ls -1 "$src/.anari_deps"
if ls -1 "$src/.anari_deps" | grep -qi gltf; then
  echo "error: glTF sample assets must not be in the source package" >&2
  exit 1
fi

zip_path="$out_dir/${name}_with_deps.zip"
rm -f "$zip_path"
(cd "$work/pkg" && zip -q -r -X "$zip_path" "$name")
echo "Wrote $zip_path"
