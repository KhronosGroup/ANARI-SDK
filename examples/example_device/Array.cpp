// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Array.h"

namespace anari {
namespace example_device {

// Array //

Array::Array(const void *appMem,
    ANARIMemoryDeleter deleter,
    const void *deleterPtr,
    ANARIDataType elementType)
    : m_mem(appMem), m_deleter(deleter), m_elementType(elementType)
{}

Array::~Array()
{
  freeAppMemory();
}

ANARIDataType Array::elementType() const
{
  return m_elementType;
}

void *Array::map()
{
  return const_cast<void*>(m_mem);
}

void Array::unmap()
{
  // TODO
}

bool Array::wasPrivatized() const
{
  return m_privatized;
}

void Array::makePrivatizedCopy(size_t numElements)
{
  const void *appMem = m_mem;

  size_t numBytes = numElements * sizeOf(elementType());
  void *tmp_mem = malloc(numBytes);
  std::memcpy(tmp_mem, appMem, numBytes);
  m_mem = tmp_mem;

  if (m_deleter) {
    m_deleter(m_deleterPtr, appMem);
    m_deleter = nullptr;
  }

  m_privatized = true;
}

void Array::freeAppMemory()
{
  if (m_deleter && m_mem) {
    m_deleter(m_deleterPtr, m_mem);
    m_mem = nullptr;
    m_deleter = nullptr;
  } else if (wasPrivatized() && m_mem) {
    free(const_cast<void*>(m_mem));
    m_mem = nullptr;
  }
}

// Array1D //

Array1D::Array1D(const void *appMemory,
    ANARIMemoryDeleter deleter,
    const void *deleterPtr,
    ANARIDataType type,
    uint64_t numItems,
    uint64_t byteStride)
    : Array(appMemory, deleter, deleterPtr, type), m_size(numItems)
{
  if (byteStride != 0)
    throw std::runtime_error("strided arrays not yet supported!");
}

ArrayShape Array1D::shape() const
{
  return ArrayShape::ARRAY1D;
}

size_t Array1D::size() const
{
  return m_size;
}

void Array1D::privatize()
{
  makePrivatizedCopy(size());
}

// Array2D //

Array2D::Array2D(const void *appMemory,
    ANARIMemoryDeleter deleter,
    const void *deleterPtr,
    ANARIDataType type,
    uint64_t numItems1,
    uint64_t numItems2,
    uint64_t byteStride1,
    uint64_t byteStride2)
    : Array(appMemory, deleter, deleterPtr, type)
{
  if (byteStride1 != 0 || byteStride2 != 0)
    throw std::runtime_error("strided arrays not yet supported!");

  m_size[0] = numItems1;
  m_size[1] = numItems2;
}

ArrayShape Array2D::shape() const
{
  return ArrayShape::ARRAY2D;
}

size_t Array2D::size(int dim) const
{
  return m_size[dim];
}

uvec2 Array2D::size() const
{
  return uvec2(uint32_t(size(0)), uint32_t(size(1)));
}

void Array2D::privatize()
{
  makePrivatizedCopy(size(0) * size(1));
}

// Array3D //

Array3D::Array3D(const void *appMemory,
    ANARIMemoryDeleter deleter,
    const void *deleterPtr,
    ANARIDataType type,
    uint64_t numItems1,
    uint64_t numItems2,
    uint64_t numItems3,
    uint64_t byteStride1,
    uint64_t byteStride2,
    uint64_t byteStride3)
    : Array(appMemory, deleter, deleterPtr, type)
{
  if (byteStride1 != 0 || byteStride2 != 0 || byteStride3 != 0)
    throw std::runtime_error("strided arrays not yet supported!");

  m_size[0] = numItems1;
  m_size[1] = numItems2;
  m_size[2] = numItems3;
}

ArrayShape Array3D::shape() const
{
  return ArrayShape::ARRAY3D;
}

size_t Array3D::size(int dim) const
{
  return m_size[dim];
}

uvec3 Array3D::size() const
{
  return uvec3(uint32_t(size(0)), uint32_t(size(1)), uint32_t(size(2)));
}

void Array3D::privatize()
{
  makePrivatizedCopy(size(0) * size(1) * size(2));
}

// ObjectArray //

ObjectArray::ObjectArray(const void *appMemory,
    ANARIMemoryDeleter deleter,
    const void *deleterPtr,
    ANARIDataType type,
    uint64_t numItems,
    uint64_t byteStride)
    : Array(appMemory, deleter, deleterPtr, type)
{
  if (byteStride != 0)
    throw std::runtime_error("strided arrays not yet supported!");

  m_handleArray.resize(numItems);

  auto **srcBegin = (Object **)appMemory;
  auto **srcEnd = srcBegin + numItems;

  std::transform(srcBegin, srcEnd, m_handleArray.begin(), [](Object *obj) {
    obj->refInc();
    return obj;
  });
}

ObjectArray::~ObjectArray()
{
  removeAppendedHandles();
  for (auto *obj : m_handleArray)
    obj->refDec();
}

ArrayShape ObjectArray::shape() const
{
  return ArrayShape::ARRAY1D;
}

size_t ObjectArray::size() const
{
  return m_handleArray.size();
}

void ObjectArray::privatize()
{
  freeAppMemory();
}

Object **ObjectArray::handles()
{
  return m_handleArray.data();
}

void ObjectArray::appendHandle(Object *o)
{
  m_handleArray.push_back(o);
  m_numAppended++;
}

void ObjectArray::removeAppendedHandles()
{
  auto originalSize = m_handleArray.size() - m_numAppended;
  m_handleArray.resize(originalSize);
  m_numAppended = 0;
}

} // namespace example_device

ANARI_TYPEFOR_DEFINITION(example_device::Array *);
ANARI_TYPEFOR_DEFINITION(example_device::Array1D *);
ANARI_TYPEFOR_DEFINITION(example_device::Array2D *);
ANARI_TYPEFOR_DEFINITION(example_device::Array3D *);
ANARI_TYPEFOR_DEFINITION(example_device::ObjectArray *);

} // namespace anari