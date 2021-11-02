#pragma once

#include "anari/anari.h"

#include <utility>
#include <stdint.h>

constexpr bool isObject(ANARIDataType type) {
    return type >= ANARI_OBJECT && type <= ANARI_WORLD;
}

template<int type>
struct ANARITypeProperties { };

#define ANARI_ENUM_TRAITS(ENUM, TYPE, COMPONENTS, NAME) \
template<>                                              \
struct ANARITypeProperties<ENUM> {                      \
    using base_type = TYPE;                             \
    static const int components = COMPONENTS;           \
    using array_type = base_type[components];           \
    static constexpr const char* enum_name = #ENUM ;    \
    static constexpr const char* type_name = #TYPE ;    \
    static constexpr const char* array_name = #TYPE "[" #COMPONENTS "]";\
    static constexpr const char* var_name = NAME;       \
};

#define ANARI_ENUM_TRAITS_VECTOR(EXT, TYPE)                 \
ANARI_ENUM_TRAITS(ANARI_##EXT##_VEC2, TYPE, 2, "vector2"#EXT)  \
ANARI_ENUM_TRAITS(ANARI_##EXT##_VEC3, TYPE, 3, "vector3"#EXT)  \
ANARI_ENUM_TRAITS(ANARI_##EXT##_VEC4, TYPE, 4, "vector4"#EXT)

#define ANARI_ENUM_TRAITS_BOX(EXT, TYPE)                \
ANARI_ENUM_TRAITS(ANARI_##EXT##_BOX1, TYPE, 2, "box1"#EXT) \
ANARI_ENUM_TRAITS(ANARI_##EXT##_BOX2, TYPE, 4, "box2"#EXT) \
ANARI_ENUM_TRAITS(ANARI_##EXT##_BOX3, TYPE, 6, "box3"#EXT) \
ANARI_ENUM_TRAITS(ANARI_##EXT##_BOX4, TYPE, 8, "box4"#EXT)

////////////////////////////////////////
// misc types
////////////////////////////////////////

ANARI_ENUM_TRAITS(ANARI_DEVICE, ANARIDevice, 1, "device")
ANARI_ENUM_TRAITS(ANARI_STRING, const char*, 1, "string")
ANARI_ENUM_TRAITS(ANARI_VOID_POINTER, void*, 1, "ptr")
ANARI_ENUM_TRAITS(ANARI_BOOL, int32_t, 1, "boolean")
ANARI_ENUM_TRAITS(ANARI_UNKNOWN, int, 1, "unknown")

////////////////////////////////////////
// object types
////////////////////////////////////////

ANARI_ENUM_TRAITS(ANARI_OBJECT, ANARIObject, 1, "object")
ANARI_ENUM_TRAITS(ANARI_ARRAY, ANARIArray, 1, "array")
ANARI_ENUM_TRAITS(ANARI_ARRAY1D, ANARIArray1D, 1, "array1d")
ANARI_ENUM_TRAITS(ANARI_ARRAY2D, ANARIArray2D, 1, "array2d")
ANARI_ENUM_TRAITS(ANARI_ARRAY3D, ANARIArray3D, 1, "array3d")
ANARI_ENUM_TRAITS(ANARI_CAMERA, ANARICamera, 1, "camera")
ANARI_ENUM_TRAITS(ANARI_FRAME, ANARIFrame, 1, "framebuffer")
ANARI_ENUM_TRAITS(ANARI_GEOMETRY, ANARIGeometry, 1, "geometry")
ANARI_ENUM_TRAITS(ANARI_GROUP, ANARIGroup, 1, "group")
ANARI_ENUM_TRAITS(ANARI_INSTANCE, ANARIInstance, 1, "instance")
ANARI_ENUM_TRAITS(ANARI_LIGHT, ANARILight, 1, "light")
ANARI_ENUM_TRAITS(ANARI_MATERIAL, ANARIMaterial, 1, "material")
ANARI_ENUM_TRAITS(ANARI_RENDERER, ANARIRenderer, 1, "renderer")
ANARI_ENUM_TRAITS(ANARI_SAMPLER, ANARISampler, 1, "sampler")
ANARI_ENUM_TRAITS(ANARI_SURFACE, ANARISurface, 1, "surface")
ANARI_ENUM_TRAITS(ANARI_SPATIAL_FIELD, ANARISpatialField, 1, "volume")
ANARI_ENUM_TRAITS(ANARI_VOLUME, ANARIVolume, 1, "volumetric_model")
ANARI_ENUM_TRAITS(ANARI_WORLD, ANARIWorld, 1, "world")

////////////////////////////////////////
// float types
////////////////////////////////////////

ANARI_ENUM_TRAITS(ANARI_FLOAT64, double, 1, "scalar")

ANARI_ENUM_TRAITS(ANARI_FLOAT32, float, 1, "scalar")
ANARI_ENUM_TRAITS_VECTOR(FLOAT32, float)

ANARI_ENUM_TRAITS(ANARI_FLOAT32_MAT2, float, 4, "linear2")
ANARI_ENUM_TRAITS(ANARI_FLOAT32_MAT3, float, 9, "linear3")
ANARI_ENUM_TRAITS(ANARI_FLOAT32_MAT2x3, float, 6, "affine2")
ANARI_ENUM_TRAITS(ANARI_FLOAT32_MAT3x4, float, 12, "affine3")

ANARI_ENUM_TRAITS_BOX(FLOAT32, float)

////////////////////////////////////////
// int types
////////////////////////////////////////

ANARI_ENUM_TRAITS(ANARI_INT32, int32_t, 1, "scalar")
ANARI_ENUM_TRAITS_VECTOR(INT32, int32_t)
ANARI_ENUM_TRAITS_BOX(INT32, int32_t)

////////////////////////////////////////
// uint types
////////////////////////////////////////

ANARI_ENUM_TRAITS(ANARI_UINT32, uint32_t, 1, "scalar")
ANARI_ENUM_TRAITS_VECTOR(UINT32, uint32_t)

////////////////////////////////////////
// char types
////////////////////////////////////////

ANARI_ENUM_TRAITS(ANARI_INT8, signed char, 1, "scalar")
ANARI_ENUM_TRAITS(ANARI_UINT8, unsigned char, 1, "scalar")
ANARI_ENUM_TRAITS_VECTOR(UINT8, unsigned char)

////////////////////////////////////////
// short types
////////////////////////////////////////

ANARI_ENUM_TRAITS(ANARI_INT16, int16_t, 1, "scalar")
ANARI_ENUM_TRAITS(ANARI_UINT16, uint16_t, 1, "scalar")
ANARI_ENUM_TRAITS_VECTOR(UINT16, uint16_t)

////////////////////////////////////////
// long types
////////////////////////////////////////

ANARI_ENUM_TRAITS(ANARI_INT64, int64_t, 1, "scalar")
ANARI_ENUM_TRAITS_VECTOR(INT64, int64_t)

////////////////////////////////////////
// ulong types
////////////////////////////////////////

ANARI_ENUM_TRAITS(ANARI_UINT64, uint64_t, 1, "scalar")
ANARI_ENUM_TRAITS_VECTOR(UINT64, uint64_t)




template <typename R, typename F, typename... Args>
R anari_type_invoke(ANARIDataType type, Args&&... args) {
    #define ANARI_TYPE_INVOKE_CASE(X) case X: return F::template invoke<X>(std::forward<Args>(args)...);

    switch (type) {
        ANARI_TYPE_INVOKE_CASE(ANARI_DEVICE)
        ANARI_TYPE_INVOKE_CASE(ANARI_VOID_POINTER)
        ANARI_TYPE_INVOKE_CASE(ANARI_BOOL)
        ANARI_TYPE_INVOKE_CASE(ANARI_OBJECT)
        ANARI_TYPE_INVOKE_CASE(ANARI_ARRAY)
        ANARI_TYPE_INVOKE_CASE(ANARI_ARRAY1D)
        ANARI_TYPE_INVOKE_CASE(ANARI_ARRAY2D)
        ANARI_TYPE_INVOKE_CASE(ANARI_ARRAY3D)
        ANARI_TYPE_INVOKE_CASE(ANARI_CAMERA)
        ANARI_TYPE_INVOKE_CASE(ANARI_FRAME)
        ANARI_TYPE_INVOKE_CASE(ANARI_GEOMETRY)
        ANARI_TYPE_INVOKE_CASE(ANARI_GROUP)
        ANARI_TYPE_INVOKE_CASE(ANARI_INSTANCE)
        ANARI_TYPE_INVOKE_CASE(ANARI_LIGHT)
        ANARI_TYPE_INVOKE_CASE(ANARI_MATERIAL)
        ANARI_TYPE_INVOKE_CASE(ANARI_RENDERER)
        ANARI_TYPE_INVOKE_CASE(ANARI_SAMPLER)
        ANARI_TYPE_INVOKE_CASE(ANARI_SURFACE)
        ANARI_TYPE_INVOKE_CASE(ANARI_SPATIAL_FIELD)
        ANARI_TYPE_INVOKE_CASE(ANARI_VOLUME)
        ANARI_TYPE_INVOKE_CASE(ANARI_WORLD)
        ANARI_TYPE_INVOKE_CASE(ANARI_STRING)
        ANARI_TYPE_INVOKE_CASE(ANARI_INT8)
        ANARI_TYPE_INVOKE_CASE(ANARI_UINT8)
        ANARI_TYPE_INVOKE_CASE(ANARI_UINT8_VEC2)
        ANARI_TYPE_INVOKE_CASE(ANARI_UINT8_VEC3)
        ANARI_TYPE_INVOKE_CASE(ANARI_UINT8_VEC4)
        ANARI_TYPE_INVOKE_CASE(ANARI_INT16)
        ANARI_TYPE_INVOKE_CASE(ANARI_UINT16)
        ANARI_TYPE_INVOKE_CASE(ANARI_UINT16_VEC2)
        ANARI_TYPE_INVOKE_CASE(ANARI_UINT16_VEC3)
        ANARI_TYPE_INVOKE_CASE(ANARI_UINT16_VEC4)
        ANARI_TYPE_INVOKE_CASE(ANARI_INT32)
        ANARI_TYPE_INVOKE_CASE(ANARI_INT32_VEC2)
        ANARI_TYPE_INVOKE_CASE(ANARI_INT32_VEC3)
        ANARI_TYPE_INVOKE_CASE(ANARI_INT32_VEC4)
        ANARI_TYPE_INVOKE_CASE(ANARI_UINT32)
        ANARI_TYPE_INVOKE_CASE(ANARI_UINT32_VEC2)
        ANARI_TYPE_INVOKE_CASE(ANARI_UINT32_VEC3)
        ANARI_TYPE_INVOKE_CASE(ANARI_UINT32_VEC4)
        ANARI_TYPE_INVOKE_CASE(ANARI_INT64)
        ANARI_TYPE_INVOKE_CASE(ANARI_INT64_VEC2)
        ANARI_TYPE_INVOKE_CASE(ANARI_INT64_VEC3)
        ANARI_TYPE_INVOKE_CASE(ANARI_INT64_VEC4)
        ANARI_TYPE_INVOKE_CASE(ANARI_UINT64)
        ANARI_TYPE_INVOKE_CASE(ANARI_UINT64_VEC2)
        ANARI_TYPE_INVOKE_CASE(ANARI_UINT64_VEC3)
        ANARI_TYPE_INVOKE_CASE(ANARI_UINT64_VEC4)
        ANARI_TYPE_INVOKE_CASE(ANARI_FLOAT32)
        ANARI_TYPE_INVOKE_CASE(ANARI_FLOAT32_VEC2)
        ANARI_TYPE_INVOKE_CASE(ANARI_FLOAT32_VEC3)
        ANARI_TYPE_INVOKE_CASE(ANARI_FLOAT32_VEC4)
        ANARI_TYPE_INVOKE_CASE(ANARI_FLOAT64)
        ANARI_TYPE_INVOKE_CASE(ANARI_INT32_BOX1)
        ANARI_TYPE_INVOKE_CASE(ANARI_INT32_BOX2)
        ANARI_TYPE_INVOKE_CASE(ANARI_INT32_BOX3)
        ANARI_TYPE_INVOKE_CASE(ANARI_INT32_BOX4)
        ANARI_TYPE_INVOKE_CASE(ANARI_FLOAT32_BOX1)
        ANARI_TYPE_INVOKE_CASE(ANARI_FLOAT32_BOX2)
        ANARI_TYPE_INVOKE_CASE(ANARI_FLOAT32_BOX3)
        ANARI_TYPE_INVOKE_CASE(ANARI_FLOAT32_BOX4)
        ANARI_TYPE_INVOKE_CASE(ANARI_FLOAT32_MAT2)
        ANARI_TYPE_INVOKE_CASE(ANARI_FLOAT32_MAT3)
        ANARI_TYPE_INVOKE_CASE(ANARI_FLOAT32_MAT2x3)
        ANARI_TYPE_INVOKE_CASE(ANARI_FLOAT32_MAT3x4)
        default:
        ANARI_TYPE_INVOKE_CASE(ANARI_UNKNOWN)
    }

    #undef ANARI_TYPE_INVOKE_CASE
}
