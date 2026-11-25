---
status: accepted
---
# CORTAO is a third, experimental occlusion estimator, ported for still frames on the CPU

ADR 0001 made ambient occlusion a screen-space estimate from the hit buffer, with the
Alchemy/SAO estimator (`g3d`, `sao`). SAO estimates an obscurance whose falloff and contrast
are tuned by eye, not the occluded share of the hemisphere. CORTAO (Circular Occluder
Ray-Traced Ambient Occlusion, github.com/theagentd/CORTAO) estimates that share: it
approximates each pixel's neighbourhood by circular occluders built from depth samples and
tests a fixed set of hemisphere rays against them. Its author reports GTAO's quality at
under half its GPU time. The question was whether it fits halcyon's CPU preview renderer,
which renders still frames and has no temporal filtering.

## Decision

`ambientOcclusionEstimator` gains `"cortao"`, an ISPC port of CORTAO's reference
implementation (v1.0.3, `420b168d`, MIT; `renderer/preview/Cortao.isph`). It slots into the
existing passes: the hit-buffer pass, the occlusion pass, the blur passes and the
intensity exponent are shared, and `g3d` stays the default. It is experimental. The port
departs from the reference where the reference relies on the GPU or on temporal filtering:

- **World space.** It works on the hit buffer's world positions and normals and projects
  the sampling disk with halcyon's camera, so it needs no projection matrix. The disk's
  pixel positions are linear in the disk offset but for the perspective division, so the
  projections of the pixel's position and the disk's axes are computed once per pixel, as
  the reference's `offsetTransform` does.
- **No lookup table.** Rays are tested against each occluder's angular radius directly,
  the reference's own path without its table. The table is built from the same test
  against the same rays, so it would only trade arithmetic for memory, and vendoring its
  binary data isn't needed.
- **Deterministic random values.** The reference re-randomises per frame and leaves the
  noise to TAA. Here the pixel's cell of the 4×4 pattern `AO_ROTATIONS` picks its random
  values, as for the other estimators, so the first blur pass cancels the pattern and
  renders repeat exactly.
- **The mip chain holds positions.** The reference's prepass builds a point-decimated mip
  chain of packed depth and normal and unprojects the depth at the sample's own
  coordinates. Here the hit-buffer pass builds the same chain (with the reference's child
  choice at random seed 0), each texel the world position and octahedral normal of the
  pixel it holds, and a sample uses that position as stored.
- **Occluders move 0.75 radii into their surfaces, not 1** (`CORTAO_OCCLUDER_OFFSET_SCALE`):
  see Evidence.

`ambientOcclusionBlurPasses` now defaults to 2 for every estimator, not 1.

## Evidence

Measured on a test scene (a room corner with stacked boxes, a pillar, spheres of several
sizes resting on the floor and a packed cluster of spheres; 960×720) against brute-force
ray-traced occlusion with the same radius (1,024 cosine-weighted rays per pixel), from two
camera positions. Timings are medians of 40 interleaved frames on 16 threads. The
accuracy figures were measured with two blur passes for `cortao` and one for `g3d` and
`sao`, the timings with one for all three.

- **Accuracy.** Mean absolute error of the blurred factor: `cortao` 0.025, `g3d` 0.042,
  `sao` 0.063 on the first view; 0.032, 0.049 and 0.063 on the second.
- **Cost.** The `cortao` passes cost about 1.4 times `g3d`'s (31 ms against 22 ms at
  960×720, 122 ms against 90 ms at 1920×1440). The cost is arithmetic per sample, not
  memory: removing the fetch didn't change it, while computing the disk projection once
  per pixel instead of per sample took it from 1.65 to 1.4. The mip chain then matters at
  high resolution (1920×1440: 161 ms with every sample on level 0, 122 ms with the chain)
  and little at 960×720.
- **Blotches, and the blur default.** With 16 samples, the outer, heavily weighted samples
  leave blotches near occluders larger than the first blur pass's window: one sample landing
  on a nearby surface or not toggles an occluder the size of its whole strip of the disk,
  and neighbouring pixels with the same random values sample the same surfaces, so their
  errors are correlated over that area. 64 samples remove them at about 3.3 times `g3d`'s
  cost; a second, à-trous blur pass removes most of them at little cost, and a third
  softens surfaces visibly. Stratifying the cell's random values jointly (a Hammersley set)
  and a lower skew didn't help. Hence two passes by default; `g3d` and `sao` gain the same
  smoothing of the bands near contacts that the second pass was added for.
- **Contacts, and the occluder offset.** With the reference's offset of one radius,
  `cortao` is too light in contact regions (signed error +0.082 where the reference factor
  is below 0.85). Ray count, the pixel's normal bias and the screen-space radius clamp make
  no difference; the offset does (no offset: −0.049, and overall error nearly doubles, as
  convex surfaces occlude themselves). Scanning its scale, 0.75 lowered the error on both
  views (overall 0.027 to 0.025 and 0.034 to 0.032; contacts 0.101 to 0.091 and 0.112 to
  0.102); 0.5 over-darkened walls and convex surfaces on the first view.

## Consequences

- `cortao`'s remaining error is mostly the screen-space limit of ADR 0001: occluders the
  camera doesn't see, such as the underside of a sphere resting on a floor, darken nothing.
  `g3d` is too light in the same places.
- The occluder offset scale was tuned on two views of one scene, a defensible default
  rather than a calibrated one; a broader set of scenes may move it.
- Default images of all estimators change with the second blur pass, and every render with
  ambient occlusion allocates the intermediate blur buffer.
- The port is kept in step with the reference by hand: `Cortao.isph`'s header names the
  ported functions and lists each departure, and `THIRD-PARTY.md` lists the port.
- The sample count is the renderer's `cortaoSamples` (default 16, the reference's), so
  an app can trade cost for fewer blotches; temporal accumulation for frames that don't
  change is the other lever left.
