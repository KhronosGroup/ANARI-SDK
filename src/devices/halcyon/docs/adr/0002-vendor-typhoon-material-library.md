---
status: accepted
---
# Typhoon's material library is vendored as an unmodified copy

> **Tracker references.** This record predates halcyon's move into VisRTX: its ticket
> and follow-up numbers ("pathtracer ticket N", "follow-up N") refer to halcyon's
> original private tracker, and its commit hashes to the deprecated `anari-halcyon`
> repository (see AGENTS.md, Overview).

The `quality` renderer reuses Typhoon's material code (MaterialXCpp graphs and BSDL
closures) unmodified, so that it can follow upstream by moving a pin. That code lives in
`pxr/imaging/plugin/hdEmbree/` of the NVIDIA-Omniverse/OpenUSD repository. A git submodule
can't check out part of a repository, so a submodule, or a fetch of the commit's archive,
would download all of OpenUSD to build about 2 MB of sources. We copy those sources into
`external/typhoon/src/` instead, unmodified, with `import.sh` to copy them again at another
commit and `PROVENANCE.md` to record the commit and what was left out. `src/` is the include
root, so upstream's `<renderer/...>` includes need no shim.

## Consequences

- A pin move is `external/typhoon/import.sh <commit>`, a PROVENANCE update and a rebuild;
  the diff shows what upstream changed.
- Local patches to the copy are not allowed: workarounds go in halcyon's own files (the test
  runner's missing suites are stubbed in `tests/typhoon_materials/`).
- Imath is the only new dependency: the system's if CMake finds it, else fetched and built
  statically, like Embree. A system Imath may be a shared library; the maintainer accepted
  (2026-10-04) that the device may then need libImath at run time.
- Typhoon code outside this copy that halcyon needs, such as the integrator's closure
  classification (whose directory needs OpenUSD), is ported by hand into halcyon's own
  files, not vendored, and kept in step with the pin by hand. Decided 2026-10-04 by the
  maintainer: ported files carry halcyon's Apache-2.0 SPDX header with a prose credit of
  the source file, the pin and Pixar's copyright and license, and are listed in
  `THIRD-PARTY.md`. Amended 2026-10-15, before the move into VisRTX: halcyon's own files
  became NVIDIA BSD-3-Clause, and the ports keep their Apache-2.0 header and credit.
  Amended 2026-10-16 by the maintainer: the ports of Typhoon's and OpenUSD's code are
  tagged `LicenseRef-TOST-1.0` (TOST 1.0 is not Apache 2.0: its Section 6 differs, and
  it has no SPDX identifier yet), joined with `AND` to the license of other code a file
  ports (`THIRD-PARTY.md`).
- Typhoon's BSDF test suite (`testMaterialXCppBsdf.cpp`) includes Typhoon's integrator and
  OpenUSD types, so it is vendored but only builds against OpenUSD.
- Amended by ticket 31: the copy was first built only with the `HALCYON_ENABLE_PATHTRACER`
  option; the option is gone, so every build compiles it and needs Imath.
- Amended by ticket 17: the copy also holds the integrator's `medium.{h,cpp}`
  (homogeneous media, which MaterialXCpp's closures name), about 2.4 MB in all.
- Amended by ticket 47: code inside the copy may also be ported where halcyon must deviate
  from it (`renderer/pathtracer/ClosureSampling.*` ports file-local functions of
  `closureTraversal.cpp` to weigh a mix's delta samples); the copy itself stays unmodified.
- Amended by ticket 29: moving the pin also means redoing the `ContextGroups.cpp` audit
  (`material_graph` fails until it matches `PROVENANCE.md`) and bringing the hand ports
  (native kinds, media, subsurface, hero wavelength, mesh tangents, the transport) in step
  (AGENTS.md lists them).
- Amended when the OpenUSD-linked reference harness (`HALCYON_TYPHOON_REFERENCE`, which
  compared the ports with Typhoon's own C++) and the image comparison scripts were removed,
  so that halcyon carries no OpenUSD dependency on its way into VisRTX: the vendored BSDF
  suite, which needs Typhoon's integrator, is copied but no longer built.
