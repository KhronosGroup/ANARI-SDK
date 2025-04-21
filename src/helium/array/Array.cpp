// Copyright 2023-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "array/Array.h"

namespace helium {

// Helper functions //

template <typename T>
static void zeroOutStruct(T &v)
{
  std::memset(&v, 0, sizeof(T));
}

// BaseArray //

BaseArray::BaseArray(ANARIDataType type, BaseGlobalDeviceState *s)
    : BaseObject(type, s)
{}

bool BaseArray::isValid() const
{
  return true;
}

// Array //

Array::Array(ANARIDataType type,
    BaseGlobalDeviceState *state,
    const ArrayMemoryDescriptor &d)
    : BaseArray(type, state), m_elementType(d.elementType)
{
  if (d.appMemory) {
    m_ownership =
        d.deleter ? ArrayDataOwnership::CAPTURED : ArrayDataOwnership::SHARED;
    markDataModified();
  } else
    m_ownership = ArrayDataOwnership::MANAGED;

  switch (ownership()) {
  case ArrayDataOwnership::SHARED:
    m_hostData.shared.mem = d.appMemory;
    break;
  case ArrayDataOwnership::CAPTURED:
    m_hostData.captured.mem = d.appMemory;
    m_hostData.captured.deleter = d.deleter;
    m_hostData.captured.deleterPtr = d.deleterPtr;
    break;
  default:
    break;
  }
}

Array::~Array()
{
  freeAppMemory();
}

ANARIDataType Array::elementType() const
{
  return m_elementType;
}

ArrayDataOwnership Array::ownership() const
{
  return m_ownership;
}

const void *Array::data() const
{
  switch (ownership()) {
  case ArrayDataOwnership::SHARED:
    return wasPrivatized() ? m_hostData.privatized.mem : m_hostData.shared.mem;
    break;
  case ArrayDataOwnership::CAPTURED:
    return m_hostData.captured.mem;
    break;
  case ArrayDataOwnership::MANAGED:
    return m_hostData.managed.mem;
    break;
  default:
    break;
  }

  return nullptr;
}

size_t Array::totalCapacity() const
{
  return totalSize();
}

void *Array::map()
{
  if (isMapped()) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "array mapped again without being previously unmapped");
  }
  m_mapped = true;
  return const_cast<void *>(data());
}

void Array::unmap()
{
  if (!isMapped()) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "array unmapped again without being previously mapped");
    return;
  }
  m_mapped = false;
  markDataModified();
  notifyChangeObservers();
}

bool Array::isMapped() const
{
  return m_mapped;
}

bool Array::wasPrivatized() const
{
  return m_privatized;
}

void Array::markDataModified()
{
  m_lastDataModified = helium::newTimeStamp();
}

bool Array::getProperty(
    const std::string_view &name, ANARIDataType type, void *ptr, uint32_t flags)
{
  return 0;
}

void Array::commitParameters()
{
  // no-op
}

void Array::finalize()
{
  // no-op
}

void Array::makePrivatizedCopy(size_t numElements)
{
  if (ownership() != ArrayDataOwnership::SHARED)
    return;

  if (!anari::isObject(elementType())) {
    reportMessage(ANARI_SEVERITY_PERFORMANCE_WARNING,
        "making private copy of shared array (type '%s') | ownership: (%i:%i)",
        anari::toString(elementType()),
        this->useCount(helium::RefType::PUBLIC),
        this->useCount(helium::RefType::INTERNAL));

    size_t numBytes = numElements * anari::sizeOf(elementType());
    m_hostData.privatized.mem = malloc(numBytes);
    std::memcpy(m_hostData.privatized.mem, m_hostData.shared.mem, numBytes);
  }

  m_privatized = true;
  zeroOutStruct(m_hostData.shared);
}

void Array::freeAppMemory()
{
  if (ownership() == ArrayDataOwnership::CAPTURED) {
    auto &captured = m_hostData.captured;
    reportMessage(ANARI_SEVERITY_DEBUG, "invoking array deleter");
    if (captured.deleter)
      captured.deleter(captured.deleterPtr, captured.mem);
    zeroOutStruct(captured);
  } else if (ownership() == ArrayDataOwnership::MANAGED) {
    reportMessage(ANARI_SEVERITY_DEBUG, "freeing managed array");
    free(m_hostData.managed.mem);
    zeroOutStruct(m_hostData.managed);
  } else if (wasPrivatized()) {
    free(m_hostData.privatized.mem);
    zeroOutStruct(m_hostData.privatized);
  }
}

void Array::initManagedMemory()
{
  if (m_hostData.managed.mem != nullptr)
    return;

  if (ownership() == ArrayDataOwnership::MANAGED) {
    auto totalBytes = totalSize() * anari::sizeOf(elementType());
    m_hostData.managed.mem = malloc(totalBytes);
    std::memset(m_hostData.managed.mem, 0, totalBytes);
  }
}

void Array::on_NoPublicReferences()
{
  reportMessage(ANARI_SEVERITY_DEBUG, "privatizing array");
  if (!wasPrivatized() && ownership() != ArrayDataOwnership::MANAGED)
    privatize();
}

} // namespace helium

HELIUM_ANARI_TYPEFOR_DEFINITION(helium::Array *);
