# Typhoon material library

Vendored, unmodified, from Typhoon, the hdEmbree path tracer on the `typhoon/main`
branch of NVIDIA-Omniverse/OpenUSD:

- Repository: https://github.com/NVIDIA-Omniverse/OpenUSD
- Commit: `70c45e83b08a7b45588cd6cdbd2ae67f5147befb` (2026-09-16, "add on-hold banner")
- Copied by `import.sh`, which takes the commit as its argument.

| Upstream path (under `pxr/imaging/plugin/hdEmbree/`) | Here |
|---|---|
| `renderer/materials/MaterialXCpp/` | `src/renderer/materials/MaterialXCpp/` |
| `renderer/materials/BSDL/` | `src/renderer/materials/BSDL/` |
| `renderer/integrator/medium.{h,cpp}` | `src/renderer/integrator/` |
| `/LICENSE.txt`, `/NOTICE.txt` (repository root) | `LICENSE.txt`, `NOTICE.txt` |

Left out: `MaterialXCpp/tests/data/`, and the tests that need OpenUSD, OpenImageIO or
the rest of the renderer (`testMaterialXCppAdapter.cpp`,
`testMaterialXCppTextureSystem.cpp`, `testClosureClassification.cpp`). The rest of
`renderer/materials/` (`mxcppAdapter.*`, `oiioTextureSystem.*`, `material.h`,
`materialEvalContext.h`) and `renderer/colorManagement.*` are bound to OpenUSD and
are replaced in halcyon, not vendored.

`MaterialXCpp/tests/testMaterialXCppBsdf.cpp` includes Typhoon's integrator
(`shadingNormal.h`, `sss.h`), which needs OpenUSD and Embree. The `typhoon_materials`
ctest runs the Graph, Materials and Nodes suites; the BSDF suite is copied but not
built.

`src/` is the include root, so the upstream `#include <renderer/...>` paths resolve
unchanged.

## Local patches

None. Keep it that way: work around upstream in halcyon's own files (as
`tests/typhoon_materials/` does for the test runner), so that a pin move is only
`import.sh <commit>`.

## Licenses

- MaterialXCpp and `medium.*`: OpenUSD's Tomorrow Open Source Technology License 1.0
  (`LICENSE.txt`, Apache License 2.0 with a different section 6, "Trademarks"), with
  `NOTICE.txt`.
- BSDL: BSD-3-Clause, Contributors to the Open Shading Language project
  (`src/renderer/materials/BSDL/LICENSE.md`); its `src/jakobhanika_luts.cpp` is
  Apache-2.0 (`src/renderer/materials/BSDL/THIRD-PARTY.md`).
