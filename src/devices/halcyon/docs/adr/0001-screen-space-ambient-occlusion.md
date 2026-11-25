---
status: accepted
---
# Ambient occlusion is computed in screen space, not by tracing rays

Halcyon has an Embree scene at hand, so the obvious ambient occlusion is a few occlusion rays
per surface hit. We compute it in screen space instead, from a per-pixel hit buffer, because
ray-traced ambient occlusion at interactive frame rates needs accumulation over frames or
denoising to look acceptable, and halcyon has neither. Screen-space occlusion costs a fixed
amount per pixel whatever the scene, at the price of missing off-screen occluders and the
usual screen-space artifacts. It exists to find out whether that quality is good enough on
the CPU.

## Consequences

- The frame becomes multi-pass when occlusion is enabled: a hit-buffer pass, an occlusion pass,
  a blur pass, then the ordinary shading pass, which traces primary rays a second time.
- Only the first visible surface receives occlusion; layers behind a transparent surface, volumes
  and the background do not.
- The occlusion radius needs a world-space scale, so a renderer parameter exists for it.
