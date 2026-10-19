# Halcyon

An ANARI rendering device that mirrors every scene object into ISPC and shades on the CPU
with Embree. This glossary fixes the words used for the rendering pipeline's concepts.

## Language

### Frames and screen space

**First visible surface**:
The nearest surface hit along a primary ray whose opacity is above zero; mask cut-outs are
skipped. Volumes in front of it don't change which surface it is, even where they hide it.
_Avoid_: primary hit, closest hit, nearest surface

**Visible hit**:
The first visible surface, or a volume entered before it. Rays that meet neither are misses.
It is what the depth and id channels describe.

**First non-specular hit**:
Along one pathtracer path, the first surface it scatters at whose closure isn't
specular-only (every lobe a delta lobe); null events and specular-only surfaces are looked
past. It is what the pathtracer's albedo and normal channels describe.
_Avoid_: first diffuse hit, primary hit

**Hit buffer**:
Per-pixel record of the first visible surface (distance along the ray, world shading normal, hit flag)
kept for screen-space passes, whatever volumes lie in front of it. Pixels with no first
visible surface hold no surface.
_Avoid_: G-buffer, depth buffer (that is the app-facing depth channel)

**Ambient occlusion**:
Darkening of the first visible surface by nearby geometry, also where a volume lies in front
of it, estimated in screen space from the hit buffer. It never darkens volumes or the
background.
_Avoid_: AO shadowing, SSAO (as a noun for the effect; it names the method)

**Occlusion factor**:
A value in [0, 1] per pixel: 1 for an unoccluded first visible surface, 0 for fully occluded,
1 where there is no first visible surface.
_Avoid_: AO term, AO value, occlusion amount

**Occlusion radius**:
The world-space distance within which geometry counts as an occluder. Derived from the world
bounds when the app doesn't set a positive one.
_Avoid_: AO distance, sample radius, kernel radius

**Screen-space pass**:
A pass over every pixel of a frame that reads other pixels' buffers instead of tracing rays.
_Avoid_: post-process, filter pass

### The pathtracer

**Path vertex**:
A point where a pathtracer path scatters: a surface it shades, or a collision in a volume
or an interior medium. Every vertex counts as a bounce, except a subsurface walk's entry.
_Avoid_: hit, bounce (a bounce counts vertices)

**Null event**:
A surface a path passes through without scattering: a mask cut-out, a surface whose
presence it didn't draw, or a volume-only boundary. It isn't a path vertex.
_Avoid_: pass-through, skip

**Emitter-only segment**:
The ray leaving the last path vertex that may scatter: it only gathers the lights it
finds, and a surface it hits ends the path.
_Avoid_: shadow ray (that is next-event estimation's), last bounce

**Closure**:
What a material gives a path vertex: a tree of lobes, its emission, its presence and its
interior medium.
_Avoid_: BSDF (one part of it), shader, material instance

**Lobe**:
One scattering function at a leaf of a closure (diffuse, translucent, dielectric,
conductor, sheen, subsurface, ...). A delta lobe scatters into a single direction.
_Avoid_: BRDF, layer (a layer is a node joining two lobes)

**Caustic sample**:
A specular or boundary-crossing sample taken after a diffuse-like one, a translucent
sample excepted. With caustics off it ends the path, and caustic-class lobes are pruned
so it is rarely drawn.
_Avoid_: caustic path (a path is caustic from its first caustic sample on)

**Interior medium**:
The homogeneous medium inside a thick surface's object, which a path is in from crossing
into that object until crossing out of it. Its object is its owner.
_Avoid_: volume (that is an ANARI volume object), participating medium

**Material kind**:
How the pathtracer shades a material: natively, or through its graph in C++. It is
decided per material from its subtype and parameters.
_Avoid_: material type, subtype (the ANARI subtype is something else)

**Native material**:
A material of a native kind: the path loop computes what its graph's closure would give,
without calling into C++, and must agree with the graph.
_Avoid_: fast path, ISPC material

**Batched material call**:
One call from the path loop into C++ for every lane of a gang whose material isn't
native, at one path vertex.
_Avoid_: callout, FFI call, per-lane call
