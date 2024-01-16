// Copyright 2023 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../BaseObject.h"
#include "../helium_math.h"
// std
#include <sstream>

namespace helium {

// BaseArray interface ////////////////////////////////////////////////////////

struct BaseArray : public BaseObject
{
  BaseArray(ANARIDataType type, BaseGlobalDeviceState *s);

  // Implement anariMapArray()
  virtual void *map() = 0;

  // Implement anariUnmapArray()
  virtual void unmap() = 0;

  // This is invoked when this object's public ref count is 0, but still has a
  // non-zero internal ref count. See README for additional explanation.
  virtual void privatize() = 0;

  // Arrays are always considered to be valid, though subclasses can override
  bool isValid() const override;
};

// Basic, host-based Array implementation /////////////////////////////////////

enum class ArrayDataOwnership
{
  SHARED,
  CAPTURED,
  MANAGED,
  INVALID
};

struct ArrayMemoryDescriptor
{
  const void *appMemory{nullptr};
  ANARIMemoryDeleter deleter{};
  const void *deleterPtr{nullptr};
  ANARIDataType elementType{ANARI_UNKNOWN};
};

struct Array : public BaseArray
{
  Array(ANARIDataType type,
      BaseGlobalDeviceState *state,
      const ArrayMemoryDescriptor &d);
  virtual ~Array() override;

  ANARIDataType elementType() const;
  ArrayDataOwnership ownership() const;

  const void *data() const;

  template <typename T>
  const T *dataAs() const;

  virtual size_t totalSize() const = 0;
  virtual size_t totalCapacity() const;

  virtual void *map() override;
  virtual void unmap() override;
  virtual void privatize() override = 0;

  bool isMapped() const;

  bool wasPrivatized() const;

  void markDataModified();

  bool isOffloaded() const;
  void markDataIsOffloaded(bool isOffloaded = true);

  virtual void uploadArrayData() const;
  void markDataUploaded() const;
  bool needToUploadData() const;

  virtual bool getProperty(const std::string_view &name,
      ANARIDataType type,
      void *ptr,
      uint32_t flags);
  virtual void commit();

 protected:
  void makePrivatizedCopy(size_t numElements);
  void freeAppMemory();
  void initManagedMemory();

  void notifyObserver(BaseObject *) const override;

  template <typename T>
  void throwIfDifferentElementType() const;

  struct ArrayDescriptor
  {
    struct SharedData
    {
      const void *mem{nullptr};
    } shared;

    struct CapturedData
    {
      const void *mem{nullptr};
      ANARIMemoryDeleter deleter{nullptr};
      const void *deleterPtr{nullptr};
    } captured;

    struct ManagedData
    {
      void *mem{nullptr};
    } managed;

    struct PrivatizedData
    {
      void *mem{nullptr};
    } privatized;
  } m_hostData;

  helium::TimeStamp m_lastDataModified{0};
  mutable helium::TimeStamp m_lastDataUploaded{0};
  bool m_mapped{false};

 private:
  ArrayDataOwnership m_ownership{ArrayDataOwnership::INVALID};
  ANARIDataType m_elementType{ANARI_UNKNOWN};
  bool m_privatized{false};
  mutable bool m_isOffloaded{false};
};

// Inlined definitions ////////////////////////////////////////////////////////

template <typename T>
inline const T *Array::dataAs() const
{
  throwIfDifferentElementType<T>();
  return (const T *)data();
}

template <typename T>
inline void Array::throwIfDifferentElementType() const
{
  constexpr auto t = anari::ANARITypeFor<T>::value;
  static_assert(
      t != ANARI_UNKNOWN, "unknown type used to query array element type");

  if (t != elementType()) {
    std::stringstream msg;
    msg << "incorrect element type queried for array -- asked for '"
        << anari::toString(t) << "', but array stores '"
        << anari::toString(elementType()) << "'";
    throw std::runtime_error(msg.str());
  }
}

} // namespace helium

HELIUM_ANARI_TYPEFOR_SPECIALIZATION(helium::BaseArray *, ANARI_ARRAY);
HELIUM_ANARI_TYPEFOR_SPECIALIZATION(helium::Array *, ANARI_ARRAY);
