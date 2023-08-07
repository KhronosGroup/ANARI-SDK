// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// This file was generated by generate_extension_utility.py
// Don't make changes to this directly

#pragma once
#include <anari/anari.h>
typedef struct {
   int ANARI_KHR_INSTANCE_TRANSFORM;
   int ANARI_KHR_CAMERA_OMNIDIRECTIONAL;
   int ANARI_KHR_CAMERA_ORTHOGRAPHIC;
   int ANARI_KHR_CAMERA_PERSPECTIVE;
   int ANARI_KHR_GEOMETRY_CONE;
   int ANARI_KHR_GEOMETRY_CURVE;
   int ANARI_KHR_GEOMETRY_CYLINDER;
   int ANARI_KHR_GEOMETRY_QUAD;
   int ANARI_KHR_GEOMETRY_SPHERE;
   int ANARI_KHR_GEOMETRY_TRIANGLE;
   int ANARI_KHR_LIGHT_DIRECTIONAL;
   int ANARI_KHR_LIGHT_POINT;
   int ANARI_KHR_LIGHT_SPOT;
   int ANARI_KHR_MATERIAL_MATTE;
   int ANARI_KHR_MATERIAL_TRANSPARENT_MATTE;
   int ANARI_KHR_SAMPLER_IMAGE1D;
   int ANARI_KHR_SAMPLER_IMAGE2D;
   int ANARI_KHR_SAMPLER_IMAGE3D;
   int ANARI_KHR_SAMPLER_PRIMITIVE;
   int ANARI_KHR_SAMPLER_TRANSFORM;
   int ANARI_KHR_SPATIAL_FIELD_STRUCTURED_REGULAR;
   int ANARI_KHR_VOLUME_TRANSFER_FUNCTION1D;
   int ANARI_KHR_LIGHT_RING;
   int ANARI_KHR_LIGHT_QUAD;
   int ANARI_KHR_LIGHT_HDRI;
   int ANARI_KHR_MATERIAL_PHYSICALLY_BASED;
   int ANARI_KHR_FRAME_CONTINUATION;
   int ANARI_KHR_AUXILIARY_BUFFERS;
   int ANARI_KHR_AREA_LIGHTS;
   int ANARI_KHR_STOCHASTIC_RENDERING;
   int ANARI_KHR_TRANSFORMATION_MOTION_BLUR;
   int ANARI_KHR_ARRAY1D_REGION;
   int ANARI_KHR_RENDERER_AMBIENT_LIGHT;
   int ANARI_KHR_RENDERER_BACKGROUND_COLOR;
   int ANARI_KHR_RENDERER_BACKGROUND_IMAGE;
   int ANARI_EXP_VOLUME_SAMPLE_RATE;
} ANARIExtensions;
int anariGetDeviceExtensionStruct(ANARIExtensions *extensions, ANARILibrary library, const char *deviceName);
int anariGetObjectExtensionStruct(ANARIExtensions *extensions, ANARIDevice device, ANARIDataType objectType, const char *objectName);
int anariGetInstanceExtensionStruct(ANARIExtensions *extensions, ANARIDevice device, ANARIObject object);
#ifdef ANARI_EXTENSION_UTILITY_IMPL
#include <string.h>
static int extension_hash(const char *str) {
   static const uint32_t table[] = {0x4f4e0001u,0x42410002u,0x53520003u,0x4a490004u,0x605f0005u,0x4c450006u,0x5958000du,0x0u,0x0u,0x0u,0x0u,0x0u,0x49480023u,0x5150000eu,0x605f000fu,0x57560010u,0x504f0011u,0x4d4c0012u,0x56550013u,0x4e4d0014u,0x46450015u,0x605f0016u,0x54530017u,0x42410018u,0x4e4d0019u,0x5150001au,0x4d4c001bu,0x4645001cu,0x605f001du,0x5352001eu,0x4241001fu,0x55540020u,0x46450021u,0x1000022u,0x80000023u,0x53520024u,0x605f0025u,0x57410026u,0x5652003cu,0x0u,0x42410073u,0x0u,0x0u,0x535200a5u,0x464500b7u,0x0u,0x4f4e00fcu,0x0u,0x0u,0x4a49010eu,0x42410143u,0x0u,0x0u,0x0u,0x0u,0x46450179u,0x554101abu,0x53520222u,0x0u,0x504f023cu,0x53450040u,0x0u,0x0u,0x59580063u,0x4241004eu,0x0u,0x0u,0x0u,0x0u,0x0u,0x0u,0x0u,0x0u,0x0u,0x0u,0x0u,0x0u,0x42410057u,0x605f004fu,0x4d4c0050u,0x4a490051u,0x48470052u,0x49480053u,0x55540054u,0x54530055u,0x1000056u,0x8000001cu,0x5a590058u,0x32310059u,0x4544005au,0x605f005bu,0x5352005cu,0x4645005du,0x4847005eu,0x4a49005fu,0x504f0060u,0x4f4e0061u,0x1000062u,0x8000001fu,0x4a490064u,0x4d4c0065u,0x4a490066u,0x42410067u,0x53520068u,0x5a590069u,0x605f006au,0x4342006bu,0x5655006cu,0x4746006du,0x4746006eu,0x4645006fu,0x53520070u,0x54530071u,0x1000072u,0x8000001bu,0x4e4d0074u,0x46450075u,0x53520076u,0x42410077u,0x605f0078u,0x514f0079u,0x534d007bu,0x4645009au,0x4f4e0081u,0x0u,0x0u,0x0u,0x0u,0x5554008fu,0x4a490082u,0x45440083u,0x4a490084u,0x53520085u,0x46450086u,0x44430087u,0x55540088u,0x4a490089u,0x504f008au,0x4f4e008bu,0x4241008cu,0x4d4c008du,0x100008eu,0x80000001u,0x49480090u,0x504f0091u,0x48470092u,0x53520093u,0x42410094u,0x51500095u,0x49480096u,0x4a490097u,0x44430098u,0x1000099u,0x80000002u,0x5352009bu,0x5453009cu,0x5150009du,0x4645009eu,0x4443009fu,0x555400a0u,0x4a4900a1u,0x575600a2u,0x464500a3u,0x10000a4u,0x80000003u,0x424100a6u,0x4e4d00a7u,0x464500a8u,0x605f00a9u,0x444300aau,0x504f00abu,0x4f4e00acu,0x555400adu,0x4a4900aeu,0x4f4e00afu,0x565500b0u,0x424100b1u,0x555400b2u,0x4a4900b3u,0x504f00b4u,0x4f4e00b5u,0x10000b6u,0x8000001au,0x504f00b8u,0x4e4d00b9u,0x464500bau,0x555400bbu,0x535200bcu,0x5a5900bdu,0x605f00beu,0x554300bfu,0x5a4f00d1u,0x0u,0x0u,0x0u,0x0u,0x0u,0x0u,0x0u,0x0u,0x0u,0x0u,0x0u,0x0u,0x0u,0x565500eau,0x0u,0x515000eeu,0x535200f4u,0x4f4e00dcu,0x0u,0x0u,0x0u,0x0u,0x0u,0x535200dfu,0x0u,0x0u,0x0u,0x4d4c00e3u,0x464500ddu,0x10000deu,0x80000004u,0x575600e0u,0x464500e1u,0x10000e2u,0x80000005u,0x4a4900e4u,0x4f4e00e5u,0x454400e6u,0x464500e7u,0x535200e8u,0x10000e9u,0x80000006u,0x424100ebu,0x454400ecu,0x10000edu,0x80000007u,0x494800efu,0x464500f0u,0x535200f1u,0x464500f2u,0x10000f3u,0x80000008u,0x4a4900f5u,0x424100f6u,0x4f4e00f7u,0x484700f8u,0x4d4c00f9u,0x464500fau,0x10000fbu,0x80000009u,0x545300fdu,0x555400feu,0x424100ffu,0x4f4e0100u,0x44430101u,0x46450102u,0x605f0103u,0x55540104u,0x53520105u,0x42410106u,0x4f4e0107u,0x54530108u,0x47460109u,0x504f010au,0x5352010bu,0x4e4d010cu,0x100010du,0x80000000u,0x4847010fu,0x49480110u,0x55540111u,0x605f0112u,0x54440113u,0x4a490123u,0x0u,0x0u,0x0u,0x4544012eu,0x0u,0x0u,0x0u,0x0u,0x0u,0x0u,0x0u,0x504f0132u,0x56550137u,0x4a49013bu,0x5150013fu,0x53520124u,0x46450125u,0x44430126u,0x55540127u,0x4a490128u,0x504f0129u,0x4f4e012au,0x4241012bu,0x4d4c012cu,0x100012du,0x8000000au,0x5352012fu,0x4a490130u,0x1000131u,0x80000018u,0x4a490133u,0x4f4e0134u,0x55540135u,0x1000136u,0x8000000bu,0x42410138u,0x45440139u,0x100013au,0x80000017u,0x4f4e013cu,0x4847013du,0x100013eu,0x80000016u,0x504f0140u,0x55540141u,0x1000142u,0x8000000cu,0x55540144u,0x46450145u,0x53520146u,0x4a490147u,0x42410148u,0x4d4c0149u,0x605f014au,0x554d014bu,0x42410153u,0x0u,0x0u,0x49480158u,0x0u,0x0u,0x0u,0x53520168u,0x55540154u,0x55540155u,0x46450156u,0x1000157u,0x8000000du,0x5a590159u,0x5453015au,0x4a49015bu,0x4443015cu,0x4241015du,0x4d4c015eu,0x4d4c015fu,0x5a590160u,0x605f0161u,0x43420162u,0x42410163u,0x54530164u,0x46450165u,0x45440166u,0x1000167u,0x80000019u,0x42410169u,0x4f4e016au,0x5453016bu,0x5150016cu,0x4241016du,0x5352016eu,0x4645016fu,0x4f4e0170u,0x55540171u,0x605f0172u,0x4e4d0173u,0x42410174u,0x55540175u,0x55540176u,0x46450177u,0x1000178u,0x8000000eu,0x4f4e017au,0x4544017bu,0x4645017cu,0x5352017du,0x4645017eu,0x5352017fu,0x605f0180u,0x43410181u,0x4e4d0183u,0x42410190u,0x43420184u,0x4a490185u,0x46450186u,0x4f4e0187u,0x55540188u,0x605f0189u,0x4d4c018au,0x4a49018bu,0x4847018cu,0x4948018du,0x5554018eu,0x100018fu,0x80000020u,0x44430191u,0x4c4b0192u,0x48470193u,0x53520194u,0x504f0195u,0x56550196u,0x4f4e0197u,0x45440198u,0x605f0199u,0x4a43019au,0x504f01a1u,0x0u,0x0u,0x0u,0x0u,0x0u,0x4e4d01a6u,0x4d4c01a2u,0x504f01a3u,0x535201a4u,0x10001a5u,0x80000021u,0x424101a7u,0x484701a8u,0x464501a9u,0x10001aau,0x80000022u,0x4e4d01bfu,0x0u,0x0u,0x0u,0x0u,0x0u,0x0u,0x0u,0x0u,0x0u,0x0u,0x0u,0x0u,0x0u,0x0u,0x424101f0u,0x0u,0x0u,0x0u,0x504f020fu,0x515001c0u,0x4d4c01c1u,0x464501c2u,0x535201c3u,0x605f01c4u,0x554901c5u,0x4e4d01d1u,0x0u,0x0u,0x0u,0x0u,0x0u,0x0u,0x535201deu,0x0u,0x0u,0x0u,0x535201e7u,0x424101d2u,0x484701d3u,0x464501d4u,0x343101d5u,0x454401d8u,0x454401dau,0x454401dcu,0x10001d9u,0x8000000fu,0x10001dbu,0x80000010u,0x10001ddu,0x80000011u,0x4a4901dfu,0x4e4d01e0u,0x4a4901e1u,0x555401e2u,0x4a4901e3u,0x575601e4u,0x464501e5u,0x10001e6u,0x80000012u,0x424101e8u,0x4f4e01e9u,0x545301eau,0x474601ebu,0x504f01ecu,0x535201edu,0x4e4d01eeu,0x10001efu,0x80000013u,0x555401f1u,0x4a4901f2u,0x424101f3u,0x4d4c01f4u,0x605f01f5u,0x474601f6u,0x4a4901f7u,0x464501f8u,0x4d4c01f9u,0x454401fau,0x605f01fbu,0x545301fcu,0x555401fdu,0x535201feu,0x565501ffu,0x44430200u,0x55540201u,0x56550202u,0x53520203u,0x46450204u,0x45440205u,0x605f0206u,0x53520207u,0x46450208u,0x48470209u,0x5655020au,0x4d4c020bu,0x4241020cu,0x5352020du,0x100020eu,0x80000014u,0x44430210u,0x49480211u,0x42410212u,0x54530213u,0x55540214u,0x4a490215u,0x44430216u,0x605f0217u,0x53520218u,0x46450219u,0x4f4e021au,0x4544021bu,0x4645021cu,0x5352021du,0x4a49021eu,0x4f4e021fu,0x48470220u,0x1000221u,0x8000001du,0x42410223u,0x4f4e0224u,0x54530225u,0x47460226u,0x504f0227u,0x53520228u,0x4e4d0229u,0x4241022au,0x5554022bu,0x4a49022cu,0x504f022du,0x4f4e022eu,0x605f022fu,0x4e4d0230u,0x504f0231u,0x55540232u,0x4a490233u,0x504f0234u,0x4f4e0235u,0x605f0236u,0x43420237u,0x4d4c0238u,0x56550239u,0x5352023au,0x100023bu,0x8000001eu,0x4d4c023du,0x5655023eu,0x4e4d023fu,0x46450240u,0x605f0241u,0x55540242u,0x53520243u,0x42410244u,0x4f4e0245u,0x54530246u,0x47460247u,0x46450248u,0x53520249u,0x605f024au,0x4746024bu,0x5655024cu,0x4f4e024du,0x4443024eu,0x5554024fu,0x4a490250u,0x504f0251u,0x4f4e0252u,0x32310253u,0x45440254u,0x1000255u,0x80000015u};
   uint32_t cur = 0x42410000u;
   for(int i = 0;cur!=0;++i) {
      uint32_t idx = cur&0xFFFFu;
      uint32_t low = (cur>>16u)&0xFFu;
      uint32_t high = (cur>>24u)&0xFFu;
      uint32_t c = (uint32_t)str[i];
      if(c>=low && c<high) {
         cur = table[idx+c-low];
      } else {
         break;
      }
      if(cur&0x80000000u) {
         return cur&0xFFFFu;
      }
      if(str[i]==0) {
         break;
      }
   }
   return -1;
}
static void fillExtensionStruct(ANARIExtensions *extensions, const char *const *list) {
    memset(extensions, 0, sizeof(ANARIExtensions));
    for(const char *const *i = list;*i!=NULL;++i) {
        switch(extension_hash(*i)) {
            case 0: extensions->ANARI_KHR_INSTANCE_TRANSFORM = 1; break;
            case 1: extensions->ANARI_KHR_CAMERA_OMNIDIRECTIONAL = 1; break;
            case 2: extensions->ANARI_KHR_CAMERA_ORTHOGRAPHIC = 1; break;
            case 3: extensions->ANARI_KHR_CAMERA_PERSPECTIVE = 1; break;
            case 4: extensions->ANARI_KHR_GEOMETRY_CONE = 1; break;
            case 5: extensions->ANARI_KHR_GEOMETRY_CURVE = 1; break;
            case 6: extensions->ANARI_KHR_GEOMETRY_CYLINDER = 1; break;
            case 7: extensions->ANARI_KHR_GEOMETRY_QUAD = 1; break;
            case 8: extensions->ANARI_KHR_GEOMETRY_SPHERE = 1; break;
            case 9: extensions->ANARI_KHR_GEOMETRY_TRIANGLE = 1; break;
            case 10: extensions->ANARI_KHR_LIGHT_DIRECTIONAL = 1; break;
            case 11: extensions->ANARI_KHR_LIGHT_POINT = 1; break;
            case 12: extensions->ANARI_KHR_LIGHT_SPOT = 1; break;
            case 13: extensions->ANARI_KHR_MATERIAL_MATTE = 1; break;
            case 14: extensions->ANARI_KHR_MATERIAL_TRANSPARENT_MATTE = 1; break;
            case 15: extensions->ANARI_KHR_SAMPLER_IMAGE1D = 1; break;
            case 16: extensions->ANARI_KHR_SAMPLER_IMAGE2D = 1; break;
            case 17: extensions->ANARI_KHR_SAMPLER_IMAGE3D = 1; break;
            case 18: extensions->ANARI_KHR_SAMPLER_PRIMITIVE = 1; break;
            case 19: extensions->ANARI_KHR_SAMPLER_TRANSFORM = 1; break;
            case 20: extensions->ANARI_KHR_SPATIAL_FIELD_STRUCTURED_REGULAR = 1; break;
            case 21: extensions->ANARI_KHR_VOLUME_TRANSFER_FUNCTION1D = 1; break;
            case 22: extensions->ANARI_KHR_LIGHT_RING = 1; break;
            case 23: extensions->ANARI_KHR_LIGHT_QUAD = 1; break;
            case 24: extensions->ANARI_KHR_LIGHT_HDRI = 1; break;
            case 25: extensions->ANARI_KHR_MATERIAL_PHYSICALLY_BASED = 1; break;
            case 26: extensions->ANARI_KHR_FRAME_CONTINUATION = 1; break;
            case 27: extensions->ANARI_KHR_AUXILIARY_BUFFERS = 1; break;
            case 28: extensions->ANARI_KHR_AREA_LIGHTS = 1; break;
            case 29: extensions->ANARI_KHR_STOCHASTIC_RENDERING = 1; break;
            case 30: extensions->ANARI_KHR_TRANSFORMATION_MOTION_BLUR = 1; break;
            case 31: extensions->ANARI_KHR_ARRAY1D_REGION = 1; break;
            case 32: extensions->ANARI_KHR_RENDERER_AMBIENT_LIGHT = 1; break;
            case 33: extensions->ANARI_KHR_RENDERER_BACKGROUND_COLOR = 1; break;
            case 34: extensions->ANARI_KHR_RENDERER_BACKGROUND_IMAGE = 1; break;
            case 35: extensions->ANARI_EXP_VOLUME_SAMPLE_RATE = 1; break;
            default: break;
        }
    }
}
int anariGetDeviceExtensionStruct(ANARIExtensions *extensions, ANARILibrary library, const char *deviceName) {
    const char *const *list = (const char *const *)anariGetDeviceExtensions(library, deviceName);
    if(list) {
        fillExtensionStruct(extensions, list);
        return 0;
    } else {
        return 1;
    }
}
int anariGetObjectExtensionStruct(ANARIExtensions *extensions, ANARIDevice device, ANARIDataType objectType, const char *objectName) {
    const char *const *list = (const char *const *)anariGetObjectInfo(device, objectType, objectName, "extension", ANARI_STRING_LIST);
    if(list) {
        fillExtensionStruct(extensions, list);
        return 0;
    } else {
        return 1;
    }
}
int anariGetInstanceExtensionStruct(ANARIExtensions *extensions, ANARIDevice device, ANARIObject object) {
    const char *const *list = NULL;
    anariGetProperty(device, object, "extension", ANARI_STRING_LIST, &list, sizeof(list), ANARI_WAIT);
    if(list) {
        fillExtensionStruct(extensions, list);
        return 0;
    } else {
        return 1;
    }
}
#endif