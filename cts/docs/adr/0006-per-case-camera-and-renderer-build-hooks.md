# Tests may supply a per-Case camera and configure the runner-owned renderer

The runner frames a perspective camera from the world's bounds and creates the
selected renderer subtype with baseline renderer parameters. That is right for
the geometry-style tests, but the camera, renderer, and light categories exist
precisely to vary camera and renderer parameters, which the defaults cannot
express.

A Test may therefore supply a camera builder and a renderer configuration hook:
`cameraBuild(BuildContext&, Bounds)` and
`rendererConfig(BuildContext&, Renderer)`, set via `.camera(...)` and
`.renderer(...)`. When present the runner calls them once per Case. The camera
builder returns a committed camera owned by the runner. The renderer hook
receives the runner-created renderer after its subtype and baseline parameters
have been applied, overrides only what the Test exercises, and leaves the final
commit and ownership to the runner.

We chose this over widening `BuildFn` to return a `{world, camera, renderer}`
bundle (which would force every existing geometry test to hand back nulls and
re-thread the bounds-framing logic into each one) and over having the runner
read camera/renderer axis values directly (which would hard-code parameter names
and value semantics into the runner instead of leaving world authorship in the
Test). Runner-owned renderer construction also ensures the CLI-selected subtype
and baseline parameters apply to every Test, while a parameter Test can still
override the value it is exercising.

Consequences: the runner builds the camera and renderer once per Case (not per
channel), commits the renderer after Test overrides, and releases both with the
world. The camera builder receives world bounds so it can still frame relative
to the scene when it only wants to vary intrinsics (fovy/near/far). A frame
output-format axis (`frame_color_type`) is handled separately by the runner,
which reads it from the Case and selects the color buffer's `ANARIDataType`,
quantizing float color buffers to the 8-bit comparison image identically in
generate and run.
