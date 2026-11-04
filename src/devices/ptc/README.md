# The `ptc` device

`ptc` (pass-through compositing) is an ANARI device for data-parallel
rendering with MPI. Each MPI rank renders its own part of the data with
another ANARI device, the *wrapped* device, and `ptc` composites the ranks'
images by depth into one image on rank 0. An application that already has an
ANARI rendering path becomes data-parallel by loading `ptc` instead of its
device. `ptc` passes the scene through to the wrapped device and wraps each
frame: the wrapped frame renders the rank's data, and `ptc` composites it.

`ptc` implements `KHR_DATA_PARALLEL_MPI` and `KHR_FRAME_COMPLETION_CALLBACK`
itself. Compositing runs on the CPU, in host memory, whichever device it
wraps.

For the background, see

```
Standardized Data-Parallel Rendering Using ANARI
Ingo Wald, Stefan Zellmann, Jefferson Amstutz, Qi Wu, Kevin Griffin, Milan Jaros, Stefan Wesner
IEEE Symposium on Large Data Analysis and Visualization (LDAV), 2024
```

(free PDF at <https://arxiv.org/abs/2407.00179>). `ptc` started out as
[ANARI-PTC](https://github.com/ingowald/ANARI-PTC).

## Building

Configure the SDK with `-DBUILD_PTC_DEVICE=ON`. This builds
`anari_library_ptc`, so `anariLoadLibrary("ptc", ...)` loads it. It needs:

- MPI development packages (any MPI implementation)
- oneTBB 2021.6 or newer

`src/devices/ptc` is also a source tree of its own, built against an
installed ANARI SDK that `find_package(anari)` finds:

```bash
cmake -S src/devices/ptc -B build-ptc -DCMAKE_PREFIX_PATH=/path/to/anari/install
cmake --build build-ptc
cmake --install build-ptc --prefix /path/to/install
```

### Tests

With `BUILD_TESTING=ON` and `BUILD_HELIDE_DEVICE=ON`, the device's MPI tests
are registered with ctest. Built on its own, ptc has its own `BUILD_TESTING`
option (default ON), and the tests wrap the helide installed with the SDK it
was built against. They run under `mpiexec` on 1 to 4 ranks, with
`helide` as the wrapped device:

```bash
ctest -R '^ptc_'
```

The tests set `LD_LIBRARY_PATH` themselves so that the freshly built `ptc`
and `helide` come first, and each test checks which `ptc` library it loaded.
They are skipped, with a warning at configure time, if `mpiexec` is not
found. With Open MPI, the multi-rank tests pass `--oversubscribe`, so they
also run on machines with fewer cores than ranks.

## Usage

Load `ptc` instead of the device the application would use, for example with
`ANARI_LIBRARY=ptc`, and choose the wrapped device as below. Run the
application with one process per rank under `mpiexec`.

### Choosing the wrapped device

Which library `ptc` wraps is decided when the device first needs it, in this
order:

1. The device parameter `wrappedLibrary` (`ANARI_STRING`, a library name as
   for `anariLoadLibrary`), if the app set it. There is no fallback: if it
   fails to load, that is a fatal error.
2. The environment variable `PTC_ANARI_LIBRARY`, if set. If it fails to load,
   `ptc` warns and falls back to `helide`.
3. `helide`.

The device parameter `wrappedDeviceSubtype` (`ANARI_STRING`, default
`"default"`) selects the device subtype created from that library.

```cpp
anariSetParameter(device, device, "wrappedLibrary", ANARI_STRING, "visrtx");
anariCommitParameters(device, device);
```

`ptc` loads the wrapped device at the device's first commit, or earlier if
an object is created or the device is queried (`anariGetProperty`,
`anariGetObjectSubtypes`, ...) before that. So set both parameters before
the first `anariCommitParameters` on the device. Changing them later is
ignored with a warning. They are not forwarded to the
wrapped device; every other device parameter is.

All ranks must wrap the same device type (the same library and subtype).

If the wrapped library or device fails to load, `ptc` reports an
`ANARI_SEVERITY_FATAL_ERROR` once. After that, object creation returns null,
and every call that needs the wrapped device reports an error and does
nothing. `ptc`'s own device properties (`extension`, `version`) still answer.

### Device properties

The device property `version` (and `version.major`, `.minor`, `.patch` and
`.name`) is `ptc`'s own. `anariVersion.*`, `geometryMaxIndex` and other
properties are the wrapped device's. The `extension` list is the wrapped
device's, plus `KHR_DATA_PARALLEL_MPI` and `KHR_FRAME_COMPLETION_CALLBACK`.

`KHR_DATA_PARALLEL_MPI` makes `anariGetProperty` collective. `ptc` answers it
from the local rank without any communication, which is why all ranks must
wrap the same device type.

The library-level list from `anariGetDeviceExtensions(library, ...)` has no
device to read `wrappedLibrary` from, so it reflects only `PTC_ANARI_LIBRARY`
or `helide` (with the same warning on fallback), plus `ptc`'s own extensions.
When a device wraps a library chosen by parameter, query its `extension`
property instead.

### Compositing

Each rank renders its own data, then `ptc` composites the ranks' frames
pixel by pixel:

- A pixel's fragments, one per rank, are sorted by depth, nearest first;
  fragments at equal depth are ordered by rank. A depth of 1e20 or more
  (helide writes `FLT_MAX` for a miss, other devices +infinity) or NaN is a
  miss and sorts behind every hit.
- The sorted fragments are blended with premultiplied "over", nearest in
  front, and the output alpha is the composited alpha. The colors are taken to be
  premultiplied, as helide and VisRTX write them. `UFIXED8_RGBA_SRGB` is
  decoded to linear before blending and encoded after; its alpha is linear.
- The composited `channel.depth` is the nearest fragment's depth. Where
  every rank missed it is +infinity, even if the wrapped device wrote
  `FLT_MAX`.
- The id channels (`channel.primitiveId`, `channel.objectId`,
  `channel.instanceId`) are the nearest fragment's.

`channel.color` can be `UFIXED8_VEC4`, `UFIXED8_RGBA_SRGB` or `FLOAT32_VEC4`.
Other channels (normal, albedo, ...) are not composited, and mapping them is
refused.

**Render each rank with a transparent background**: set the renderer's
`background` to a color with alpha 0. Every rank's background is a fragment
too, so an opaque background on one rank hides every rank behind it. helide's
default background is opaque black, so set it explicitly. Where all ranks
miss, the backgrounds are blended in rank order.

### The `mpiCommunicator` frame parameter

By default a frame composites over `MPI_COMM_WORLD`, and world rank 0
receives the image. To composite over another communicator, set the frame's
`mpiCommunicator` parameter (type `ANARI_VOID_POINTER`) to a *pointer to* an
`MPI_Comm`:

```cpp
MPI_Comm comm = ...; // e.g. from MPI_Comm_split
anariSetParameter(device, frame, "mpiCommunicator", ANARI_VOID_POINTER, &comm);
anariCommitParameters(device, frame);
```

Passing a pointer works the same whether `MPI_Comm` is an `int` (MPICH) or a
pointer (Open MPI). `ptc` reads it during the frame's commit, so the
`MPI_Comm` variable only has to stay valid until `anariCommitParameters`
returns. `ptc` keeps its own duplicate of the communicator, so the app may
free its communicator after the commit.

- Rank 0 *of that communicator* receives the composited image. On the other
  ranks the frame can still be mapped, but its contents are undefined.
- A frame's first commit is collective over its communicator, which is
  `MPI_COMM_WORLD` unless `mpiCommunicator` is set before that commit. A
  frame created on only some ranks must have `mpiCommunicator` set before
  its first commit, or the commit hangs.
- Committing a frame with a new communicator is collective: every rank of the
  new communicator, and of the one it replaces, must commit the frame
  together. A communicator with the same ranks in the same order as the
  current one is kept as it is.
- Unsetting `mpiCommunicator` goes back to `MPI_COMM_WORLD`.
- A pointer to `MPI_COMM_NULL` (e.g. on a rank that `MPI_Comm_split` left out)
  takes part in tearing down the frame's previous communicator, then reports
  an error; the frame cannot be rendered until another communicator is
  committed.
- Rendering and releasing a frame are collective over its communicator: every
  rank of it must render, or release, the frame.
- If rendering or preparing a frame fails on some ranks only (e.g. out of
  memory, or an unsupported color type), the ranks agree on that before
  compositing: every rank skips compositing and reports an error, instead of
  the others hanging. Committing a communicator agrees the same way on whether
  every rank created its compositor. A failure *inside* the compositor's
  communication can still leave the other ranks waiting.

### Frame completion callback

`ptc` implements `KHR_FRAME_COMPLETION_CALLBACK` itself; the wrapped device's
callback, if it has one, is not used.

- The callback runs synchronously on the application's thread, inside
  `anariRenderFrame`, after compositing. It receives the `ptc` device and
  frame handles.
- It runs on every rank of the frame's communicator, not only on the rank
  that receives the image.
- It runs even when the render failed, after the render's error was
  reported.
- Rendering or committing the frame from inside its own callback is refused
  with an error.

### Diagnostics

`ptc` prints nothing: everything it has to say goes through the ANARI status
callback.

- A frame parameter `ptc` tracks (`size`, `channel.color`, `channel.depth`,
  the id channels, `mpiCommunicator`, `frameCompletionCallback` and its user
  data) set with the wrong type is ignored, as in helide, and the next commit
  warns about it once.
- A `channel.color` type `ptc` cannot composite is rejected at commit with an
  error and `ANARI_STATUS_INVALID_ARGUMENT`, once, at the commit after it was
  set. The frame then fails to render, with an error each time, on every rank
  of its communicator, until a supported type is set or `channel.color` is
  unset.
- After a failed render, mapping any channel is refused with a warning that
  says so.

### MPI initialization and device lifetime

If the app has initialized MPI before a `ptc` device first needs it, `ptc`
uses it and never finalizes it. Otherwise `ptc` calls `MPI_Init` itself, and
keeps MPI initialized until the last `ptc` device is released, which then
calls `MPI_Finalize`. MPI cannot be initialized again after that, so an app
that creates `ptc` devices one after another should initialize MPI itself.

Releasing a device is collective: every rank must release its devices in the
same order.

**Release a device's frames before the device.** Releasing the device first
is not supported: the frames' later release then uses the freed device, and
may tear down their compositors after `MPI_Finalize`.

Each compositor holds a TBB scheduler handle and calls `tbb::finalize` when
it is destroyed, so that TBB's worker threads do not outlive the library.
This is best effort: it does not help if the application or the wrapped
device also uses TBB.

### Limits

The compositor's pixel counts and MPI message counts and offsets are `int`.
A frame with `FLOAT32_VEC4` color must have fewer than 2^29 (about 536
million) pixels, and one with 8-bit color fewer than 2^31. The split of the
frame between ranks also limits it to fewer than 2^37 / *ranks* pixels (about
536 million on 256 ranks). Larger frames are not checked for and are not
supported.
