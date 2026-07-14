# ANARI 1.1 CTS Extension Coverage

Source inputs:

- ANARI 1.1 specification PDF, title `ANARI(TM) 1.1.0 - Specification`, generated October 8, 2025.
- ANARI 1.1 extension semantics from PDF section 2.4, object chapters 4-5, and Appendix B.
- Active CTS catalog from `anariCts query-metadata`, after rebuilding the current checkout.
- CTS source under `src/anari_test_scenes/cts/tests/`.

Spec premise: section 2.4 says extensions are observable ANARI API behavior, usually object subtypes but also parameter behavior, function pre/post conditions, or property availability. Appendix B lists 54 KHR extensions in ANARI 1.1. The active CTS catalog has 83 tests and 314 cases.

## Active CTS Size

| Category | Tests | Cases |
| --- | ---: | ---: |
| camera | 4 | 16 |
| frame | 11 | 16 |
| geometry | 37 | 80 |
| instance | 1 | 1 |
| light | 6 | 43 |
| material | 15 | 86 |
| renderer | 3 | 11 |
| sampler | 5 | 51 |
| volume | 1 | 10 |

No active `gltf/*` tests registered in this build, even with `CTS_ENABLE_GLTF=ON` and `ANARI_CTS_GLTF_ASSETS` pointed at `cts/gltf-Sample-Assets`. That is a separate catalog/registration issue; glTF tests also do not declare ANARI extension requirements, so they are not counted as extension coverage here.

## Extensions Currently Tested

The CTS currently gates tests on 36 ANARI 1.1 extension names. A unit test
validates every active gate against the canonical names emitted by the extension
utility generator.

| Extension | CTS coverage |
| --- | --- |
| `KHR_CAMERA_OMNIDIRECTIONAL` | `camera/omnidirectional` exercises `layout` with default and `equirectangular`. |
| `KHR_CAMERA_ORTHOGRAPHIC` | `camera/orthographic` exercises `aspect`, `height`, `near`, `far`. |
| `KHR_CAMERA_PERSPECTIVE` | `camera/camera_general` exercises `direction`, `up`, `imageRegion`; `camera/perspective` exercises `fovy`, `aspect`, `near`, `far`. |
| `KHR_FRAME_ACCUMULATION` | `frame/progressive_rendering` opts into frame accumulation and verifies that repeated renders refine the image. |
| `KHR_FRAME_CHANNEL_ALBEDO` | `frame/frame_albedo_channel` exercises `UFIXED8_VEC3`, `UFIXED8_RGB_SRGB`, `FLOAT32_VEC3`. |
| `KHR_FRAME_CHANNEL_INSTANCE_ID` | `frame/frame_instanceID_channel` exercises explicit instance IDs. |
| `KHR_FRAME_CHANNEL_NORMAL` | `frame/frame_normal_channel` exercises `FIXED16_VEC3`, `FLOAT32_VEC3`. |
| `KHR_FRAME_CHANNEL_OBJECT_ID` | `frame/frame_objectID_channel_surface`, `frame/frame_objectID_channel_group`, `frame/frame_objectID_channel_volume`. |
| `KHR_FRAME_CHANNEL_PRIMITIVE_ID` | `frame/frame_primitiveID_channel` exercises implicit triangle primitive indices. |
| `KHR_FRAME_COMPLETION_CALLBACK` | `frame/frame_completion_callback` behavior check verifies callback firing. |
| `KHR_GEOMETRY_CONE` | 6 tests: basic primitive mode, color, frame color type, global caps, vertex caps, unused indexed vertices. |
| `KHR_GEOMETRY_CURVE` | 5 tests: primitive mode, color, frame color type, global radius, unused indexed vertices. |
| `KHR_GEOMETRY_CYLINDER` | 7 tests: primitive mode, color, frame color type, global caps, global radius, vertex caps, unused indexed vertices. |
| `KHR_GEOMETRY_ISOSURFACE` | `geometry/isosurface` uses structuredRegular field and array `isovalue`. |
| `KHR_GEOMETRY_QUAD` | 6 tests: primitive mode, cube layout, color, frame color type, vertex normal/tangent, unused indexed vertices. |
| `KHR_GEOMETRY_SPHERE` | 5 tests: primitive count/mode, color, frame color type, global radius, unused indexed vertices. |
| `KHR_GEOMETRY_TRIANGLE` | 14 tests across geometry, camera, frame, and instance support scenes. Geometry-specific tests include primitive mode, quad/cube layouts, color, vertex normal/tangent, random attributes, and unused indexed vertices. |
| `KHR_INSTANCE_TRANSFORM` | `instance/instance` uses two transform instances sharing one group. |
| `KHR_LIGHT_DIRECTIONAL` | `light/directional` exercises `direction`, `irradiance`. |
| `KHR_LIGHT_HDRI` | `light/hdri` exercises `up`, `direction`, `scale`; helper supplies radiance image. |
| `KHR_LIGHT_POINT` | `light/point` exercises `position`, `intensity`, `power`. |
| `KHR_LIGHT_QUAD` | `light/quad` exercises position/edge geometry, intensity/power/radiance, side, 1D and 2D intensity distribution. |
| `KHR_LIGHT_RING` | `light/ring` exercises position/direction, radii, opening/falloff, intensity/power/radiance, `c0`, 1D and 2D intensity distribution. |
| `KHR_LIGHT_SPOT` | `light/spot` exercises position/direction, opening/falloff, intensity/power. |
| `KHR_MATERIAL_MATTE` | 3 material tests plus many support scenes; direct tests cover `alphaMode`, `alphaCutoff`, `color`, `opacity`. |
| `KHR_MATERIAL_PHYSICALLY_BASED` | 12 tests cover alpha/baseColor/clearcoat/emissive/ior/iridescence/metallic/roughness/opacity/sheen/specular/thickness/transmission. |
| `KHR_RENDERER_AMBIENT_LIGHT` | `renderer/renderer_ambient` exercises `ambientColor`, `ambientRadiance`. |
| `KHR_RENDERER_BACKGROUND_COLOR` | `renderer/renderer_background_color` exercises several `FLOAT32_VEC4` backgrounds. |
| `KHR_RENDERER_BACKGROUND_IMAGE` | `renderer/renderer_background_image` exercises `ARRAY2D` background. |
| `KHR_SAMPLER_IMAGE1D` | `sampler/image1D` exercises filter, wrap, in/out transform, in/out offset. |
| `KHR_SAMPLER_IMAGE2D` | `sampler/image2D` exercises filter, wrapMode1/2, in/out transform, in/out offset. |
| `KHR_SAMPLER_IMAGE3D` | `sampler/image3D` exercises filter, wrapMode1/2/3, in/out transform, in/out offset. |
| `KHR_SAMPLER_PRIMITIVE` | `sampler/primitive` exercises 1-4 component arrays and offset. |
| `KHR_SAMPLER_TRANSFORM` | `sampler/transform` exercises transform and outOffset. |
| `KHR_SPATIAL_FIELD_STRUCTURED_REGULAR` | `volume/volume`, `geometry/isosurface`, and frame object-ID volume support use structuredRegular fields. |
| `KHR_VOLUME_TRANSFER_FUNCTION1D` | `volume/volume` exercises value field, filter/origin/spacing, valueRange, color, opacity, unitDistance; `frame/frame_objectID_channel_volume` exercises object IDs on `transferFunction1D` volumes. |

## ANARI 1.1 Extensions Not Tested

| Extension | Observable feature missing from CTS |
| --- | --- |
| `KHR_ARRAY1D_REGION` | `ANARIArray1D::region` parameter for changing the valid array region at runtime. |
| `KHR_CAMERA_DEPTH_OF_FIELD` | Camera `apertureRadius` and `focusDistance`. |
| `KHR_CAMERA_MOTION_TRANSFORMATION` | Camera `motion.transform`, `motion.scale`, `motion.rotation`, `motion.translation`, and `time` with shutter/motion blur. |
| `KHR_CAMERA_ROLLING_SHUTTER` | `rollingShutterDirection` and `rollingShutterDuration`. |
| `KHR_CAMERA_SHUTTER` | Camera `shutter` and motion-blur interactions. |
| `KHR_CAMERA_STEREO` | `stereoMode` values `left`, `right`, `sideBySide`, `topBottom`; `interpupillaryDistance`. |
| `KHR_DATA_PARALLEL_MPI` | Frame `mpiCommunicator`. This is hard to image-compare, but still query/behavior-testable. |
| `KHR_DEVICE_SYNCHRONIZATION` | Relaxed API synchronization semantics. Could be query/behavior tested, not image tested. |
| `KHR_GEOMETRY_QUAD_MOTION_DEFORMATION` | Quad `motion.vertex.position`, `motion.vertex.normal`, `motion.vertex.tangent` arrays-of-arrays plus `time`. |
| `KHR_GEOMETRY_TRIANGLE_MOTION_DEFORMATION` | Triangle deformation arrays-of-arrays plus `time`. |
| `KHR_INSTANCE_TRANSFORM_ARRAY` | Matrix-array transforms and array instance IDs on transform instances. |
| `KHR_INSTANCE_MOTION_TRANSFORM` | `motionTransform` instance subtype, `motion.transform`, `time`, and shutter interaction. |
| `KHR_INSTANCE_MOTION_SCALE_ROTATION_TRANSLATION` | `motionScaleRotationTranslation` subtype and decomposed motion arrays. |
| `KHR_LIGHT_PRIMARY_VISIBILITY` | `visible` behavior for area/environment lights. |
| `KHR_RENDERER_DENOISE` | Renderer `denoise` boolean; the runner can request it, but no dedicated Test verifies the behavior. |
| `KHR_SPATIAL_FIELD_NANOVDB` | `nanovdb` field subtype, `data`, and filter. |
| `KHR_SPATIAL_FIELD_STRUCTURED_REGULAR_FILTER_CUBIC` | `structuredRegular.filter = cubic`. |
| `KHR_SPATIAL_FIELD_UNSTRUCTURED` | `unstructured` field subtype and its vertex/cell topology/data parameters. |

## Partial Coverage Inside Tested Areas

Geometry coverage is broad for subtype smoke/rendering, but shallow across legal parameter types. The tests cover indexed vs soup, colors, some attributes, caps/radii, normal/tangent on triangle/quad, and depth/color channels. They do not cover uniform geometry `color` or `attribute0..3`, face-varying data on triangle/quad, most allowed color element types, `UINT64` indices, scalar `isovalue`, or motion deformation.

Material coverage is one of the stronger areas. It tests matte and physicallyBased parameters using constants, attributes, samplers, and unset defaults for most visible PBR knobs. Gaps remain for physicallyBased `occlusion`, full texture/element-type diversity, interaction tests between multiple PBR features, and edge/default-value assertions beyond image comparison.

Sampler tests cover filter, wrap, transform, offsets, and primitive sampler dimensionality. They do not cover the large allowed image/array element-type matrix, integer/fixed/sRGB variants systematically, `clampToBorder`/`borderColor` from the repo's non-Appendix-B sampler extension, or error/default behavior.

Camera tests cover perspective, orthographic, and omnidirectional basics, but do not cover depth of field, shutter, rolling shutter, stereo, or motion transformation. General `position/direction/up/imageRegion` is only tested through a perspective camera.

Frame tests cover color/depth plus albedo/normal/primitive/object/instance ID channels, callback behavior, and opt-in frame accumulation. Gaps are format breadth, default ID behavior after the ANARI 1.1 change to no implicit IDs, and `UINT64` ID paths.

Renderer tests cover ambient light, background color, and background image. They do not have a dedicated denoise Test, nor do they test precedence/typing when both background color and image are supported.

Light tests cover the main subtype parameter surfaces for directional, point, spot, quad, ring, and hdri lights. Gaps are `KHR_LIGHT_PRIMARY_VISIBILITY`, HDRI `color`/`radiance`/`layout` variations beyond the helper default, and broader energy-unit/default interactions.

Volume/spatial-field coverage tests structuredRegular plus transferFunction1D, including origin/spacing/filter, valueRange, color/opacity modes, and unitDistance. It does not cover cubic filtering, nanovdb, unstructured fields, structuredRegular data element-type breadth, transfer-function `visible`, or `FLOAT64_BOX1` valueRange.

Instance coverage only tests the basic `transform` subtype with one explicit matrix. It does not cover instance color/attribute parameters, transform arrays, motion instance subtypes, instance `id` arrays, or motion blur/shutter interactions.

## Priority Fix List

1. Add focused CTS tests for the 1.1 new extensions: MPI query/behavior, transform arrays, light primary visibility, denoise, nanovdb, cubic filter, and unstructured field; isosurface already has a Test.
2. Add motion/shutter family tests as a group: camera shutter, camera rolling shutter, camera motion transformation, triangle/quad deformation, instance motion transform, and instance decomposed motion.
3. Deepen typed-format coverage for geometry attributes/colors, samplers, frame channels, volume color/valueRange, and ID channels.
