# AGENTS.md

## Overview

Halcyon is an ANARI device built on helium (the ANARI SDK's device framework), Embree 4 and
ISPC. Its reference is **helide**, the ANARI SDK's Embree device
(`src/devices/helide` in the ANARI-SDK repo): halcyon aims to support what helide supports and
to render the same images, with the deliberate exceptions listed under
[Deliberate differences from helide](#deliberate-differences-from-helide). The main structural
difference is that helide shades in C++ (`renderer/Renderer.cpp`), while halcyon mirrors every
object into an ISPC struct and shades in ISPC (`renderer/preview/Preview.ispc`). Porting a helide feature
therefore usually means rewriting it in ISPC. The fixes to helide, helium and the CTS that
halcyon's work found landed in ANARI-SDK 0.17 (`next_release`); where this file compares with helide or helium, it
says which version it means.

**"Maintainer decision, keep it"** marks a behaviour halcyon's maintainer (Jefferson
Amstutz) chose deliberately, often against helide, Typhoon or the specification: don't
change it without asking them. halcyon was developed in its own repository
(`jeffamstutz/anari-halcyon`, now deprecated), then briefly lived in VisRTX
(`devices/halcyon`), before it moved here to the ANARI SDK under Apache-2.0; commit hashes this
file and `docs/` cite are that repository's, and the ticket numbers in `docs/` refer to
its private tracker.

## Build and test

Configure once with CMake (see `README.md` for dependencies), then:

```sh
cmake --build <build> -j
ctest --test-dir <build> --output-on-failure        # all tests
ctest --test-dir <build> -R '^halcyon::quad$' --output-on-failure   # one test
LD_LIBRARY_PATH=<build> anariInfo -l halcyon        # introspection as apps see it
```

`ctest --test-dir` needs CMake 3.20; with 3.19 (the minimum), run `ctest` inside the build
directory.

halcyon builds alone (`<build>` above) or in the ANARI SDK's tree as `src/devices/halcyon`,
with `-DBUILD_HALCYON_DEVICE=ON` (default OFF: it needs ISPC, which the SDK's other devices
and CI don't). Either way it is its own CMake project, as the SDK's other devices are: it
takes the ANARI SDK's version (the root's in the SDK's tree, the found SDK's when built
alone), writes its outputs to `CMAKE_BINARY_DIR`, and names its ctests `halcyon::<name>`.

Embree is fetched and built statically at configure time, as helide does
(`external/embree/CMakeLists.txt`, target `anari_embree`, the same file as helide's: in the
SDK's tree whichever device configures first builds Embree and the other links it): Embree 4.3.3, internal tasking,
x86 ISAs SSE2 through AVX-512 (NEON2X on ARM). The device links no TBB; `parallel_for()` loops
use Embree's `algorithms/parallel_for.h` on the same thread pool as BVH builds. There is
no `EMBREE_API_NAMESPACE`: the ISPC code calls the C API, which a namespace would mangle. Embree's API symbols are hidden in the static library instead, and the tasking
library's exported `TaskScheduler` is kept out of the device's dynamic symbols
(`--exclude-libs`), so another Embree in the process can't interpose it.

Every build includes the `quality` renderer. For it, the build also compiles
`halcyon_typhoon_materials`, Typhoon's MaterialXCpp and BSDL vendored unmodified in
`external/typhoon/src/` (pin and exclusions in `external/typhoon/PROVENANCE.md`, why in
`docs/adr/0002-vendor-typhoon-material-library.md`), with the ctests `typhoon_materials`
and `typhoon_bsdl`. Never edit the vendored files; re-import with
`external/typhoon/import.sh <commit>`. Moving the pin also means redoing the audit
behind `renderer/pathtracer/ContextGroups.cpp`, which records what each node type reads of
the shading context; `material_graph` fails until its audited commit matches
`PROVENANCE.md`. It also builds `halcyon_path_materials`, the pathtracer's material
interface (`renderer/pathtracer/`): `PathMaterial.isph` is what the
path loop calls per lane (native material kinds inline, such as matte's in
`PathMatte.isph` and physicallyBased's in `PathPbm.isph` (whose shade and eval are in
`PathPbm.ispc`), C++ kinds through batched calls, closures in a per-thread arena),
`PathMaterial.h`/`.cpp` the C++ side over Typhoon's
materials, `ClosureClassification.*` a port of Typhoon's closure classification,
`ClosureSampling.*` Typhoon's BSDF sampling with a mix's delta samples weighed as it
evaluates them (see [Materials in the pathtracer](#materials-in-the-pathtracer)),
`ContextGroups.*` which parts of the shading context a graph reads, `MaterialConverter.*`
the graphs of halcyon's materials and `HalcyonTextureSystem.*` their texture system (see
[Materials in the pathtracer](#materials-in-the-pathtracer)), `ColorManagement.*` a port
of Typhoon's colour management for its linear Rec. 709 working space, `Medium.*` homogeneous
media (see [Interior media](#interior-media)) and `Subsurface.*` the subsurface random walk
(see [Subsurface scattering](#subsurface-scattering)). Why and
how: `docs/adr/0003-batched-material-callout.md` (call shape, arena, costs) and
`docs/adr/0004-pathtracer-material-interface.md` (the interface). The ctest `path_material`
tests it. Last, it builds the `quality` renderer into the device (see
[The quality renderer](#the-quality-renderer)) and its ctest `pathtracer`: the device
links `halcyon_path_materials` (and so Typhoon's materials and Imath), with the static
libraries kept out of its dynamic symbols (`--exclude-libs`, as for Embree's tasking).
Imath is the system's where CMake finds it, else fetched and built statically. A system
Imath may be a shared library, which the device would then need at run time: that is
acceptable (maintainer decision, keep it). Today the device has no `NEEDED` entry for
libImath, as it references no out-of-line Imath symbol.
OpenQMC 0.7.1 (header-only) is fetched by `external/openqmc` at configure time, and the
device compiles in only its Sobol blue-noise tables (`halcyon_openqmc_data`,
`renderer/pathtracer/SobolBnTables.cpp`, about 512 KB), for the ISPC port of its sampler;
nothing else of OpenQMC is built. The tables ship in the
device from the fetch rather than being committed under `external/` (maintainer decision,
keep it).

Typhoon and OpenQMC code that halcyon ports (to ISPC, or to C++ outside the vendored copy,
such as `ClosureClassification.*`) is ported by hand, not vendored, and is kept in step
with the pin by hand. halcyon's own files carry the ANARI SDK's header (`Copyright <years> The Khronos Group`,
`SPDX-License-Identifier: Apache-2.0`; the SDK's `LICENSE`); each ported file instead
carries its source's license with a prose credit naming the source file, the pin and
its copyright and license, and is listed in `THIRD-PARTY.md` (maintainer decision, keep
it): `SPDX-License-Identifier: LicenseRef-TOST-1.0` for Typhoon's and OpenUSD's code (the
Tomorrow Open Source Technology License 1.0, `external/typhoon/LICENSE.txt`, which has no
SPDX identifier yet), `Apache-2.0` for OpenQMC's, joined with `AND` where a file ports
both (`THIRD-PARTY.md` lists the tags). The tests whose C++ reference ports helide code
(`samplers`, `spatial_field`, `volume`) keep `Apache-2.0` likewise. Ports of
the integrator, lights, sampler and other renderer code were checked against Typhoon's own
C++ while halcyon had an OpenUSD-linked reference harness (see [Verification against
Typhoon](#verification-against-typhoon)); today the ctests check them against analytic
values. The native material kinds, ports of what the vendored materials compute, are
checked against the materials' own graphs, by `path_material`, `native_materials` and
`path_pbm` (see [Tests](#tests)).

**Moving the Typhoon pin** is a ticket of its own, not drift:

1. `external/typhoon/import.sh <commit>`, then the pin in `PROVENANCE.md` by hand.
2. Redo the audit behind `renderer/pathtracer/ContextGroups.cpp` (`material_graph` fails
   until its audited commit matches `PROVENANCE.md`).
3. Bring every port in step with its upstream file: `grep -rl 70c45e83` (the current pin)
   lists them, each naming its source in its header; that includes the native kinds
   (`PathMatte.isph`, `PathPbm*`), which follow the vendored materials' graphs. Update
   the pin in their credits.
4. Run ctest. halcyon no longer has the tools that compared its ports and images with
   Typhoon's (see [Verification against Typhoon](#verification-against-typhoon)); checking
   ports that changed upstream against Typhoon means bringing them back from history or
   comparing by hand.

For the `materialx` material subtype, `external/materialx` provides MaterialX's Core and
Format libraries (`halcyon_materialx`): an installed MaterialX 1.39 (1.39.6 or later) with
static libraries and its example resources when `find_package()` finds one (OpenUSD's, through `CMAKE_PREFIX_PATH`, say), else MaterialX
fetched at commit `38368ee0` (1.39.6, the version OpenUSD 26.05 links) and built from its
sources under target names (`halcyon_materialx_core`/`_format`) and a namespace
(`MaterialX_halcyon_v1_39_6`) of halcyon's own, so a configure that also finds another
MaterialX's targets (OpenUSD's `pxrConfig.cmake` defines them) sees no clash, nor a process
that loads another MaterialX. That fallback departs from VisRTX's `rtx` device, which
requires an installed MaterialX (`devices/rtx/CMakeLists.txt`) because building MaterialX's
own CMake in-tree would clash with OpenUSD's targets; halcyon's own target names avoid
that, and it always needs MaterialX (the `materialx` subtype and MaterialXCpp aren't
optional), so it keeps the fallback (maintainer decision, 2026-10-16). Checked on
2026-10-16: VisRTX configured with `rtx` (MDL and MaterialX on) and halcyon builds, both
devices on one installed static MaterialX 1.39.6, and one process loads both and renders
a `materialx` document with each. It also embeds
MaterialX's data library (`halcyon_materialx_definitions`: the 31 `.mtlx` files of
`libraries/` but the shader generators' and `targets/`, from the MaterialX the device links;
`renderer/pathtracer/MaterialXLibrary.*` reads them once), so the device needs no MaterialX
search path at run time. Only `halcyon_materialx_documents` (`renderer/pathtracer/
MaterialXDocument.*`, `MaterialXLibrary.*`) includes MaterialX's headers, so no MaterialX
symbol is instantiated in the device's own objects. `external/images` fetches stb_image
(2.30, commit `2c980bb5`) and tinyexr 1.0.13 with its miniz, the image file readers of
`renderer/pathtracer/ImageFile.cpp` (in `halcyon_path_materials`). All of these are linked
statically with their symbols kept out of the device's dynamic symbols (`--exclude-libs`);
`device_dependencies` also fails if the device exports a MaterialX, pugixml, stb_image,
tinyexr or miniz symbol. The `rtx` device vendors its own stb_image (2.26,
`devices/rtx/external/stb_image`); halcyon keeps its fetched copy rather than reach into
another device's tree, so that each device still builds alone, and compiles it into
`ImageFile.cpp` as static functions (`STB_IMAGE_STATIC`), so the two can't interpose in
one process (maintainer decision, 2026-10-16).

Neither the device nor its tests need OpenUSD. The `device_dependencies` ctest
(`tests/checkDeviceDependencies.cmake`) fails if the built device depends on a library
named like OpenUSD (`libusd_*`, or an unprefixed build's `libtf.so` etc.), TBB, Python or
Boost.Python, or if its RPATH or RUNPATH names a directory of OpenUSD's libraries.

The ctests that load the device set `LD_LIBRARY_PATH` to the build directory, so they get the
freshly built `libanari_library_halcyon.so` and not an installed copy. Do the same when running the SDK's
`anariInfo`, `anariRenderTests` or `anariCts` against halcyon, which is how renders are compared
with helide.

**Linux is the only platform built and tested.** The code compiles for MSVC and macOS
(`forEachLane()` in `PathMaterial.cpp` uses `_BitScanForward` on MSVC), but nothing
has been built there. The symbol hiding (`--exclude-libs`) and `device_dependencies`
are ELF only (`UNIX AND NOT APPLE`): a Windows DLL exports nothing by default, but on
macOS the device would export the statically linked libraries' symbols. The
device-level ctests set only `LD_LIBRARY_PATH`, so they need the build directory on
`PATH` (Windows) or `DYLD_LIBRARY_PATH` (macOS) elsewhere (maintainer decision,
2026-10-15).

`HalcyonDefinitions.json` is the input to `anari_generate_queries()`, which generates
`anari_library_halcyon_queries.h` in the build directory at build time. A new extension,
subtype or device/renderer parameter only shows up in introspection once it is declared there.
halcyon's own extensions (`HALCYON_GEOMETRY_CURVE_BASIS`, `HALCYON_MATERIAL_MATERIALX`,
`HALCYON_MATERIAL_OPENPBR`, `HALCYON_MATERIAL_USD_PREVIEW_SURFACE`)
are defined one per file in `extensions/` (the registry's format, file named after the
extension in lower case), which the generator reads as an extra JSON root, and listed among
`HalcyonDefinitions.json`'s dependencies like the KHR ones. Each renderer object there lists
the extensions it honours (see [Parameters and properties](#parameters-and-properties)).
They keep the `HALCYON_` prefix, not the `rtx` device's `VISRTX_`: halcyon
doesn't declare `VISRTX_MATERIAL_MATERIALX`, though `HALCYON_MATERIAL_MATERIALX` takes
its parameters, nor `VISRTX_RENDERER_QUALITY`, the `rtx` device's `quality` renderer
parameters, which halcyon's `quality` doesn't share (maintainer decisions, 2026-10-16).
The renderer objects are listed `preview` first, then `quality`: the generator lists
subtypes in the order of the JSON, and `default`, the spec's alias of the first renderer
listed, creates `preview`. The SDK's JSON declares its core and extension renderer
parameters (`name`, `background`, `ambientColor`, ...) on a renderer named `default`, which
the generator merges by name; halcyon has none, so the generated queries get a `default`
object with only those parameters. `HalcyonDevice`'s query functions hide it: renderer
subtypes leave it out, and object and parameter info for `default` answer `preview`'s.
Each renderer object therefore declares the core and extension parameters it reads
itself.

## Object hierarchy

Every object derives from `halcyon::Object` (`Object.h`) → `helium::BaseObject`, except `Frame`,
which derives from `helium::BaseFrame` but owns an ISPC slot the same way. Each base type has a
static `createInstance(subtype, state)`; `HalcyonDevice` calls it. Unknown subtypes become an
object that is never valid (`UnknownObject`, or `UnknownVolume` for volumes), renderers
too (helide gives them its default renderer); `default`, the spec's alias of the first
renderer listed, gets the `preview` renderer.

```
HalcyonDevice
├── Camera: perspective, orthographic            camera/
├── Geometry: triangle, quad, sphere, curve,     geometry/
│             cylinder, cone (LinearCone)
├── Material: matte, physicallyBased,            material/
│             openPBR, usdPreviewSurface,
│             materialx
├── Sampler: image1D/2D/3D, transform, primitive sampler/
├── SpatialField: structuredRegular              spatial_field/
├── Volume: transferFunction1D                   volume/
├── Light: directional, point, spot, quad         light/
│          (QuadLight), ring, hdri (only
│          the pathtracer reads them)
├── Surface                                      surface/
├── World → Instance → Group                     world/
├── Renderer: preview (Preview), quality         renderer/, renderer/preview/,
│             (PathTracer)                       renderer/pathtracer/
└── Frame                                        frame/
```

Geometry subtypes all live in `Geometry.cpp`; sampler subtypes in `Sampler.cpp`.

## ISPC mirroring

- Each object's ISPC struct is declared in `shading/halcyon_objects.isph`. The struct lives in a
  fixed-size slot, `IspcObjectStorage<N>`, in `HalcyonDeviceGlobalState::registry`, one
  `IndexedVector` per object type. `getIE()` returns the slot's address.
- `IndexedVector` is backed by `std::deque`, so slots never move. ISPC structs hold raw pointers
  to other objects' slots (a `Surface` points at its `Geometry` and `Material`), so anything
  that could relocate or shrink the storage is a use-after-free.
- The C++ side writes the struct only through exported setters in the type's `.ispc` file
  (`Geometry_setAttribute()`, `Frame_setCamera()`, ...), usually from `finalize()`.
- Each `.ispc` exports `sizeof_<Type>()`. The `Object` constructor compares it with the slot
  size and reports an ERROR "size mismatch for type ..." if they differ (`Frame`'s constructor
  reports "sizeof(ispc::Frame) == ..."); they must be equal. **After changing an ISPC struct,
  set the slot size in `HalcyonDeviceGlobalState.h` to the size the error reports.**
- helium only calls `finalize()` on objects whose parameters were committed. A struct that must be
  valid before its first commit is initialized in the constructor (`Renderer::updateISPC()`,
  `Preview::updatePreviewISPC()`, `Sampler_setDefaults()`, `SpatialField_setDefaults()`).
- ISPC pointers to copied data (attribute arrays, texels, voxels, ids) point into
  `std::vector`s owned by the C++ object and replaced only in `finalize()`.

### Subtypes: function-pointer hooks and named members

Geometry, Sampler and SpatialField have one ISPC struct for all subtypes:

- A per-subtype **hook** (function pointer) that a subtype's `finalize()` installs through an
  exported setter: `Geometry_VertexAttributeFcn getVertexAttribute` (and
  `getFaceVaryingAttribute`, triangle and quad only), `Geometry_DerivativesFcn
  getDerivatives` (the pathtracer's surface derivatives, see
  [Shading context](#shading-context)), `Geometry_TriangleCornersFcn getTriangleCorners`
  (triangle only: corner positions and shading normals for the pathtracer's shadow
  terminator lift), `Geometry_SurfacePositionFcn getSurfacePosition` (triangle and quad
  only: the object position from the primitive's vertices, see
  [Shading context](#shading-context)), `Sampler_SampleFcn sample`,
  `SpatialField_SampleFcn sample`. A NULL sampler or field hook means invalid; materials skip
  such samplers. A sampler also records its hook's `kind` (`SamplerKind`), so that the
  pathtracer's texture system, in another library, runs the same sampling code
  (`shading/halcyon_sampler.isph`) without the hook's address.
- Subtype data as a **named member** of the base struct (`Geometry::triangle`, `quad`,
  `sphere`, `curve`, `cone`; `Sampler::image`, `primitive`; `SpatialField::structuredRegular`;
  `Renderer::preview`, `pathtracer`). Every member makes every slot of that type bigger, so keep members
  to pointers and counts. One accepted exception: `CurveGeometry::basis`, an enum the
  curve hooks branch on, which padding makes 16 bytes of every geometry slot (544 to 560;
  maintainer decision, keep it, rather than one hook per basis).

Camera and Material instead share one layout across subtypes (a `CameraType` enum; matte
`color` and PBM `baseColor` fill the same field; `colorAlphaIsOpacity` says whether the
colour's alpha multiplies the opacity, false for openPBR, usdPreviewSurface and materialx,
whose colours are color3). `Material::kind` (`MaterialKind`) says how the pathtracer shades it: matte
is `MATERIAL_KIND_MATTE` (native, `Material::nativeKind()`), physicallyBased
`MATERIAL_KIND_PHYSICALLY_BASED` (native) where its parameters allow it, else
`MATERIAL_KIND_CPP` like every other subtype; every kind has the
`cppMaterial` that points at the `halcyon::pathtracer::CppMaterial` their `finalize()` compiles
and whose `attributeMask` and `contextMask` list the attributes and the parts of the
shading context it reads (see
[Materials in the pathtracer](#materials-in-the-pathtracer)); `MATERIAL_KIND_NONE` if it
didn't compile. `MATERIAL_KIND_LAMBERT` (native) is only the integrator's fallback for
surfaces without a closure. `Material::physicallyBased` points at the
`PhysicallyBasedParameters` (`MaterialParameter`s: constant, attribute or sampler) the
physicallyBased material holds for `MATERIAL_KIND_PHYSICALLY_BASED`, NULL otherwise.

### Array data: copies and shared memory

Helium array memory can move: helium copies an app's shared array into its own memory when the
app releases its last public reference while objects still use it (`privatize()`). Halcyon
copies most array data into object-owned buffers in `finalize()`:

- Geometry `vertex.*`/`primitive.*`/`faceVarying.*` attributes, the renderer's background
  image, image sampler texels, the primitive sampler's `array` and transfer function colours
  become packed float4 via `toAttributeValues()` (`array/Array1D.h`); ISPC reads them with
  `load_float4()`. Instance attributes are packed to float4 per transform from
  `getUniformAttributes()`.
- Face-varying attributes (`faceVarying.color`, `faceVarying.attribute0`–`3`; triangle and
  quad only, which opt in with `commitFaceVaryingAttributes()`) hold one value per primitive
  corner: elements `3 * primID + i` (quad `4 * primID + i`) whatever `primitive.index` says,
  interpolated like vertex values. A shorter array warns (`checkFaceVaryingSizes()`) and the
  missing corners use its last element.
- Shading normals (`vertex.normal`, `faceVarying.normal`; triangle and quad only, which opt
  in with `commitNormals()`) must be FLOAT32_VEC3 or FIXED16_VEC3 (value / 32767); other
  element types warn and are ignored. They become packed float4 (`toNormals()`) and are
  interpolated through the same hooks as vertex and face-varying attributes, with the same
  indexing, clamping and short-array warning.
- Tangents (`vertex.tangent`, `faceVarying.tangent`; triangle and quad only, which opt in
  with `commitTangents()`) must be FLOAT32_VEC3/VEC4 or FIXED16_VEC3/VEC4 (value / 32767,
  w the handedness, 1 for VEC3); other element types warn and are ignored, and a short
  `faceVarying.tangent` warns like the other face-varying arrays. Only the pathtracer reads
  them. `finalizeTangentFrames()` (from triangle's and quad's `finalizeSubtype()`) turns
  them into a tangent frame per primitive corner (`Geometry::cornerTangents`/
  `cornerBitangents`, packed float4, corners numbered like face-varying values): the authored
  tangent (face-varying first, as for normals; clamped like them) with the bitangent
  sign(w) × N × tangent, N the corner's normalized shading normal as authored (else the
  face's; corners aren't turned into `Ng`'s hemisphere, only the normal at a hit is), as
  VisRTX and glTF build it; without authored tangents, Typhoon's smoothed ones (see
  [Shading context](#shading-context)). Each finalize computes them, for every renderer,
  in parallel on Embree's threads (about 30 ns per triangle on 48 threads, 32 bytes per
  corner). That cost is accepted (maintainer decision, keep it): it recurs on every
  triangle or quad commit, also in the `preview` renderer, which doesn't read the frames
  (a 2M-triangle mesh's first render went from about 110 to 175 ms on 48 threads).
- `primitive.id` becomes uint32 (UINT64 values are truncated with a warning). Every subtype
  reads a UINT32 `primitive.index` in place and converts a UINT64 one to uint32 the same way
  (`sharedIndices()` in `Geometry.cpp`). A `primitive.index` whose element type the spec
  doesn't allow for the subtype makes the geometry invalid with a warning. Instance `id`
  arrays must be UINT32 and at least as long as `transform`, and are copied per transform;
  others are dropped with a warning, and every transform then reports `~0u` (the array
  takes the name of the uniform `id`, so there is no uniform value to fall back to).
- Structured-regular voxels are byte-copied in their native type (FLOAT32, FLOAT64, UFIXED8,
  UFIXED16, FIXED16), with one sample hook per type, so 8/16-bit volumes don't grow as floats.
- Sphere, curve, cylinder and cone build Embree-owned buffers (`rtcSetNewGeometryBuffer`).
  Curve's ISPC hook reads the (clamped) segment starts from its Embree index buffer; the
  sphere, cylinder and cone hooks read `primitive.index` (see below).

**Array ranges:** a 1D array's `begin`/`end` parameters select its elements `[begin, end)`,
and every reader honours them: count with `size()` (for `Array1D`, `totalSize()` is the same)
and read from `begin()`/`beginAs<T>()`, never `data()`/`dataAs<T>()`/`valueAt()`. In
helium up to 0.16, `readAsAttributeValue()`, `valueAtLinear()` and `valueAtClosest()` start
at the buffer's first element, so use `toAttributeValues()` or `attributeValueAt()`
(`array/Array1D.h`) to convert elements to float4. Object arrays are read through helium's
`ObjectArray::handlesBegin()`, which up to 0.16 honours `begin` only if helium last copied
the handles (at creation or unmap) while `begin` was 0: the copy puts `[begin, end)` at the
front of its handle vector, and `handlesBegin()` adds `begin` again (the vector is private,
so halcyon can't work around it). ANARI-SDK 0.17 (`next_release`, helium squash `a3a3eaa`)
fixes both. Committing only an
array's `begin`/`end` notifies its observers, so they are finalized again.
`tests/test_array_begin.cpp` renders every geometry subtype, the primitive and image1D samplers,
the transfer function and instances with junk before `begin` and after `end`, and compares with
the plain arrays.

Some geometry reads helium memory in place: triangle and quad pass `vertex.position` and a
UINT32 `primitive.index` to Embree with `rtcSetSharedGeometryBuffer`, and the ISPC
vertex-attribute hooks of triangle, quad, sphere, cylinder and cone read a UINT32
`primitive.index` from the helium array. The shared `vertex.position` breaks Embree's
contract that a vertex buffer's last element be readable with a 16-byte load: Embree
reads up to 4 bytes past the array's end (valgrind reports it in BVH builds), which
could fault only where the array ends at the end of a mapped page. helide shares it the
same way. Kept zero-copy rather than copied into a padded Embree buffer (maintainer
decision, 2026-10-15).
**A privatize is a change:** halcyon's `Array1D`/`Array2D`/`Array3D` (`array/Array*D.h`, created
by `HalcyonDevice`) override `privatize()` to copy and then `notifyChangeObservers()`, so the
observers are finalized again at the next flush and re-point at the copy. Code that keeps a
pointer into an array must take it in `finalize()` from an observed array
(`ChangeObserverPtr`); it then stays valid. `ObjectArray` needs no override
(helium keeps its handles in its own vectors). `Array1D::privatize()` copies the whole buffer
(`totalCapacity()`): helium's copies `size()` elements from the start up to 0.16, which loses
the end of a range with `begin > 0`. `HalcyonDevice::release()` holds device work off while a
release privatizes (see [Synchronization](#synchronization)). ANARI-SDK 0.17 copies the whole
range, notifies observers on privatize, and runs a privatize that copies app memory through a
device hook (`BaseDevice::runDeviceRelease()`) that a device can override to wait for its
work. With it the overrides' copy and notify are redundant and the release hold could become
that override; halcyon keeps its own while it supports helium 0.15 and 0.16.

## Rendering pipeline

1. `Frame::renderFrame()` enqueues a job on `state->taskQueue` (a single worker thread).
2. The job holds `RenderingSemaphore::DeviceWork` while it runs `Frame::flushAndRender()`:
   apply committed device parameters (`applyDeviceParameters()`); flush the commit buffer; if
   the frame is invalid, clear the colour buffer (depth and id channels keep their old
   values, like helide), drop the renderer's state for the frame (so it reports none of its
   properties), report an ERROR and stop; make the renderer's state for the frame if the
   frame hasn't rendered with this renderer since (a new state counts as a change); if no
   object was finalized and no device parameters were applied since the last render, skip
   rendering, unless the renderer wants to go on (`Renderer::wantsRender()`: the
   `quality` while its image isn't done refining, which refines the last image instead,
   whatever the frame's `accumulation` says; see [Adaptive sampling](#adaptive-sampling));
   otherwise
   `World::embreeSceneUpdate()` (rebuild or recommit the groups' bottom-level Embree scenes,
   rebuild the top-level scene, update the volume instance list and cached bounds; a world's
   `finalize()` releases its top-level scene, so a world commit alone rebuilds it) and render.
   Either way the completion callback fires (step 5).
3. The frame hands itself to the renderer's `render()`, which runs every pass of the
   render; `Frame` knows no renderer subtype. With it goes the renderer's state for this
   frame (`RendererFrameState`, made by `Renderer::newFrameState()` the first time the
   frame renders with that renderer, and owned by the frame, since a renderer may serve
   several frames) and whether anything changed since the frame's last render. Each pass
   goes through `Frame::runPass()`, which calls a span function for every SIMD span with
   an `embree::parallel_for` over tiles of `taskGrainSizeHeight` rows ×
   `taskGrainSizeWidth` pixels, rounded up to whole `programCount` spans, so all passes
   share one decomposition and grain size. The kernels take the frame's ISPC struct and
   their state's: `PathTracer_renderFrame()` for the `quality`; for `preview`, when it
   reads the occlusion factor and the world has surfaces, the hit-buffer pass
   (`Preview_renderHitBuffer()`, which traces primary rays), the screen-space occlusion
   pass (`Preview_renderOcclusion()`) and blur passes (`Preview_renderOcclusionBlur()`,
   one by default), each over the whole frame (see [Ambient occlusion
   passes](#ambient-occlusion-passes)), then the shading pass
   (`Preview_renderFrame()`).
4. In `renderer/preview/Preview.ispc` `renderFrame()` (the `preview` renderer; for the `quality` see
   [The quality renderer](#the-quality-renderer)): camera ray, then in the `default` mode
   `renderDefault()`, a loop of `rtcIntersectV` → `getHitInfo()` → `intersectVolumes()`
   (volumes clipped to `[tnear, surface hit]`) → composite the volume and the surface →
   `continuePastHit()` (`shading/halcyon_ray.isph`) while the surface isn't opaque; in the
   `ambientOcclusion` and `ambientOcclusion.unblurred` modes `renderAmbientOcclusion()`,
   which traces to the first visible surface with `traceFirstVisibleSurface()` (as does the
   hit-buffer pass); in other modes one `rtcIntersectV` → `getHitInfo()` →
   `intersectVolumes()` → `shadeRay()`. Then
   `writeOutputs()` (colour in FLOAT32_VEC4, UFIXED8_VEC4 or UFIXED8_RGBA_SRGB; depth; id
   channels). If the frame has an albedo or normal channel, `renderAlbedoAndNormal()`
   then writes them, whatever the mode (see [Frame channels](#frame-channels)). The
   helpers both renderers' kernels use (channel writes, `background()`, `missColor()`,
   `traceFirstVisibleSurface()`) are in `shading/halcyon_frame.isph`.
5. After the device work ends, the completion callback runs on the worker. It fires once per
   `anariRenderFrame`, also for invalid frames, skipped renders and dropped renders (see
   [Synchronization](#synchronization)); only a render issued from the frame's own callback
   fires none.

Details that are easy to get wrong:

- Every instance is an Embree instance array, even with one transform; `HitInfo::instArrayId` is
  Embree's `instPrimID`. Embree `geomID` indexes `Group::surfaces`, `instID` indexes
  `World::instances`. Names spell `Id` (`primId`, `instArrayId`), except Embree's fields
  (`primID`, `geomID`, `instID`, `instPrimID`, mirrored in `RayHit`), parameters that take
  one unchanged (the geometry hooks' `primID`), and the `embree*ID` mode names.
- The `default` mode composites front to back, non-premultiplied like helide: the volume in
  front of each surface hit, then the surface weighted by `Surface_getCompositeOpacity()`
  (colour alpha × material opacity after `alphaMode`; opaque without a valid material), then
  goes on past the surface until the ray misses or the opacity reaches 0.99, then what a
  miss shows (`missColor()`, below). Its depth and id channels come from the visible hit:
  the first volume entered, or the first surface with opacity above 0 (mask cut-outs are
  skipped); the `ambientOcclusion` mode reports the same hit. The other modes (`shadeRay()`) treat the first
  surface hit as opaque, like helide; opacity shows there only in `opacityHeatmap`. The
  restart epsilon is VisRTX's `epsilonFrom()`, so a surface coplanar with the one just hit is
  skipped.
- Primary rays go through pixel centres: `Camera_screenFromPixel()`
  (`shading/halcyon_camera.isph`) maps pixel (x, y) to ((x, y) + 0.5) / size, then into the
  camera's `imageRegion`. The background image, `testFrame` and every other mode use that same
  screen position, so a background image the size of the frame shows texel (x, y) at pixel
  (x, y). Tests that recompute a pixel's ray use the centre too.
- **What a miss shows** (`missColor()` in `shading/halcyon_frame.isph`), in both
  renderers, wherever a camera ray that misses shows the background (the `default`
  mode after compositing, the debug modes, both ambient occlusion modes, the
  `quality`'s camera segments): the sum of the `visible` hdri lights along the ray,
  opaque (alpha 1), else the background. The hdri lookup (`visibleHdris()`,
  `HdriLight_radiance()`) is one function in `shading/halcyon_hdri.isph`, which the
  `quality`'s light sampling (`renderer/pathtracer/Lights.isph`) uses too, so both
  renderers show a miss pixel alike (see [Lights](#lights)). The `preview` renderer is
  never lit by the hdris. `testFrame` traces nothing and shows the ray direction.
- Missed rays write depth `FLT_MAX`, like helide: it is every ray's initial `tfar`
  (`HALCYON_RAY_TFAR` in `shading/halcyon_ray.isph`), which also bounds volume intervals.
- 8-bit colour rounds to the nearest code, and `UFIXED8_RGBA_SRGB` uses the exact sRGB curve
  with linear alpha (`shading/halcyon_util.isph`), matching helium's encoder from ANARI-SDK
  0.17 (`169bd05`; see [Deliberate differences](#deliberate-differences-from-helide)).
- Shared ISPC helpers are in `shading/*.isph`: attributes (`halcyon_attributes.isph`, resolved
  in helide's order, with face-varying ahead of vertex: ray-derived → instance →
  face-varying → vertex → primitive → uniform → (0,0,0,1)),
  material colour/opacity (`halcyon_getSurfaceColor.isph`), samplers, volumes, math.
- Two normals: the geometric normal `Ng` (Embree's, which the winding decides) and the
  shading normal (`objectShadingNormal()`/`worldShadingNormal()` in
  `halcyon_attributes.isph`): interpolated `faceVarying.normal`, else `vertex.normal`,
  else `Ng`, also where the interpolated normal has zero length, **turned into `Ng`'s
  hemisphere**: negated at a hit where it makes an obtuse angle with `Ng`, as Typhoon
  turns its smooth normal at every hit (`surfaceShading.cpp`'s
  `_TryBuildSurfaceInteraction()`). So a mesh wound against its
  normals renders as the mesh with its normals negated, wound with them, in both
  renderers: the winding decides the side (see [Deliberate
  differences](#deliberate-differences-from-helide) for what that changes). The angle is
  taken in world space, between the two normals through the instance's normal matrix, as
  Typhoon takes it (a non-uniform scale can change the sign of the object-space dot
  product); `objectShadingNormal()` is negated by the same test. Only interpolated normals
  can turn: `Ng` itself never does. Neither normal is turned to face the viewer, except in
  the ambient occlusion hit buffer. The eye light of the `default` and `opacityHeatmap`
  modes, the `worldNormal`/`objectNormal` attributes (so the turned normal), the `Ns`
  modes, the hit buffer, both renderers' normal channel and the `quality`'s shading
  context use the shading normal; the `Ng` and `backface` modes use `Ng`. World-space
  normals go through the instance's normal matrix.

Embree geometry types: triangle `TRIANGLE`, quad `QUAD`, sphere `SPHERE_POINT`, curve
`ROUND_LINEAR_CURVE`, cylinder and cone `CONE_LINEAR_CURVE` (the same as helide); a curve
with a cubic `basis` `ROUND_BSPLINE_CURVE` or `ROUND_CATMULL_ROM_CURVE`.

**Curve bases** (`HALCYON_GEOMETRY_CURVE_BASIS`,
`extensions/halcyon_geometry_curve_basis.json`): the curve's `basis` (STRING) is `linear`
(default, KHR_GEOMETRY_CURVE's segments), `bspline` or `catmullRom`, uniform cubic round
curves for sci-vis apps' smooth streamlines; an unknown value warns at commit and draws
linear segments. A cubic `primitive.index` i is the segment over control points i to i +
3; without it, n vertices are one strand of n − 3 segments; fewer than 4 vertices make the
geometry invalid with a warning, and indices past n − 4 use the last segment with the
linear curve's warning. `primitive.*` attributes and ids are per segment, whose index is
Embree's `primID`. The radius (`vertex.radius` or `radius`, as Embree's fourth vertex
component) and vertex attributes follow the basis: the attribute hook evaluates Embree's
basis (`Curve_cubicWeights()` in `Geometry.isph`, `BSplineBasis`/`CatmullRomBasis`) at the
hit's u over the four control points, unclamped, so Catmull-Rom values may overshoot.
Nothing adds end points: a B-spline doesn't reach its first and last control points, and a
Catmull-Rom strand runs from the second to the next-to-last, so apps repeat the end points
to reach them. `CurveGeometry::basis` tells the hooks the basis; Embree's curve type is
fixed at creation, so a commit that changes the basis replaces the Embree geometry
(`Geometry::replaceEmbreeGeometry()`) and has groups rebuild their scenes. Bezier, flat
(ribbon) curves and `wrap` aren't supported (until an app asks).

### Ambient occlusion passes

Ambient occlusion is computed in screen space, in `renderer/preview/Preview.ispc` (why:
`docs/adr/0001-screen-space-ambient-occlusion.md`; terms: `CONTEXT.md`). The passes run
only when the factor is read and the world has surfaces to occlude, decided by each render
in `Preview::render()` (after the frame's `World::embreeSceneUpdate()`):

- the renderer reads the factor (`Preview::readsOcclusionFactor()`): the mode is
  `default` with `ambientOcclusion` set, or `ambientOcclusion` or
  `ambientOcclusion.unblurred` (which run them whatever the boolean), and
  `ambientOcclusionIntensity` is above 0 (0 gives a factor of 1);
- and the world has surfaces rays can hit (`World::hasSurfaces()`: the surfaces' Embree
  bounds aren't empty), so not for an empty world or one with only volumes.

The buffers are the frame's `PreviewFrameState` (`renderer/preview/Preview.cpp`, and its
ISPC struct in `Preview.ispc`, which the passes' kernels take). A render that runs them
sizes the hit buffer and the unblurred and blurred occlusion buffers, plus an
intermediate blur buffer if `ambientOcclusionBlurPasses` is above 1; one that doesn't
frees them all, and one that runs a single blur pass frees the intermediate buffer
(`PreviewFrameState::resize()`, which reports a DEBUG message when it allocates or frees
the occlusion buffers, and another for the intermediate buffer). The shading pass reads a
factor of 1 without buffers. Changing the mode, the intensity or the world's contents
therefore starts or stops the passes at the next render. The passes run through `runPass()`
before the shading pass, each over the whole frame before the next starts:

1. Hit-buffer pass (`renderHitBuffer()`): traces each pixel's primary ray with
   `traceFirstVisibleSurface()` and stores its first visible surface (distance along the ray,
   world shading normal, hit flag), going on through mask cut-outs and any volume in front
   of it, so a volume's bounding box doesn't cut occlusion out of the geometry seen through
   it. The normal is put on the viewer's side of the geometry: flipped where it points away
   from `Ng` turned to face the viewer (the shading normal is already in `Ng`'s hemisphere,
   so it is flipped where `Ng` faces away). A shading normal tilted off a flat facet lifts
   the facet's taps on one side above its tangent plane, so the facet occludes itself
   there; tessellated curved surfaces with smooth normals get smooth occlusion in
   exchange. A pixel with no first visible surface stores none.
2. Occlusion pass (`occlusionAt()`, constants `AO_*` above it), a screen-space pass: the
   Alchemy/SAO estimator. 16 taps on a 7-turn spiral within the occlusion radius `r`
   projected to pixels, rotated per pixel by a 4×4 pattern keyed by the pixel's position,
   so renders are deterministic. Each tap's position is rebuilt from its own pixel's camera
   ray and stored distance; taps outside the frame or on pixels with no surface are
   dropped, and the sum is renormalised over the `landed` taps (factor 1 if none land).
   For a tap at offset `v` from the pixel's position `P` (normal `n`), with bias
   `b = 0.01 r` and `ε = 0.01 r²`, the renderer's `ambientOcclusionEstimator` selects:
   - `g3d` (default; G3D's high-quality SAO code): each tap adds
     `max(1 - v·v / r², 0) · max((v·n - b) / sqrt(v·v + ε), 0)`, and
     `A = max(1 - sqrt(3 · sum / landed), 0)` (`AO_G3D_SCALE`); the square root also
     sets the contrast.
   - `sao` (the HPG 2012 paper's form): each tap adds
     `max(r² - v·v, 0)³ · max(v·n - b, 0) / (v·v + ε)`, and
     `A = max(1 - 5 · sum / (landed · r⁵), 0)` (`AO_SAO_SCALE`). The paper divides by
     `r⁶` and multiplies by a linear intensity `σ`; halcyon sets `σ = r`, which is the
     paper's form at `r = 1`. The tap sum over `r⁶` goes as 1 / length, so with `σ = r`
     the factor doesn't depend on the scene's units: a scene scaled with its radius gets
     the same factor, as with `g3d`. The code takes each factor of a tap relative to `r`
     (`max(1 - v·v / r², 0)³`, `(v·n - b) / r`, `v·v / r² + 0.01`) so that `r⁵` can't
     overflow or underflow.

   The factor is `A^intensity` (`ambientOcclusionIntensity`; 1 at intensity 0, also where
   `A` is 0). The exponent applies here, before the blur, so the `ambientOcclusion.unblurred`
   mode shows `A^intensity` and the blur averages those factors.
3. Blur pass (`blurredOcclusionAt()`), a screen-space pass: a 5×5 window centred on the
   pixel (offsets −2 to +2 in x and y, `AO_BLUR_RADIUS`), so the blurred factor lines up with
   the colour image. Each neighbour's spatial weight is the product of a per-axis weight
   (`AO_BLUR_WEIGHTS`) of 0.5 at offset ±2 and 1 otherwise, so corners weigh 0.25. Offsets
   −2 and +2 fall on the same row or column of the 4×4 rotation pattern, so every rotation
   gets total weight 1 and the pattern cancels exactly. Neighbours with no surface are
   skipped, and the spatial weight of the others is multiplied by `1 - (d / r)²`, `d` being
   the difference of their hit distance from the pixel's, so neighbours farther apart than
   `r` are rejected and occlusion doesn't bleed across silhouettes.
4. Further blur passes, only if the renderer's `ambientOcclusionBlurPasses` (default 1,
   clamped to [1, 5]) is above 1: à-trous passes, each blurring the previous pass's result
   with the same 5×5 window and edge rules, its offsets multiplied by the stride
   2^(k−1) for pass k (2, 4, 8, 16) and its per-axis weights the B3-spline
   {1/16, 1/4, 3/8, 1/4, 1/16} (`AO_ATROUS_WEIGHTS`). Near a floor contact the true factor
   changes quickly with height, so the first pass can't cancel the rotation pattern there
   and leaves bands a few rows apart on faces seen from a low camera; two more passes
   remove them. `PreviewFrameState::runBlurPasses()` alternates each pass's target between
   the intermediate and the blurred buffer so that the last pass writes the blurred one;
   the unblurred buffer is only read.

The shading pass reads the last blur pass's factor with `occlusionFactor()` (1 without
buffers):
the `default` mode multiplies the first visible surface's eye-lit colour by it, then
composites any volume in front of that surface over it as before, and the
`ambientOcclusion` mode shows it as grey (volumes aren't drawn in that mode). The
`ambientOcclusion.unblurred` mode shows the occlusion pass's factor instead
(`unblurredOcclusionFactor()`), to compare with the blurred one.

The occlusion and blur passes use the radius `Preview::occlusionRadius()`, which
`Preview::render()` hands to ISPC (`PreviewFrameState_setOcclusionRadius()`) after the
frame's `World::embreeSceneUpdate()`: the renderer's `ambientOcclusionRadius`, or if that is 0, 3
percent of the diagonal of the world's bounds (`World::bounds()`), so a derived radius
follows the bounds whenever they change. `Preview::commitParameters()` clamps a negative
`ambientOcclusionRadius` to 0 without a warning, so a negative radius derives the radius
too. A world with nothing in it has empty bounds and a radius of 0, though such a world
runs no passes. The radius covers `pixelScale() · r / depth` pixels at a surface's view
depth for a perspective camera, and the same number at every depth for an orthographic
one; positions are rebuilt through each camera's own rays.

## The quality renderer

The `quality` renderer subtype (VisRTX's name for its path tracer;
`renderer/pathtracer/PathTracer.{h,cpp,ispc}`), which this file also calls the pathtracer,
is the start of a port of Typhoon's path tracer. `PathTracer` derives from `Renderer`, as
`Preview` (the `preview` renderer, `renderer/preview/Preview.{h,cpp,ispc}`) does: the base
holds only what the subtypes share (`background`, the task grain size, the `extension`
property and the interface `Frame` calls), and every subtype's ISPC struct is the base's
with each subtype's parameters as a named member (`Renderer::preview`,
`Renderer::pathtracer`). `PathTracer` overrides `render()` (its one pass),
`wantsRender()` (until the image is done refining) and `newFrameState()` (its refinement
state, `PathTracerFrameState`; see [Adaptive sampling](#adaptive-sampling)). Its shading
pass is Typhoon's
lit path loop (`_IntegratePath()`), ported in `PathTracer.ispc` with its transport policy in
`renderer/pathtracer/Transport.isph`:

- `PathTracer_renderFrame()` traces `pixelSamples` paths per pixel through jittered points
  of the pixel. Each loop iteration traces one segment: a finite light nearer than the
  surface ends the path with its radiance (camera segments see only `visible` sphere
  lights; later segments every sphere light, MIS-weighted against light sampling by the
  power heuristic with the multi-sample light pdf `lightSamplesPerHit` × pdf); a miss ends
  it with the `visible` hdri lights (camera segments, opaque; see [Lights](#lights)), else
  the background (camera segments: colour and alpha, which doesn't light the scene), or
  with what a ray leaving a surface sees (`gatherEnvironment()`: directional lights with a
  cone, every hdri light and the ambient light's dome, MIS-weighted; see
  [Background and ambient light](#background-and-ambient-light)); otherwise the surface is
  shaded through the material interface (`PathMaterial_shade()`,
  `renderer/pathtracer/PathMaterial.isph`).
  A segment through transferFunction1D volumes may first collide in one, which is then
  the path's next vertex (see [Volumes](#volumes)).
- A vertex adds its emission, then next-event estimation (`directLighting()`, Typhoon's
  `_ComputeDirectLightingMIS()`): for every light, `lightSamplesPerHit` samples stratified
  over a grid of floor(sqrt(n)) columns, each from the vertex's
  `Chain(DirectLightSelect, light)` / `Split(DirectLightSample, n, i)` domains, times
  `PathMaterial_eval()`, the light's 1 / pdf, the shadow ray's visibility and the power
  heuristic against the BSDF pdf (none for delta lights; an hdri is sampled in the
  hemisphere the BSDF reflects into where it is reflection-only), clamped to
  `fireflyClampThreshold` per sample and averaged; every contribution to the path is
  clamped again (`addPathRadiance()`). The path then follows the BSDF sample with throughput
  f / pdf (f for specular samples), remembering the pdf for MIS (0 after specular events).
- `maxBounces` is Typhoon's: vertices up to `maxBounces` scatter, and the ray leaving the
  last one only gathers the lights it finds (Typhoon's emitter-only segment); a surface it
  hits ends the path, except a mask cut-out, which it passes as a null event (a native
  material's by its opacity, a C++ material's by a visibility-only evaluation of its
  presence; a partly present `blend` surface ends it). This follows step 7 of Typhoon's
  `ARCHITECTURE.md`, not its code: Typhoon traces that segment with its light-only ray
  mask (`RayMask::Light`), which ordinary surfaces don't carry, so it finds sphere lights
  behind walls and, where the segment then misses, gathers distant (and dome) lights
  through them. This is a deliberate difference from Typhoon (maintainer decision, keep
  it): at low `maxBounces` (an interior lit through a window at 0–2) Typhoon's images are
  brighter, and the segment costs halcyon a full-scene trace. A volume-only boundary (an
  interior medium's, see [Interior media](#interior-media)) is passed like a mask cut-out,
  into or out of its medium, whose transmittance the segment takes, as Typhoon's
  light-only mask passes it (maintainer decision, keep it; the
  segment's rule as a whole stays a difference from Typhoon, listed in [Typhoon
  differences](#deliberate-differences-from-typhoon)). Russian roulette starts at vertex
  `minBouncesBeforeRR` (survival the throughput's largest component, at most 0.95, from
  the `RussianRoulette` domain), not on the emitter-only segment.
- Shadow rays (`visibility()`, Typhoon's `_Visibility()`) go on through surfaces up to 16
  material hits (256 hits in all), multiplying what each lets through: 1 − presence plus
  presence × its straight-through transmission (tinted) for thin-walled blockers and, with
  caustics off, thick ones (`PathMaterial_shadow()` returns the material's facts); 1 −
  opacity for thick blockers with `enableCaustics`, and with caustics off for thick
  blockers of the vertex's own object when its BSDF has a coupled thick dielectric.
  Surfaces without a material and `visible` sphere lights (Typhoon's light
  geometry) block them. `disableShadows` makes every shadow ray unblocked. They leave from
  the hit lifted towards the smooth surface of smooth-shaded triangles near the terminator
  (`smoothShadowOffset()`: the triangle's corner hook `getTriangleCorners`, `Geometry.ispc`,
  and Typhoon's `ComputeSmoothTriangleShadowOffset()`), and stop at 0.99 of the light's
  distance, or once no more than the ray offset is left (Typhoon: 0.001). Each stretch
  between surfaces multiplies the transmittance of the volumes on it (ratio tracking, see
  [Volumes](#volumes)) and of the interior medium it is in (Beer-Lambert, see
  [Interior media](#interior-media)).
- Ray offsets: rays leave surfaces (path, shadow and volume-boundary rays) from the hit
  moved along the geometric normal to the side they go to (Typhoon's `OffsetRayOrigin()`
  direction logic), by `epsilonFrom()` (`shading/halcyon_ray.isph`, the `preview` renderer's
  `continuePastHit()` epsilon): about 8 ulps of the larger of the hit's magnitude and its
  distance along the ray, at least the smallest normal float (`TRANSPORT_EPSILON_MIN`;
  the `preview` renderer keeps VisRTX's 1e-8, `HALCYON_EPSILON_MIN`, which would be 1% of
  a scene of size 1e-6). That is also their tnear (camera rays 0). Typhoon uses a fixed
  1e-4 in world units, which leaks light through contacts in small scenes and rounds away
  (acne) far from the origin; ANARI scenes have no unit, so halcyon's offsets follow the
  scale. Likewise a sphere light's intersection (`SphereLight_intersect()`) takes hits
  nearer than `epsilonFrom()` of the ray's origin for the origin itself, where Typhoon's
  fixed 1e-6 drops lights in scenes smaller than about 1e-5. This is a deliberate
  difference from Typhoon (maintainer decision, keep it); `path_integrator` renders one
  scene at scales 1e-7, 1e-3, 1 and 1e4. The shadow terminator lift is Typhoon's.
- Presence: a material's presence is the probability that a path interacts with it
  (`Presence` domain); otherwise the hit is a null event: the ray goes on past it by the
  hit's `epsilonFrom()`, with its differential, and the vertex counts as a path event but
  not a bounce. A native material whose opacity is 0 (a mask cut-out) is passed without
  shading it (a C++ one draws its presence, with the same outcome). As in
  Typhoon, light found after a null event is MIS-weighted with the light's pdf from the
  point past the surface, not from the last scattering vertex.
- Materials are Typhoon's C++ closures of MaterialXCpp graphs, or for matte and most
  physicallyBased materials a native port of what their graphs compute (see
  [Materials in the pathtracer](#materials-in-the-pathtracer)), whose presence is their
  opacity after `alphaMode`: `blend` surfaces are stochastic cut-outs to paths and let
  1 − opacity of a shadow ray through.
  A surface with no closure (no valid material, or a graph that didn't compile) is lit, as
  in Typhoon, only by next-event estimation without MIS (which samples the ambient light's
  dome too) and goes no further, as a Lambertian (`MATERIAL_KIND_LAMBERT`) of the device's
  `invalidMaterialColor` (Typhoon: of its display colour), passed in as
  `PathShadeInput::baseColor`; the native kind takes `PathShadeInput::baseOpacity` (the
  opacity after `alphaMode`, `Surface_getCompositeOpacity()`) as its presence and opacity.
- Caustics are off by default, as by Typhoon's `enableCaustics`: after a diffuse-like
  event the shading call prunes caustic-class lobes, and a specular or boundary-crossing
  sample ends the path, but for a translucent one (diffuse transmission); with
  `enableCaustics` such paths go on, their contributions clamped to
  `causticsClampThreshold`. A path that meets a dispersive closure follows one
  hero wavelength from there. See [Dispersion and caustics](#dispersion-and-caustics).
- A path that crosses into a thick surface with an interior medium, by any sample, or
  crosses a volume-only boundary, is in that medium until it crosses out (see
  [Interior media](#interior-media)).
- A subsurface sample enters the surface and walks through its medium to where it leaves
  (see [Subsurface scattering](#subsurface-scattering)).
- Random numbers come from Typhoon's sample generator, ported to ISPC in
  `renderer/pathtracer/Sampling.isph`: OpenQMC's blue-noise Sobol sampler
  (`oqmc::SobolBnSampler`) with Typhoon's named domains (`SAMPLE_DOMAIN_*`, Typhoon's
  `ty::SampleDomainKey`). Each pixel sample has a root domain (`SampleDomain_root()`, from the
  frame seed, the pixel and the sample index), and every decision draws from a domain
  derived from it as Typhoon derives it: the camera jitter from `CameraJitter`, each path
  vertex from `Chain(PathBounce, pathEvent)` with `Presence`, `Wavelength` and `BsdfSample`
  forks. `pathEvent` counts every surface the path meets, including the mask cut-outs it
  passes through and a subsurface walk's exit, as Typhoon's does, and every volume or
  medium collision (see [Volumes](#volumes), [Interior media](#interior-media)). The
  numbers are bit-identical to Typhoon's (see [Verification against
  Typhoon](#verification-against-typhoon)). New random decisions must draw from Typhoon's domain for them,
  never from a sequence of dimensions; decisions Typhoon doesn't take (the volumes') draw
  from halcyon's own keys (`SAMPLE_DOMAIN_VOLUME_*`, from 0x1000), derived the same way.
  Sample indices count
  a pixel's samples from the last change over every accumulated render (`PathTracerFrameState::firstSample`
  plus the sample within the render), so N renders of one sample equal one render of N
  samples bitwise, and a render after a change repeats the first render's samples.
- Accumulation and adaptive sampling: see [Adaptive sampling](#adaptive-sampling).
- Depth and id channels come from the visible hit of the pixel centre's ray
  (`traceFirstVisibleSurface()`), as in the `preview` renderer, traced only if the frame
  has one of those channels, and only on the first render after a change. The albedo and
  normal channels come from the paths themselves and accumulate with the colour (see
  [Frame channels](#frame-channels)).
- The device links `halcyon_path_materials`, the C++ side of the interface, which shades
  every material that isn't native. At each path vertex the gang's lanes of C++ materials
  go together through one batched call (`PathMaterial_shadeSoA()`: SoA shading inputs and
  the active-lane mask in, per lane the material's state, BSDF sample and a handle out),
  which evaluates each lane's graph into a closure kept in the calling thread's arena
  (`thread_local`, grow-only); later calls at the same vertex (`_evalSoA()` per light
  sample, `_subsurfaceSoA()`, `_mediumSoA()`) name the closure by its handle. The shading
  call resets the arena, so a closure lives until that thread's next shading call, the
  gang's next vertex; the shadow call (`_shadowSoA()`) evaluates blockers without it. Native
  kinds run inline in ISPC and never call. Why and costs: ADRs 0003 and 0004.

Typhoon's other settings are declared in `HalcyonDefinitions.json` only once halcyon
honours them.

### Adaptive sampling

The pathtracer refines its image progressively with Typhoon's adaptive sampling
(`TY/renderer/renderer.cpp` `Render()`, `TY/renderer/aov/aovOutput.cpp`):

- **Always progressive:** a render with nothing changed since the last adds samples to the
  image, **whatever the frame's `accumulation` says** (`KHR_FRAME_ACCUMULATION` makes it an
  opt-in, default false). This is a deliberate difference from the spec (maintainer
  decision, keep it): VisRTX does the same, and hdAnari (usdview) never sets
  `accumulation`, so an opt-in pathtracer would never converge there. `Frame` doesn't read
  `accumulation`, and asks the renderer whether to render without a change
  (`PathTracer::wantsRender()`); the `preview` renderer keeps the base's answer, so its
  frames are skipped when nothing changed, as before. Any change (an object finalized, a
  device parameter applied) starts over.
- **Per frame:** the refinement is the frame's `PathTracerFrameState`
  (`renderer/pathtracer/PathTracer.cpp`, its ISPC struct in `PathTracer.ispc`), which the
  frame owns: the per-pixel buffers below, `accumId` (renders since the last change, 0
  after one), the samples taken and whether the image is done, and the properties.
- **A render is a pass:** each render adds `pixelSamples` samples to every pixel that isn't
  converged (Typhoon's passes add one). Every sample goes into the pixel's running mean
  colour (`accum`, float4) and, for its colour, Welford sum of squared deviations
  (`varianceM2`), one by one (`AdaptiveSampling_addSample()`, Typhoon's
  `_UpdateVariance()`); the albedo and normal channels' running means take the same
  samples. After its samples, a pixel is **converged** (`converged`) once it has
  `minSamplesBeforeAdaptive` (64) samples and, in every colour channel, the variance of its
  mean is at most 1e-6 + `adaptiveThreshold` (0.01) × mean² (`AdaptiveSampling_converged()`,
  Typhoon's `_IsPerChannelVarianceConverged()`). Converged pixels take no more samples and
  keep all their channels until a change. So pixels converge between renders: with
  `pixelSamples` 1 exactly where Typhoon's do, with more at the end of the render that
  reaches the test.
- **Sample indices stay Typhoon's:** pixels that aren't converged have all taken the same
  samples since the last change (`firstSample`), so a pixel's k-th sample is sample
  k of Typhoon's per-pixel sequence (`SampleDomain_root()`), adaptive or not.
  `AdaptiveSampling.ispc` is compiled without fused multiply-adds and divides as `GfVec3f`
  does (by the reciprocal, in double), so a pixel converges after the same sample as in
  Typhoon. The colour shown is that Welford mean; Typhoon
  shows its render buffer's sum / count, which rounds differently in the last bits.
- **Done refining:** once every pixel is converged, or the samples per pixel reach the
  **sample limit** (`PathTracer::m_sampleLimit`; whole renders, so with `pixelSamples` not
  dividing it the last render goes past it), the image is done and renders with nothing
  changed are skipped again (`PathTracerFrameState::updateRefinement()`, which scans the converged flags
  after each render). `minSamplesBeforeAdaptive` at least a positive sample limit, or
  negative, turns adaptive sampling off.
- **The sample limit** is `sampleLimit` (VisRTX's name) where it is set (an INT32), else
  `convergedSamplesPerPixel` (Typhoon's name); both are declared with the default 256
  (`PathTracer::commitSampleLimit()`). Why: hdAnari forwards every declared setting at its
  declared default unless the user changes it in usdview, so both always arrive, and it
  reads `sampleLimit` as the render setting it stops at (falling back to 128 for renderers
  that declare none), waiting for `numSamples` to reach it. A limit taken from
  `convergedSamplesPerPixel` there would stop halcyon's refinement short of what hdAnari
  waits for, and usdrecord would never finish (maintainer decision, 2026-10-15; before
  it, whichever of the two differed from 256 won). So from usdview only `sampleLimit` sets
  the limit. A set `sampleLimit` with a `convergedSamplesPerPixel` other than 256 and
  other than it warns that `convergedSamplesPerPixel` doesn't stop refinement, once per pair
  of values (committing the same pair again doesn't warn again); one at 256 doesn't warn,
  as hdAnari sends it. `sampleLimit`
  0 (negative values are clamped to 0) means no limit, as in VisRTX: renders stop only once every pixel converged (never with
  adaptive sampling off), and the ray differentials keep `convergedSamplesPerPixel`'s scale.
  hdAnari treats 0 as unbounded and never stops on `numSamples`: it renders until
  something changes, and halcyon skips its renders once every pixel converged.
- **Reported** as frame properties, as of the last render (an `ANARI_WAIT` query runs after
  the renders queued before it; each is its own `std::atomic`, written by the worker, so a
  query without `ANARI_WAIT` during a render may read one from the new render and the
  other from the last):
  `refinementProgress` (FLOAT32, `KHR_FRAME_ACCUMULATION`'s standard property):
  `numSamples` / the sample limit, at most 1, or without a limit (`sampleLimit` 0) the
  fraction of converged pixels; it is 1 exactly when the image is done. `numSamples`
  (INT32; VisRTX's property, which hdAnari reads and compares with `sampleLimit`; declared
  in `HalcyonDefinitions.json`): the samples per pixel since the last change, or the sample
  limit once every pixel has converged (without a limit, the samples taken), so a client
  comparing it with the limit sees the image done. That is a deliberate difference from
  VisRTX's `numSamples`, the samples accumulated (maintainer decision, keep it): with the
  true count after early convergence (say 70 of 256), hdAnari would see 70 < 256 and
  render forever, each render skipped as unchanged, so usdrecord would never finish.
  The frame asks its renderer's state for them (`RendererFrameState::getProperty()`).
  Neither is reported (`anariGetProperty` returns 0) before the frame's first `quality`
  render, after an invalid frame or for the `preview` renderer.
- **Not ported:** Typhoon's coarse preview passes (strides 8, 4, 2 before the first full
  pass, then discarded): an ANARI render returns one image, and the first render after a
  change is already a full one-sample image. Typhoon's `adaptiveHeatmap` AOV has no halcyon
  channel (the converged flags are there to drive one).
- The sample limit also scales the camera ray differentials, as Typhoon's
  `samplesToConvergence` does, and `convergedSamplesPerPixel` without a limit (see
  [Shading context](#shading-context)).

The pathtracer's `mode` is `default` (path tracing) or one value of the shading context at
the pixel center's first visible surface, written raw into the colour channel for debugging:
`position` (object space), `normal`, `tangent`, `bitangent`, `texcoord`, `displayColor`,
`dPdu`, `dPdv`, `dndu`, `dndv`, `dPdx`, `dPdy`, `texcoordDerivatives` (du/dx, dv/dx, du/dy,
dv/dy); a miss is transparent black. Unknown names path trace.

### Lights

The `directional`, `point`, `spot`, `quad`, `ring` and `hdri` lights (`light/`,
KHR_LIGHT_DIRECTIONAL, _POINT, _SPOT, _QUAD, _RING and _HDRI) light only the
`quality`'s images;
the `preview` renderer isn't lit by them, as helide isn't (it has no lights), and doesn't
list their extensions (see [Parameters and properties](#parameters-and-properties)), except
the hdri's: it shows the `visible` hdris where its camera rays miss, as the `quality`
does, through the same lookup (`missColor()`, see [Rendering pipeline](#rendering-pipeline);
maintainer decision, keep it), and so lists `KHR_LIGHT_HDRI` (not
`KHR_LIGHT_PRIMARY_VISIBILITY`, though it reads the hdris' `visible`). The world writes
the light records for both renderers (`World::updateLights()`, from
`World::embreeSceneUpdate()`); a world without lights has none, and the miss lookup
then costs a loop over none. The quad light's
class is `QuadLight`, as `Quad` is the quad geometry's (`Geometry.cpp`, same namespace).

- A light has no ISPC slot. The world keeps the mirror: on each scene update after any
  object was finalized, `World::updateLights()` writes a world-space record (`struct
  Light` in `shading/halcyon_objects.isph`, through the setters in `light/Light.ispc`)
  for every valid light of every valid instance, once per instance transform, into an
  array ISPC reads as `World::lights`. Instance transforms move `position`, turn
  `direction`, `up` and `c0` and map a quad's `edge1`/`edge2` (vectors, so a scale grows its
  area), but don't scale `radius` or `innerRadius` (the spec). A light that emits nothing
  gets no record, except a `visible` hdri (of `scale` 0, `color` 0 or a black map), which
  camera rays see black, as Typhoon's see a dome without radiance. A quad's or ring's `intensityDistribution` is copied in `finalize()`
  (`Light::finalizeDistribution()`), as is an hdri's map with its sampling distribution,
  and the records point at the copies.
- **Units** follow ANARI's radiometric definitions, not Typhoon's UsdLux ones (intensity ×
  2^exposure, `normalize`): `Light.h` has the conversion table. Each light's brightness
  parameter (the first one set, in the spec's order) becomes the one quantity its shape is
  sampled with, `Light::emission` (× `color`): a directional light's irradiance (delta) or
  radiance (`angularDiameter` above 0; irradiance / (π sin² a) for half angle a), a point
  light's intensity (delta; power / 4π) or radiance (`radius` above 0; intensity / (π r²)),
  a spot's intensity along its axis, a quad's or ring's radiance (intensity / area; a ring
  of `radius` 0 is a delta point of that intensity), an hdri's `scale` (its texels are
  radiance). Off the axis the intensity follows the
  light's profile: a spot's cone weight, a ring's cone weight × cos γ (a Lambertian
  emitter) and a quad's cos γ, or with `intensityDistribution` the distribution in place
  of cos γ (so the radiance is intensity × distribution / (area cos γ)). `power` is
  divided by the profile's integral over the directions the light emits to; for a spot the
  spec defines that integral cosine-weighted (≈ π sin²(openingAngle / 2)), and halcyon
  follows it. A quad emitting from `both` sides spreads `power` over both. `radiance`
  without a size emits nothing and warns.
- **Shapes and cones:** a quad's front is `edge2` × `edge1`, the spec's (with the default
  edges it faces −z, Typhoon's rect light's local emission axis); `edge1` and `edge2` may
  be any parallelogram. A ring is a disk of `radius` facing `direction`, an annulus with
  `innerRadius` (an `innerRadius` not below `radius` is ignored, with a warning if
  `radius` is above 0), emitting from
  that side. `openingAngle` (π, clamped to [0, 2π]) and `falloffAngle` (0.1, clamped to
  [0, openingAngle / 2] silently) give spots and rings Typhoon's shaping cone: weight 1 up
  to `openingAngle` / 2 − `falloffAngle` off the axis, 0 past `openingAngle` / 2, a
  smoothstep in the angle between (Typhoon's `shaping:cone:angle` = `openingAngle` / 2,
  `softness` = `falloffAngle` / (`openingAngle` / 2)).
- **`intensityDistribution`** (quad and ring; an ARRAY1D or ARRAY2D of FLOAT32 with at
  least two values over γ, otherwise it warns and is ignored; negative or non-finite values
  warn and count as 0) follows the spec's C-γ mapping:
  values over γ in [0, π] from the axis (a ring's `direction`, a quad's front normal, or
  its back's if only the back emits), rows over the C half-planes in [0, 2π) from the C0
  half-plane (`c0` projected into the ring's plane, a quad's `edge1`), C growing
  right-handed about the axis (C90 = axis × C0), interpolated linearly in γ and
  periodically in C. It has no Typhoon counterpart (Typhoon reads IES files).
- **`hdri`** (`light/Hdri.*`) is Typhoon's dome light: an
  equirectangular map at infinity, its `radiance` (an ARRAY2D of FLOAT32_VEC3, W/sr/m²)
  times `scale` and `color`, looked up unfiltered (nearest texel, as Typhoon's
  `SampleLightTexture()`). The spec leaves the mapping open; halcyon's is VisRTX's, and so
  what hdAnari sends for a USD dome light (`up` +y and `direction` +z for a Y-up stage,
  file rows flipped): `up` is kept, `direction` made perpendicular to it, and with left =
  `up` × `direction` the array's columns turn from −`direction` (u = 0) through left
  (0.25), `direction` (0.5, the image's center) and −left (0.75), its rows from −`up` (row
  0) to `up` (the last row). That is Typhoon's dome light frame (X = left, Y = `up`, Z =
  `direction`) with Typhoon's rows reversed, so `finalize()` copies the texels rows
  reversed and runs Typhoon's code unchanged on them: its sampling distribution
  (`BuildDomeLightSamplingDistribution()`, ported in `light/HdriDistribution.cpp`:
  each texel's Rec. 709 luminance × its row's solid angle, a marginal CDF over rows and a
  conditional CDF per row; black rows weigh their texels by solid angle, a black map is
  uniform) and in `Lights.isph` its sampling and pdf (`SampleDomeLight()`,
  `EvaluateDomeLightDirection()`). Where the BSDF is reflection-only (the BSDF that
  samples, after caustic pruning: `PATH_MATERIAL_BSDF_REFLECTION_ONLY`, at next-event
  estimation and, the last vertex's, when a ray then misses) the sample is folded into the
  hemisphere around the shading normal (mirrored across its plane, pdf the sum of both
  directions'), as Typhoon's `SamplingMode::ReflectionHemisphere`; volume collisions
  sample the whole sphere. Typhoon takes the unpruned closure's flag for the miss, so
  where caustic pruning removes a transmission lobe its MIS weights don't sum to 1;
  halcyon's one flag is a deliberate difference from Typhoon (maintainer decision, keep
  it; listed in [Typhoon
  differences](#deliberate-differences-from-typhoon)): see [Dispersion and
  caustics](#dispersion-and-caustics). Rays leaving surfaces gather every hdri where they
  miss, MIS-weighted (full weight where its pdf is 0, at the poles); camera rays that miss
  see the sum of the `visible` hdris, opaque (alpha 1), in place of the background, or the
  background if none is visible (Typhoon's `domeLightCameraVisibility` setting, per
  light), in both renderers (`missColor()`; the map lookup, `HdriLight_radiance()`, is in
  `shading/halcyon_hdri.isph`). `visible` false hides it from camera rays only: it still
  lights the scene (the spec's "can be directly seen"). Both rules are a maintainer
  decision (keep them) and a
  deliberate difference from Typhoon (listed in [Typhoon
  differences](#deliberate-differences-from-typhoon)): there an invisible dome (USD
  visibility off) contributes nothing, and camera visibility is one setting for every dome
  (`domeLightCameraVisibility`; on, camera misses see the sum of the visible domes, black
  where there are domes but none is visible). A non-rigid instance transform turns `up`
  and `direction` and the frame is rebuilt orthonormal from them. `layout` other than
  `equirectangular` warns and is ignored; a missing `radiance`, one of another element
  type or an empty one, and `up` and `direction` zero, non-finite or parallel, warn and
  make the light invalid; negative or non-finite texels warn and count as 0.
- **Sampling** (`renderer/pathtracer/Lights.isph`) is ported from Typhoon's light
  samplers: `Light_sample()` (`ty::LightSampler::GetLightSample()`) and `Light_eval()`
  (`EvaluateLightDirection()`) return Typhoon's `LightSample`, with its 1 / pdf in
  steradians and `delta` for lights only light sampling finds. `directional` is Typhoon's
  distant light (a cone sampled uniformly), `point` with a radius its sphere light
  (uniform in solid angle, else in area), `quad` its rect light and `ring` with a radius
  its disk light (both uniform in area; the annulus maps the disk's sqrt(u) onto the area
  between the radii). The delta point, spot and ring lights are halcyon's own (Typhoon's
  spheres and disks need a radius). A delta directional light (`angularDiameter` 0) is
  found by light sampling only: rays leaving surfaces that miss (`gatherEnvironment()`)
  don't gather it. Typhoon's gather it where their direction's cosine to its axis is
  within 1e-5 of 1 (`EvaluateDistantLightDirection()`, an angular radius of about 0.26°),
  at its irradiance as radiance and, after a specular event, at full weight, so mirrors
  and smooth glass show a delta sun as a disc of radius about 0.26° (diameter about 0.51°;
  and rough lobes pick up a little, weighed by MIS against a pdf of 1). That is a
  deliberate difference from Typhoon (maintainer decision, keep it; listed in [Typhoon
  differences](#deliberate-differences-from-typhoon), to report upstream); a sun of
  `angularDiameter` above 0 is a cone of radiance, which rays find and mirrors show.
  Typhoon's gather also skips distant lights that aren't `visible` (`lighting.cpp:61`), as
  its light sampling does (USD visibility); halcyon reads `visible` as camera visibility
  only, so a directional cone that isn't `visible` still lights the scene and is gathered:
  an existing difference from Typhoon (listed in [Typhoon
  differences](#deliberate-differences-from-typhoon)). Typhoon samples shaped lights half
  by their shaping (`ApplyShapingAwareFinitePdf()`); halcyon samples them by area only,
  which is unbiased but noisier for narrow cones and peaked distributions (maintainer
  decision, keep it, until narrow cones prove too noisy). Typhoon's textures, focus, IES
  files, colour temperature and cylinder light aren't ported (no ANARI counterpart).
- `visible` (KHR_LIGHT_PRIMARY_VISIBILITY's parameter, default true) decides whether camera
  rays see a sphere, quad, ring or hdri light, and whether a sphere, quad or ring blocks
  shadow rays (Typhoon puts
  lights visible to camera rays into its Embree scene, where shadow rays hit them). Rays
  meet visible lights from both sides, as Typhoon's Embree geometry: a side of a quad or
  ring that doesn't emit is black and ends the path, and blocks shadow rays. Other rays
  find every finite light from the sides it emits to. KHR_LIGHT_PRIMARY_VISIBILITY is
  listed (`quality` only), as halcyon has every light extension it depends on; it
  declares `visible` on every light.

`test_lights.cpp` (ctest `lights`) renders a Lambertian floor under each light and
brightness parameter and compares every pixel with albedo / π × the irradiance (in closed
form for points, spots, polygons and parallel disks, by quadrature over the light for
cones and distributions), and checks spot and ring cones, quad sides, a mirror facing a
sun (a delta sun reflects nothing, exactly, also 0.2° off the reflection; one of
`angularDiameter` 0.5 its radiance),
`intensityDistribution` (1D, and 2D for the sense of C), instancing, shadows, camera
visibility, the warnings and that the `preview` renderer ignores them (the hdri aside,
which `hdri` checks). `test_hdri.cpp`
(ctest `hdri`) checks the hdri: the map's orientation as camera rays see it (frames, a
`direction` made perpendicular, an instance rotation), furnace tests (a white Lambertian
sphere, and a white scattering volume, in a uniform map render to its radiance), a
Lambertian floor under textured maps (a smooth sky, a bright lower hemisphere it mustn't
see, a sun texel, several light samples) against the irradiance integrated over the
texels, `visible`, `scale`, the warnings, and the `preview` renderer: its miss pixels
bitwise the `quality`'s (colour and alpha, where the pixel and its neighbours show one
texel, so the pathtracer's jitter sees it too, so pixels on texel edges aren't compared;
also through an instance rotation, around a floor, in the `Ng`, `primitiveId`, `hitVolume`
and both ambient occlusion modes, and for two visible hdris, which sum), the background
where `visible` is false, and surfaces rendered bitwise as without the light. Typhoon
renders the same scene (UsdPreviewSurface spheres on a floor under a dome with a sun, also
rotated) within rounding of halcyon at the same seed (a one-off comparison, not a
ctest).

### Background and ambient light

What the `quality` does with the renderer's `background` (KHR_RENDERER_BACKGROUND_COLOR,
_IMAGE) and `ambientColor`/`ambientRadiance` (KHR_RENDERER_AMBIENT_LIGHT);
`test_background_ambient.cpp` (ctest `background_ambient`) checks each
combination:

- **The background** is only what camera rays that miss see (`missColor()`), with its
  alpha (a colour, or the image at the jittered screen position, as in the `preview`
  renderer): it lights nothing, as in the `preview` renderer and VisRTX. Where an hdri
  is `visible`, camera rays see the hdris instead, opaque (see [Lights](#lights)).
- **The ambient light** is a constant dome of radiance `ambientColor` × `ambientRadiance`
  (negative values clamped to 0) that camera rays don't see: Typhoon's dome light
  without a texture, an hdri record (`Light_setAmbient()` in `light/Light.ispc`) of
  one white texel without a sampling distribution, in the world frame, that
  `PathTracer::updatePathTracerISPC()` writes and `PathTracerParameters::ambientLight`
  points at (NULL where it emits nothing, so a scene without it costs nothing). The path
  loop treats it as one more light after the world's (`pathLight()` in
  `PathTracer.ispc`, chained with index `world->numLights`, so the world's lights keep
  their sample numbers): next-event estimation samples it uniformly over the sphere, or
  over the hemisphere for a reflection-only BSDF, as Typhoon's `SampleDomeLight()` of a
  dome without a texture, and rays
  that miss gather it weighed against that by MIS. So surfaces without a closure and
  volume collisions see it through next-event estimation, and its shadow rays give
  ambient occlusion. A lone Lambertian under it is no longer exact per sample (BSDF
  sampling alone would be): its pixels are noisy until they converge.
- Together with hdri lights: the ambient light and every hdri, visible or not, add up to
  what lights the scene; camera misses never see the ambient light.

### Volumes

`transferFunction1D` volumes over `structuredRegular` fields scatter under the pathtracer
(`renderer/pathtracer/VolumeTransport.isph`).
Typhoon has no grid volumes, so this is halcyon's own; its oracles are the `preview`
renderer (alpha, depth, ids) and analytic scenes.

- **Medium:** the extinction is σt = −ln(1 − α) / `unitDistance` (`Volume_extinction()`
  in `shading/halcyon_volume.isph`), α the transfer function's opacity × its colour's
  alpha at the sample (as the `preview` renderer's march reads them), clamped to
  [0, 0.999] (`VOLUME_MAX_ALPHA`). The transmittance over a distance d is then
  (1 − α)^(d / `unitDistance`), the `preview` renderer's per-step compositing, so alpha
  matches it up to its march's step error. Distances are along the world-space ray, as
  the march's are, also through scaled instances. The colour (rgb, clamped to [0, 1]) is
  the single-scattering albedo (σs = colour × σt); volumes don't emit.
  `volumeSamplingRate` isn't read. A `unitDistance` of 0 or less, or not finite, makes the
  volume invalid in both renderers, with a warning (it has no extinction to give; helide
  marches it, opaque at 0 and with growing transmittance below; maintainer decision,
  2026-10-15).
- **Majorants:** one per volume, `Volume::majorant`, computed by
  `Volume_setTransferFunction1D()` at each finalize: the extinction of the largest
  opacity times the largest colour alpha (magnitudes) the transfer function blends over the
  values the field takes (`SpatialField::valueLower`/`valueUpper`; all of the transfer
  function where that range is unknown), and one per **macrocell**: a structuredRegular
  field's grid of cells of 8³ voxel intervals over its bounds
  (`SpatialField::macrocellDims`/`macrocellInvSize`), each with the range of the voxels
  its samples blend (`SpatialField::macrocellRanges`: voxels 8i to 8i + 8 along each axis,
  so neighbours share a layer; NaN voxels skipped), which the field's `finalize()`
  computes in parallel on Embree's threads (`SpatialField_computeMacrocellRanges()`) and
  from which it takes its value range. The volume's `finalize()` maps each cell's range
  through the transfer function the same way (`Volume_computeMacrocellMajorants()`, also
  in parallel; 0 for a cell of NaN only) into `Volume::macrocellMajorants`. So a field
  change (the volume observes its field, which helium finalizes first) or a transfer
  function change rebuilds them. Memory: 8 bytes per cell in the field and 4 in each
  volume over it, 12 bytes per 512 voxels (about 0.6% of FLOAT32 voxels, 2.3% of UFIXED8
  ones); the ranges replace the field's former whole-voxel scan. Samples are clamped to
  the volume's majorant. The cell size is fixed, and fields and volumes compute their
  cells at every finalize whichever renderer the app uses, though only the `quality` reads
  them (maintainer decision, keep it).
- **Tracking steps cell by cell:** the majorant along a ray just past t is, for each
  volume the ray is in there, its macrocell's (`Volume_majorantFrom()` in
  `shading/halcyon_volume.isph`), which holds until the ray leaves that cell; the cell
  is taken from the next cell boundary past t (per axis, `Volume_macrocellStep()`), so
  each step makes progress where rounding puts the point on a boundary. Empty cells
  (majorant 0) are skipped without drawing. Where a step can't resolve its cell (t too
  large for the cell size) the volume's majorant holds instead. A sample that rounding
  puts across a cell boundary may exceed its cell's majorant: ratio tracking clamps the
  extinction to the majorant, free flights then collide.
- **Overlap:** extinctions add: majorants sum piecewise along the ray, each piece ending
  where the ray enters or leaves a volume or one of its macrocells, and a collision
  scatters in a volume chosen by its share of the
  extinction (`SAMPLE_DOMAIN_VOLUME_SCATTER_CHOICE`). The `preview` renderer marches only
  the first volume entered. A homogeneous interior medium the path is in adds its
  extinction too (see [Interior media](#interior-media)).
- **Paths** delta-track (`VolumeTransport_freeFlight()`) every segment but the
  emitter-only one, from its tnear to the finite light it meets, else its surface hit,
  else the ray's end. A collision is a path vertex like a surface's: it counts as a
  `pathEvent` and a bounce (`maxBounces`), multiplies the throughput by the colour, adds
  next-event estimation over every light (`volumeDirectLighting()`: `directLighting()`'s
  samples with the phase function in place of the BSDF, MIS-weighted against phase
  sampling but for delta lights, shadow rays from the collision itself), then samples
  the phase function (`VolumePhase_sample()`: the interior media's Henyey-Greenstein,
  `Medium.ispc`, of anisotropy 0, so isotropic; its directions differ bitwise from those
  of the isotropic sampler volumes had before media, as the same distribution), whose pdf a
  light the path then finds is weighed against, and
  plays Russian roulette as surfaces do. It is a diffuse-like event (with caustics off,
  caustics after it end the path; as interior media's collisions, maintainer decision,
  keep it) and drops the ray differential. On a path in
  hero-wavelength mode the colour and transmittances weigh its spectral value and
  next-event estimation multiplies spectral values, as for Typhoon's media (halcyon's own
  extension of them; see [Dispersion and caustics](#dispersion-and-caustics)).
- **Shadow rays and the emitter-only segment** ratio-track (`VolumeTransport_transmittance()`):
  each stretch of a shadow ray between surfaces multiplies its visibility, and the
  emitter-only segment's throughput is multiplied before it gathers lights.
  `disableShadows` skips shadow rays' tracking with the rest of the shadow ray.
- **Alpha:** a camera segment that scatters gives alpha 1, one that passes the volumes
  what it finds behind them, so alpha converges to 1 − T × (1 − background alpha). The
  same rule holds for interior media (see [Interior media](#interior-media)), whose
  transmittance is a weight rather than a probability: a camera ray that misses shows
  alpha + (1 − T)(1 − alpha) of the background's alpha, T the mean of its throughput's
  channels (1 for passes of grid volumes, whose delta tracking leaves it unweighted).
- **Albedo and normal channels:** a collision is a first non-specular hit: albedo the
  throughput × the colour, normal 0 (OIDN's guidance).
- **Random numbers** come from halcyon's own domains (`Sampling.isph`, no Typhoon
  counterpart): `SAMPLE_DOMAIN_VOLUME_FREE_FLIGHT`, chained per tentative collision from
  the segment's `PathBounce` domain, `_SCATTER_CHOICE` and `_PHASE`, forked from it, and
  `_RATIO_TRACKING`, chained per tentative collision from the light sample's
  `DirectLightSample` domain (shadow rays) or the `PathBounce` domain (the emitter-only
  segment). Next-event estimation and Russian roulette at a collision draw from the
  surfaces' `DirectLighting` and `RussianRoulette` forks.
- **Cost:** every step tests every transform of every instance with volumes, in its object
  space; only the macrocells accelerate it. Measured when they landed
  (`pathtracer_volumes --bench`: a 256³ field over a floor, 0 but for four small dense
  blobs, 512 × 512, 4 samples, `maxBounces` 8, 48 threads): a render from 244 ms with one
  majorant per volume to 20.5 ms (12×), the first render with its finalize from 288 to
  about 60 ms. `VOLUME_MAX_COLLISIONS` (65536) bounds one tracking:
  past it a free flight goes on without a collision, and ratio tracking keeps the
  transmittance it found. That cap, shadow rays stopping at 0.99 of the light's distance
  (Typhoon's; light sampling skips the volume in the last 1% before a finite light), a
  ratio-tracked visibility below 1e-4 counting as blocked and samples clamped to the
  majorant are small biases kept on purpose (maintainer decision, keep them). Scenes
  without volumes skip all of it.

`test_pathtracer_volumes.cpp` (ctest `pathtracer_volumes`) checks a constant box's alpha
and transmittance against (1 − α)^(d / `unitDistance`) and the `preview` renderer, a
varying field in a scaled instance against the `preview` renderer's alpha, single
scattering in a slab under a directional light (with and without `disableShadows`, and a
floor lit through it) and an absorbing slab's ambient light (shadow rays and the
emitter-only segment) against their analytic values, overlapping volumes against one of
the summed extinction, a sparse field of many macrocells against the `preview` renderer's
alpha (also after its transfer function and its field change), the albedo and normal
channels at a collision, and depth and ids against the `preview` renderer's; `--bench`
times a sparse volume. `volume` checks that a `unitDistance` of 0 or less makes the volume invalid, the
majorant and the macrocells (rays walked cell
by cell through a sparse field: progress, one cell per step, every sample within its
cell's majorant, most of the length in empty cells).

### Interior media

Closures with an interior medium scatter and absorb inside their objects, as Typhoon's
homogeneous media do (`TY/renderer/integrator/medium.*`, `volumeTransport.cpp`): the
transport in `PathTracer.ispc`, the medium in
`renderer/pathtracer/Medium.isph` and `Medium.ispc` (compiled with `--opt=disable-fma
--math-lib=system`, so that it rounds as the C++ does).

- **Media:** a closure's `interiorMedium` (Typhoon's `mxcpp::MediumProperties`: absorption,
  scattering, Henyey-Greenstein anisotropy), which `PathMaterial_medium()` reads: OpenPBR's
  and standard_surface's transmission depth, colour, scatter and scatter anisotropy
  (`MakeTransmissionMedium()`, through the `openPBR` and `materialx` subtypes; OpenPBR's
  `transmission_color` then colours the medium, not the surface, while standard_surface's
  colours both: its transmission lobe keeps the colour as its tint), and a `materialx` volume
  shader (`anisotropic_vdf`, `absorption_vdf`): alone, a transparent boundary of the medium
  (Typhoon's `ND_hdembree_surface_volume_material`, a volume-only boundary), with a surface
  shader that surface's interior. Thin-walled surfaces have none. Native kinds have none
  (Typhoon's glTF PBR doesn't read `attenuationDistance`/`attenuationColor`).
- **Entering and leaving** (Typhoon's `_UpdatePathMedium()`): any boundary-crossing sample
  going inwards (against the exterior geometric normal) through a thick surface, or the
  crossing of a volume-only boundary, enters the closure's medium while the path is in
  none, and any boundary-crossing sample going outwards through its owner's surface leaves
  it, whatever their IOR ratio (a translucent lobe's too). Typhoon enters and leaves only
  on a refraction (IOR ratio other than 1), so a translucent sample (eta 1) crosses the
  interior without the medium and, leaving, keeps the medium outside, while next-event
  estimation through the same lobe starts in the medium inwards and out of it outwards
  (see the shadow rays below), and MIS combines estimators that disagree: on the `media`
  test's glass-and-translucent slab of absorbing interior (caustics on) 0.721 under the
  ambient light against 0.662 under an emitting enclosure of the same radiance with
  Typhoon's rules, which render alike with halcyon's exit (0.810 both with Typhoon's
  entry, 0.494 both with halcyon's, where translucent paths inside take the medium
  too). Both are deliberate differences from Typhoon (maintainer decisions, keep them;
  listed in [Typhoon
  differences](#deliberate-differences-from-typhoon), to report upstream). They concern
  only materials with an interior medium and a translucent-type lobe (custom MaterialX
  documents). **One medium at a time,** as in Typhoon: inside a medium the surfaces of
  other objects neither enter theirs nor leave it, so an object nested in another takes
  the outer medium inside it too (`media` checks it). Typhoon has no medium stack, and
  halcyon keeps it so (maintainer decision, keep it; revisit if nested media matter to an
  app). The owner is the object whose surface the path crossed: the instance, its
  transform and the surface (Embree's `instID`, `instPrimID`, `geomID`, `PathObject`);
  Typhoon's is the prototype (its mesh, shared by every instance of it), so there an
  instance of the same mesh elsewhere also leaves the medium. That is a deliberate
  difference from Typhoon (maintainer decision, keep it; listed in [Typhoon
  differences](#deliberate-differences-from-typhoon)). Camera paths start in no medium,
  and a subsurface walk leaves its path in none (Typhoon's).
- **Segments in a medium** (Typhoon's `_TraceVolumeTransmission()`, `Medium_beginTrack()`):
  for a medium that scatters, one RGB channel picked by Chiang's channel MIS (throughput ×
  single-scattering albedo, `MediumChannel` domain) samples a free-flight distance with its
  extinction (`MediumFreeFlight`), and the collision's weight (scattering × transmittance)
  or that of passing to the light or surface (transmittance) is divided by the three
  channels' mixture pdf, so chromatic media converge without a majorant's variance; an
  absorbing-only medium takes its Beer-Lambert transmittance and draws nothing. A ray that
  leaves the scene inside a medium ends the path (an open mesh's medium goes dark, as in
  Typhoon). The emitter-only segment is tracked too.
- **Collisions** before the surface and the light are vertices, as Typhoon's medium events:
  a `pathEvent` and a bounce; next-event estimation (`volumeDirectLighting()`, Typhoon's
  `_ComputeMediumDirectLighting()`, from the `MediumDirectLighting` fork with its
  `MediumDirectLightSelect`/`MediumDirectLightSample` domains, every light sampled over the
  whole sphere, weighed against the phase function's pdf only below `maxBounces`); at or
  past `maxBounces` (also on the emitter-only segment) the path ends there; Russian roulette
  from `MediumRussianRoulette`; a Henyey-Greenstein direction from `MediumPhase`
  (`Medium_sampleHenyeyGreenstein()`, the pdf MIS remembers). The path counts as after a
  diffuse event, drops its ray differential, and gathers hdris over the whole sphere where
  it then misses. The next ray starts at the collision with tnear 0 (Typhoon's 1e-4 along
  the new direction; halcyon's offsets follow the scale, see [The quality
  renderer](#the-quality-renderer)). The albedo channel takes a collision as a first
  non-specular hit: the throughput × the single-scattering albedo, normal 0.
- **With transferFunction1D volumes** their extinctions add (see [Volumes](#volumes)): the
  medium's collision is sampled first, then the grid volumes are delta-tracked up to it (or
  to the light or surface); a grid collision before it is the vertex, weighed by the
  medium's weight of passing to it; the emitter-only segment ratio-tracks them up to it.
  Collisions of either kind send shadow rays from the path's medium.
- **Shadow rays** (`visibility()`, Typhoon's `_Visibility()`) start in the vertex's medium
  and multiply its Beer-Lambert transmittance over each stretch between surfaces. They
  leave it at its owner's surface, which lets through max(1 − opacity, transmission), or
  with caustics off its straight-through transmission untinted; where in none, they enter
  a volume-only boundary's medium, and with caustics off a thick transmissive surface's
  crossed inwards (straight through, untinted: the medium colours it). A shadow ray from a
  vertex on its medium's own surface (which the path reached from inside) that leaves that
  surface outwards starts out of the medium. Typhoon's keeps the medium for its whole
  length, as it never crosses the owner again, so light through the glass from the inside
  of a coloured block is attenuated as if the medium went on to the light (all of it under
  a directional light): a deliberate difference from Typhoon (maintainer decision, keep
  it; listed in [Typhoon differences](#deliberate-differences-from-typhoon), to report
  upstream). The path leaves
  the medium there by any sample too (see Entering and leaving above). The mirror image:
  with the path outside (in no medium, at a surface it hit from outside), a shadow ray
  going inwards through the vertex's own thick surface with an interior medium, which it
  passes straight through (caustics off, and the vertex's closure has no coupled thick
  dielectric, so the own-object rule doesn't block it), starts in that medium, as it would
  had it crossed the surface there, and so takes the medium's transmittance across the
  object. A vertex hit from inside with the path in no medium (reached by a camera inside,
  nesting) keeps none, as its continuations do. Typhoon's starts in none, as the origin
  isn't a crossing, so light through such an object is untinted by its medium. The
  materials this concerns are standard_surface's transmission of a `transmission_depth`
  (OpenPBR's dielectric is coupled, so the own-object rule blocks those shadow rays; a
  MaterialX `dielectric_bsdf` under `ND_surface` leaves the closure's transmission 0, so
  its surface lets no straight shadow ray through). That is a deliberate difference from
  Typhoon too (maintainer decision, keep it; listed in [Typhoon
  differences](#deliberate-differences-from-typhoon), to report upstream), and so is its
  narrower scope (maintainer decision, keep it): only with caustics off, at a closure
  without a coupled thick dielectric, at a surface hit from outside. Coupled dielectrics
  and caustics on keep Typhoon's rule: there such a shadow ray meets its own object's far
  side as a thick blocker, which lets 1 − opacity through, where starting in the medium
  would make it the medium's owner, letting its transmission through (and counting light
  BSDF sampling finds by refraction twice).
- **Not ported:** Typhoon's Adobe OpenPBR volume transport (`useAdobeOpenPBR`, which halcyon
  never sets). Its hero-wavelength weights are (see [Dispersion and
  caustics](#dispersion-and-caustics)).
- **Not energy-conserving in glass, as in Typhoon** (a known Typhoon limitation, kept as
  Typhoon's: maintainer decision, keep it; to report upstream): light leaving a scattering
  interior through its glass is counted twice with `enableCaustics` and mostly lost
  without. Next-event estimation at a collision reaches the lights through the owner's
  surface (max(1 − opacity, transmission), so 1 for clear glass) with its MIS weight
  against the phase function, while a phase-sampled ray that then refracts out takes full
  weight (the refraction is specular, so the light it finds isn't weighed against light
  sampling); with caustics off that refraction, after a diffuse-like event, is pruned and
  ends the path, losing the phase function's share and every interreflection. A white
  furnace (smooth OpenPBR glass sphere of a white scattering interior, scatter 2 at depth
  1, under a white dome, 64 bounces) renders to 0.31 with caustics off and 2.03 with them
  on in halcyon, and 0.32 and 2.03 in Typhoon (its Sphere_Poly mesh); 1 would be right.
- **Camera rays through a volume-only boundary** stay camera rays (Typhoon's
  `isFirstBounce` survives the null crossing), and where one passes a medium without
  colliding and misses, what a miss shows (`missColor()`: the visible hdris, else the
  background) is weighed by its throughput, the transmittance of the media it crossed (in
  a chromatic medium, over the channel mixture's pdf), so an absorbing volume-only box is
  seen against the background. Its alpha follows the grid volumes' rule (see
  [Volumes](#volumes)): alpha + (1 − T)(1 − alpha) of the miss's alpha, T the mean of the
  throughput's channels, which converges to 1 − T × (1 − alpha) with T the media's mean
  transmittance (the mean, as Cycles' transparent film takes it, since one alpha can't
  hold a chromatic transmittance; a throughput of 1, as without media, leaves the miss's
  alpha exactly). Typhoon shows the background unweighted, so an absorbing volume-only box
  is invisible against it: a deliberate difference from Typhoon (maintainer decision,
  keep it; listed in [Typhoon differences](#deliberate-differences-from-typhoon), to
  report upstream). A refraction
  ends the camera segment, so what a ray finds through glass of transmission depth was
  weighted before too.
- **Cost:** none measurable for scenes without media (within run-to-run noise, about ±1%):
  medians of 3 runs of `native_materials --bench` (48 threads), matte native, matte through
  its graphs, physicallyBased native and through its graphs: before media (at 32adaaf)
  193.3, 408.0, 164.5 and 619.2 ms, with them 194.0, 409.7, 164.5 and 621.0 ms, and with
  all the medium code compiled out 193.8, 408.2, 164.5 and 623.6 ms (an earlier
  interleaved measurement showed 1–2%, within the same noise); `pathtracer_volumes
  --bench` 20.9 → 20.6 ms and `subsurface --bench` 25.7 ms both. The later differences
  from Typhoon (camera misses through media, the emitter-only segment through
  volume-only boundaries, shadow rays leaving a medium) cost nothing measurable either:
  `native_materials --bench` against 26cfb25's library in the same tree, 10 runs each,
  interleaved (half of them each first, as the second of a pair ran up to 4% slower and
  the machine drifted by about 5% over the runs), medians matte 205.7 → 204.4 ms native
  and 436.4 → 431.2 ms through its graphs, physicallyBased 174.9 → 173.8 and 662.7 → 659.6
  ms. Nor do translucent exits and shadow rays entering a medium (f4d7a8e's library
  against theirs in the same tree, 8 runs each, interleaved, two sets; medians):
  `native_materials --bench` matte 206.6 → 209.7 and 208.2 → 207.5 ms native, 435.8 →
  434.5 and 437.1 → 436.8 ms through its graphs, physicallyBased 173.9 → 177.0 and 176.6 →
  175.7 ms native, 662.1 → 660.5 and 673.9 → 663.0 ms through its graphs;
  `pathtracer_volumes --bench` 21.6 → 22.3 and 21.8 → 22.0 ms; `subsurface --bench` 27.65
  → 27.85 ms (each set's runs spread over 5–10%). Next-event estimation reads the vertex's
  interior medium (a C++ call) only where inward shadow rays may start in it (a material
  of such a medium, caustics off, the path in none).

`test_media.cpp` (ctest `media`) checks, against analytic values: an emissive plate
through an absorbing medium between volume-only boundaries (exactly emission ×
exp(−absorption × thickness)) and through a smooth OpenPBR glass block of transmission
depth (the clear block's (1 − F)² / (1 − F²) times the medium's transmittance with the
interreflections' series); one medium at a time (an absorbing box nested in another: the
outer medium throughout; an instance array of a box with a smaller transform inside the
other: another object, so the same); distances in world space (a box scaled by its
instance); shadow rays through an absorbing slab (exact) and through the glass block (the
clear block's shadow times the transmittance, exactly); single scattering in a slab lit
from behind, seen at 20° and 70°, against the analytic integral for Henyey-Greenstein
anisotropy 0.6, −0.5 and 0; furnaces (a white medium sphere, isotropic and anisotropic,
also overlapping a white transferFunction1D volume, renders to the ambient radiance; a
slab of chromatic white medium, scattering (1, 3, 6), alone and with a white grid volume,
scatters 1 − exp(−extinction) of it per channel against a black background, which only
correct channel-MIS and grid-collision weights give; these fail if either weight is
broken); a black grid volume inside an absorbing medium (the product of the
transmittances); and the albedo and normal channels at a collision; and the
differences from Typhoon: a camera ray through an absorbing volume-only box that misses
shows the background times the transmittance (colour, and alpha by the rule above, every
pixel exactly); a matte floor under the ambient light, seen from below an absorbing
volume-only slab, with `maxBounces` 0, renders to albedo × radiance × 2 E3(absorption ×
thickness), which needs the emitter-only segment to pass the slab (otherwise MIS's BSDF
share is lost); and a slab of a translucent lobe alone (a `materialx` document whose
material node takes a surface and a volume shader), of absorbing interior, lit from below
by a directional light, seen from above with `maxBounces` 1, renders at 2 E3(absorption ×
thickness) of the same slab without absorption, per channel within 1%: the paths enter the
medium by a translucent sample (Typhoon's don't, which leaves the ratio 1) and
reach the light by next-event estimation at the bottom face out of the medium (Typhoon's
shadow rays keep it to the light and lose everything); the top face adds nothing, as the
slab's surface lets no straight shadow ray through (its `ND_surface` graph leaves the
closure's transmission 0). Translucent samples leaving and entering media: a slab of
smooth glass mixed half and half with that lobe (absorption 1, 8 seeds × 64 samples,
`maxBounces` 64), with and without its medium, caustics on and off, renders under the
ambient light as under a black enclosure emitting the same radiance inwards, which only
BSDF sampling finds (each within 4 standard errors and 0.3%; with absorption and caustics
on −0.015%, 0.3 standard errors, and with Typhoon's exit rule +9%, 125 standard errors;
with caustics off and Typhoon's caustics policy, where a translucent sample after a
diffuse-like event ended the path while next-event estimation through the lobe didn't,
+8.9% without a medium and +14.4% with it); and a slab of rough standard_surface
transmission (not coupled) lit from below by a directional light, seen from above with
`maxBounces` 0 (so every pixel is next-event estimation at the top face, through the slab
straight), renders at transmission depth D1 as at D2 times c^((h / cos θ)(1 / D1 − 1 /
D2)), every pixel exactly (Typhoon's shadow rays start out of the medium and don't depend
on the depth), but seen from a camera inside a box of it, the box's inner faces lit
through it, every pixel bitwise the same at both depths (a vertex hit from inside with the
path in no medium starts no shadow ray in it).

**Typhoon comparison:** Typhoon's media scenes (a rough OpenPBR glass cube of scattering
interior under a dome and two sphere lights, and the same cube in standard_surface
transmission, absorbing OpenPBR glass and a volume-only MaterialX material) rendered
within 0.02% and 0.9 standard errors of halcyon's at the same seeds, and within 0.32% (2.3
standard errors) once halcyon's deliberate differences from Typhoon above landed. The
script is gone; the full record is in `docs/typhoon-verification.md` ("The media,
subsurface and dispersion comparisons").

### Subsurface scattering

Closures with a subsurface lobe (OpenPBR's and standard_surface's `subsurface`, through
the `openPBR` and `materialx` subtypes) scatter below the surface by Typhoon's random
walk (`TY/renderer/integrator/sss.cpp`, Cycles' method):
`traceSubsurface()` in `PathTracer.ispc` (Typhoon's `_TraceSubsurface()`) and the walk in
`renderer/pathtracer/Subsurface.ispc` (`ty::RandomWalkSSS()`), compiled with
`--opt=disable-fma --math-lib=system` so that it rounds as the C++ does.

- **Entry:** where the vertex's BSDF sample is the subsurface lobe's (and `bounce` is below
  `maxBounces`), the path takes the sample's weight and enters along the material's entry
  direction (`PathMaterial_subsurface()`: the sample's, else Typhoon's
  `SampleSubsurfaceEntry()` from the vertex's `SssEntryDirection` domain; it must point
  below the lobe's and the geometric normal, else the path ends). The entry vertex adds no
  emission or direct light (the subsurface lobe evaluates to 0), as in Typhoon.
  That holds for the
  other lobes of a closure that mixes or layers subsurface with them too, so their
  emission and next-event estimation count only on the vertices that don't choose
  subsurface: an
  OpenPBR sphere of white base and subsurface colour, `subsurface_weight` 0.5, renders 1
  in an enclosure emitting 1 (BSDF sampling only) but 0.90 under the ambient light of
  radiance 1. Kept as Typhoon's (maintainer decision, keep it; to report
  upstream).
- **Walk:** through a homogeneous medium from the closure's `subsurfaceColor`,
  `subsurfaceRadius` × `subsurfaceRadiusScale` (world units) and `subsurfaceAnisotropy`
  (or its precomputed coefficients), by Chiang's remap (the colour is about the diffuse
  reflectance of a semi-infinite slab), with spectral channel MIS, Henyey-Greenstein and
  forward and backward Dwivedi guided directions, the similarity relation after 9 steps,
  at most 256 steps, from the vertex's `SssEntry` domain (`Chain(SssBounce, step)` and its
  forks, as Typhoon's). The walk sees only the surface it entered (Typhoon traces the
  owner's prototype scene, which holds that mesh alone): halcyon traces the world's scene
  in world space, one lane at a time with `rtcIntersect1()` (as Typhoon), with an Embree
  argument filter that drops every hit but the owner's (instance, transform and surface:
  Embree's `instID`, `instPrimID` and `geomID`), so other surfaces of any subtype, in its
  group or other instances, other transforms of its own instance array, a surface
  coplanar with a face (a floor under a box), volumes and lights don't stop or attenuate
  it. Single rays because Embree's packet kernels for triangles and quads skip argument
  filters unless the scene is built with `RTC_SCENE_FLAG_FILTER_FUNCTION_IN_ARGUMENTS`,
  which would cost every ray of both renderers; other rays don't pay for it. Tracing the
  whole world with the owner filter, rather than a scene per owner, is a maintainer
  decision (keep it).
- **Cost:** each walk step traces the whole world, where Typhoon's traces the owner's own
  BVH, and runs the filter on every other surface the ray meets. Measured when walks
  landed (`subsurface --bench`: an SSS box of mean free path 0.4 of its width, 512 × 512,
  4 samples, 48 threads): 25.2 ms alone, 26.3 ms with a 4096-triangle mesh hidden
  inside it (1.04×), 27.6 ms with a 262144-triangle one (1.10×). A walk that finds no
  exit (an open mesh, NaN inputs) runs to the 256-step cap, each step a trace. Scenes
  without subsurface scattering cost the same as before it (medians of 3 runs, before
  at 74e5558, after at 7f39bd2: `native_materials --bench` matte 191.1 → 191.6 ms native
  and 403.7 → 404.5 ms through its graphs, physicallyBased 162.9 → 162.8 and 616.8 →
  617.6 ms; `pathtracer_volumes --bench` about 20.5 ms both).
- **Exit:** where the walk reaches the surface, the path takes the walk's weight and goes
  on from a hit there (the exit point, its geometric normal faced along the walk's last
  direction, the walk's primitive and hit coordinates; Typhoon's `_PopulateSssExitRayHit()`)
  shaded by a white Lambertian in place of the material (`PATH_SHADE_SUBSURFACE_EXIT`, C++'s
  `subsurfaceExitClosure()`), so the colour isn't counted twice. No light or volume lies
  on the way to it. The path has no differential after it, counts as after a diffuse
  event, and its MIS pdf is the exit's.
- **Bounces:** entry, walk and exit are one bounce, the exit's: the entry doesn't count,
  so with `maxBounces` 0 no walk is taken, and the exit is a `pathEvent` of its own.
- **Ray offsets:** the walk's rays start at `epsilonFrom()` of their origin (the first at
  the entry hit's), where Typhoon's start at a fixed 1e-4, which also bounds the distance
  to the opposite interface Dwivedi's backward guiding needs. This is a deliberate
  difference from Typhoon, like the path's own offsets (see [The quality
  renderer](#the-quality-renderer)); maintainer decision, keep it (listed in [Typhoon
  differences](#deliberate-differences-from-typhoon)):
  Typhoon's walks pass surfaces nearer than 1e-4 to a collision, which loses or gains a
  little where the mean free path is short (about 0.3% of the light of a channel of mean
  free path 0.03 in the comparison below), and fails in scenes smaller than about 1e-3.
- Typhoon's walk traces in the owner's object space; halcyon's in world space. The same
  in exact arithmetic (distances are world units either way); the hits round differently.

`test_subsurface.cpp` (ctest `subsurface`) renders openPBR materials of pure subsurface:
a colour of 0 renders black exactly, a white sphere under the ambient light renders to
its radiance (furnace, within 0.5% at three radii), a deep box with a short mean free
path reflects a little less than its colour (Chiang's fit: 0.81, 0.89 and 0.96 of 0.2,
0.5 and 0.8, as Typhoon's walk does), `maxBounces` 0 takes no walk and 1 and 2 render a
lone sphere alike (bitwise), and other surfaces change nothing (bitwise): an opaque
sphere inside a sphere and triangle and quad meshes inside a box (each in the group or
another instance), a plate under a box's top face that its first walk rays reach, a
smaller second transform of the box's own instance array, a cut-out floor in the plane of
its bottom face, a dense transferFunction1D volume inside a box (walks don't track
volumes; the same volume through the box's top face must show); an instance's scale scales the medium as building the sphere at
that size does, and a scene at 1e-3 renders as at 1.

**Typhoon comparison:** Typhoon's subsurface scenes (cubes of pure OpenPBR and
standard_surface subsurface) rendered within 0.37% and 3.8 standard errors of halcyon's
at the same seeds (blue, the shortest mean free path, from the scale-relative walk
offsets above; with Typhoon's fixed offset, within 0.5 standard errors). The script is
gone; the full record is in `docs/typhoon-verification.md` ("The media, subsurface and
dispersion comparisons").

### Dispersion and caustics

Hero-wavelength dispersion and Typhoon's caustics policy:
`renderer/pathtracer/HeroWavelength.isph` and `HeroWavelength.ispc` (Typhoon's
`heroWavelength.h` and MaterialXCpp's `spectral.h`; the `.ispc`, compiled without fused
multiply-adds, holds the two conversions) over `spectral.h`'s tables, read in place
(`SpectralTables.cpp`), and the path loop in `PathTracer.ispc`.

- **Hero wavelength:** a path is RGB until the first closure that disperses (Typhoon's
  `SurfaceClosure::HasDispersion()`, of the BSDF that samples, after caustic pruning:
  a dielectric of Abbe number above 0, so OpenPBR's `transmission_dispersion_scale` above 0
  with an Abbe number above 0 and standard_surface's `transmission_dispersion`; also a
  dielectric interface with a thin film, so OpenPBR's `thin_film_*` and physicallyBased's
  iridescence, which keeps that material a C++ kind). Its shading call samples one
  wavelength uniformly over 380–780 nm from the vertex's `Wavelength` domain (C++,
  `PATH_MATERIAL_HERO_STARTED`), and from there the path's throughput is one spectral value
  at it (`PathThroughput`), at which the material interface evaluates every later closure
  (`PathShadeInput::heroWavelengthNm`: IORs follow Cauchy's law there). Native kinds never
  disperse (ANARI's physicallyBased has no dispersion parameter).
- **Weights**, as Typhoon's: RGB weights the path takes after (BSDF samples, interior
  media's segment weights, subsurface walks' entry and exit) multiply its spectral value
  by their upsampling to the wavelength (`HeroWavelength_rgbToSpectral()`: `spectral.h`'s
  sRGB basis at the nearest 5 nm); radiance it finds (emission, lights it hits, the
  environment) is the spectral throughput times the radiance's spectral value, brought back
  to RGB (`HeroWavelength_spectralToRgb()`: the CIE 1931 response under D65, over the
  wavelength's pdf, balanced so that a white spectrum averages to white); each
  next-event estimation sample multiplies the spectral values of the light's radiance, the
  BSDF (or phase function) and the visibility, is brought back to RGB and clamped, and the
  vertex's sum is multiplied by the spectral throughput; Russian roulette's survival is the
  largest component of the throughput as RGB. halcyon's own transferFunction1D volumes
  weigh it the same way (their colour and transmittances; halcyon's extension of Typhoon's
  media), and the albedo channel takes the throughput as RGB. Only the linear Rec. 709
  working space is ported (halcyon's only one; Typhoon's ACEScg branch upsamples through
  Jakob-Hanika tables).
- **RGB paths are unchanged**, up to rounding: a path that meets no dispersive closure
  computes what it did before dispersion, but the restructured loop compiles to other fused
  multiply-adds, so a few values differ in the last bits (the media comparison's scenes at
  8 samples against aeca598: 57 to 201 of 259200 values, by at most 1.4e-6, 3.5e-7
  relative; the scene without glass bitwise).
- **Caustics:** `enableCaustics` (default false, Typhoon's) off: after a diffuse-like
  event the shading call prunes caustic-class lobes (Typhoon's `PruneCausticClassLobes()`:
  transmission and delta lobes, and effectively smooth ones; native kinds prune as their
  graphs, see [Materials in the pathtracer](#materials-in-the-pathtracer)), a specular or
  boundary-crossing sample after one ends the path, and shadow rays pass thick
  transmissive surfaces straight through (see [The quality
  renderer](#the-quality-renderer)). A translucent sample (a diffuse-like transmission,
  eta 1) crosses without being a caustic one, whether caustics are on or off: pruning
  keeps its lobe and next-event estimation goes through it, where Typhoon ends the path on
  every crossing after a diffuse-like event (`pathIntegrator.cpp:627`), so a translucent
  slab under the ambient light rendered 8.9% brighter than in an enclosure emitting the
  same radiance (a deliberate difference from Typhoon, maintainer decision, keep it;
  listed in [Typhoon differences](#deliberate-differences-from-typhoon), to report
  upstream). On: nothing is
  pruned, such a sample is a caustic one and the path goes on, shadow rays let 1 − opacity
  through thick surfaces (the medium's owner max(1 − opacity, transmission)), and from its
  first caustic sample every contribution to the path is clamped by luminance to
  `causticsClampThreshold` (5; 0 or less doesn't clamp) before the firefly clamp
  (Typhoon's `currentPathIsCaustic`).
- **The hdri's MIS weights with pruning:** next-event estimation folds an hdri's (and the
  ambient light's) samples into the reflection hemisphere by the pruned BSDF's flag
  (`PATH_MATERIAL_BSDF_REFLECTION_ONLY`), and a missed BSDF ray's pdf by the same flag, so
  the two MIS weights sum to 1. Typhoon folds the miss's by the closure's
  (`pathIntegrator.cpp:694`; `PATH_MATERIAL_REFLECTION_ONLY`), so where pruning removes a
  transmission lobe the light's folded pdf is twice the miss's, and their weights sum to
  more than 1 (for a Lambertian lobe under a uniform dome, up to 4/3, where its pdf is the
  geometric mean of the two): its images are a little too bright there. One flag for both
  is a deliberate difference from Typhoon (maintainer decision, keep it; listed in
  [Typhoon differences](#deliberate-differences-from-typhoon), to report upstream).
  Typhoon's rule, measured (a matte floor of albedo 0.5 under the
  ambient light, beside a sphere of OpenPBR, base colour 0.8, specular roughness 0.5,
  transmission weight 0.5, caustics off, 16 seeds of 64 samples, 64 × 64): the floor 0.36%
  brighter than with one flag (thick sphere; 0.385% thin-walled, 0.09% at transmission
  weight 0.9), 28–57 standard errors, and the sphere 0.08–0.11% (within 2 standard
  errors). With `enableCaustics` nothing is pruned, the flags agree, and the two rules
  render bitwise alike.
- **Cost:** none measurable for scenes without dispersion or caustics but about 1% for
  subsurface scattering: interleaved medians of 5 runs against aeca598 (48 threads),
  `native_materials --bench` matte 197.8 → 196.9 ms native and 418.5 → 414.4 ms through
  its graphs, physicallyBased 166.1 → 167.2 and 628.8 → 632.2 ms (all within the runs'
  ±2% spread); `pathtracer_volumes --bench` 20.4 → 20.8 ms, then 20.5 → 20.3 ms in 6 more
  runs (noise); `subsurface --bench` 25.8 → 26.2 ms, then 25.15 → 25.45 ms in 6 more runs
  (+1.2%, the runs' ranges apart); two 64-sample renders of the caustics comparison's
  glass scene 1.04 → 1.05 s (within noise).

`test_spectral_caustics.cpp` (ctest `spectral_caustics`) checks: a white emissive stripe
seen through a prism of dispersive OpenPBR glass (Abbe number 10) shows blue above red
(7 pixels apart at 64 × 64, the test asks for 2; green between them, a non-dispersive
prism's band between both, every pixel of which is grey exactly, and the dispersive band's
light per channel within 5% of it); a closure of Abbe number 0 renders bitwise as one
without dispersion parameters; a white dispersive glass sphere under the ambient light renders to its
radiance in every channel (furnace, 1.5%); an emitter seen through dispersive glass of a
grey absorbing interior as through clear glass (3%: blue loses a little more at the faces);
a floor beside a mirror under a sphere light gets the light's irradiance with caustics off
and the mirror image's too with them on (both analytic, 2% and 4%), and with a light
bright enough the mirror's share is clamped to `causticsClampThreshold` (a quarter of it),
or not with a threshold of 0; a floor under a thick glass slab is lit through it (0.75
to 0.95 of beside it) with caustics off and black under a directional light with them on,
and with them on under a sphere light lit by refraction through it (over a quarter of
beside it);
a scene without caustic-class lobes renders bitwise alike with caustics on and off; and
MIS under pruning: the measurement scene above, with the sphere's
transmission black (so that shadow rays don't pass it straight through), renders its
floor around the sphere under the ambient light as under a black, non-metallic enclosure
emitting the same radiance inwards, which only BSDF sampling finds and so is unbiased
whatever the flags (8 seeds of 64 samples each: −0.003%, 0.2 standard errors; with
Typhoon's rule +0.25%, 13.9).

**Typhoon comparison:** Typhoon's dispersion and caustics scenes (cut glass in dispersive
OpenPBR and standard_surface glass under a dome, with and without caustics, and its
caustics-off policy scene) rendered within 0.51% and 1.9 standard errors of halcyon's at
the same seeds, but for a rough glass sphere, 1.3–1.5% darker in halcyon at 16 bounces:
Typhoon's emitter-only segment passes the sphere's wall (see [The quality
renderer](#the-quality-renderer), `maxBounces`), so its image is brighter than its own
converged one; at 64 bounces both agree within 0.33%. The script is gone; the full
record is in `docs/typhoon-verification.md` ("The media, subsurface and dispersion
comparisons").

### Shading context

Every path vertex builds Typhoon's `mxcpp::ShadingContext` (its `_BuildShadingContext()`) in
ISPC: `ShadingContext_build()` in `renderer/pathtracer/ShadingContext.isph` fills
`PathShadeInput`, which carries it across the material interface (`PathShadeInSoA`; the C++
side copies it into an `mxcpp::ShadingContext`, `shadingContext()` in `PathMaterial.cpp`).
Only the parts the material's graph can read cross. The display colour, the primitive,
the surface derivatives, the screen-space derivatives and the instance transforms
(`PathContextGroup`) are sent only to materials whose `contextMask` lists them. That mask
comes from `contextGroupsRead()` in `ContextGroups.*`: a table of the node types that read
each part, audited against the vendored sources (ADR 0004 and its amendment). A node type
that reads one of these parts must be listed there, including any node halcyon registers
itself. One more bit of the mask, `PATH_CONTEXT_OBJECT_POSITION`, is not a part sent but
how one is made (below); it comes from Typhoon's own flag on the compiled graph
(`EvalGraph::RequiresObjectSpacePosition()`), not from the table.

- **Texture coordinates** (Typhoon's `st`) are the `attribute0` attribute's x and y
  (`PATH_TEXCOORD_ATTRIBUTE`, like the samplers' `inAttribute` default; no material can
  name another yet, though the derivative hooks take the attribute), resolved like any
  attribute (instance, face-varying, vertex, primitive, uniform; (0, 0) where nothing sets
  it). **Display colour and opacity** (Typhoon's `displayColor`/`displayOpacity`, read by
  MaterialX's geomcolor nodes) are the `color` attribute's rgb and alpha, resolved the same
  way (`getSuppliedAttribute()`), Typhoon's grey 0.5 and 1 where nothing sets it.
  `PathShadeInput::baseColor` is something else: the colour native kinds reflect (native
  matte's colour and native physicallyBased's base colour at the hit,
  `Material_getColor()`; the native Lambertian's, the invalid material colour of surfaces
  without a closure). Native physicallyBased's other parameters at the hit are in
  `PathShadeInput::pbm` (`nativeInputs()`, `materialParameterAt()`).
- **Primvars** (Typhoon's geomprops, `ND_geompropvalue_*` nodes) are halcyon attributes by
  name: `ShadingContext_build()` resolves the attributes in the hit material's
  `attributeMask` like any attribute (`getPresentAttribute()`: instance, face-varying,
  vertex, primitive, uniform, ray-derived) into `PathShadeInput::attributes`, with whether
  the hit has each (`attributePresent`: ray-derived ones always, the others where the
  instance or the geometry sets them, as `getSuppliedAttribute()` decides), and the C++
  side's `geomPropLookup` hands a node the one its handle names where the hit has it, and
  nothing where it doesn't, so the node gives its `default`, as MaterialX and Typhoon give
  for a primvar the mesh lacks (maintainer decision). The geomprop
  nodes halcyon builds itself (attribute parameters, samplers' `inAttribute`, see
  [Materials in the pathtracer](#materials-in-the-pathtracer)) have the default (0, 0, 0,
  1), what every renderer reads for a missing attribute, so they still match the `preview`
  renderer and the native kinds. Uniform (string) geomprops have nothing to read: no
  halcyon attribute is a string. halcyon's attributes are float4, which the lookup
  converts to the node's type (x; xy; xyz; xyzw; int(x); x ≠ 0), where Typhoon hands a
  node the primvar's value in its authored type (`SamplePrimvar()`, `primvarSampling.h`)
  and the node gives its default where that isn't the node's type (its `ValueHolds()`
  check fails; a float2 primvar read by a vector3 node, say): a difference from Typhoon
  (listed in [Typhoon
  differences](#deliberate-differences-from-typhoon)). Cost: none measurable, against
  1cc733d (48 threads, two sets of 8 interleaved runs, medians): `native_materials
  --bench` matte 203.8 → 204.2 and 209.2 → 208.5 ms native, 420.6 → 427.2 and 440.5 →
  442.6 ms through its graphs, physicallyBased 173.7 → 172.1 and 174.7 → 173.6 ms native,
  645.8 → 648.7 and 670.3 → 664.6 ms through its graphs; `path_material --bench` within
  ±1% for C++ materials, and for native kinds (55–290 ns per point, whose runs spread by
  up to ±30%) −0.3 to +7%, within that spread.
- **The smooth normal** (`PathShadeInput::Nsrf`, Typhoon's `normalSrfWldExt`) is the
  world shading normal turned into `Ng`'s hemisphere (see "Two normals" under
  [Rendering pipeline](#rendering-pipeline)), as Typhoon's `_TryBuildSurfaceInteraction()`
  turns it. It and `Ng` are taken to world space as `_TryBuildSurfaceInteraction()` does
  (`TransformNormalToWorld()`, then `TryNormalizeDirection()` again:
  `Transport_surfaceNormalToWorld()` in `renderer/pathtracer/Transport.isph`; in double,
  each component rounded to the nearest float), not by a float `normalize()` (the native
  shadow call's `Ng` too). Typhoon's GGX lobes take the smooth
  normal's length as 1, and just above the delta threshold (alpha 1e-3) their albedo
  depends on its last bits: at roughness 0.04 a length of 1 + 1.2e-7 (one ulp) reflects
  9% more, 1 − 6e-8 (one ulp) 4.4% less (`GGX_D()`'s 1 − NdotH² cancels). Correctly
  rounded components err to both sides, so over a curved surface the errors cancel, as in
  Typhoon; a float `normalize()` erred to one side on average, which side depending on the
  mesh and its transform (short on microfacet_ms_smoke's `uni` mesh, whose roughness-0.05
  metals rendered 0.36% darker than Typhoon's; long on Typhoon's 92-vertex sphere, 0.5%
  brighter); `normal_rounding` checks it. A flat surface has one normal, whose rounding
  still decides its albedo at such roughness in either renderer: Typhoon's ill-conditioning,
  recorded in `docs/typhoon-verification.md` (decision 5). The derivative hooks take the
  smooth normal back in object space as Typhoon's `TransformNormalToObject()` does
  (`Transport_normalToObject()`), so dndu/dndv are the turned normal's (their
  `normalizeNormalDerivatives()` faces them like it, as Typhoon's
  `ComputeTriangleSurfaceDerivatives()` does), and the tangent frame, the screen-space
  derivatives, the normal channel and the materials' normal resolution all start from it.
  Cost (48 threads, 5 interleaved runs, medians; single runs spread by ±2–4%):
  `native_materials --bench`, from a float `normalize()` of both normals, matte 211.6 →
  223.0 ms native and 445.0 → 462.6 ms through its graphs, physicallyBased 177.0 → 185.8
  and 671.5 → 687.3 ms (+5%, +4%, +5%, +2%); of that, the smooth normal alone costs
  212.7, 450.0, 180.9 and 683.6 ms, and `Ng`'s port the rest (most of the native kinds'
  share: it also runs at every native shadow blocker).
  The shadow terminator lift turns each corner normal to `Ng`'s side itself (Typhoon's
  `ComputeSmoothTriangleShadowOffsetFromContext()`), so it is unchanged. A mesh wound
  against its normals therefore shades as the mesh with its normals negated, bitwise
  (`normal_winding`; without authored tangents, and with them where their handedness w
  is negated too, since their bitangents follow the authored normal). A known
  consequence, as in Typhoon (Typhoon's rule, maintainer decision, keep it): on a mesh
  wound against its normals a normal map's relief comes out inverted (bumps become
  dents), as the tangent frame around the turned normal keeps a bitangent that follows
  dPdv (smoothed frames) or the authored normal (authored tangents), so the map's
  tangent-space z points the other way; with the normals negated to match the winding,
  the same map shows the relief as authored.
- **Surface derivatives**: each geometry subtype's `getDerivatives` hook
  (`GeometryDerivatives.ispc`, which `Geometry.ispc`'s setters install; built with
  `--opt=disable-fma`, so that it rounds like Typhoon's C++, which x86-64 builds without
  fused multiply-adds: with them a degenerate triangle's edges cancel to a residue, not 0)
  gives object-space dP/du, dP/dv and the shading normal's dn/du, dn/dv, from the vertex
  positions `Geometry_setVertices()` hands it. Triangles follow Typhoon's mesh frame (Embree's
  edges, taken to the texture coordinates' (s, t) where those vary over the triangle; normal
  derivatives from the corner normals) and curves its curve frame (the centerline derivative
  projected off the normal, dPdv the unit N x dPdu, no normal derivatives; for cubic bases
  the basis's derivative, as Embree's `rtcInterpolate()` gives Typhoon, taken relative to
  the second control point so that a segment of one repeated point has none and gets the
  orthonormal frame, where Typhoon's frame follows Embree's rounding). The rest are
  halcyon's own: quads the bilinear patch the attributes interpolate on (also taken to
  (s, t)), spheres longitude u = phi / 2pi around +z and latitude v from -z to +z (the
  orthonormal frame at the poles), cylinders and cones the curve frame along their axis.
  Texture coordinates an instance sets are constant, so they don't change the derivatives.
  Degenerate frames become the orthonormal frame around the normal (OpenUSD's
  `GfBuildOrthonormalFrame()`, ported in `shading/halcyon_derivatives.isph`).
- The **tangent frame** (`tangentFrame()`) is Typhoon's. On triangles and quads it starts
  from the geometry's frame per corner (see [Array
  data](#array-data-copies-and-shared-memory)), interpolated like face-varying values and
  transformed like directions: the tangent projected off the smooth normal, the bitangent
  N x tangent signed by the handedness of the interpolated tangent and bitangent.
  Elsewhere, or where those are degenerate, dPdu projected off the smooth normal, signed
  by dPdu and dPdv's handedness. Without authored tangents the corner frames are the ones
  Typhoon smooths across a mesh's faces when it loads it (`smoothTangentFrames()` in
  `geometry/MeshTangents.cpp`, a port of
  `HdEmbreeMesh::_UpdateTangentFrameCache()`): each corner's dPdu (the derivative hook's
  at the corner, `Geometry_getCornerDerivatives()`) projected off its normal, accumulated
  over the corners that share its vertex and exact normal, so normal splits keep their
  seams, and orthonormalized again. ANARI's tangent is the authored one (Typhoon's needs a
  `tangent` and a `bitangent` primvar; ANARI has only the tangent and its handedness).
  Quads are halcyon's own (Typhoon triangulates them): each corner takes the bilinear
  patch's dPdu at it, and its face normal from the corner and its two neighbours. The
  corner frames follow the geometry's own `attribute0`, also where an instance sets the
  texture coordinates (which then don't change dPdu). Curves, spheres, cylinders and cones
  have no corner frames, as in Typhoon. A mesh without `vertex.normal` or
  `faceVarying.normal` groups its corners by their face normals, so its tangents stay per
  face; Typhoon first computes smooth normals for such a mesh (`Hd_SmoothNormals`) and
  smooths its tangents with them. That follows from halcyon shading with Ng where a mesh
  has no normals (as helide), not from the tangent port. The pathtracer shades such meshes
  with Ng too, so they look faceted where Typhoon's look smooth: a deliberate difference
  from Typhoon (maintainer decision, keep it; listed in [Typhoon
  differences](#deliberate-differences-from-typhoon)).
- **Ray differentials** (`RayDifferential`): the camera ray's are the rays through the
  same point of the next pixel over in x and in y, brought towards it by 1 / sqrt of the
  renderer's sample limit (`differentialScale()`, Typhoon's 1 /
  sqrt(`samplesToConvergence`); 1 / 16 by default), the footprint of one of that many
  samples: the sample limit, or `convergedSamplesPerPixel` where there is none (see
  [Adaptive sampling](#adaptive-sampling)). The path keeps them through specular events
  (`RayDifferential_propagate()`, Typhoon's `_PropagateRayDifferential()`, with the normal
  derivatives only while the material shades with the smooth normal) and drops them at any
  other event. **Screen-space derivatives** (dPdx, dPdy and the parameters' du/dx...) come
  from where the differential rays meet the tangent plane, else (after a diffuse event)
  from the camera's pixel footprint at the hit's distance, scaled the same way. Typhoon's
  fallback multiplies its near-plane pixel size by the distance, which is the footprint
  only for a near plane at 1; halcyon uses the footprint. This is a deliberate difference
  from Typhoon (maintainer decision, keep it): as image files are filtered by their
  footprint (halcyon samplers have no mip levels and ignore it),
  file-textured images after a diffuse bounce differ from Typhoon's for orthographic
  cameras and for near planes other than 1 (USD's default is 1).
- **Colour**: the context's `colorTransform` callback and `luminanceCoefficients` come from
  `renderer/pathtracer/ColorManagement.*`, a port of Typhoon's `colorManagement.cpp` (and the
  parts of OpenUSD's `GfColorSpace`/nanocolor it calls) for its default working space,
  linear Rec. 709 (`lin_rec709_scene`; bitwise OpenUSD's). The
  callback converts OpenUSD colour space names into the working space and declines others;
  MaterialXCpp's colortransform nodes pass MaterialX names (`g22_rec709` to `lin_rec709`),
  so they keep their built-in conversions, as in Typhoon, which installs no callback.
  `convertToWorkingSpace()` is there for authored colour spaces and image
  files; halcyon samplers' values aren't converted.
- **Textures**: the context's `textureSystem` is the C++ material's `HalcyonTextureSystem`
  (see [Materials in the pathtracer](#materials-in-the-pathtracer)).
- **Object position**: the hit taken to object space (the
  instance's inverse transform), except for graphs that read the object position, which
  get the surface's own from the primitive's vertices (Typhoon's
  `ResolveObjectSpaceSurfacePosition()`, `surfaceDerivatives.cpp`), so that a position on a
  plane of the mesh lies on it exactly, not a few ulps off (cellnoise of the position on a
  plane at a cell boundary takes one cell, as in Typhoon). Which graphs: Typhoon's rule,
  an `ND_position_vector3` the surface reaches whose `space` isn't world, including the
  ones `EvalGraph::Compile()` adds for unconnected position inputs (noise, cellnoise,
  triplanar projection, ...); the material's `contextMask` then has
  `PATH_CONTEXT_OBJECT_POSITION` (`CppMaterial::compile()`; `Material::finalize()` copies
  it into the ISPC material, below), and `ShadingContext_build()` calls the geometry's
  `getSurfacePosition` hook after the surface derivatives (which take the hit's, as
  Typhoon's). Triangles interpolate their corners at Embree's barycentrics
  exactly as Typhoon's `InterpolateTrianglePosition()` (`GeometryDerivatives.ispc`,
  bitwise); quads, halcyon's own (Typhoon triangulates them), take
  Embree's own interpolation of its quads (`rtcInterpolate()`: barycentric over the
  triangle hit, (v0, v1, v3) or (v2, v3, v1)), the surface Embree intersects, not the
  bilinear patch the attributes interpolate on, which leaves the surface where a quad isn't
  a parallelogram; a result that isn't finite keeps the hit's, and indices are clamped as
  for the other hooks. Typhoon instead falls back to Embree's interpolation of the
  triangle (`rtcInterpolate1()`), then the hit's where that isn't finite either; the same
  where it matters (Embree's interpolation of corners that aren't finite isn't finite
  either). Curves keep the hit's,
  as in Typhoon (interpolating their vertices gives the centerline); spheres, cylinders and
  cones too (halcyon's own: their vertices are centers and axes). The world position (`P`,
  the differentials' origin) stays the hit's, as in Typhoon. The `position` debug mode
  shows the context's, so the re-interpolated position for such a graph; the
  `objectPosition` ray-derived attribute (the `preview` renderer, and geomprops naming it)
  stays the hit's in object space. The quads' surface, the hit for spheres, cylinders and
  cones, clamped indices, the debug mode's value and the attribute's are maintainer
  decisions (keep them). Native kinds never read it. Which graphs read it is
  the high bit of `Material::attributeMask` (`MATERIAL_READS_OBJECT_POSITION`), which the
  shading context gathers anyway, so graphs that don't read it pay no gather of their own,
  only a test, and the hook's pointer is loaded only for lanes that need it. Cost,
  measured against 0eb59a0 (10 interleaved rounds, the order rotated,
  medians of each variant and of the per-round ratios; the machine shared, load 5–32):
  variants 0eb59a0, this change, and this change with the hook call compiled out (a
  control for code layout). A 1024 × 1024 render (8 samples, `maxBounces` 4) of a
  64k-triangle sphere in a `materialx` standard_surface, a constant graph: 57.5 → 57.2 ms
  on 48 threads (ratio 1.004; control 1.012) and 285.2 → 285.1 ms on 8 (1.000; control
  1.008); the same graph taking cellnoise of the position: 72.5 → 72.5 ms (0.998) and
  364.3 → 365.5 ms (1.003). `native_materials --bench` (48 threads): matte 201.4 → 200.7
  ms native (1.003) and 425.4 → 422.8 ms through its graphs (0.989), physicallyBased
  169.9 → 170.4 (0.999) and 645.9 → 641.4 ms (0.993). Every ratio within ±1.2% and none
  systematically above 1: no measurable cost, for graphs that read the position either.
  (A first version gathered `Material::contextMask` per vertex in a block of its own: about
  +1% there, on the constant graph at 8 threads, 1.016–1.034 over 20 rounds.)
- Not in the context: frame/time.

`test_shading_context.cpp` (ctest `shading_context`) renders the debug modes for every
subtype (and both cubic curve bases) on simple shapes and checks them against their
analytic values, that
`convergedSamplesPerPixel` scales the differentials, and that a `materialx` graph reading
the object position (cellnoise of it, as emission) sees y exactly 0 on a plane y = 0 of
triangles and of a quad that isn't a parallelogram, through a turned instance, where a
matte shows the hit a few ulps off it, so the cellnoise takes one cell over the plane, and
that a sphere keeps the hit's position;
`test_mesh_tangents.cpp` (`mesh_tangents`) the smoothed tangents on a coarse sphere of
triangles and of quads against the sphere's own frame, and the authored ones.

### Materials in the pathtracer

Every material (matte, physicallyBased, openPBR, usdPreviewSurface, materialx) is a
MaterialXCpp graph, built from its parameters
by `renderer/pathtracer/MaterialConverter.*` (halcyon's counterpart of Typhoon's
`mxcppAdapter.cpp`; materialx's from its document, `MaterialXDocument.*`) and compiled in `Material::finalize()` into a `CppMaterial`, which
Typhoon's C++ shades (`MATERIAL_KIND_CPP`), except matte and most physicallyBased
materials, which ISPC shades natively from the same parameters (`MATERIAL_KIND_MATTE`,
`MATERIAL_KIND_PHYSICALLY_BASED`, below). A graph that doesn't compile warns, and the
pathtracer lights the surface as one without a material.

- **matte**: MaterialX's Lambertian, `ND_oren_nayar_diffuse_bsdf` of roughness 0 under
  `ND_surface` (Typhoon has no matte; this is what an equivalent USD MaterialX material
  gives it), whose `opacity` (the closure's presence) is the material's after `alphaMode`.
  Its `color` default is the specification's 0.8 grey, as in the `preview` renderer
  (helide's is white; see
  [Deliberate differences](#deliberate-differences-from-helide)).
  **Native matte** (`renderer/pathtracer/PathMatte.isph`): ISPC
  computes what the graph's closure gives the integrator, from the colour and opacity the
  `preview` renderer reads (`Material_getColor()`, `Surface_getCompositeOpacity()`, which
  `makeShadeInput()` and the shadow call put in `PathShadeInput::baseColor`/`baseOpacity`):
  presence (`Presence` domain, as the graph's), the shading normal, the flags (reflection-
  only, kept by caustic pruning), Typhoon's `SampleLambertian()` in its
  `Frame::FromNormal()` from the `BsdfSample` domain's numbers, the leaf's evaluation and
  validity (Oren-Nayar's factor is 1 at roughness 0; above the leaf's and the geometric
  normal; bump shadowing between the leaf's normal, which `ValidateLeafNormals()`
  renormalizes in double, and the smooth one), eval, the shadow call's facts (opacity 1,
  presence the opacity, no transmission), the albedo channel's summary (the colour clamped
  to [0, 1]) and no emission. A matte whose `color` is a FLOAT32 or whose `opacity` is a
  FLOAT32_VEC3/VEC4 (types the specification doesn't allow, which the graph reads and the
  `preview` renderer's readers ignore) stays `MATERIAL_KIND_CPP`. The material still
  compiles its graph, which the renderer's
  `nativeMaterials` false (default true; declared in `HalcyonDefinitions.json`) shades
  instead, on the same sample sequences: images agree to rounding except where a path's
  direction, a few ulps apart (ISPC's `sin`/`cos` and fused multiply-adds), falls on the
  other side of an edge. `path_material` compares the two point by point,
  `native_materials` image by image (colour, albedo, normal) and times them (`--bench`).
  Speedup measured when it landed (48 threads, AVX-512 gang of 16): a shading call, one eval
  and the subsurface and medium calls per point fall from about 500 ns through the graph to
  about 42 ns natively (`path_material --bench`, about 12×); a 512 × 512 render of
  `native_materials`' matte box (every surface native matte but one physicallyBased sphere
  with a thin film and one graph-only matte sphere, 4 samples, `maxBounces` 16,
  accumulating) from 277 ms to 145 ms (1.9×, also on one thread). A
  change to the matte graph (`matteGraph()`) or to what Typhoon's Oren-Nayar leaf, ND_surface
  or the normal pipeline do needs the same change in `PathMatte.isph`; the two tests catch
  a difference.
- **physicallyBased**: one `ND_gltf_pbr_surfaceshader`, every KHR parameter set on its glTF
  input (`baseColor` → `base_color`, `clearcoat` → `clearcoat`, `sheenColor` →
  `sheen_color`, ...), so ANARI's defaults hold where glTF's differ (`specular` 0, as
  VisRTX reads it; glTF's is 1). `alphaMode`/`alphaCutoff` become `alpha_mode`/
  `alpha_cutoff`. What glTF PBR lacks or Typhoon's evaluation of it ignores: `occlusion`
  (no input), `attenuationDistance`/`attenuationColor` (set, but Typhoon doesn't read
  them), and nothing gives the node an anisotropy, dispersion or emissive strength (ANARI
  has none).
  **Native physicallyBased** (`renderer/pathtracer/PathPbm.isph`, `PathPbmLobes.isph`,
  `PathPbm.ispc`) shades a material whose parameters
  `nativePhysicallyBasedCovers()` (`MaterialConverter.h`) admits, and whose `baseColor`
  and `opacity` have the types native matte requires (not a FLOAT32 `baseColor`, not a
  FLOAT32_VEC3/VEC4 `opacity`); `PhysicallyBased::nativeKind()` in `Material.cpp`
  decides at every commit; `Material::finalize()` reports the kind it chose in a DEBUG
  message ("shades this material natively (matte)", "(physicallyBased)", or "through its
  MaterialXCpp graph in C++"). **The rule:** no thin film, which needs constants:
  `iridescence` a constant at most 1e-7, or `iridescenceThickness` a constant at most
  1e-7 (a thin film stays C++). Every other parameter may be a constant, attribute or
  sampler, `normal` and `clearcoatNormal` any sampler, and `alphaMode` any; `occlusion`
  doesn't matter. Such a graph's closure (`EvalGltfPbr()`) is a clearcoat layer (where
  `clearcoat` is above 0 at the hit) over a sheen layer (where `sheenColor`'s largest
  component is) over the base, `mix(layer(dielectric reflection, mix(Lambertian,
  transmission, clamp01(transmission))), metal, clamp01(metallic))`: two isotropic
  generalized Schlick GGX lobes (colours F0 `saturate(specularColor × ((ior − 1)/(ior +
  1))²) × clamp01(specular)` and F90 `clamp01(specular)`; the metal `baseColor` and 1), a
  GGX dielectric transmission of tint `baseColor` and IOR max(`ior`, 1) (rough with VNDF
  sampling, or delta; refracting, or reflecting totally inside), Charlie sheen, and a
  dielectric GGX clearcoat of IOR 1.5, with Turquin's multiple-scattering compensation
  and the layer throughputs from Typhoon's tables (the GGX energy table
  `ggxEnergyLut.h` and BSDL's dielectric layer filter `dielectricReflFrontLut.h`, read
  in place through `PathPbmTables.cpp`), the glossy normals raised towards Ng as
  `PrepareShadingNormals()` does, delta lobes below alpha 1e-3, the normal maps as the
  graph builds them (the sampler's value encoded to [0, 1], MaterialX's
  `ND_normalmap_float` in the frame of the world normal, tangent and bitangent nodes;
  `ResolveGraphNormal()` for `normal`, which the base and sheen take;
  `ValidateLeafNormals()` for the clearcoat's own, `clearcoatNormal`'s or else the
  unperturbed normal, where either map is set; bump shadowing compares each lobe's
  normal with the smooth one), and caustic pruning (which drops the transmission and
  delta lobes, and a node that loses a child leaves the other: a mix's weighed, a
  layer's alone; `PathPbmNode`). The port runs the closure tree's choices, evaluation,
  pdfs and finalizations as `closureTraversal.cpp` does (but for its mixes' delta
  samples, which it weighs as `ClosureSampling.*` does, below), plus the flags (reflection-only
  while opaque and without transmission, thin-walled at `thickness` 0 or less,
  specular-only albedo), the refraction's eta, emission and the shadow call's facts
  (opacity and presence the material's opacity, the transmission, its colour and its
  straight fraction, `StraightShadowDielectricTransmission()`'s Schlick; the shadow call
  resolves the base colour and the parameters of native physicallyBased blockers, as
  shading does, without the normal maps). The parameters are resolved at the hit as the
  graph's inputs (`PhysicallyBasedParameters`, `nativeInputs()` and
  `materialParameterAt()` in `PathTracer.ispc`: a valid sampler's value, else the
  attribute's, else the constant; scalars from x). `PathPbm.ispc` is compiled with
  `--opt=disable-fma --math-lib=system`, so it rounds as the C++ does (x86-64 builds have
  no FMA; the system's `cosf()`, `sinf()`, `powf()`): native and graph results are the
  same bits, and `nativeMaterials` false renders the same image. That holds only while
  the C++ isn't contracted to FMAs or built with fast math; elsewhere both are still
  correct, and the tests compare within rounding (see [Tests](#tests)). The BSDL filter assumes the default dielectric layer throughput
  mode (`Bsdf::SetDielectricLayerThroughputMode()`, process-wide): the mode stays fixed at
  Typhoon's default, `Bsdl`, and is never exposed as a parameter (maintainer decision,
  keep it), so nothing in halcyon may call the setter. Speedup (48 threads, AVX-512 gang of 16): a
  vertex 1680 → 148 ns per point (ANARI's default rough metal), 2100 → 170 ns (a half
  metal), 2980 → 181 ns (a sheen) and 4340 → 225 ns (a clearcoat and normal maps),
  11–19× (`path_material --bench`); a 512 × 512 render of `native_materials`'
  physicallyBased box (every surface native but an iridescent sphere, 4 samples,
  `maxBounces` 16, accumulating) 450 → 120 ms (3.75×). A change to
  `physicallyBasedGraph()`, `EvalGltfPbr()` or the lobes it builds needs the same change
  in `PathPbmLobes.isph`/`PathPbm.ispc` and in `nativePhysicallyBasedCovers()`; the
  tests catch a difference.
- **openPBR** (`HALCYON_MATERIAL_OPENPBR`) and **usdPreviewSurface**
  (`HALCYON_MATERIAL_USD_PREVIEW_SURFACE`), halcyon's vendor extensions
  (`extensions/*.json`): one `ND_open_pbr_surface_surfaceshader` (OpenPBR 1.1) or
  `ND_UsdPreviewSurface_surfaceshader`, each input set from the ANARI parameter of the
  same name (`base_color`, `specular_roughness`, `diffuseColor`, `opacityThreshold`, ...).
  Float and color3 inputs take a constant (`FLOAT32`/`FLOAT32_VEC3`), an attribute or a
  sampler, like physicallyBased's, and their defaults are MaterialX's nodedefs'
  (`openPbrInputs()`/`usdPreviewSurfaceInputs()`, one table each, which the graph and
  `Material.cpp` go by), all set on the node, so they hold where Typhoon's evaluation
  falls back to others. Constants only: `geometry_thin_walled` (`BOOL`),
  `useSpecularWorkflow` (`INT32`, or `BOOL`), `opacityMode` (`STRING`
  `"transparent"`/`"presence"`, or the nodedef's `INT32` 0/1; others warn and use
  transparent) and `opacityThreshold` (`FLOAT32`). openPBR
  `geometry_normal`/`geometry_coat_normal` are the nodedef's world-space vector3s
  (constant, attribute or sampler, a sampler's value used as given, not decoded as a
  normal map), as the `materialx` subtype reads those inputs; unset, or set to an
  invalid sampler or a name that isn't an attribute (checked when the graph is built,
  which every sampler change does again; maintainer decision, 2026-10-15, where Typhoon
  falls back to world +z), the unperturbed normal (the
  nodedef's `Nworld`: a base normal doesn't perturb the coat, where Typhoon's evaluation
  would give the coat the base's normal). A tangent-space normal map on openPBR needs a
  graph that decodes it (`ND_normalmap`), so a `materialx` material. openPBR
  `geometry_tangent`/`geometry_coat_tangent` are world-space vector3s (constant, attribute
  or sampler); unset (or invalid, as for the normals), the surface tangent
  (`ND_tangent_vector3`, the nodedef's `Tworld`,
  where Typhoon's evaluation takes world x for the base and `geometry_tangent` for the
  coat), connected only while the lobe's anisotropy isn't the constant 0. Unset,
  `geometry_coat_normal`, `geometry_tangent` and `geometry_coat_tangent` keep the
  nodedef's defaults as deliberate differences from Typhoon (maintainer decision, keep
  them; listed in [Typhoon differences](#deliberate-differences-from-typhoon)).
  usdPreviewSurface `normal` is the model's tangent-space vector3 (constant, attribute or
  sampler, in [-1, 1] as UsdPreviewSurface takes it: a normal-map image needs the
  sampler's `outTransform`/ `outOffset` scale 2 and offset −1), set on the node only where
  the app sets it. `displacement` and `occlusion` aren't read (Typhoon's evaluation
  ignores them; a DEBUG message says so where they are set). The `preview` renderer sees
  these models as their diffuse colour (`base_color`, `diffuseColor`, as matte's `color`,
  its alpha ignored) and their opacity (`geometry_opacity`, `opacity`) with `alphaMode`
  blend, or mask with `alphaCutoff` `opacityThreshold` where that is above 0; opaque while
  the opacity is the constant 1. The pathtracer's fallback for a graph that doesn't
  compile uses the same.
- **materialx** (`HALCYON_MATERIAL_MATERIALX`,
  `extensions/halcyon_material_materialx.json`; why and how:
  `docs/adr/0006-materialx-documents.md`): a material of a MaterialX document, with
  VisRTX's contract (`VISRTX_MATERIAL_MATERIALX`: `sourceType` `documentFile` (default) or
  `documentInline`, `source`, `materialName`) under halcyon's own extension name.
  `readMaterialX()` (`renderer/pathtracer/MaterialXDocument.*`) reads the document against
  the embedded data library (a read error is an ERROR; `Document::validate()` failures a
  WARNING), selects the material by name path or name (empty: the first material node with
  a surface or volume shader; an unknown name is an ERROR), inlines only the nodes
  MaterialXCpp lacks (`flattenSubgraphs()` with a filter keeping registered nodes and the
  models graph compilation dispatches, which a one-node probe compile tells apart, on the
  document and on each node graph), and converts the shaders and what they read as
  Typhoon's `mxcppAdapter.cpp` would: node keys are element name paths, node types nodedef
  names (its `ND_UsdPrimvarReader_*` → `ND_geompropvalue_*` and `ND_UsdTransform2d` →
  `ND_place2d_vector2` renames with their inputs; geomprop nodes read halcyon attributes
  by name, and their `default` where the hit lacks the attribute, see "Primvars" under
  [Shading context](#shading-context)), `nodegraph`/`output`, `interfacename` and
  multi-output connections resolved to the driving node, values as `_ConvertValue()`,
  `filename` inputs through `fileprefix` and the document's (or included file's) directory
  into absolute paths (inline documents: the working directory). Terminals: `surface` (the
  surface shader), `volume` (a volume shader, which MaterialXCpp makes the surface's
  interior medium) and `displacement` (compiled to check it, then unused, with a DEBUG
  message). Colour spaces follow MaterialX: an input's active colour
  space (inherited from its node, graph and document) is mapped to OpenUSD's name
  (`openUsdColorSpace()`: `lin_rec709` → `lin_rec709_scene`, `srgb_texture` →
  `srgb_rec709_scene`, `acescg` → `lin_ap1_scene`, ...; `none`/`raw` none;
  `rec709_display` and unknown names warn and stay unconverted); colour constants are
  converted into the working space, and image files get a `colorSpace:file` parameter the
  texture system converts sampled colours from. Every other parameter overrides
  (`applyMaterialXOverrides()`, on a copy of the graph, without reading the document
  again): on a shader input by name, a value of its type (FLOAT32 float, FLOAT32_VEC2/3/4
  vector2/color3/vector3/color4/vector4, INT32 integer, BOOL boolean, STRING string), or a
  sampler of any subtype or an attribute name on a float, vector2, vector3, color3,
  vector4 or color4 input through the sampler or geomprop node as for physicallyBased (the
  input's value, as authored or the nodedef's default, is the sampler's default); a
  sampler that is invalid when the graph is built (every sampler change builds it again)
  counts as unset, so the input keeps what the document gives it: its value or
  connection, else the nodedef's default or `defaultgeomprop` (`Nworld` for a normal,
  where the sampler's default would be (0, 0, 0); MaterialXCpp's normal resolution falls
  back to `Nworld` from a zero normal anyway); on a
  texture input by its path (`image1/file`, as `textureInputs` lists it), a STRING
  replaces the file (relative paths resolve against the document) and a sampler the file
  with its `anari:sampler/<index>` key (sampled with the image node's own texture
  coordinates, not colour-managed). Other names and types warn and are ignored. The
  document is read again only when `sourceType`, `source`, `materialName` or the file's
  modification time changes. The properties `materialNames` (the materials' name paths)
  and `textureInputs` (the selected material's `filename` inputs by element path), both
  `ANARI_STRING_LIST`, answer only while the material is valid (after a successful
  commit), and a list stays valid until the next commit. A document that doesn't read, an
  unknown material and a graph that doesn't compile are ERRORs and make the material
  invalid (`isValid()`), so its surfaces show `invalidMaterialColor` in both renderers (or
  are left out with `allowInvalidMaterials` false) until a commit fixes it; surfaces
  observe such materials (`Material::validityCanChange()`) to see their validity change,
  and a `finalize()` only for that rebuilds the scene only if the surface's validity
  changed. The `preview` renderer sees the surface shader as its diffuse colour and
  opacity, as for openPBR and usdPreviewSurface: standard_surface `base_color`,
  open_pbr_surface `base_color` and `geometry_opacity`, gltf_pbr `base_color` and `alpha`
  (with `alpha_mode`/`alpha_cutoff`), UsdPreviewSurface `diffuseColor` and `opacity` (mask
  at `opacityThreshold` above 0), disney_principled `baseColor`, each an override
  (constant, attribute or sampler) if set, else the document's constant, else the
  nodedef's default (also where the document connects it to a graph); other shaders white
  and opaque. Limits from MaterialXCpp: the hair's helper nodes (`ND_chiang_hair_*`,
  `ND_deon_hair_absorption_from_melanin`) and Lama nodes are missing, and a shading model
  can't sit upstream of another node (a `mix_surfaceshader` of two standard_surfaces is
  invalid). Every material of MaterialX's 50 example documents (66) compiles and renders
  but the hair's. The pathtracer shades materialx through its graph (`MATERIAL_KIND_CPP`).
  Colour spaces are a deliberate difference from Typhoon (maintainer decision, keep it):
  Typhoon reading the same `.mtlx` through usdMtlx doesn't know MaterialX's names and
  leaves `srgb_texture` images unconverted (with a warning); see [Typhoon
  differences](#deliberate-differences-from-typhoon). Typhoon comparison (22's
  acceptance): render a chosen set of the example documents here and through Typhoon from
  a USD stage that references each `.mtlx`, with OpenUSD's colour-space names on Typhoon's
  side where they differ, within noise. Accepted on the maintainer's own testing (not a
  script here); the Typhoon sweep (`docs/typhoon-verification.md`) compares `materialx`
  materials translated from Typhoon's UsdShade test scenes.
- **Delta lobes in mixes**: Typhoon's closure traversal divides a
  delta sample by the probability of each mix's choice of it (`closureTraversal.cpp`,
  `SampleNode()`'s `MixData` case), but a mix evaluates to the lerp of its children, whose
  leaves don't carry the mix weight, so choosing the child with that probability already
  gives the delta part its weight: in a white furnace by BSDF sampling a smooth white
  physicallyBased of metallic 0.5 reflects 1.5 (metallic 0.3: 1.7), a translucent lobe
  mixed half and half with smooth glass about 4.6. Typhoon's own caustic pruning agrees
  with the fix, not with its division: `PruneCausticClassLobes()` turns a mix that keeps
  one child into `Multiply(child, mix)`, which weighs that child's delta samples by the
  mix (`closureTraversal.cpp:178-195`). Layers and adds, which evaluate to the sum of
  their children, keep the division. halcyon passes a mix's delta samples up unscaled: the
  C++ side samples through `sampleSurface()` (`renderer/pathtracer/ClosureSampling.*`, a
  port of the traversal's composite nodes with the `_ApproxWeight()` and
  `_EvalThroughput()` they need, which are file-local there; the vendored file stays
  unmodified), which takes Typhoon's sample and, only where it is a delta sample and the
  tree has a mix, samples the tree again through the port, whose choices don't depend on
  the values and so reach the same leaf. Every other sample is Typhoon's, bitwise. Native
  physicallyBased does the same at its two mixes (transmission, metal), so native and
  graph shading stay bitwise and `nativeMaterials` false renders the same image. A
  deliberate difference from Typhoon (maintainer decision, keep it; listed in [Typhoon
  differences](#deliberate-differences-from-typhoon), to report upstream). A subsurface
  sample keeps Typhoon's division at every mix (maintainer decision, keep it; to report
  upstream), though that division is right only at the mix whose weight the subsurface
  leaf repeats as its own (Typhoon's OpenPBR and standard_surface give the leaf the weight
  of the mix that brings it in, which that mix's division makes up for) and overweights
  subsurface by 1 / (the choice's probability) at every other mix on its way up: OpenPBR's
  metal mix (`openPbr.cpp:528`) and standard_surface's transmission and metal mixes
  (`standardSurface.cpp:476`, 526) enclose its subsurface mix, so with metalness m its
  subsurface takes 1 / (1 − m) of its share (`closure_sampling` pins it: a subsurface leaf
  of weight 0.5 in a mix of 0.5 under a metal mix of 0.5 is sampled with probability 0.25
  and weighs 2, Typhoon's); and a subsurface leaf of weight 1 (disney_principled's, or a
  MaterialX document's `subsurface_bsdf` in a `mix`) is overweighted at its own mix too (a
  white furnace of a Lambertian mixed half and half with `subsurface_bsdf` reflects 1.5 by
  BSDF sampling). Cost: the C++ side samples a delta sample through a mix twice (choices,
  layer weights and the leaf; no evaluation), and checks each delta sample's tree for a
  mix. Measured against 4168e1c (48 threads), interleaved medians of 8 runs
  each: `native_materials --bench` matte 195.0 → 196.0 ms native and 408.6 → 411.6 ms
  through its graphs, physicallyBased 166.4 → 165.9 and 626.0 → 623.8 ms; `path_material
  --bench` within ±1% for every C++ material and native kind (an earlier set of 6 runs
  read +1 to +3% everywhere, also where nothing changed: noise). Where every hit is a
  delta lobe through a mix half the time (a lone smooth half-metal sphere under the
  ambient light, 256 × 256, 16 samples, through its graph) a render takes 42.2 → 43.8 ms
  (+4%, medians of 7 renders over 3 interleaved runs); natively nothing is added.
- **Alpha**: the colour's alpha (`color`/`baseColor`'s w) times `opacity`, as the other
  renderers (`Surface_getOpacity()`); constants are folded, otherwise an
  `ND_multiply_float`. Mask compares the clamped alpha with the cutoff (matte: an
  `ND_clamp_float` and `ND_ifgreatereq_float`).
- **Constant** parameters are node parameters (a float takes x, a colour xyz).
  **Attribute** parameters (a string naming an attribute; an unknown name is the default,
  as in the other renderers) become one `ND_geompropvalue_vector4` per attribute, whose
  `geomprop` is the attribute's name and whose `default` is (0, 0, 0, 1), so a hit
  without the attribute reads what the other renderers read there, then
  `ND_extract_vector4` (x, or w for alpha) or `ND_convert_vector4_color3`. hdAnari binds a
  USD primvar to `attribute0`–`3` (or `color`) and names that attribute in the material's
  parameter, so its assignment order doesn't matter here.
- **Sampler** parameters become a sampler node (`halcyon_sampler_vector4`, one per sampler
  and parameter default, so a colour and its alpha share one), then the same components.
  Its `in` is the sampler's `inAttribute` through that attribute's geomprop node (none for
  primitive samplers and `inAttribute` "none"), and it evaluates the whole sampler (in and
  out transforms, every subtype, a primitive sampler at the hit's primitive) with the
  `preview` renderer's ISPC code; an invalid sampler gives the parameter's default, as in
  the `preview` renderer. So textured surfaces render alike in both renderers. Since the
  node reads the sampler's `inAttribute` when the graph is built, `Material` observes its
  samplers and builds the graph again when one is finalized. The texture coordinates are
  each sampler's own `inAttribute` (how hdAnari points at the attribute it bound `st` to),
  not the shading context's `attribute0`, and are used unflipped (ANARI's convention: the
  first texel at 0). physicallyBased `normal` and `clearcoatNormal` (tangent-space normals
  in [-1, 1]) are re-encoded to [0, 1] for `ND_normalmap_float` on glTF PBR's `normal` and
  `clearcoat_normal`; without its own map the clearcoat keeps the unperturbed normal
  (`ND_normal_vector3`), as glTF specifies. `occlusion` is not used (glTF PBR has no
  input; the specification applies it to ambient light only), and `finalize()` says so in
  a DEBUG message. The normal maps' tangent and bitangent are the shading context's
  tangent frame (the world tangent and bitangent nodes `EvalGraph::Compile()` connects to
  them), so they follow authored and smoothed mesh tangents. hdAnari's `opacity` attribute
  (displayOpacity) halcyon doesn't read (open: the comparisons bypass hdAnari, see
  `docs/typhoon-verification.md`).
- **Texture system** (`renderer/pathtracer/HalcyonTextureSystem.*`, why:
  `docs/adr/0005-pathtracer-texture-system.md`): each `CppMaterial` owns one over the
  samplers its graph names by index (`ConvertedMaterial::samplers`), and its shading
  contexts point at it. The sampler node calls its `sample()`; MaterialX image nodes call
  `Sample2D()` with a `file` key `anari:sampler/<index>` (which `materialx` texture
  overrides use), which samples the sampler with `st` as its input attribute's value (s,
  t, 0, 1); a sampler that takes no input there (a primitive sampler, `inAttribute`
  "none") is `Missing`. Both reach `shading/halcyon_sampler.isph`, the sampling code of
  the `preview` renderer's hooks, through `HalcyonTextureSystem.ispc`'s single-lane entry
  points, which tell the subtypes apart by `Sampler::kind`. The request's derivatives,
  blur, address modes and filter are ignored (halcyon samplers have no mip levels; their
  own wrap modes and filter decide), and values aren't colour-managed. Other keys are
  image files (`renderer/pathtracer/ImageFile.*`): `Material::finalize()` reads the files
  its graph names (`imageFiles()`) through the device's `ImageFileCache`
  (`HalcyonDeviceGlobalState::imageFiles`: one image per file while materials use it, read
  again when the file changes, a file that doesn't read a WARNING once per file) and hands
  them to the `CppMaterial`. stb_image reads PNG, JPEG, Radiance HDR, TGA, BMP and binary
  PNM, tinyexr OpenEXR, `ImageFile.cpp` ASCII PNM (P2, P3: Typhoon's colour-management
  test images); values as stored (8 and 16 bits over their maximum, HDR and EXR as floats,
  PNG alpha not premultiplied, as Typhoon's OIIO backend reads PNG; other formats aren't
  premultiplied either), grey as (g, g, g), into float RGBA texels the bottom row first
  (MaterialX's `st` starts at the lower left, files at the top), with a box-filtered mip
  chain to 1 × 1. `Sample2D()` samples a file with the image2D sampler's ISPC code
  (`Image2D_filterAt()` in `halcyon_sampler.isph`, through `HalcyonTexture_sampleImage()`)
  at the mip level of the request's derivatives (log2 of the larger footprint in texels,
  trilinear between two levels), with its address modes (periodic, clamp, mirror; constant
  and useMetadata give the request's default outside [0, 1]: files here carry no wrap
  metadata) and filter (closest picks the nearest level and texel; linear, cubic and smart
  bicubic filter linearly), the request's fill value as alpha where the file has none, and
  colour data (role Color, three channels or more) converted from its `sourceColorSpace`
  into the working space after filtering, as Typhoon's backend does. Blur, frames, layers
  and UDIMs are ignored; a file the material couldn't read is an Error with the default
  value. Differences from Typhoon's OIIO backend (minification filtering, no bicubic, no
  `.tx`/TIFF/UDIM) are known; OIIO could sit behind the same keys if they are needed.
  Never change the sampling of only one of the two renderers: change
  `halcyon_sampler.isph`.
- `CppMaterial::compile()` gives each geomprop node's name its node type
  (`name#type`) before collecting the handle table, so a handle has one value type, and
  binds each handle to an attribute (`attributeFromName()`, helium's names) or to nothing
  (the node's default). The material's `attributeMask` tells the integrator which
  attributes to resolve; `PathShadeInSoA::attributes` carries them (only the rows in the
  mask are written), `PathShadeInSoA::attributePresent` which of them the hit has, and
  `lookupGeomProp()` in `PathMaterial.cpp` returns them as the node's type (x; xy; xyz;
  xyzw; int(x); x ≠ 0), or nothing (the node's default) for one the hit lacks.
- Every material commit compiles its graph, whichever renderer the app
  uses.
- Typhoon spot check: the Typhoon sweep compares
  glTF PBR (`gltfPbr_parameter_parity`) and MaterialX Lambertians through Typhoon
  (`docs/typhoon-verification.md`), and the native kinds match their graphs
  (`path_material`, `native_materials`); the subtypes rendered through Typhoon from
  equivalent USD were accepted on the maintainer's own testing (not a script here).

`test_material_graph.cpp` (ctest `material_graph`) checks the graphs against glTF PBR
nodes with the expected inputs, alpha, attributes through a geomprop lookup (a node's
default for an attribute the point lacks, (0, 0, 0, 1) in every graph halcyon builds,
samplers' inputs too), samplers of every kind through the texture system (ISPC samplers
filled with `Sampler.ispc`'s setters, which it links), `Sample2D()` and its keys, an image
node, the normal maps, the geomprop bindings and the parts of the shading context graphs
read (and that the table behind them was audited at the vendored commit; and which graphs
read the object position: position nodes the surface reaches, also the ones `Compile()` adds
to noise and triplanar projection, not world-space ones), and the openPBR
and usdPreviewSurface graphs against single model nodes (every input, the nodedefs'
defaults written out independently of the tables, attributes, world-space normals,
tangents, the constants-only inputs); `test_pathtracer_materials.cpp`
(`pathtracer_materials`) renders them exactly (constants, primitive and ray-derived
attributes, alpha modes, emission, mask cut-outs passed by every ray, normal maps,
openPBR's world-space normals, the two models set to Lambertians), and every sampler
subtype, wrap mode, filter and transform against the `preview` renderer: pixels whose
neighbourhood the `preview` renderer shows in one colour must match exactly, linearly
filtered ones within the jitter's error, including a sampler committed again with another
`inAttribute`.

## Frame channels

Both renderers write every channel the device declares: `channel.color`, `channel.depth`,
the id channels (`primitiveId`, `objectId`, `instanceId`) and, with
`KHR_FRAME_CHANNEL_ALBEDO`/`_NORMAL`, `channel.albedo` (FLOAT32_VEC3, UFIXED8_VEC3 or
UFIXED8_RGB_SRGB, rounded and sRGB-encoded as colour) and `channel.normal` (FLOAT32_VEC3, or
FIXED16_VEC3: each component in [-1, 1] times 32767, rounded). A channel of another pixel
type isn't mapped (`anariMapFrame` returns NULL), as for the other channels. Asking for the
albedo or normal changes no other channel. Depth and ids describe the visible hit of the
pixel centre's ray in both renderers (see `CONTEXT.md`). Misses give 0 albedo and normal.

- `preview` renderer (`renderAlbedoAndNormal()` in `renderer/preview/Preview.ispc`, an extra ray per
  pixel, traced only if the frame has one of the two channels): at the pixel centre's first
  visible surface, whatever volumes lie in front of it and whatever the `mode`, the
  albedo is the surface colour (`Surface_getColor()`: the material's `color`/`baseColor`
  as the `default` mode shows it, the diffuse colour of `openPBR`/`usdPreviewSurface`/`materialx`, the
  invalid material colour without a valid material) and the normal the world shading
  normal (as the `Ns` mode; turned into `Ng`'s hemisphere, not faced to the viewer).
- `quality` (`tracePath()` in `PathTracer.ispc`): each path gives the albedo and normal
  of its **first non-specular hit**, the first surface it scatters at whose closure isn't
  specular-only; the channels are their means over the pixel's samples since the last
  change (`PathTracerFrameState`'s `albedoAccum`/`normalAccum`), like the colour (and a converged pixel keeps
  them), so they are noisy at edges and through glass until they converge. The normal is
  that surface's smooth shading normal (`PathShadeInput::Nsrf`: turned into `Ng`'s
  hemisphere, not faced to the ray, no normal map, as the `preview` renderer's; Typhoon's
  `normal` AOV is the same turned normal). The albedo is the path's
  throughput there (the specular weights, and Russian roulette's, of the specular-only
  surfaces before it) times the closure's albedo:
  - native Lambertian (surfaces without a closure: no valid material, or a graph that
    didn't compile): the device's invalid material colour;
  - native matte: its colour clamped to [0, 1], what `closureAlbedo()` gives its graph;
  - native physicallyBased: what `closureAlbedo()` gives its graph: the layer
    F0 + (1 − F0) × (base colour, mixed by transmission with base colour × (1 − its
    F0)) mixed with the clamped base colour by metallic, under the sheen's and the
    clearcoat's (each top t over the rest: t + (1 − t) × rest);
  - C++ closures (every other material): `closureAlbedo()` (`renderer/pathtracer/ClosureAlbedo.*`,
    halcyon's own; Typhoon has no albedo output), a summary of the evaluated closure's BSDF
    tree (not its legacy summary fields, which `ND_surface` graphs such as matte's leave
    cleared), each lobe at normal incidence: diffuse, translucent and subsurface lobes
    weight × colour; sheen weight × colour × its directional albedo at normal incidence
    (`ApproxSheenDirAlbedo()`); dielectric weight × tint × F0 (reflection), × (1 − F0)
    (transmission) or × 1 (both), F0 = ((ior − 1) / (ior + 1))²; dielectric interface
    reflection weight × tint × F0 + transmission weight × tint × (1 − F0); conductor
    weight × its F0 from `ior` and `extinction`; generalized Schlick weight × `color0`
    (reflection), × (1 − `color0`) (transmission) or × 1 (both); Adobe OpenPBR (not used:
    halcyon never sets `useAdobeOpenPBR`) base weight × base colour. Mixes interpolate,
    adds sum, multiplies scale, and a layer is top + (1 − top) × base; the result is
    clamped to [0, 1]. So matte gives its colour, physicallyBased F0 + (1 − F0) × base
    colour for a dielectric with `specular` 1 (its base colour with the default 0) and its
    base colour as a metal, usdPreviewSurface its diffuse colour plus F0 (its lobes add).
    With Russian roulette's factor in the throughput a single path's albedo can exceed 1
    (FLOAT32_VEC3; 8-bit types clamp); the mean is unbiased.
    A closure without a BSDF tree (emission only) has albedo 0.
  - A closure is specular-only if every lobe that contributes (weight above 0, outside a
    mix's unused side) is a delta lobe (Typhoon's `IsEffectivelyDeltaAlpha()`); the path's
    channels then look past it along its specular sample. Null events (mask cut-outs,
    rejected presence, volume-only boundaries) are looked past too. A path that ends before
    a non-specular hit (a miss, a light, Russian roulette, `maxBounces`) gives albedo 0 and
    the normal of the last surface it met.
  - A collision in a transferFunction1D volume is a non-specular hit: albedo the
    throughput × the volume's colour, normal 0 (see [Volumes](#volumes)); so is one in an
    interior medium, of its single-scattering albedo (see
    [Interior media](#interior-media)).
  - The shading call computes the summary only while the path is still looking
    (`PATH_SHADE_ALBEDO`, returning `PathMaterialState::albedo` and
    `PATH_MATERIAL_SPECULAR_ONLY`), and only if the frame has one of the two channels. In
    the debug modes, paths are traced for the two channels alone.
  - Typhoon's `normal` AOV is the smooth surface normal where its camera ray first hits; its optional AOVs
    `ambocc` (a separate AO estimate; halcyon's `preview` renderer has screen-space ambient
    occlusion instead), `adaptiveHeatmap` (see [Adaptive sampling](#adaptive-sampling))
    and `primvars:<name>` have no halcyon channel.

`test_aovs.cpp` (ctest `aovs`) checks both renderers' albedo and normal on a matte floor,
without a material, through a mirror (the `quality` sees the wall it reflects), the
summary of physicallyBased and usdPreviewSurface closures, accumulation, the pathtracer's
debug modes, every pixel type (also after a frame commit changes one), and that the other
channels don't change; `pathtracer` compares its depth and ids with the `preview`
renderer's.

## Renderer modes

The `preview` renderer's `mode` parameter (`renderModeFromString()` in
`renderer/preview/Preview.cpp`, `RenderMode` in `halcyon_objects.isph`, shading in
`shadeGeometry()` in `renderer/preview/Preview.ispc`):

- `default`: eye-lit surfaces plus ray-marched volumes, composited through surfaces that
  aren't opaque (volumes and transparency only render in this mode)
- `primitiveId`, `objectId`, `instanceId`: random colour per user id
- `embreePrimID`, `embreeGeomID`, `embreeInstID`: random colour per raw Embree index
- `Ng`, `Ng.abs`: world-space geometric normal; `Ns`, `Ns.abs`: world-space shading
  normal (see [Rendering pipeline](#rendering-pipeline)); `Ns.flipped`: whether the
  interpolated shading normal was turned into `Ng`'s hemisphere (red), kept as authored
  (green), or absent so that `Ns` is `Ng` (blue), for debugging winding against authored
  normals; `uvw`: Embree hit coordinates
- `backface`, `hitSurface`, `hitVolume`: green/red predicates (`hitVolume` colours surfaces
  behind a volume green, others show the background or the visible hdris)
- `geometry.attribute0`–`3`, `geometry.color`: the geometry's own attribute, ignoring instances
- `opacityHeatmap`: surface opacity after the alpha mode; `testFrame`: ray direction, no tracing
- `ambientOcclusion` (`renderAmbientOcclusion()`): the occlusion factor as grey (white =
  unoccluded) where there is a first visible surface, whatever volumes lie in front of it
  (see `CONTEXT.md`; volumes aren't drawn), what a miss shows elsewhere (the visible hdri
  lights, else the background: `missColor()`); depth and ids as in
  `default` (from the visible hit, which may be a volume). The mode runs the occlusion
  passes whatever `ambientOcclusion` says (see
  [Ambient occlusion passes](#ambient-occlusion-passes)).
- `ambientOcclusion.unblurred`: the same as `ambientOcclusion`, but showing the occlusion
  factor before the blur pass.

Wherever a mode shows the background, the `visible` hdri lights show instead if there are
any (`missColor()`, see [Rendering pipeline](#rendering-pipeline)); they light nothing.
Unknown mode names fall back to `default`, as in helide.

## Adding a geometry subtype

Follow `Quad` (`geometry/Geometry.cpp`, `Geometry.ispc`):

1. In `shading/halcyon_objects.isph`, add a subtype data struct (like `QuadGeometry`: indices
   pointer plus count) and a named member of it in `struct Geometry`.
2. In `Geometry.ispc`, add a `Geometry_VertexAttributeFcn` hook (`Quad_getVertexAttribute`
   interpolates bilinearly with Embree's `uv`; clamp the primitive index into range) and an
   exported setter that fills the member and installs the hooks (`Geometry_setQuadIndices`);
   in `GeometryDerivatives.ispc` (declared in `Geometry.isph`), a `Geometry_DerivativesFcn`
   hook (`Quad_getDerivatives`: the surface's derivatives along the parameterization the
   attributes interpolate with, see [Shading context](#shading-context)) and, for a mesh
   subtype, a `Geometry_SurfacePositionFcn` hook (`Quad_getSurfacePosition`: the surface
   point Embree hit, from the primitive's vertices; subtypes without one keep the hit's
   position).
3. In `Geometry.cpp`, add a `Geometry` subclass. The constructor calls `rtcNewGeometry`.
   `commitParameters()` calls `Geometry::commitParameters()` first, then reads the subtype's
   parameters. `finalizeSubtype()` (the base `finalize()` updates `primitive.id` and attributes,
   then calls it) first resets the ISPC setter to no indices (so an early return leaves no
   stale pointer) and installs the real ones once they are read; reads `primitive.index`
   with `sharedIndices()` (UINT32 in place, UINT64 converted, as the spec allows), builds
   Embree-owned buffers, hands the vertex positions its derivative hook reads to
   `Geometry_setVertices()` (the base `finalize()` resets them), warns when `primitive.id`
   is shorter than the primitive count, calls
   `rtcCommitGeometry` and returns true. A mesh subtype (one with corners and face-varying
   values, like triangle and quad) also calls `commitTangents()` and, once its indices and
   vertices are set, `finalizeTangentFrames()`. An early `return false` (a missing
   `vertex.position`) leaves the geometry invalid; groups leave invalid geometry out of their
   Embree scenes.
4. Add the subtype to `Geometry::createInstance()`.
5. Update the `geometries` slot size (see the size check under
   [ISPC mirroring](#ispc-mirroring)).
6. Add the `khr_geometry_*` extension to `HalcyonDefinitions.json`.
7. Add `tests/test_<name>.cpp` and its ctest (see [Tests](#tests)); `test_quad.cpp` checks
   vertex colours, primitive ids and Embree primitive indices at probe pixels. Add a scene
   to `test_shading_context.cpp` with the analytic derivatives of a simple shape. Also add the
   subtype to `subtypeScenes()` in `test_idchannels.cpp` (short `primitive.id` warning) and to
   `subtypes()` and `indexSetups()` in `test_geometry_validity.cpp` (UINT64 indices), and
   to `clampSetups()` there if `finalizeSubtype()` clamps out-of-range indices.

The Embree `primID` must be the primitive index apps expect: primitive attributes, the
primitive sampler and `primitive.id` are all indexed by it.

## Parameters and properties

Device parameters (`HalcyonDevice::deviceCommitParameters()`). A device commit runs on the
app thread, so it only stores the values (`HalcyonDeviceGlobalState::commitDeviceParameters()`);
the next render puts them into effect on the worker and renders every frame again:

| Parameter | Type | Default | Effect |
|---|---|---|---|
| `allowInvalidMaterials` | bool | true | Render surfaces with a missing or invalid material in `invalidMaterialColor`; if false, such surfaces are left out |
| `invalidMaterialColor` | float4 | (1, 0, 1, 1) | Colour for those surfaces |

The environment variable `HALCYON_NUM_THREADS` sets the thread count of Embree's tasking
system, which BVH builds and rendering share (default 0, i.e. Embree's choice: all hardware
threads).

`preview` renderer parameters (`Preview::commitParameters()`, after the base's
`Renderer::commitParameters()`): `mode` ("default"); `background`
(float4 colour, default (0, 0, 0, 1), or an `Array2D` image); `ambientRadiance` (0);
`ignoreAmbientLighting` (true, forces `ambientRadiance` to 1); `eyeLightBlendRatio` (0.5,
clamped to [0, 1]); `volumeSamplingRate` (0.5, clamped to [0.001, 10]); `taskGrainSizeWidth`,
`taskGrainSizeHeight` (4, clamped to [1, 128]); `ambientOcclusion` (true);
`ambientOcclusionRadius` (0; 0 derives the radius from the world's bounds, and negative
values are clamped to 0 without a warning, so they derive it too);
`ambientOcclusionEstimator` ("g3d" or "sao", default "g3d"; other values
warn and use "g3d"); `ambientOcclusionIntensity` (1, negative values clamped to 0: the
occlusion factor's exponent, above 1 darkens, below 1 lightens, 0 gives a factor of 1);
`ambientOcclusionBlurPasses` (1, clamped to [1, 5] without a warning: the number of blur
passes over the occlusion factor, each after the first an à-trous pass that removes bands
near contacts).
`ambientColor` is not read (nor in helide). In the `default` mode the first visible
surface's eye-lit colour (also behind a volume) is multiplied by the pixel's
occlusion factor (`occlusionFactor()` in `renderer/preview/Preview.ispc`, 1 unless ambient occlusion runs;
see [Ambient occlusion passes](#ambient-occlusion-passes)); the background, volumes and
surfaces behind it are not.

`quality` renderer parameters (`PathTracer::commitParameters()`): `pixelSamples` (1,
values below 1 clamped to 1); `maxBounces` (16, Typhoon's; negative values clamped to 0);
`background`, `taskGrainSizeWidth` and `taskGrainSizeHeight` as for `preview`
(`Renderer::commitParameters()`; camera misses only, see [Background and ambient
light](#background-and-ambient-light)); `ambientColor` ((1, 1, 1)) and `ambientRadiance`
(0, the spec's; no `ignoreAmbientLighting`; negative values clamped to 0): the ambient
light, a constant dome camera rays don't see; `randomNumberSeed` (int, -1: the sample
generator's seed; -1 is Typhoon's default, the bits of the scene frame, and ANARI has no
scene frame, so it is frame 0's seed, 0); Typhoon's integrator settings with its defaults:
`lightSamplesPerHit` (1, values below 1 clamped to 1), `minBouncesBeforeRR` (2, negative
values clamped to 0), `fireflyClampThreshold` (20; 0 or less doesn't clamp),
`disableShadows` (false), `enableCaustics` (false) and `causticsClampThreshold` (5; 0 or
less doesn't clamp; see [Dispersion and caustics](#dispersion-and-caustics)),
`convergedSamplesPerPixel` (256, values below 1 clamped to 1),
`sampleLimit` (VisRTX's alias of it: 256; the sample limit where set, else
`convergedSamplesPerPixel`; 0 for no limit, negative values clamped to 0),
`adaptiveThreshold` (0.01, negative values clamped to 0) and `minSamplesBeforeAdaptive`
(64; negative values turn adaptive sampling off, as in Typhoon, which compares it as
unsigned). See [The quality renderer](#the-quality-renderer) and [Adaptive
sampling](#adaptive-sampling).

Light parameters (read only by the `quality`; see [Lights](#lights)): `color` ((1, 1, 1),
negative components clamped to 0) and `visible` (true) on all; `directional`: `direction`
((0, 0, -1); a zero or non-finite direction makes the light invalid, with a warning),
`angularDiameter` (0, radians, clamped to [0, 2π]), `irradiance` (1) or `radiance`; `point`:
`position` ((0, 0, 0)), `radius` (0, negative values clamped to 0), `intensity` (1), `power`
or `radiance`; `spot`: `position`, `direction` (as `directional`'s), `openingAngle` (π),
`falloffAngle` (0.1), `intensity` (1) or `power`; `quad`: `position`, `edge1` ((1, 0, 0)),
`edge2` ((0, 1, 0); edges spanning no area make the light invalid, with a warning), `side`
(`front`; `front`, `back` or `both`, other values warn and use `front`), `intensity` (1),
`power` or `radiance`, `intensityDistribution`; `ring`: `position`, `direction`, `radius`
(0), `innerRadius` (0), `openingAngle`, `falloffAngle`, `intensity` (1), `power` or
`radiance`, `intensityDistribution`, `c0` ((1, 0, 0); a `c0` parallel to `direction` warns
where a 2D distribution needs it, and another direction is used); `hdri`: `radiance`
(ARRAY2D of FLOAT32_VEC3, required), `up` ((0, 0, 1)), `direction` ((1, 0, 0)), `layout`
(`equirectangular`, the only value), `scale` (1; below 0 clamped to 0). Sizes below 0 are
clamped to 0, brightness values below 0 are clamped to 0; a brightness parameter counts as
set only as a FLOAT32.

Frame parameter `accumulation` (bool, false; `KHR_FRAME_ACCUMULATION`): declared but not
read; the `quality` refines its image whatever it says (see
[Adaptive sampling](#adaptive-sampling)). Frame properties: `duration`, and those of the
renderer's state for the frame: the `quality`'s `refinementProgress` and `numSamples` (see
[Adaptive sampling](#adaptive-sampling)). Frame channels: `channel.color`,
`channel.depth`, `channel.primitiveId`, `channel.objectId`, `channel.instanceId`,
`channel.albedo` and `channel.normal` (see [Frame channels](#frame-channels)).

Device properties (`HalcyonDevice::deviceGetProperty()`): `version`, `version.major/minor/patch`
and `version.name` ("halcyon 0.17.0") report the ANARI SDK's version, as helide's do
(`version` is major × 10000 + minor × 100 + patch). The number comes from `CMakeLists.txt`
(`ANARI_SDK_VERSION` in the SDK's tree, the found SDK's `anari_VERSION` when built alone),
which passes it to
`HalcyonDevice.cpp` as `HALCYON_VERSION_*` definitions; the `device` ctest pins the
literal, so an SDK version bump also updates that test. Also `extension` (every extension a renderer
honours, see below), `anariVersion.major/minor` (1.1), `geometryMaxIndex` (UINT32_MAX) and
`halcyon` (true).

**Extensions per renderer:** each renderer reports only the extensions it honours, through
introspection (`anariGetObjectInfo(device, ANARI_RENDERER, subtype, "extension",
ANARI_STRING_LIST)`) and its `extension` property (`Renderer::getProperty()`, the list
introspection gives its subtype, `Renderer::extensionSubtype()`; `default` reports the
`preview` renderer's, an unknown subtype none). Both come from the renderer objects' `extension` arrays in
`HalcyonDefinitions.json`, which the query generator emits in place of the device's list
(written there without the `ANARI_` prefix, which the generator adds). The `preview`
renderer lists all but
`KHR_FRAME_ACCUMULATION` and the light extensions other than `KHR_LIGHT_HDRI` (it shows
visible hdris where rays miss, unlit), the `quality` all of them. The `preview`
renderer keeps helide's `KHR_RENDERER_AMBIENT_LIGHT`, though it reads only
`ambientRadiance` (with `ignoreAmbientLighting` false), not `ambientColor`. The device's
`extension` property, `anariGetDeviceExtensions()` and its object info list every extension
the device implements, their union (the `dependencies` of `HalcyonDefinitions.json`). A new
extension goes into the `dependencies` and into the `extension` array of each renderer that
honours it; the `renderer_extensions` ctest pins both renderers' lists and the union.

## Synchronization

`khr_device_synchronization` is implemented with `RenderingSemaphore`
(`utility/RenderingSemaphore.hpp`; read its header comment before touching any wait path):

- Device work is a render (including its commit buffer flush) or an `ANARI_WAIT` property query
  on an object. `HalcyonDevice::getProperty()` sends those queries to the task queue, so they
  also wait for renders already queued. Device work waits until no array is mapped; mapping an
  array waits for device work in progress to end.
- `HalcyonDevice` makes each map, unmap and release of an array one step (`m_arrayMapMutex`),
  so the semaphore's record agrees with helium's `isMapped()` even when threads race on one
  array. Releasing an array's last public reference while it is mapped unmaps it (keeping what
  the app wrote) and reports an ERROR.
- Releasing the last public reference of a shared array that objects still use (which
  privatizes it) waits for device work in progress and holds device work off until the copy is
  made, like a mapping, so a render can't read app memory the app frees after `anariRelease`.
  helium's own release (up to 0.16; 0.17's unless the device overrides `runDeviceRelease()`)
  neither flushes nor waits. The wait happens without holding
  `m_arrayMapMutex`. From inside device work (a status callback on the worker during a render)
  the release can't wait and goes ahead with a WARNING: that render may still read the app's
  memory, so the app must keep it until the render ends.
- The release's hold is a named hold (`tryBeginReleaseHold()`), so a status callback raised
  during the copy (helium's "making private copy" performance warning) that waits on device
  work gets an ERROR naming the release, not a mapped array.
- Device work nested in device work on the same thread (a status callback issuing an
  `ANARI_WAIT` query during a render's flush) joins the outer work: it doesn't wait, flush again
  or end the outer work. Mapping never waits for device work while holding `m_arrayMapMutex`,
  so such a callback may also unmap or release arrays.
- A thread holding a mapped array cannot wait on device work. Instead of hanging, halcyon reports
  an ERROR and does not wait: `anariGetProperty(..., ANARI_WAIT)` returns 0,
  `anariFrameReady(WAIT)` returns 0, `anariMapFrame` returns the current buffers (a queued
  render will overwrite them), and `anariRenderFrame` on a frame whose previous render is still
  queued is **dropped**. A dropped render still fires the completion callback once, from a job
  queued behind the frame's render; that job replaces the frame's future (and takes over its
  internal reference), so waiting on the frame also waits for the callback. Until that job
  starts, a thread holding a mapped array is refused a wait on the frame too, since other
  frames' renders may be queued ahead of it (`Frame::m_jobsAwaitingDeviceWork`).
- A completion callback waiting on, mapping or rendering a *different* frame whose job is still
  queued gets the same ERROR-and-carry-on treatment (the single worker would deadlock); a
  render dropped this way also fires the callback. Rendering a frame from its own completion
  callback is ignored with a warning and fires no callback (it would recurse).
- The callback runs after the render's device work ends, so it may map arrays (helide 0.16
  and older deadlock there).
- **Known remaining deadlocks** (no test covers them):
  - An app thread holds a mapped array and waits on frame A, whose render has finished and
    whose callback is running; if A's callback calls `getProperty(..., ANARI_WAIT)`, both
    threads hang.
  - A status callback that maps an array during device work (a render or its flush, on the
    worker) hangs: the map waits for the device work its own thread is running.
  - Device work is tracked per thread (`DeviceWork::active()`), so a status callback raised on
    another thread during a render (an Embree tasking thread) isn't seen as inside device work. If it maps
    an array, releases the last public reference of a shared array that objects still use, or
    waits on the device (`ANARI_WAIT` queries, frame waits), it waits for the render, which
    waits for the callback. No such callback is known today: Embree reports errors, including
    BVH build errors, on the thread that called its API, which is the worker.
  - An app thread waiting on or mapping frame B holds B's helium object lock while it waits for
    B's job. If another frame's completion callback, running ahead of that job, waits on, maps
    or renders B, it blocks on that lock before reaching the ERROR-and-carry-on path above, and
    both threads hang (`cross frame wait` waits on the frames in queue order).
- Every commit buffer flush runs on the worker (a render's, or an object `ANARI_WAIT` query's),
  after the renders queued before it, so a frame's commit never waits for its render;
  `Frame::finalize()` asserts it runs on the worker. A new flush path must go through the task
  queue too, and through `HalcyonDeviceGlobalState::flushCommitBuffer()`, which flushes again
  (up to 8 times) while the objects a flush finalized have notified observers: helium stages
  those for the next flush, and the render or query at hand must see them (a `materialx`
  material's surfaces, which follow its validity, say). The repeated flush is a maintainer
  decision (keep it). ANARI-SDK 0.17 (`next_release`, helium squash `a3a3eaa`) drains
  them in the same flush (an observer notified on the flushing thread joins the buffer
  being walked), which leaves the extra passes nothing to do; the loop stays while
  halcyon supports helium 0.15 and 0.16.
- `anariGetProperty` on the device handle goes to helium's `deviceGetProperty()`, which neither
  flushes nor waits.

`tests/test_synchronization.cpp` covers these rules, including a 500-frame stress test for torn
frames.

## Deliberate differences from helide

**Policy:** match helide, except where helide contradicts the ANARI specification, contradicts
itself, or reads out of bounds, and where the maintainer has decided otherwise. Any other change
to what halcyon renders or reports that departs from helide needs the maintainer's decision
first.

Where helide contradicts the spec or itself or reads out of bounds (confirmed; keep them).
helide in ANARI-SDK 0.17 (`next_release`, squash `a068030`, with helium's `a3a3eaa`) fixed
most of what this list held: instance normal matrices (the inverse transpose of the linear
part), the `khr_material_physically_based` spelling, the spec sampler names, the primitive
sampler's `inOffset`, image3D's offsets, `repeat` wrapping of negative indices, volume
intervals (clipped to the surface, ties, every transform of an instance array, instanced
world bounds, opacity × alpha once), sphere and curve index and radius bounds, curve UINT64
indices, short instance `id` arrays, `volumeSamplingRate` clamping and array `begin`/`end`
in every reader. Against helide 0.16 and older those still differ. What differs from 0.17:

- **Object-space attributes:** `objectPosition` and `objectNormal` are computed in object
  space (TODOs in helide, which reports world space).
- **Samplers:** only the spec names `outTransform` (transform sampler) and `inOffset`
  (primitive sampler) are read; helide still falls back to its older `transform`/`offset`.
- **Robustness:** short `primitive.id` (which helide doesn't read) and cylinder and cone
  index or radius arrays warn and clamp where helide reads past them; renderer parameters
  out of range are clamped (helide clamps only `volumeSamplingRate`); structured-regular
  fields with non-positive `spacing` are invalid, and so are transferFunction1D volumes of
  `unitDistance` 0 or less and image samplers with an empty
  `image` (with a warning). Geometry whose `vertex.position` is unset after a valid commit
  becomes invalid and stops rendering (helide keeps the old buffers). `primitive.index` may
  be UINT64 on every subtype, as the spec allows (truncated to 32 bits like `primitive.id`;
  helide's triangle and quad throw), and a `primitive.index` of another element type makes
  the geometry invalid with a warning (helide throws).

Other maintainer decisions (keep them):

- **Frames:** every `anariRenderFrame` fires the completion callback once, including a render
  skipped because nothing changed, an invalid frame and a dropped render (helide fires none
  for these); the ERROR is still reported. The exception is a render requested from the
  frame's own completion callback, which is ignored with a warning and fires none. A device
  parameter commit counts as a change, so the next render of every frame renders (helide skips
  it and keeps the old image). The callback runs after the render's device work ends, and waits
  that helide 0.16 and older deadlock on report errors (see
  [Synchronization](#synchronization)); helide 0.17 reports errors for them too, by rules of
  its own (`src/devices/helide/AGENTS.md`), which differ in detail (its `anariMapFrame`
  returns NULL where halcyon's returns the current buffers).
- **Defaults:** the code uses the defaults the specification and `HalcyonDefinitions.json`
  declare, where helide's code doesn't: `ambientRadiance` 0 (helide 1; only visible with
  `ignoreAmbientLighting` false) and matte `color` 0.8 grey (helide white), in both
  renderers. Comparisons with helide set these explicitly. (helide 0.16 and older also
  default sphere `radius` to 0.01 and `volumeSamplingRate` to 1.0, where 0.17 agrees with
  halcyon's 1.0 and 0.5.)
- **8-bit colour and pixel centres** (see [Rendering pipeline](#rendering-pipeline)): helium
  0.17 encodes sRGB exactly and rounds 8-bit colour to nearest, and helide 0.17 traces
  through pixel centres, as halcyon does. Against helide 0.16 and older, colour differs by a
  few codes (it truncates and uses `pow(v, 1/2.2)`; compare in FLOAT32_VEC4) and images,
  including the background image, are shifted by half a pixel (its rays go through the
  pixel's lower-left corner), so edges and single-pixel features differ.
- **Renderer subtypes:** helide's renderer is `default`; halcyon's is named `preview`,
  listed first, and `default` (the spec's alias of the first renderer listed) creates it.
  An unknown renderer subtype gets an object that is never valid, as for other object
  types, where helide gives it its default renderer, so a frame using one reports an
  ERROR and renders nothing.
- **Transparency:** the `default` mode composites through `blend` surfaces by their opacity
  and skips `mask` cut-outs, with volumes marched between the layers; depth and ids come from
  the visible hit (see [Rendering pipeline](#rendering-pipeline)). helide renders every
  surface opaque, so compare scenes with non-opaque materials only in other modes or with
  opaque materials. Debug modes still show the first surface hit, opaque, as in helide
  (except the two ambient occlusion modes, which show the first visible surface).
- **Ambient occlusion** (see [Ambient occlusion passes](#ambient-occlusion-passes)): halcyon
  only; helide has none. The `preview` renderer's `ambientOcclusion` darkens the first
  visible surface in the `default` mode, and the `ambientOcclusion` and
  `ambientOcclusion.unblurred` modes show the occlusion factor after and before the blur. It
  is on by default, so default images don't match helide: comparisons with helide set
  `ambientOcclusion` to false. It is computed in screen space
  from a hit buffer, not with occlusion rays, so it stays interactive without accumulation
  or denoising (`docs/adr/0001-screen-space-ambient-occlusion.md`). Decided since:
  - A surface seen through a volume is still the first visible surface and receives
    occlusion, so a volume's bounding box doesn't cut occlusion out of what lies behind it.
  - The blur window is centred on the pixel (5×5, outer offsets at half weight), so the
    blurred factor lines up with the colour image.
  - `ambientOcclusionEstimator` selects the estimator, `"g3d"` (G3D's high-quality code)
    by default or `"sao"` (the paper's), and the strength is an exponent,
    `ambientOcclusionIntensity`, not a scale.
  - `"sao"` sets the paper's linear intensity `σ` to the radius, so neither estimator
    depends on the scene's units.
  - The passes run only when the render reads their result: not in modes that ignore the
    factor, at intensity 0, or for a world without surfaces.
  - A negative `ambientOcclusionRadius` is clamped to 0 silently, so it derives the radius
    like 0.
  - The extra à-trous blur passes, which remove the bands a single blur leaves near
    contacts, are opt-in: `ambientOcclusionBlurPasses` defaults to 1, so the default cost
    and images stay those of the single blur.
- **Release paths** (see [Synchronization](#synchronization)): releasing a mapped array unmaps
  it, keeps the app's writes and reports an ERROR; a status callback that releases a shared
  array during a render goes ahead and reports a WARNING that the render may still read the
  app's memory. helide 0.16 and older handle neither case; helide 0.17 handles the second the
  same way.
- **Face-varying attributes** (see [Array data](#array-data-copies-and-shared-memory)):
  triangle and quad read `faceVarying.*` attributes (ANARI 1.1, needed by OpenUSD scenes);
  helide ignores them. Triangles follow VisRTX (corner indexing, precedence over `vertex.*`);
  VisRTX has no face-varying quads, so quads extend the same rule to four corners.
- **The quality renderer** (see [The quality renderer](#the-quality-renderer)):
  halcyon only; helide has none (its renderer ignores the subtype). Its reference is
  Typhoon, not helide. For it, the device also declares `KHR_FRAME_ACCUMULATION` and the
  light extensions (`KHR_LIGHT_DIRECTIONAL`, `_POINT`, `_SPOT`, `_QUAD`, `_RING`, `_HDRI`,
  `_PRIMARY_VISIBILITY`), which
  helide doesn't. Each renderer reports only the extensions it honours (see
  [Parameters and properties](#parameters-and-properties); maintainer decision, keep it):
  the `preview` renderer doesn't list these, as it ignores them (it isn't progressive, so
  its renders are skipped as before when nothing changed, and it renders scenes with lights
  as it does without them), except `KHR_LIGHT_HDRI` (see the next item). The `quality`
  lists every extension. The device's list is
  their union, so it names extensions one renderer ignores:
  tools that check only the device's list (`anariInfo`, `anariCts query-features`,
  `anariRenderTests`) still send lights to the `preview` renderer, which renders those
  scenes unlit where helide's tests are skipped. The pathtracer
  refines whatever the frame's `accumulation` says, a deliberate difference from the spec
  (see [Adaptive sampling](#adaptive-sampling)).
- **Hdri lights in the `preview` renderer** (see [Lights](#lights);
  maintainer decision, keep it): the `preview` renderer shows the `visible` hdri lights
  wherever its camera rays miss and it would show the background (every mode but
  `testFrame`), opaque, exactly as the `quality`'s camera misses (one lookup,
  `shading/halcyon_hdri.isph`), but isn't lit by them, and lists `KHR_LIGHT_HDRI`. helide
  ignores lights, so compare with helide without hdris.
- **Volumes in the pathtracer** (see [Volumes](#volumes);
  maintainer decisions, keep them): the `quality` scatters light in transferFunction1D
  volumes, whose colour is the single-scattering albedo, where the `preview` renderer
  composites the colour as emission and absorption; the two agree on alpha (the same
  transmittance), not on colour. Where volumes overlap the `quality` adds their
  extinctions, where the `preview` renderer marches only the first volume entered.
- **Albedo and normal channels** (see [Frame channels](#frame-channels);
  maintainer decisions, keep them): `KHR_FRAME_CHANNEL_ALBEDO` and `_NORMAL` are
  halcyon only; helide declares neither. Both renderers list them: the `preview` renderer
  fills them too, from the pixel centre's first visible surface (its surface colour and
  shading normal), rather than leaving them empty; this adds a ray per pixel only to
  frames that ask for them, and changes no other channel. The `quality`'s come from the
  first non-specular hit (as denoisers expect), give albedo 0 where a path never reaches
  one (a mirror reflecting the background, glass whose path escapes) and report the smooth
  shading normal without normal maps: deliberate differences from the specification ("the
  first hit"), from VisRTX (the first hit, its normal with normal maps) and from Typhoon
  (whose `normal` AOV takes the camera ray's first hit; listed in [Typhoon
  differences](#deliberate-differences-from-typhoon)). The two renderers' albedos of one
  material differ where it reflects specularly: `default` reports the diffuse colour, the
  `quality` its closure's summary (physicallyBased with `specular` 1: F0 + (1 − F0) ×
  base colour; usdPreviewSurface: diffuse colour + F0).
- **MaterialX model subtypes** (see
  [Materials in the pathtracer](#materials-in-the-pathtracer)): `openPBR` and
  `usdPreviewSurface` (halcyon's `HALCYON_MATERIAL_*` extensions) are halcyon only; helide
  has neither. The `preview` renderer shows them as their diffuse colour and opacity (blend,
  or mask at `opacityThreshold`), like a matte material, and so lists their extensions, as
  it lists `KHR_MATERIAL_PHYSICALLY_BASED`, which it also shows only as a colour.
- **materialx** (see [Materials in the pathtracer](#materials-in-the-pathtracer)): halcyon
  only, with VisRTX's parameters under halcyon's own extension name
  (`HALCYON_MATERIAL_MATERIALX`, not VisRTX's vendor name), and beyond VisRTX's contract:
  samplers of any subtype on any shader input (VisRTX accepts only image2D samplers and
  splices an image node), attribute names on inputs, volume shaders, and MaterialX's
  colour-space semantics (maintainer decisions, keep them). An invalid document makes the
  material invalid, so surfaces show `invalidMaterialColor` (VisRTX falls back to a white
  diffuse material). The `preview` renderer shows the surface shader as its diffuse colour
  and opacity, as for openPBR and usdPreviewSurface, so a document whose colour comes
  from a texture looks flat there, in the nodedef's default colour.
- **Curve bases** (see [Rendering pipeline](#rendering-pipeline)): the curve's `basis`
  (`bspline`, `catmullRom`) is halcyon's vendor extension `HALCYON_GEOMETRY_CURVE_BASIS`
  (maintainer decision, keep it); helide, like KHR_GEOMETRY_CURVE,
  has linear segments only. Without `basis`, or with `linear`, curves render as before.
- **Shading normals** (see [Rendering pipeline](#rendering-pipeline)): triangle and quad read
  `vertex.normal` and `faceVarying.normal`; helide ignores them (helide_gpu reads
  `vertex.normal` on triangles, VisRTX both on triangles). Precedence and corner indexing
  follow VisRTX, and quads extend them to four corners. The `worldNormal`/`objectNormal`
  attributes report the shading normal, as in VisRTX, and the `Ns`/`Ns.abs` modes show it;
  ambient occlusion uses it too, as helide_gpu's does. Scenes without normals render as
  before, so they still match helide.
- **Shading normals turned into `Ng`'s hemisphere** (see "Two normals" under
  [Rendering pipeline](#rendering-pipeline); maintainer decision, keep it): both renderers
  negate the interpolated shading normal at a hit where it makes an obtuse angle with `Ng`
  (in world space), as Typhoon does, so a mesh wound against its normals shades as the
  mesh with its normals negated: the winding decides the side (USD content often has such
  meshes: most of Typhoon's test scenes' ground planes, cornell_box's walls, which
  rendered black before). So the `Ns` mode, the normal channel and the
  `worldNormal`/`objectNormal` attributes report the turned normal, a normal map's relief
  comes out inverted (see "The smooth normal" under [Shading context](#shading-context)),
  and the inside and outside of thick or transmissive materials follow the winding; such a
  mesh matches the same mesh in the other winding only for materials that don't depend on
  orientation (opaque, without normal maps). The eye light and `Ns.abs` take the normal's
  absolute facing and the ambient occlusion hit buffer faced it to the viewer already, so
  they are unchanged. Normals on the winding's side, and scenes without normals, render as
  before. In the `quality` this is Typhoon's rule; in the `preview` renderer it departs
  from helide, which reads no normals (VisRTX turns its shading normal too, but to the
  side of an `Ng` faced to the ray, and takes the angle in object space, `populateHit.h`).
  Cost (measured against 855a5c4, 48 threads, interleaved medians of 8 runs each): nothing
  measurable where meshes have no normals (`native_materials --bench`, 10 runs: matte
  201.5 → 201.3 ms native and 421.8 → 423.4 ms through its graphs, physicallyBased 169.6 →
  169.1 and 642.5 → 642.0 ms); on a 1M-triangle sphere with `vertex.normal`, 1920 × 1080,
  the `preview` renderer 4.56 → 4.53 ms (`default` mode), 9.15 → 9.08 ms (with ambient
  occlusion and the normal channel), 4.48 → 4.47 ms (`Ns`), all within the runs' spread,
  and a one-sample `quality` render 9.68 → 9.74 ms (+0.7%, within the runs' ranges,
  9.65–9.78 and 9.69–9.84 ms), all normals on the winding's side.

## Deliberate differences from Typhoon

The `quality`'s reference is Typhoon at the pin. These are where it renders differently
on purpose, each a maintainer decision (keep them) and described in full where linked;
those marked "report" are Typhoon defects to report upstream (none reported yet).
`docs/typhoon-verification.md` measures what the comparison scenes show of them.

- **Units and inputs:** lights use ANARI's radiometric definitions, not UsdLux's
  ([Lights](#lights)); `visible` is camera visibility only, per light, so invisible domes
  and directional cones still light the scene ([Lights](#lights)); `materialx` honours
  MaterialX's colour-space names ([Materials in the
  pathtracer](#materials-in-the-pathtracer)); openPBR's unset `geometry_coat_normal`,
  `geometry_tangent` and `geometry_coat_tangent` keep the nodedef's defaults (ibid.);
  attributes reach geomprop nodes as float4 converted to the node's type, where Typhoon's
  primvars keep their authored type ([Shading context](#shading-context), "Primvars").
- **Geometry:** meshes without normals shade with `Ng`, faceted, where Typhoon smooths them
  ([Shading context](#shading-context)); a medium's owner is the instance, transform and
  surface, not the prototype ([Interior media](#interior-media)).
- **Scale:** ray offsets, sphere-light origins and subsurface walk steps follow the
  scene's scale (`epsilonFrom()`), where Typhoon uses fixed 1e-4 and 1e-6 ([The quality
  renderer](#the-quality-renderer), [Subsurface scattering](#subsurface-scattering));
  the screen-space derivative fallback is the pixel footprint ([Shading
  context](#shading-context)).
- **Path loop:** the emitter-only segment is traced against the whole scene, passing only
  mask cut-outs and volume-only boundaries, where Typhoon's light-only ray mask passes
  every surface ([The quality renderer](#the-quality-renderer)); rays don't gather a
  delta sun (report; [Lights](#lights)); one reflection-only flag, the pruned BSDF's,
  folds both MIS estimators of the hdris and the ambient light (report; [Dispersion and
  caustics](#dispersion-and-caustics)); a mix's delta samples aren't divided by the
  mix's choice (report; [Materials in the pathtracer](#materials-in-the-pathtracer));
  translucent samples aren't caustic samples (report; [Dispersion and
  caustics](#dispersion-and-caustics)).
- **Media** ([Interior media](#interior-media)): paths enter a medium on any inward and
  leave it on any outward crossing, whatever the IOR ratio (report); shadow rays leaving
  the owner outwards start out of the medium (report), and with caustics off shadow rays
  going inwards through a surface they pass straight start in it (report); camera rays
  through volume-only boundaries see the background weighed by the transmittance (report).
- **Channels:** the albedo and normal channels come from each path's first non-specular
  hit, where Typhoon's `normal` AOV is the camera ray's first hit; Typhoon has no albedo
  ([Frame channels](#frame-channels)).
- **halcyon only:** transferFunction1D volumes scatter ([Volumes](#volumes)); refinement
  ignores the frame's `accumulation` ([Adaptive sampling](#adaptive-sampling)).

Kept as Typhoon's, though they are Typhoon defects to report: a subsurface sample's weight
at enclosing mixes and the entry vertex's lost lobes ([Subsurface
scattering](#subsurface-scattering), [Materials in the
pathtracer](#materials-in-the-pathtracer)); glass with a scattering interior doesn't
conserve energy ([Interior media](#interior-media)); one medium at a time (ibid.); GGX's
sensitivity to the shading normal's last bits (`docs/typhoon-verification.md`, decision 5).

## Tests

One executable per area, `tests/test_<name>.cpp`, registered in `tests/CMakeLists.txt` as
ctest `halcyon::<name>` (`ctest -N` lists them; this file names them without the
prefix). Those not described below (`camera`,
`framebuffer`, `instance`, `object_storage`, `renderer`, `rendermodes`, `transparency`)
check their area of the `preview` renderer and device by rendering small scenes.

- **ISPC-level tests** (`math`, `hitinfo`, `attributes`, `spatial_field`, `volume`) compile a
  `test_<name>.ispc`, plus the device `.ispc` sources they exercise (`attributes`:
  `Geometry.ispc` and `GeometryDerivatives.ispc`, whose hooks its setters install, with
  the device's `--opt=disable-fma`; `spatial_field`: `SpatialField.ispc`; `volume`: that and `Volume.ispc`), and fill
  ISPC structs by hand. Their `.ispc` is built with `--woff` because they gather by design.
  `material_graph` also compiles `Sampler.ispc`, to fill the samplers it samples with its
  setters.
  `spatial_field` and `volume` also load the device. `path_material` shades points
  through `renderer/pathtracer/PathMaterial.isph` and compares C++ lanes bitwise with
  Typhoon's path-vertex sequence run directly with the whole shading context (its BSDF
  sample through `sampleSurface()`, see `closure_sampling`), so a part a
  graph reads but isn't sent shows up there, native matte within rounding with its
  graph (bitwise with `nativeMaterials` false) and native physicallyBased bitwise with its
  graph (also `nativePhysicallyBasedCovers()`); `--bench` prints the costs. `path_pbm`
  compares native physicallyBased's lobe math (`PathPbmLobes.isph`: the GGX energy table
  lookup, Turquin's scale, Fresnel, the layer's reflectance, VNDF sampling and pdf, the
  normal correction, BSDL's dielectric layer filter, and whole generalized Schlick,
  clearcoat, transmission and sheen leaves) bitwise with the vendored C++ functions; its
  `.ispc` is built as `PathPbm.ispc`.
  **Bitwise only on x86-64 without FMA** (maintainer decision, keep it): the native
  physicallyBased checks of `path_pbm`, `path_material` and `native_materials` are
  bitwise only where the C++ rounds as `PathPbm.ispc` does (`tests/native_rounding.h`).
  `tests/CMakeLists.txt` decides it at configure time (`HALCYON_NATIVE_BITWISE`, printed
  as "Native material checks: bitwise" or "within rounding") by probing
  `CMAKE_CXX_FLAGS` and the build type's flags (not per-target ones): bitwise on x86-64
  without fast math (`__FAST_MATH__`, `-funsafe-math-optimizations`, ...) and either
  without an FMA ISA (`__FMA__`, as with `-march=native`, `-march=haswell`, `-mfma`) or
  with a last `-ffp-contract=off`. The cache option `HALCYON_NATIVE_CHECKS` (`AUTO`, the
  default, probes; `BITWISE` or `TOLERANT` decides) overrides the probe for builds whose
  flags it can't see (per-target flags, a multi-config generator's per-configuration
  ones). aarch64 (GCC contracts by default), Apple clang on
  arm64, `-march=native` and `-ffast-math` builds compare those checks within rounding
  instead: values within 1e-4 relative to max(1, |value|) (native matte's tolerance), times
  (0.2 / alpha)² for GGX lobes of alpha between 1e-3 and 0.2, whose peak amplifies a
  direction's rounding by about 1 / alpha² (`ggxConditioning()`), and 10 for the glossy
  normal correction's quadratic; a few may differ by more or take another branch at a
  threshold (0.1% of values in `path_pbm`, 1% of native physicallyBased points in
  `path_material`), but by no more than 100 times their tolerance (`OUTLIER`; at most 6.2
  seen); `native_materials`' physicallyBased colours must differ by less than 2.5% of the
  mean difference between two seeds (measured with `-march=native`: 0.8%), and fewer than
  1% of pixels by more than their own difference between the two seeds (4 of 4096
  seen), not by the per-pixel `close()` rule, which paths through near-specular lobes
  fail. The GGX conditioning isn't capped: near alpha 1e-3 values really differ by up to
  0.84 relative.
  C++ lanes and graphs stay bitwise and native matte within rounding in every build. The
  default x86-64 build stays bitwise. `closure_sampling`'s bitwise check of
  `sampleSurface()` against Typhoon assumes that the ported `approxWeight()` and
  `evalThroughput()` (`ClosureSampling.cpp`) round as the vendored `_ApproxWeight()` and
  `_EvalThroughput()` do, that is that both translation units contract alike: where a
  build contracts one and not the other, a delta sample's replayed choice at a layer or
  add may rarely fall on the other side of its threshold. Check the tolerant branch in a
  throwaway tree configured with `-DCMAKE_CXX_FLAGS=-march=native`.
- **Device-level tests** load the device with `anari::loadLibrary("halcyon", statusFunc)` and
  need the ctest `ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:${PROJECT_NAME}>:..."`
  property; copy an existing entry. `device` instead links the library and calls
  `anariNewHalcyonDevice()`, as an application would. `indexed_vector` tests the C++
  container alone.
- Each check prints `PASS <name>` or `FAIL <name>` (`math` prints only failures and a summary);
  the process exits non-zero on any failure.
- The status callback fails the test on any device ERROR. The geometry tests (`quad`, `sphere`,
  `curve`, `cylinder_cone`), `array_begin`, `shading_context` and `renderer_extensions` also
  fail on warnings, so
  they cannot exercise warning paths.
  `frame`, `synchronization`, `privatize`, `geometry_validity`, `idchannels` and
  `face_varying` (the short face-varying array warning), `shading_normals` (the short
  `faceVarying.normal` and wrong element type warnings), `mesh_tangents` (the same for
  tangents), `curve_basis` (clamped indices, short strands, unknown bases) and
  `sample_limit` (a set `sampleLimit` overriding a changed `convergedSamplesPerPixel`)
  record
  messages and check for the expected ones (`idchannels` covers the short-`primitive.id`
  warning of every geometry subtype, UINT64 truncation and the instance `id` warnings;
  `ambient_occlusion` the unknown `ambientOcclusionEstimator` warning, and counts the DEBUG
  messages of a frame allocating and freeing its occlusion buffers and its intermediate
  blur buffer);
  `samplers` records warnings, and `materials` checks usdPreviewSurface's unknown
  `opacityMode` warnings; `materialx` records messages and checks the expected ERRORs and
  WARNINGs (documents that don't read, unknown materials, sourceTypes, overrides and
  files; a file that doesn't read warns once); `pathtracer_materials` fails on warnings and checks the
  DEBUG message of an unused `occlusion` sampler; `native_materials` fails on warnings and
  compares the renders of `nativeMaterials` true and false per pixel, of a matte box and
  of a physicallyBased box (the latter's colour against the noise where the build isn't
  bitwise, see below; `--bench` times them), and checks the kind the device routes
  materials to by `Material::finalize()`'s DEBUG message; `pathtracer` and `aovs` expect only the
  missing-material warning of their surface without one; `lights`
  expects only the warnings it provokes (`radiance` without a size, and one per invalid
  spot, quad or ring parameter), and `hdri` one per invalid hdri parameter. Tests that could deadlock set
  `TIMEOUT 60`, so a hang shows up as a timeout.
- Parity tests compare against C++ ports of helide's algorithms (image sampling, structured
  field sampling, transfer function ray marching including helide's jitter) or probe pixels
  against expected values.
- `path_integrator` checks the path loop against analytic values: inside a diffuse
  sphere lit by a point light at its center every bounce adds a known amount (exact up to
  Russian roulette, in expectation after it), a sphere light at its center lights the
  wall exactly only if MIS weighs light and BSDF samples to one, floors behind opaque,
  `blend`, `mask` and `disableShadows` blockers keep their exact share of a directional
  light, the firefly clamp caps a sample's luminance, and a `blend` surface averages to
  its opacity's mix; and the convergence of a Cornell box (seeds, sample counts and light
  sample counts agree on the mean, the noise between seeds falls with the samples); and
  one scene at scales 1e-7, 1e-3, 1 and 1e4 renders the same image with the same samples
  (well within the noise between seeds), so the ray offsets follow the scale.
- `curve_basis` checks the curve's cubic bases: B-spline and Catmull-Rom strands are hit
  exactly where the analytic union of discs r(u) around P(u) lies, their vertex colours
  (each control point's position and radius) put every hit within r(u) of its pixel, a
  Catmull-Rom strand passes through its control points, implicit and explicit
  `primitive.index` with per-segment attributes and ids, clamping, short strands, unknown
  bases, `linear` as no `basis`, and a basis committed again; `geometry_validity` and
  `idchannels` run their curve cases with both cubic bases too.
- `normal_winding` checks the shading normal's turn into `Ng`'s hemisphere (see "Two
  normals" under [Rendering pipeline](#rendering-pipeline)) on curved triangle and quad
  meshes with `vertex.normal` or `faceVarying.normal`, in both windings: negating the
  normals changes nothing, bitwise, in the `preview` renderer's normal-reading modes
  (`default`, `Ns`, `Ns.abs`, `opacityHeatmap`, both ambient occlusion modes), its albedo
  and normal channels and the `worldNormal`/`objectNormal` attributes (through a
  transform sampler), and in the `quality`'s shading context debug modes, its albedo
  and normal channels and path-traced images (native matte and physicallyBased, with a
  normal map and clearcoat, and a coated anisotropic openPBR through its graph); a mesh
  wound against its normals renders as one wound with them within rounding where the
  material doesn't depend on orientation (normals negated, path-traced means within
  0.2%; a normal map's relief is inverted, as in Typhoon, so that material isn't
  compared); the same negation holds bitwise under a mirroring instance (scale (−1, 1,
  1)), where the normals still land on `Ng`'s side per pixel, and with authored tangents
  whose w is negated too; thick glass over a checkerboard renders bitwise alike with its
  normals negated but differently in the other winding (RMS 15 times the noise between
  seeds), so the winding decides the inside; and under a non-uniform instance scale the
  turn is decided in world space (both renderers' normals and `objectNormal`); and the
  `Ns.flipped` mode shows the turn (red wound against the normals, green with them, also
  under a mirroring instance and the non-uniform scale; blue without normals). It fails
  on warnings.
- `pathtracer_volumes` checks the pathtracer's volumes against the `preview` renderer and
  analytic values (see [Volumes](#volumes)); it fails on warnings.
- `spectral_caustics` checks hero-wavelength dispersion and the caustics policy against
  analytic values and each other (see [Dispersion and caustics](#dispersion-and-caustics));
  it fails on warnings. `HALCYON_SPECTRAL_IMAGES=<dir>` writes its images as PPMs.
- `subsurface` checks the pathtracer's random-walk subsurface scattering (black and white
  media, a slab's reflectance, bounce counting, the walk's owner, its scale; see
  [Subsurface scattering](#subsurface-scattering)); it fails on warnings. `--bench` times
  walks with and without a dense mesh inside.
- `media` checks the pathtracer's interior media against analytic values (Beer-Lambert
  through volume-only boundaries and glass, one medium at a time, instances, shadow rays,
  single scattering by Henyey-Greenstein, furnaces, a grid volume inside a medium, the
  albedo channel, and the differences from Typhoon: camera misses, the emitter-only
  segment, shadow rays leaving and entering a medium, translucent samples leaving and
  entering it and going on with caustics off; see
  [Interior media](#interior-media)); it fails on warnings. Not tested since the Typhoon
  comparisons went (only `media.sh` covered them): total internal reflection inside a
  medium, and subsurface scattering on a surface inside one.
- `closure_sampling` (no device) checks `sampleSurface()` against Typhoon's
  `Bsdf::SampleSurface()` on hand-built trees (no mix, a mix, nested mixes, a mix under a
  layer, under an add and a multiply, glass mixed with a translucent lobe on both sides,
  subsurface in a mix and under a metal mix): every sample bitwise Typhoon's but a delta
  sample through a mix,
  whose direction, pdf and flags are Typhoon's and whose value Typhoon's divides by each
  mix's probability (exactly where the mixes are above every other composite node),
  which pins the difference from Typhoon; subsurface under the metal mix keeps Typhoon's
  overweight (pinned: kept as Typhoon's, a maintainer decision); and lossless mixes of a Lambertian
  and a mirror
  reflect 1 by BSDF sampling (Typhoon's 1 + the mirror's share × (1 / mix − 1)).
- `mix_delta` renders white furnaces of delta lobes in mixes (see [Materials in the
  pathtracer](#materials-in-the-pathtracer)): a unit sphere in an enclosure emitting
  radiance 1, which only BSDF sampling finds, and under the ambient light (MIS), must
  render to 1: smooth white physicallyBased of metallic 0.3 and 0.5, native and with
  `nativeMaterials` false (bitwise the same image where `HALCYON_NATIVE_BITWISE`), and
  `materialx` documents of a Lambertian mixed with a delta mirror and of a translucent
  lobe mixed with smooth glass (caustics on), every pixel 1 within 1e-4 by BSDF
  sampling. It fails on warnings.
- `normal_rounding` checks the smooth normal's rounding (see
  [Shading context](#shading-context)) on a tessellated sphere of smooth normals, turned
  and scaled by its instance, of white standard_surface metal under the ambient light:
  the `normal` debug mode's mean length is 1 within 2e-9 (3e-11; −1.9e-8 with a float
  `normalize()`), and the central disc renders at roughness 0.04 as at roughness 0.08
  within 0.4% (1.0002; with a float `normalize()` 1.4% darker; over seeds the ratio
  spreads by about 0.04%). It fails on warnings.
- `pathtracer` checks the renderer skeleton where the answer is exact: N one-sample
  renders against one N-sample render, a change restarting the accumulation (bitwise equal
  to a fresh frame's first render), depth and ids against the `preview` renderer's,
  `randomNumberSeed` (-1 renders as 0, the same seed the same image, a seed change
  starting over with the new seed's samples); and statistically, a lone floor under the
  ambient light (colour × ambient), convergence (error after 64 renders well below that
  after 4) and `maxBounces` gathering interreflection.
- `background_ambient` checks the pathtracer's background and ambient light (see
  [Background and ambient light](#background-and-ambient-light)): the background colour
  and image on misses, with their alpha, lighting nothing; furnace tests (a white
  Lambertian sphere and a white scattering volume under the ambient light render to its
  radiance), a floor of albedo × ambient at every `maxBounces` and light sample count, a
  surface without a material lit by it, negative values clamped; and with visible and
  invisible hdris, what misses see and what lights the floor. It expects only the
  missing-material warning.
- The MaterialX tests: `materialx_library` reads the embedded data library with
  `MATERIALX_SEARCH_PATH` pointing nowhere; `materialx_document` (no device) converts
  every material of MaterialX's example documents (`HALCYON_MATERIALX_RESOURCES`, the
  fetched or installed MaterialX's `resources/`) and checks they compile, all but the
  hair's, with the models single native nodes, plus `fileprefix` and relative paths,
  connections, colour spaces, the Usd renames, volume and displacement shaders,
  diagnostics and overrides; `image_files` reads every image format the loader takes
  (written by the test with `stb_image_write.h` and tinyexr), the mip chain, `Sample2D()`
  on files (texel centres, the flip, filters, address modes, mip levels, alpha fill,
  colour conversion) and the cache; `materialx` (device level) checks inline and file
  documents, the properties, overrides in both renderers, geomprop nodes (`geompropvalue`,
  UsdPrimvarReader) on a floor that has the attribute (vertex, primitive, uniform,
  instance, ray-derived; strips with and without it in one frame, so lanes of one batch
  mix) or lacks it (their default, integer and boolean nodes too) or of a name that isn't
  one, an attribute override the floor lacks ((0, 0, 0, 1) in both renderers), an image
  file next to a document, invalid documents, and renders every example material through
  the pathtracer (`HALCYON_MATERIALX_RENDERS=<dir>` writes the images as PPMs).
- `adaptive_sampling` checks the pathtracer's refinement through the device: it refines
  with `accumulation` unset, false or true; `numSamples` and `refinementProgress` (not
  reported by the `preview` renderer or before a render, and an `ANARI_WAIT` query sees
  the queued render); renders stop at `convergedSamplesPerPixel` (bitwise the one-render
  image, also where the last render goes past it); a threshold every pixel passes
  converges them all after `minSamplesBeforeAdaptive` samples and stops the renders; and
  with one only some pass, every pixel shows bitwise what renders without adaptive sampling
  show after some k ≥ `minSamplesBeforeAdaptive` samples (all of them for pixels that
  don't converge), so unconverged pixels keep their sample indices.
- `sample_limit` checks `sampleLimit`: declared with default 256 and minimum 0; an
  hdAnari-style loop (render until `numSamples` reaches the limit hdAnari reads) ends with
  `sampleLimit` unset, set, set over a different `convergedSamplesPerPixel` (which warns
  once per pair of values) and when every pixel converges early; hdAnari-style commits of
  both: both at 256, only `sampleLimit` changed (no warning), only
  `convergedSamplesPerPixel` changed (`sampleLimit`'s 256 decides, hdAnari's loop ends, a
  warning), an explicit `sampleLimit` 256 over 512, both changed and equal; with
  0 renders go past 256 samples, report the true count and the fraction of converged pixels, and stop once
  every pixel converged; and the differentials' scale follows the limit (`dPdx` mode).
- `renderer_extensions` checks each renderer's extensions: both renderers' introspected
  lists exactly, as the SDK's extension structs read them, their `extension` property
  (also for `default`, which reports the `preview` renderer's, and an unknown subtype, which
  reports none), and that the
  device's property, `anariGetDeviceExtensions()` and object info are their union.

### Verification against Typhoon

Until halcyon dropped its OpenUSD dependency (so that it can move into VisRTX), two kinds
of tools checked the pathtracer against Typhoon; `e80511e` is the last commit with them:

- **Differential tests** (`reference_*` ctests, `-DHALCYON_TYPHOON_REFERENCE=ON`): Typhoon's
  own C++, built against OpenUSD in a test harness (`tests/typhoon_reference/`), compared
  function by function with its ports: the sample generator, lights, transport policy,
  surface derivatives and mesh tangents, adaptive sampling, colour management, media,
  subsurface walks and hero-wavelength weights, most of them bitwise. `typhoon_bsdf` ran
  Typhoon's vendored BSDF suite, which needs its integrator.
- **Image comparisons** (`tests/typhoon_compare/`, not ctests): Typhoon's `usdrender`
  against halcyon on Typhoon's MaterialXCpp test scenes (`scenes.sh`, through a USD
  translator) and on hand-built media, subsurface and dispersion scenes (`media.sh`,
  `subsurface.sh`, `spectral.sh`), two seeds each against the noise between seeds.

Where this file says a port matches Typhoon bitwise, or a scene renders within noise of
Typhoon's, those tools measured it. Their last results, what the sweep adapted for open
decisions and the ANARI tools' results (`anariInfo`, `anariRenderTests`, `anariCts`) are
in `docs/typhoon-verification.md`.
