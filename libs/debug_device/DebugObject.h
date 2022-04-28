// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include <cstring>

#include "anari/anari.h"
#include "anari/type_utility.h"

#include "debug_device_exports.h"

#include <iostream>
namespace anari {
namespace debug_device {

struct DebugDevice;

struct DebugObjectBase {
public:
  virtual ANARIDataType getType() = 0;
  virtual const char *getName() = 0;
  virtual void setName(const char *name) = 0;
  virtual void retain() = 0;
  virtual void release() = 0;
  virtual void commit() = 0;
  virtual void setParameter(const char *name, ANARIDataType type, const void *mem) = 0;
  virtual void unsetParameter(const char *name) = 0;
  virtual void referencedBy(ANARIObject parent) = 0;
  virtual void used() = 0;
  virtual ANARIObject getHandle() = 0;
  virtual ANARIObject getWrappedHandle() = 0;
  virtual int64_t getRefCount() = 0;
  virtual int getReferences() = 0;
  virtual int getUncommittedParameters() = 0;
  virtual ~DebugObjectBase() {}
};


struct DEBUG_DEVICE_INTERFACE GenericDebugObject : public DebugObjectBase {
public:
  GenericDebugObject() = default;
  GenericDebugObject(DebugDevice *debugDevice, ANARIObject wrappedHandle, ANARIObject handle)
  : debugDevice(debugDevice), wrappedHandle(wrappedHandle), handle(handle), publicRefCount(1)
  {

  }
private:
  DebugDevice *debugDevice = nullptr;
  ANARIObject handle = 0;
  ANARIObject wrappedHandle = 0;
  int64_t publicRefCount = 0;
  int uncommittedParameters = 0;
  int references = 0;
  std::string objectName;
public:
  ANARIDataType getType() { return ANARI_OBJECT; }
  const char *getName() {
    if(objectName.empty()) {
      objectName = varnameOf(getType()) + std::to_string(uintptr_t(wrappedHandle));
    }
    return objectName.c_str();
  }
  void setName(const char *name) {
    objectName = name;
  }
  void retain() { publicRefCount += 1; }
  void release() { publicRefCount -= 1; }
  void commit() { uncommittedParameters = 0; }
  void setParameter(const char *name, ANARIDataType type, const void *mem) {
    uncommittedParameters += 1;
    if(type == ANARI_STRING && std::strncmp(name, "name", 4) == 0) {
      setName((const char*)mem);
    }
  }
  void unsetParameter(const char *name) { uncommittedParameters += 1; }
  void referencedBy(ANARIObject parent) { references += 1; }
  void used() { references += 1; }
  ANARIObject getHandle() { return handle; }
  ANARIObject getWrappedHandle() { return wrappedHandle; }
  int64_t getRefCount() { return publicRefCount; }
  int getReferences() { return references; }
  int getUncommittedParameters() { return uncommittedParameters; }

  void unknown_parameter(ANARIDataType type, const char *subtype, const char *paramname, ANARIDataType paramtype);
  void check_type(ANARIDataType type, const char *subtype, const char *paramname, ANARIDataType paramtype, ANARIDataType *valid_types);
};

template<int T>
struct DebugObject : public GenericDebugObject {
  DebugObject() = default;
  DebugObject(DebugDevice *debugDevice, ANARIObject wrappedHandle, ANARIObject handle)
  : GenericDebugObject(debugDevice, wrappedHandle, handle)
  {

  }
  virtual ANARIDataType getType() { return T; }
};

struct GenericArrayDebugObject : public GenericDebugObject {
public:
  GenericArrayDebugObject() = default;
  GenericArrayDebugObject(DebugDevice *debugDevice, ANARIObject wrappedHandle, ANARIObject handle)
  : GenericDebugObject(debugDevice, wrappedHandle, handle)
  {

  }
  void *arrayData = nullptr;
  void *mapping = nullptr;
  ANARIObject *handles = nullptr;
  ANARIDataType arrayType = ANARI_UNKNOWN;
  uint64_t numItems1;
  uint64_t numItems2;
  uint64_t numItems3;
  uint64_t byteStride1;
  uint64_t byteStride2;
  uint64_t byteStride3;

  void attachArray(
    void *array,
    ANARIDataType type,
    uint64_t numItems1,
    uint64_t numItems2,
    uint64_t numItems3,
    uint64_t byteStride1,
    uint64_t byteStride2,
    uint64_t byteStride3)
  {
    this->arrayData = array;
    this->arrayType = type;
    this->numItems1 = numItems1;
    this->numItems2 = numItems2;
    this->numItems3 = numItems3;
    this->byteStride1 = byteStride1;
    this->byteStride2 = byteStride2;
    this->byteStride3 = byteStride3;
  }
  void mapArray(void *array) {
    mapping = array;
  }
  void unmapArray() {
    mapping = nullptr;
  }
};

template<int T>
struct ArrayDebugObject : public GenericArrayDebugObject {
  ArrayDebugObject() = default;
  ArrayDebugObject(DebugDevice *debugDevice, ANARIObject wrappedHandle, ANARIObject handle)
  : GenericArrayDebugObject(debugDevice, wrappedHandle, handle)
  {

  }
  ANARIDataType getType() { return T; }
};

class DEBUG_DEVICE_INTERFACE ObjectFactory {
public:
  virtual DebugObjectBase* new_volume(const char *name, DebugDevice *td, ANARIObject wh, ANARIObject h) {
    return new DebugObject<ANARI_VOLUME>(td, wh, h);
  }
  virtual DebugObjectBase* new_geometry(const char *name, DebugDevice *td, ANARIObject wh, ANARIObject h) {
    return new DebugObject<ANARI_GEOMETRY>(td, wh, h);
  }
  virtual DebugObjectBase* new_spatial_field(const char *name, DebugDevice *td, ANARIObject wh, ANARIObject h) {
    return new DebugObject<ANARI_SPATIAL_FIELD>(td, wh, h);
  }
  virtual DebugObjectBase* new_light(const char *name, DebugDevice *td, ANARIObject wh, ANARIObject h) {
    return new DebugObject<ANARI_LIGHT>(td, wh, h);
  }
  virtual DebugObjectBase* new_camera(const char *name, DebugDevice *td, ANARIObject wh, ANARIObject h) {
    return new DebugObject<ANARI_CAMERA>(td, wh, h);
  }
  virtual DebugObjectBase* new_material(const char *name, DebugDevice *td, ANARIObject wh, ANARIObject h) {
    return new DebugObject<ANARI_MATERIAL>(td, wh, h);
  }
  virtual DebugObjectBase* new_sampler(const char *name, DebugDevice *td, ANARIObject wh, ANARIObject h) {
    return new DebugObject<ANARI_SAMPLER>(td, wh, h);
  }
  virtual DebugObjectBase* new_renderer(const char *name, DebugDevice *td, ANARIObject wh, ANARIObject h) {
    return new DebugObject<ANARI_RENDERER>(td, wh, h);
  }
  virtual DebugObjectBase* new_device(DebugDevice *td, ANARIObject wh, ANARIObject h) {
    return new DebugObject<ANARI_DEVICE>(td, wh, h);
  }
  virtual DebugObjectBase* new_array1d(DebugDevice *td, ANARIObject wh, ANARIObject h) {
    return new ArrayDebugObject<ANARI_ARRAY1D>(td, wh, h);
  }
  virtual DebugObjectBase* new_array2d(DebugDevice *td, ANARIObject wh, ANARIObject h) {
    return new ArrayDebugObject<ANARI_ARRAY2D>(td, wh, h);
  }
  virtual DebugObjectBase* new_array3d(DebugDevice *td, ANARIObject wh, ANARIObject h) {
    return new ArrayDebugObject<ANARI_ARRAY3D>(td, wh, h);
  }
  virtual DebugObjectBase* new_frame(DebugDevice *td, ANARIObject wh, ANARIObject h) {
    return new DebugObject<ANARI_FRAME>(td, wh, h);
  }
  virtual DebugObjectBase* new_group(DebugDevice *td, ANARIObject wh, ANARIObject h) {
    return new DebugObject<ANARI_GROUP>(td, wh, h);
  }
  virtual DebugObjectBase* new_instance(DebugDevice *td, ANARIObject wh, ANARIObject h) {
    return new DebugObject<ANARI_INSTANCE>(td, wh, h);
  }
  virtual DebugObjectBase* new_world(DebugDevice *td, ANARIObject wh, ANARIObject h) {
    return new DebugObject<ANARI_WORLD>(td, wh, h);
  }
  virtual DebugObjectBase* new_surface(DebugDevice *td, ANARIObject wh, ANARIObject h) {
    return new DebugObject<ANARI_SURFACE>(td, wh, h);
  }
  DebugObjectBase* new_by_type(ANARIDataType t, DebugDevice *td, ANARIObject wh, ANARIObject h);
  DebugObjectBase* new_by_subtype(ANARIDataType t, const char *name, DebugDevice *td, ANARIObject wh, ANARIObject h);
  void unknown_subtype(DebugDevice *td, ANARIDataType t, const char *name);
  virtual ~ObjectFactory() { }
};

}
}
