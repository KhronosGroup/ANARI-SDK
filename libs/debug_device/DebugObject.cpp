// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "DebugObject.h"
#include "DebugDevice.h"

namespace anari {
namespace debug_device {

void GenericDebugObject::unknown_parameter(ANARIDataType type, const char *subtype, const char *paramname, ANARIDataType paramtype) {
  debugDevice->reportStatus(
          getHandle(),
          getType(),
          ANARI_SEVERITY_WARNING,
          ANARI_STATUS_INVALID_ARGUMENT,
          "anariSetParameter: Unknown parameter \"%s\".", paramname);
}
void GenericDebugObject::check_type(ANARIDataType type, const char *subtype, const char *paramname, ANARIDataType paramtype, ANARIDataType *valid_types) {
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
        "anariSetParameter: Invalid type (%s) for parameter \"%s\".", toString(paramtype), paramname);
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
void ObjectFactory::unknown_subtype(DebugDevice *td, ANARIDataType t, const char *name) {
  td->reportStatus(
          td->this_device(),
          ANARI_DEVICE,
          ANARI_SEVERITY_WARNING,
          ANARI_STATUS_INVALID_ARGUMENT,
          "anareNew: Unknown subtype \"%s\" of type %s.", name, toString(t));

}
}
}
