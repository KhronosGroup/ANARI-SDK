---
status: accepted
---
# ISPC calls C++ materials in batched calls per gang, with closures in a per-thread arena

> **Tracker references.** This record predates halcyon's move into VisRTX: its ticket
> and follow-up numbers ("pathtracer ticket N", "follow-up N") refer to halcyon's
> original private tracker, and its commit hashes to the deprecated `anari-halcyon`
> repository (see AGENTS.md, Overview).

The `quality` renderer's path loop is ISPC, but MaterialX graphs and their BSDL closures
stay in Typhoon's C++ (ADR 0002), whose `SurfaceClosure` holds a `std::variant` closure tree
that ISPC can't read. So the boundary is the BSDF interface, in batched `extern "C"` calls
that take the active-lane mask and SoA records (one array element per lane):

- one shading call per gang and path vertex, `tyShadeSoA(mask, ShadeInSoA*, ShadeOutSoA*)`,
  which for each active lane evaluates the graph, prepares the closure's normals and samples
  it (`SampleSurface()`). The closure stays in C++ and the lane gets a handle to it;
- one evaluation call per light sample, `tyEvalSoA()`, which evaluates each lane's closure
  by handle towards that sample's direction (`EvalSurfaceCosine()`, `PdfSurface()`), so NEE
  can take any number of light samples per vertex, as Typhoon's does (every light,
  `lightSamplesPerHit` samples each).

Each evaluation call costs a whole `EvalSurfaceCosine()` + `PdfSurface()` (see
Measurements); that is accepted rather than fusing one light direction into the shading
call, which would fix NEE at one light sample per vertex. The prototype that settled this was
the `material_callout` ctest (`tests/test_material_callout.{cpp,ispc}`, `--bench` for the
numbers below); it fused one evaluation into `tyShadeSoA` and measured the evaluation call
separately. Ticket 04's interface splits them (ADR 0004), and its `path_material` ctest
replaced the prototype, which is in git history before `779178e`.

## Closure arena

Each thread has its own arena (`thread_local`), a grow-only vector of closure + interaction
entries (416 bytes each). The shading call resets it (evaluation calls don't), so a closure lives until the same
thread's next shading call, which is the gang's next path vertex: nothing after it needs the
closure (the BSDF pdf for MIS at the next hit travels in ISPC as a float). A handle is the
slot (low bits) plus the arena's generation; a handle from an earlier generation is
refused, not read (until the generation wraps, so this catches misuse, it doesn't
guarantee against it). Because the call resets the whole arena, a second shading call at
the same vertex (one per material under `foreach_unique`, or one per varying branch) would
invalidate the first call's handles: the path loop gathers all of a gang's C++-shaded lanes
into the one call. This lifetime is enough for random-walk subsurface entry and interior
media too: both read the closure at the same vertex and copy what they need (ADR 0004). Handles only mean something on the thread that made them, which holds
while one task runs one gang's path from start to end, as Embree's `parallel_for` does. A
path loop that interleaves several gangs on one thread needs an arena per gang, passed in
the call.

## Considered options

Per-lane calls (`foreach_active` + `extract()` of a scalar record, one call per lane) cost
the same once the material does real work, but they need an extra call per gang to reset the
arena (a lane's call can't, it would invalidate the other lanes' handles), their crossing
and marshalling cost twice as much (27 against 11 ns per point with every lane active, 54
against 40 per active lane with a quarter active), and they rule out C++ batching lanes by
material later.

## Measurements

Threadripper 7960X (Zen 4), ISPC's default host target (gang width 16), RelWithDebInfo,
131072 points per material, best of 7, ns per point:

| Material | `Evaluate()` | prepare normals | `SampleSurface()` | NEE eval + pdf | all, in C++ | batched | per lane |
|---|---|---|---|---|---|---|---|
| UsdPreviewSurface, constants | 228 | 34 | 706 | 321 | 1304 | +46 | +97 |
| OpenPBR, coat + fuzz | 499 | 72 | 2176 | 716 | 3517 | +46 | +101 |
| UsdPreviewSurface, noise3d colour | 438 | 26 | 452 | 199 | 1159 | +26 | +67 |

The robust number is the call alone (a C++ side that does nothing, with the same record
traffic): 11 ns per point batched against 27 per lane. The last two columns subtract two
separately timed runs of 1 to 4 us each, so they are within run-to-run noise of each other
(a rerun gave +28 to +41 batched, +39 to +60 per lane): the overhead is a few percent of
the C++ work either way. Graph evaluation is the smaller part: sampling the closure dominates, so ISPC fast
paths pay off for BSDFs more than for graphs. A second call by handle (to evaluate a
direction known only later) costs a whole `EvalSurfaceCosine()` + `PdfSurface()` again
(+205 to +736), paid once per light sample under the split calls chosen above.
24 threads (`std::thread`, not Embree's tasking) shading at once give bitwise the same
results as one, each 1.1 to 1.2 times slower than one thread alone (one run, thread start
included). The test's graphs use no `ShadingContext` callbacks (textures, primvars), and
it sets no derivatives, so texture footprints aren't exercised yet.

## Consequences

- `EvalGraph::Evaluate()` is safe to call from many threads (it is `const`, with
  `thread_local` scratch), but not re-entrant across graphs: a `ShadingContext` callback
  (texture system, `geomPropLookup`, `transformSpace`, `colorTransform`) must not evaluate
  another graph on the same thread, because the nested call reuses the outer call's scratch.
  Re-evaluating nodes of the same graph (texture footprints) has its own scratch pool.
- `Evaluate()` heap-allocates each closure tree (`std::vector` nodes); the arena keeps entry
  storage but can't avoid that without changing upstream.
- `Bsdf::SetDielectricLayerThroughputMode()` is process-wide (an atomic), so two renderers
  in one process can't choose differently: it is a device-wide setting at most. Decided
  2026-10-04 by the maintainer: it stays fixed at Typhoon's default (`Bsdl`) and is never
  exposed; native physicallyBased (ticket 26) ports only that mode's filter table.
- The SoA records have a fixed 16 lanes; a wider gang fails to compile.

## Amendments

- Amended by ticket 04: the calls are `PathMaterial_shadeSoA()`, `_evalSoA()`,
  `_subsurfaceSoA()`, `_mediumSoA()` and `_shadowSoA()` (ADR 0004); the shading call also
  takes the renderer's `PathMaterialContext` and resets the arena, and the shadow call
  evaluates without the arena.
- Amended by ticket 47: C++ lanes sample through halcyon's `sampleSurface()`
  (`ClosureSampling.*`), which takes Typhoon's `SampleSurface()` and re-weighs a mix's
  delta samples.
- The arena is a grow-only `std::deque` of entries, which now also hold the pruned closure,
  Adobe's prepared surface and the sample's direction and lobe normal; 416 bytes was the
  entry at ticket 03.
