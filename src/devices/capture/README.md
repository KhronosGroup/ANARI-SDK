# The `capture` device

`capture` is an ANARI device that renders nothing. It accepts the whole ANARI
API and, on each `anariRenderFrame()`, writes a **capture**: a canonical text
snapshot of the committed object graph the frame can reach, arrays included.

It exists so that an application can test the part of ANARI it is responsible
for: did it hand ANARI the right objects and parameters, and does it use the
frames it gets back correctly? That doesn't depend on any device's pixels. An
application test loads `capture` (for example with `ANARI_LIBRARY=capture`),
renders, and compares the capture with a committed baseline using the capture
diff (see [The capture diff](#the-capture-diff)).

Conformance of a device to the spec is the CTS's job, not this device's. The
capture device is not a CTS candidate.

## Using it

```cpp
auto lib = anari::loadLibrary("capture", statusCallback);
auto device = anari::newDevice(lib, "default");
// ... build the scene, create a frame, anari::render(device, frame) ...
std::string capture = /* anariGetProperty(frame, "capture", ANARI_STRING) */;
```

The application needs no change besides selecting the library.

### Device parameters

| Parameter          | Type              | Default            | Meaning |
|--------------------|-------------------|--------------------|---------|
| `captureDir`       | `ANARI_STRING`    | `ANARI_CAPTURE_DIR`, else none | Directory to write captures and blobs to. If neither the parameter nor the environment variable is set, no files are written. |
| `extensions`       | `ANARI_STRING_LIST` | every extension the spec defines (core and KHR) | The extensions to advertise. An empty list advertises none. |
| `inlineArrayLimit` | `ANARI_UINT32`    | 64                 | Arrays with more elements than this are written as blobs. |

The device advertises the list through `anariGetProperty(device, "extension")`
(a sorted, null-terminated string list). `anariGetDeviceExtensions()` on the
library has no device to ask and returns every extension. Subtype and
parameter queries (`anariGetObjectSubtypes`, `anariGetObjectInfo`,
`anariGetParameterInfo`) are answered from the spec definitions, whatever
`extensions` says.

### Frame properties

All are read with `anariGetProperty()`.

| Property                    | Type            | Value |
|-----------------------------|-----------------|-------|
| `capture`                   | `ANARI_STRING`  | The text of the frame's last capture. Copied into the caller's buffer, like `version.name`. Empty before the first render. |
| `capture.size`              | `ANARI_UINT64`  | Its size in bytes, including the terminating NUL. Ask this first. |
| `capture.blobs`             | `ANARI_STRING`  | The names of the blobs the last capture refers to, one per line. `capture.blobs.size` is its size. |
| `capture.blob.<name>`       | `ANARI_STRING`  | The bytes of that blob. *Raw bytes, not a C string:* exactly `capture.blob.<name>.size` bytes are copied, with no NUL. |
| `capture.blob.<name>.size`  | `ANARI_UINT64`  | The number of bytes in that blob. |
| `duration`                  | `ANARI_FLOAT32` | Always 0. |

A capture compared in memory needs its blobs too; the diff library takes them
as `Capture::blobs`.

### Files

If a capture directory is set, each render writes

```
<captureDir>/frame<FFFF>_render<RRRR>.capture
<captureDir>/blobs/<name>.bin          (for each blob the capture refers to)
```

`FFFF` is the frame's place among the frames the device has created (0, 1, 2,
...) and `RRRR` is that frame's render counter, both zero-padded to four
digits, so the last render of a test sorts last. The directory is created if
it does not exist. Blobs are named by their content, so one that already
exists is not written again. A directory that can't be written is reported as
an ERROR and the render carries on; the `capture` property is still set.

### Rendering and frame channels

Rendering is synchronous: the work is building the capture. `anariFrameReady()`
always reports complete, and the frame's completion callback, if it has one, is
called once per render.

`channel.color` can be mapped as `ANARI_FLOAT32_VEC4`, `ANARI_UFIXED8_VEC4` or
`ANARI_UFIXED8_RGBA_SRGB`, and `channel.depth` as `ANARI_FLOAT32`, as long as
the frame declares the channel (`channel.color`, `channel.depth`: an
`ANARI_DATA_TYPE` parameter) and a `size`. Any other channel the frame
declares maps as zeros, in the type it declares. A channel the frame does not
declare does not map. The contents depend only on the frame size and these
frame parameters:

| Parameter           | Type                 | Default        | Meaning |
|---------------------|----------------------|----------------|---------|
| `capture.mode`      | `ANARI_STRING`       | `checkerboard` | `checkerboard` or `solid` |
| `capture.cellSize`  | `ANARI_UINT32`       | 8              | Width and height of a checkerboard cell, in pixels |
| `capture.color0`    | `ANARI_FLOAT32_VEC3` | (1, 1, 1)      | The color of even cells, and of every pixel when `solid` |
| `capture.color1`    | `ANARI_FLOAT32_VEC3` | (0, 0, 0)      | The color of odd cells |
| `capture.alpha`     | `ANARI_FLOAT32`      | 1              | The alpha of every pixel |
| `capture.depthNear` | `ANARI_FLOAT32`      | 0              | Depth at the left edge |
| `capture.depthFar`  | `ANARI_FLOAT32`      | 1              | Depth at the right edge |

The pixel at column `x`, row `y` (rows in memory order) is in cell
`(x / cellSize + y / cellSize)`; even cells are `color0`, odd cells `color1`.

- `ANARI_FLOAT32_VEC4`: `(r, g, b, alpha)`.
- `ANARI_UFIXED8_VEC4`: each component rounded to the nearest of 255 codes.
- `ANARI_UFIXED8_RGBA_SRGB`: the same, with `r`, `g` and `b` sRGB encoded
  first (the piecewise IEC 61966-2-1 curve) and alpha left linear. These are
  the conversions helide uses.
- Depth is `depthNear + (depthFar - depthNear) * (x + 0.5) / width` in
  `float`, the same in every row.

## The capture format

A capture is UTF-8 text. This section is the contract for anything that reads
one. The current version is **1**.

### Example

This is what the device wrote for a one-triangle scene whose vertex array is
over the inline limit (4 here) and for a frame that sets a parameter the spec
does not define:

```
anari-capture 1

device {
  extensions ANARI_STRING_LIST [
    "ANARI_KHR_CAMERA_PERSPECTIVE"
    "ANARI_KHR_GEOMETRY_TRIANGLE"
    "ANARI_KHR_MATERIAL_MATTE"
  ]
}

frame@0 {
  bogus ANARI_INT32 1
  camera ANARI_CAMERA camera@0
  channel.color ANARI_DATA_TYPE ANARI_UFIXED8_RGBA_SRGB
  renderer ANARI_RENDERER renderer@0
  size ANARI_UINT32_VEC2 (64, 48)
  world ANARI_WORLD world@0
}

camera@0 "perspective" {
  fovy ANARI_FLOAT32 0.7
  position ANARI_FLOAT32_VEC3 (0, 0, -3)
}

renderer@0 "default" {
}

world@0 {
  instance ANARI_ARRAY1D array1d@0
}

array1d@0 {
  elementType ANARI_INSTANCE
  dimensions (1)
  data [
    instance@0
  ]
}

instance@0 "transform" {
  group ANARI_GROUP group@0
}

group@0 {
  surface ANARI_ARRAY1D array1d@1
}

array1d@1 {
  elementType ANARI_SURFACE
  dimensions (1)
  data [
    surface@0
  ]
}

surface@0 {
  geometry ANARI_GEOMETRY geometry@0
  material ANARI_MATERIAL material@0
}

geometry@0 "triangle" {
  vertex.position ANARI_ARRAY1D array1d@2
}

array1d@2 {
  elementType ANARI_FLOAT32_VEC3
  dimensions (6)
  count 6
  min (0, 0, 0)
  max (5, 0.5, 0)
  blob 674a2d1779237ed0
}

material@0 "matte" {
  color ANARI_FLOAT32_VEC3 (0.8, 0.2, 0.1)
}

warnings {
  "frame@0: unknown parameter \"bogus\""
}
```

### Grammar

The format is line oriented. Indentation is two spaces and carries no meaning;
readers should ignore leading and trailing whitespace. Blank lines between
blocks carry no meaning.

```
capture   = header NL { block }
header    = "anari-capture" SP version NL
block     = blockhead " {" NL { line } "}" NL
blockhead = "device" | "warnings" | id [ SP quoted ]     ; quoted: the subtype
id        = label "@" number                              ; "camera@0"
```

Blocks are, in this order: the `device` block; one block per object, in
traversal order (the frame first); the `warnings` block, only if there are
warnings. Readers must not depend on the order of the object blocks: they are
reached by following references from the frame.

**`device` block.** One line, `extensions ANARI_STRING_LIST` followed by a
list (below) of the advertised extensions, sorted.

**`warnings` block.** One quoted string per line, in the order they were seen.

**Object blocks** (everything but arrays). Each line is a parameter:

```
line   = name SP type SP value NL
```

`name` is the parameter name (no spaces; it may contain dots), `type` is an
ANARI data type name such as `ANARI_FLOAT32_VEC3` (as `anari::toString()`
spells it), and parameters are sorted by name (byte order). Only the parameters
that were set *and committed* are written (see Snapshot semantics). The
subtype in the block head is the subtype the object was created with; it is
left out if the object has none (surfaces, groups, worlds, frames).

**Array blocks** (`array1d`, `array2d`, `array3d`) have fields instead of
parameters, in this order:

```
elementType <ANARI type name>
dimensions (<n>)  |  (<n>, <n>)  |  (<n>, <n>, <n>)
```

followed by either

```
data [
  <element>
  ...
]
```

for arrays with at most `inlineArrayLimit` elements, and always for arrays of
objects, whose elements are IDs (or `null`) in element order; or

```
count <number of elements>
min <value>        (numeric element types only)
max <value>        (numeric element types only)
blob <name>
```

for larger arrays of data. `min` and `max` hold the smallest and largest value
of each component, written like an element (NaNs are ignored unless all values
are NaNs). A 1D array with a region (`KHR_ARRAY1D_REGION`) is written as the
elements of its region, and its dimension is the region's size. Arrays have no
parameters in the capture.

**Values.** What follows a parameter's type, by type:

| Type | Value |
|------|-------|
| an object or array type (`ANARI_CAMERA`, `ANARI_ARRAY1D`, ...) | The object's ID, e.g. `camera@0`. |
| `ANARI_STRING` | A quoted string. Inside the quotes, `\"` is a quote, `\\` a backslash, `\n`, `\r`, `\t` are control characters, and any other control character is `\xHH`. Other bytes are written as they are. |
| `ANARI_STRING_LIST` | `[]` if empty; otherwise `[`, one quoted string per line, and `]` on a line of its own. |
| `ANARI_DATA_TYPE` | The type's name. |
| `ANARI_BOOL` | `true` or `false`. |
| integer and fixed-point types | Decimal integers. Fixed-point types (`UFIXED8`, `FIXED16`, ...) are written as their raw stored integers, not as the normalized value. |
| float types | Floats (below). |
| `ANARI_VOID_POINTER`, `ANARI_FUNCTION_POINTER`, `ANARI_MEMORY_DELETER`, `ANARI_STATUS_CALLBACK`, `ANARI_FRAME_COMPLETION_CALLBACK` | The fixed placeholder `<opaque>`. The pointer value is never written, so captures are reproducible. |
| any other type | `<opaque>`. |

Types with more than one component (vectors, boxes, regions, matrices,
quaternions, `*_SRGB` colors) are written as a tuple in memory order,
`(0.1, 0.2, 0.3)`, one value per component with `, ` between. A matrix is its
components in the order they are stored. A one-component type is a bare value.
Array elements are written the same way, one per line, and by the same rule.

**Floats** are written in the shortest decimal form that reads back as the
same bits at their own precision (`float` for `FLOAT16` and `FLOAT32`,
`double` for `FLOAT64`): `0.1`, `0.33333334`, `-10`. Decimal notation is used
when the decimal exponent is from -5 to 15, otherwise `d.ddde+XX` (`1e-30`,
`3.4028235e+38`). `nan`, `inf`, `-inf` and `-0` are written like that. A
capture is an exact record: any tolerance is the diff's.

### Snapshot semantics

A capture is taken at `anariRenderFrame()`, after the device flushes its
commit buffer, and holds each reachable object's **committed** parameters at
that moment:

- A parameter that was set and not committed is absent. A value set after the
  last commit is absent; the value at the last commit is written.
- A parameter that was unset and committed is absent. One that was unset but
  not committed is still there.
- Reachability starts at the frame: its parameters (`camera`, `renderer`,
  `world`, ...), then every object and array reachable through object-typed
  parameters and arrays of objects. Objects the frame can't reach are left out,
  however they were created.
- An object reached more than once is written once and referred to by ID
  (the graph is a DAG).
- Arrays that share the application's memory (`anariNewArray1D` with a
  non-null pointer) are written with what that memory holds at render time.

### IDs

An ID is `<label>@<n>`. The label is one of `frame`, `camera`, `renderer`,
`world`, `instance`, `group`, `light`, `surface`, `geometry`, `material`,
`sampler`, `spatialField`, `volume`, `array1d`, `array2d`, `array3d`. `n`
counts from 0 per label, in traversal order: depth first, parameters in name
order, the elements of an object array in element order, an ID assigned when
an object is first reached. IDs come from the frame's graph, never from
creation order or handles, so a refactor that creates objects in another order
doesn't change a capture.

Readers should treat IDs as local to a capture: the diff pairs objects by
their place in the graph, never by ID.

### Blobs

A blob holds the raw bytes of an array's elements (host byte order), exactly
`count * sizeof(element)` bytes, in a file `blobs/<name>.bin` beside the
capture file. Its name is the 64-bit FNV-1a hash (offset basis
`0xcbf29ce484222325`, prime `0x100000001b3`) of these bytes, as 16 lowercase
hex digits:

1. the element type's `ANARIDataType` value, as 4 little-endian bytes;
2. the number of dimensions, as 4 little-endian bytes;
3. each dimension, as 8 little-endian bytes;
4. the element bytes.

So a blob of 4 `ANARI_UINT8` values `1, 2, 3, 4` in a 1D array is named
`f05edf276f4dba45`. The name identifies the content: two arrays with the same
name have the same type, shape and bytes. There is no cryptographic guarantee.
A capture that is not written to files keeps its blobs in memory (see Frame
properties).

### Warnings

While it builds a capture, the device checks each reachable object against the
spec definitions the device was built from (every core and KHR extension), and
notes API misuse. Each problem is written to the `warnings` block and
reported to the status callback as a WARNING *with the same text*. These are
the messages (`<id>` is the object's ID):

| Message | When |
|---------|------|
| `<id>: unknown subtype "<s>"` | The object type has subtypes, and `<s>` is not one. The parameters of such an object are not checked. |
| `<id>: unknown parameter "<p>"` | The spec defines no parameter `<p>` for the object's type and subtype. |
| `<id>: parameter "<p>" has type <T>, expected <T1> or <T2>` | The parameter was set with a type the spec does not allow. The value is still written. |
| `<id>: array is still mapped at render time` | An array was mapped and not unmapped. |

Renderers are not checked, as each device defines its own renderer subtypes
and parameters. The device only checks; it creates an object of any subtype
and accepts any parameter, so an application's mistake is visible and the
rest of the capture still comes out. The extensions the device advertises do
not change these checks.

### Versions

The version is the number on the first line. A reader must reject a version it
does not know. A change that changes what a capture says (new meaning for a
line, a different float format, a different hash) increments the version;
adding a field readers can ignore is not expected, so readers should treat
unknown lines as an error.

## The capture diff

`anari_capture_diff` is a small C++ library (it has no dependency on the
device or on the ANARI headers, so an application can link it alone), and
`anariCaptureDiff` is a command-line wrapper. Both are built with the device
(`BUILD_CAPTURE_DEVICE`, on by default) and installed; the library is the
exported CMake target `anari::anari_capture_diff`.

The comparison walks both object graphs from the frame, pairing objects by
their place in the graph. A harmless reordering of blocks in the file is not a
difference, and neither is renaming IDs. Integers, strings, data types,
subtypes, array element types and sizes, and the structure of the graph
(which objects are shared) must match exactly. Floats match within a
tolerance. Every difference is reported as a path built from parameter names
and array indices:

```
world.surface[2].geometry.vertex.position[17]: expected (0.1, 0.2, 0.3), got (0.1, 0.2, 0)
camera.fovy: expected ANARI_FLOAT32 0.5, got <absent>
camera.@subtype: expected "perspective", got "orthographic"
```

`@subtype`, `@elementType` and `@dimensions` stand for those properties of the
object at the path. The frame is the root; its parameters are `camera`,
`world`, and so on. The `device` block's extensions are compared as a set
(`device.extensions`) and the warnings in order (`warnings[0]`).

### Options

- **Float tolerance.** Two floats match if they are equal, or within an
  absolute epsilon, or within a relative epsilon times the larger magnitude,
  or at most a number of ULPs apart. By default all are zero. NaNs match NaNs.
  It applies to values and to array elements, blobs included, never to
  integers.
- **Unordered paths.** Object-array parameters to compare as multisets, by
  their path from the frame: `world.surface`. A segment without an index
  matches any index (`world.instance.group.surface` is every group's
  surfaces); `[3]` matches one index and `[*]` any. Elements are paired by a
  structural hash of their subgraph, then, for what is left, by comparing
  them (so floats within the tolerance still pair). Elements that stay
  unpaired are reported.
- **Caps.** At most `maxDifferences` (default 50) differences are listed; all
  are counted, and the report says how many there are. For an array, at most
  `maxIndicesPerArray` (default 8) differing elements are listed, then one
  line says how many more there were.

When two blobs have different names, both are loaded and compared element by
element, which is also how an array written inline in one capture compares
with one written as a blob in the other.

### Library

```cpp
#include <anari/ext/capture/CaptureDiff.h>
namespace diff = anari::capture_diff;

diff::Capture expected, actual;          // text, plus blobs held in memory
std::string error;
diff::readCaptureFile("baseline.capture", expected, error);  // blobs/ beside it
actual.text = captureText;               // from the frame property
actual.blobs[name] = bytes;              // in-memory blobs

diff::DiffOptions options;
options.tolerance.absolute = 1e-6;
options.unorderedPaths = {"world.surface"};

diff::DiffResult result = diff::diffCaptures(expected, actual, options);
if (result.outcome != diff::Outcome::MATCH)
  std::cerr << result.report();
```

`Outcome::INVALID` is for input the diff can't use: text that is not a
capture, a format version it does not know, a blob that can't be found.
`diffCaptureFiles()` reads both files, resolving each file's blobs in the
`blobs` directory beside it.

### Command line

```
anariCaptureDiff [options] <expected> <actual>

  --abs-tol <x>          floats match within x of each other
  --rel-tol <x>          floats match within x times their magnitude
  --ulps <n>             floats match within n representable values
  --unordered <path>     compare the object array at <path> as unordered
                         (repeatable)
  --max-differences <n>  list at most n differences (default 50)
  --quiet                print nothing
```

Exit status: **0** if the captures match, **1** if they differ (the report is
on stdout), **2** if an input is unreadable, has a format version the tool does
not support, or the command line is wrong (the reason is on stderr).

## Not in scope

The capture device records state, not API calls: for a trace, use the debug
layer. It renders nothing that depends on the scene (rasterized bounds, ID
buffers). It does not merge or compare captures from several MPI ranks (each
rank's device writes its own), and nothing reads a capture back into a device.
