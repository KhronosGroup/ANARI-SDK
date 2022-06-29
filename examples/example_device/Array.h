// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"
// std
#include <vector>
// anari
#include "anari/anari_cpp/Traits.h"

namespace anari {
namespace example_device {

enum class ArrayShape
{
  ARRAY1D,
  ARRAY2D,
  ARRAY3D
};

struct Array : public Object
{
  Array(const void *appMemory,
      ANARIMemoryDeleter deleter,
      const void *deleterPtr,
      ANARIDataType elementType);
  virtual ~Array();

  ANARIDataType elementType() const;

  void *map();
  void unmap();

  template <typename T>
  T *dataAs();

  virtual ArrayShape shape() const = 0;

  virtual void privatize() = 0;
  bool wasPrivatized() const;

 protected:
  void makePrivatizedCopy(size_t numElements);
  void freeAppMemory();

  const void *m_mem{nullptr};
  ANARIMemoryDeleter m_deleter{nullptr};
  const void *m_deleterPtr{nullptr};

 private:
  ANARIDataType m_elementType{ANARI_UNKNOWN};
  bool m_privatized{false};
};

struct Array1D : public Array
{
  Array1D(const void *appMemory,
      ANARIMemoryDeleter deleter,
      const void *deleterPtr,
      ANARIDataType type,
      uint64_t numItems,
      uint64_t byteStride);

  ArrayShape shape() const override;

  size_t size() const;

  void privatize() override;

 private:
  size_t m_size{0};
};

struct Array2D : public Array
{
  Array2D(const void *appMemory,
      ANARIMemoryDeleter deleter,
      const void *deleterPtr,
      ANARIDataType type,
      uint64_t numItems1,
      uint64_t numItems2,
      uint64_t byteStride1,
      uint64_t byteStride2);

  ArrayShape shape() const override;

  size_t size(int dim) const;
  uvec2 size() const;

  void privatize() override;

 private:
  size_t m_size[2] = {0, 0};
};

struct Array3D : public Array
{
  Array3D(const void *appMemory,
      ANARIMemoryDeleter deleter,
      const void *deleterPtr,
      ANARIDataType type,
      uint64_t numItems1,
      uint64_t numItems2,
      uint64_t numItems3,
      uint64_t byteStride1,
      uint64_t byteStride2,
      uint64_t byteStride3);

  ArrayShape shape() const override;

  size_t size(int dim) const;
  uvec3 size() const;

  void privatize() override;

 private:
  size_t m_size[3] = {0, 0, 0};
};

struct ObjectArray : public Array
{
  ObjectArray(const void *appMemory,
      ANARIMemoryDeleter deleter,
      const void *deleterPtr,
      ANARIDataType type,
      uint64_t numItems,
      uint64_t byteStride);
  ~ObjectArray();

  ArrayShape shape() const override;

  size_t size() const;

  void privatize() override;

  Object **handles();

  void appendHandle(Object *);
  void removeAppendedHandles();

 private:
  std::vector<Object *> m_handleArray;
  size_t m_numAppended{0};
};

// Inlined definitions ////////////////////////////////////////////////////////

template <typename T>
inline T *Array::dataAs()
{
  if (ANARITypeFor<T>::value != m_elementType)
    throw std::runtime_error("incorrect element type queried for array");
  return (T *)m_mem;
}

// Object specializations /////////////////////////////////////////////////////

template <>
inline ObjectArray *Object::getParamObject<ObjectArray>(
    const std::string &name, ObjectArray *valIfNotFound)
{
  if (!hasParam(name))
    return valIfNotFound;

  using PTR_T = IntrusivePtr<Array1D>;
  PTR_T val = getParam<PTR_T>(name, PTR_T());
  return (ObjectArray *)val.ptr;
}

} // namespace example_device

ANARI_TYPEFOR_SPECIALIZATION(example_device::Array *, ANARI_ARRAY);
ANARI_TYPEFOR_SPECIALIZATION(example_device::Array1D *, ANARI_ARRAY1D);
ANARI_TYPEFOR_SPECIALIZATION(example_device::Array2D *, ANARI_ARRAY2D);
ANARI_TYPEFOR_SPECIALIZATION(example_device::Array3D *, ANARI_ARRAY3D);
ANARI_TYPEFOR_SPECIALIZATION(example_device::ObjectArray *, ANARI_ARRAY1D);

} // namespace anari
