// Copyright 2023-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../BaseObject.h"
#include "../helium_math.h"
// std
#include <sstream>

namespace helium {

// BaseArray interface ////////////////////////////////////////////////////////

/*
 * Abstract interface for all ANARI array objects. Adds map()/unmap() and
 * privatize() to the BaseObject interface. privatize() is called when the
 * application releases its last public reference while the device still holds
 * an internal reference; host arrays copy the application memory at that point
 * so it remains valid after the app frees it. Derived classes that wrap GPU
 * memory should override privatize() to copy the data to device-owned storage.
 */
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

/*
 * Describes who owns the backing memory of a host Array:
 *   SHARED   — application retains ownership; the array holds a raw pointer.
 *   CAPTURED — array takes ownership and calls the deleter on destruction.
 *   MANAGED  — array allocates and manages its own heap memory.
 *   INVALID  — uninitialized/error state.
 */
enum class ArrayDataOwnership
{
  SHARED,
  CAPTURED,
  MANAGED,
  INVALID
};

/*
 * Input descriptor passed to Array constructors, carrying the raw memory
 * pointer, optional deleter callback, and element type from the ANARI API call.
 */
struct ArrayMemoryDescriptor
{
  const void *appMemory{nullptr};
  ANARIMemoryDeleter deleter{};
  const void *deleterPtr{nullptr};
  ANARIDataType elementType{ANARI_UNKNOWN};
};

/*
 * Host-side array implementation that manages a flat buffer of uniformly typed
 * elements. Handles all three ownership modes (SHARED/CAPTURED/MANAGED) and
 * implements privatize() by copying the application buffer to a private heap
 * allocation when the app releases its public reference. Subclasses (Array1D,
 * Array2D, Array3D, ObjectArray) specialize the dimensionality interpretation.
 * m_lastDataModified is bumped on unmap() so change observers can detect when
 * new data has been written without a parameter change being logged.
 */
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

  template <typename T>
  const T *valueAt(size_t i) const;

  anari::math::float4 readAsAttributeValue(
      int32_t i, WrapMode wrap = WrapMode::DEFAULT) const;
  template <typename T>
  T valueAtLinear(float in) const; // 'in' must be clamped to [0, 1]
  template <typename T>
  T valueAtClosest(float in) const; // 'in' must be clamped to [0, 1]

  virtual void *map() override;
  virtual void unmap() override;

  bool isMapped() const;

  bool wasPrivatized() const;

  void markDataModified();
  helium::TimeStamp lastDataModified() const;

  virtual bool getProperty(const std::string_view &name,
      ANARIDataType type,
      void *ptr,
      uint64_t size,
      uint32_t flags) override;
  virtual void commitParameters() override;
  virtual void finalize() override;

 protected:
  virtual void privatize() override = 0;

  void makePrivatizedCopy(size_t numElements);
  void freeAppMemory();
  void initManagedMemory();

  template <typename T>
  void throwIfDifferentElementType() const;

  /*
   * Union-like descriptor that holds the memory pointer for whichever ownership
   * mode is active. Only one of shared/captured/managed/privatized is in use
   * at any given time, determined by m_ownership.
   */
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
  bool m_mapped{false};

 private:
  void on_NoPublicReferences() override;

  ArrayDataOwnership m_ownership{ArrayDataOwnership::INVALID};
  ANARIDataType m_elementType{ANARI_UNKNOWN};
  bool m_privatized{false};
};

anari::math::float4 readAttributeValue(const Array *arr,
    uint32_t i,
    const anari::math::float4 &defaultValue = DEFAULT_ATTRIBUTE_VALUE);

// Inlined definitions ////////////////////////////////////////////////////////

template <typename T>
inline const T *Array::dataAs() const
{
  throwIfDifferentElementType<T>();
  return (const T *)data();
}

template <typename T>
inline const T *Array::valueAt(size_t i) const
{
  return &dataAs<T>()[i];
}

template <typename T>
inline T Array::valueAtLinear(float in) const
{
  const T *data = dataAs<T>();
  const auto i = getInterpolant(in, totalSize(), false);
  return linalg::lerp(data[i.lower], data[i.upper], i.frac);
}

template <typename T>
inline T Array::valueAtClosest(float in) const
{
  const T *data = dataAs<T>();
  const auto i = getInterpolant(in, totalSize(), false);
  return i.frac <= 0.5f ? data[i.lower] : data[i.upper];
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
