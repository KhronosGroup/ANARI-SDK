// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "anari/ext/debug/DebugObject.h"
#include "DebugDevice.h"

#include <cstdarg>
#include <cstring>

namespace anari {
namespace debug_device {

void GenericDebugObject::unknown_parameter(ANARIDataType type, const char *subtype, const char *paramname, ANARIDataType paramtype) {
  (void)type;
  (void)subtype;
  (void)paramtype;
  debugDevice->reportStatus(
          getHandle(),
          getType(),
          ANARI_SEVERITY_WARNING,
          ANARI_STATUS_INVALID_ARGUMENT,
          "anariSetParameter: Unknown parameter \"%s\" on object \"%s\" (%s).", paramname, getName(), anari::toString(getType()));
}
void GenericDebugObject::check_type(ANARIDataType type, const char *subtype, const char *paramname, ANARIDataType paramtype, ANARIDataType *valid_types) {
  (void)type;
  (void)subtype;
  for(int i = 0;valid_types[i] != ANARI_UNKNOWN;++i) {
    if(valid_types[i] == paramtype) {
      return; // it's in the list, nothing to report
    }
  }
  debugDevice->reportStatus(
        getHandle(),
        getType(),
        ANARI_SEVERITY_WARNING,
        ANARI_STATUS_INVALID_ARGUMENT,
        "anariSetParameter: Invalid type (%s) for parameter \"%s\" on object \"%s\" (%s).", toString(paramtype), paramname, getName(), anari::toString(getType()));
}

void DebugObject<ANARI_FRAME>::setParameter(const char *name, ANARIDataType type, const void *mem) {
  GenericDebugObject::setParameter(name, type, mem);

  // frame completion callbacks require special treatment
  if(type == ANARI_UINT32_VEC2 && std::strncmp(name, "size", 4)==0) {
    std::memcpy(size, mem, sizeof(size));
  } else if(type == ANARI_DATA_TYPE && std::strncmp(name, "color", 5)==0) {
    colorType = *(const ANARIDataType*)mem;
  } else if(type == ANARI_DATA_TYPE && std::strncmp(name, "depth", 5)==0) {
    depthType = *(const ANARIDataType*)mem;
  } else if(type == ANARI_FRAME_COMPLETION_CALLBACK && std::strncmp(name, "frameCompletionCallback", 23)==0) {
    frameContinuationFun = *(ANARIFrameCompletionCallback*)mem;
  } else if(type == ANARI_VOID_POINTER && std::strncmp(name, "frameCompletionCallbackUserData", 31)==0) {
    userdata = mem;
  }
}

DebugObjectBase* ObjectFactory::new_by_type(ANARIDataType t, DebugDevice *td, ANARIObject wh, ANARIObject h) {
  switch(t) {
    case ANARI_DEVICE: return new_device(td, wh, h);
    case ANARI_ARRAY1D: return new_array1d(td, wh, h);
    case ANARI_ARRAY2D: return new_array2d(td, wh, h);
    case ANARI_ARRAY3D: return new_array3d(td, wh, h);
    case ANARI_FRAME: return new_frame(td, wh, h);
    case ANARI_GROUP: return new_group(td, wh, h);
    case ANARI_INSTANCE: return new_instance(td, wh, h);
    case ANARI_WORLD: return new_world(td, wh, h);
    case ANARI_SURFACE: return new_surface(td, wh, h);
    default: return new GenericDebugObject(td, wh, h);
  }
}
DebugObjectBase* ObjectFactory::new_by_subtype(ANARIDataType t, const char *name, DebugDevice *td, ANARIObject wh, ANARIObject h) {
  switch(t) {
    case ANARI_VOLUME: return new_volume(name, td, wh, h);
    case ANARI_GEOMETRY: return new_geometry(name, td, wh, h);
    case ANARI_SPATIAL_FIELD: return new_spatial_field(name, td, wh, h);
    case ANARI_LIGHT: return new_light(name, td, wh, h);
    case ANARI_CAMERA: return new_camera(name, td, wh, h);
    case ANARI_MATERIAL: return new_material(name, td, wh, h);
    case ANARI_SAMPLER: return new_sampler(name, td, wh, h);
    case ANARI_RENDERER: return new_renderer(name, td, wh, h);
    default: return new GenericDebugObject(td, wh, h);
  }
}
void ObjectFactory::info(DebugDevice *td, const char *format, ...) {
  va_list arglist;
  va_start(arglist, format);
  td->vreportStatus(
          td->this_device(),
          ANARI_DEVICE,
          ANARI_SEVERITY_INFO,
          ANARI_STATUS_NO_ERROR,
          format, arglist);
  va_end(arglist);
}
void ObjectFactory::unknown_subtype(DebugDevice *td, ANARIDataType t, const char *name) {
  td->reportStatus(
          td->this_device(),
          ANARI_DEVICE,
          ANARI_SEVERITY_WARNING,
          ANARI_STATUS_INVALID_ARGUMENT,
          "anariNew: Unknown subtype \"%s\" of type %s.", name, toString(t));

}
}
}
