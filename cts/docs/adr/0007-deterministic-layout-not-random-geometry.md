# CTS geometry uses a deterministic, human-viewable Layout, not random generation

The CTS builds each geometry Case as a **Layout** — a near-square grid of
identical canonical primitives facing the camera, at fixed orientation and
uniform size — instead of the previous randomly positioned, oriented, and sized
primitives produced by `PrimitiveGenerator`. The same applies to the material
backdrop (regular grid, planar UVs, a left-to-right opacity ramp) and to the
structured field behind isosurface and volume (a deterministic analytic field —
radial distance to start, parameterized by a swappable field function — instead
of random scalars). The feature combinations a Test asserts are unchanged: they
live in the Test's axes (soup-vs-indexed, primitive count, shape, caps, global
vs per-vertex radius, unused vertices, color binding, frame color type, volume
filter/origin/spacing/valueRange/color), all of which are preserved. Only the
*incidental* geometric variety random gave for free is dropped.

## Considered Options

- **Keep random generation (status quo).** Rejected: the renders are a chaotic
  pile that no human can read, which makes visual review and debugging of a
  failing Case almost useless — the original motivation for the change.
- **Deterministic Layout (chosen).** Renders are orderly and reviewable; the
  named feature axes still get full coverage.

## Consequences

- Random generation gave incidental coverage of many orientations, positions,
  sizes, and degenerate frustums "for free." We deliberately trade that breadth
  for readability, betting that conformance is asserted by the *named* axes, not
  by geometric variety. If a specific orientation/degeneracy ever matters, it
  should become an explicit, named Axis rather than a side effect of RNG.
- `GeometryOptions::seed` and the per-Test `seed` arguments are removed; the CTS
  geometry path carries no RNG. `PrimitiveGenerator` remains, used only by the
  viewer's `scenes/performance/primitives.cpp` stress scene, where a large random
  pile is the point.
- This strengthens the cross-platform reproducibility ADR-0005 depends on.
  ADR-0005 named the `seed` parameters as the determinism mechanism, but
  `std::mt19937` + `std::shuffle` are not guaranteed to produce identical results
  across STL implementations — a latent hazard for generate-on-demand ground
  truth. An RNG-free Layout makes the reference device deterministic by
  construction, removing that hazard.
- Index draw-order independence (previously exercised by an RNG `shuffleVector`
  on indexed primitives) is no longer covered; it was never a named axis. If
  wanted, it should be added as an explicit Axis.
