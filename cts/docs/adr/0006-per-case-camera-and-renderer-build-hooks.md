# Tests may supply a per-Case camera and renderer; the runner owns defaults

The runner frames a perspective camera from the world's bounds and renders with
a parameterless `default` renderer. That is right for the geometry-style tests,
but the camera, renderer, and light categories exist precisely to vary camera
and renderer parameters, which the bounds-framed defaults cannot express.

A Test may therefore supply two optional builders on its definition:
`cameraBuild(BuildContext&, Bounds)` and `rendererBuild(BuildContext&)`, set via
the builder verbs `.camera(...)` / `.renderer(...)`. When present the runner
calls them once per Case (reading that Case's axis values from the context) and
takes ownership of the returned, committed object; when absent it falls back to
the bounds-framed camera / `default` renderer. World-build, camera-build, and
renderer-build stay three separate functions so each category only overrides
what it exercises.

We chose this over widening `BuildFn` to return a `{world, camera, renderer}`
bundle (which would force every existing geometry test to hand back nulls and
re-thread the bounds-framing logic into each one) and over having the runner
read camera/renderer axis values directly (which would hard-code parameter names
and value semantics into the runner instead of leaving world authorship in the
Test). The two optional `std::function`s add the least surface while keeping the
common case — no hook — unchanged.

Consequences: the runner builds the camera and renderer once per Case (not per
channel) and releases them with the world. The camera builder receives the
world bounds so it can still frame relative to the scene when it only wants to
vary intrinsics (fovy/near/far). A frame output-format axis (`frame_color_type`)
is handled separately by the runner, which reads it from the Case and selects
the color buffer's `ANARIDataType`, quantizing float color buffers to the 8-bit
comparison image identically in generate and run.
