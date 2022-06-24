// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <array>

namespace anari {

/* 'Optional' implements a single item container which only _may_ have a
 * value in it. Use Optional<>::value_or() as a way to get either the value
 * or some default if the value doesn't exist --> makes it easier to read
 * "branchy" code.
 *
 * NOTE: Similar (but perhaps not identical to) 'boost::optional' or C++17's
 * 'std::optional'.
 *
 *  Example:
 *
 *      Optional<int> myOpt;        // 'myOpt' doesn't contain a value
 *      assert(!myOpt.has_value()); // true.
 *      assert(!myOpt);             // true, shorthand for has_value().
 *
 *      myOpt = 5;                  // 'myOpt' now has a valid value, above
 *                                  // asserts no longer true.
 *
 *      assert(myOpt.value());      // true.
 *      assert(*myOpt == 5);        // true, shorthand for value().
 *      assert(myOpt.value_or(3)    // true because myOpt has a value.
 *             == 5);
 *
 *      myOpt.reset();              // destroy held value.
 *      assert(myOpt.value_or(3)    // now true because myOpt had value
 *             == 3);               // removed by reset().
 */
template <typename T>
struct Optional
{
  // Members in C++17 specified std::optional<T> interface in C++11 //

  Optional() = default;

  Optional(const Optional<T> &other);
  template <typename U>
  Optional(const Optional<U> &other);

  Optional(Optional<T> &&other);
  template <typename U>
  Optional(Optional<U> &&other);

  Optional(const T &value);

  ~Optional();

  Optional &operator=(const Optional &other);
  Optional &operator=(Optional &&other);

  template <typename U>
  Optional &operator=(U &&value);

  template <typename U>
  Optional &operator=(const Optional<U> &other);

  template <typename U>
  Optional &operator=(Optional<U> &&other);

  const T *operator->() const;
  T *operator->();

  const T &operator*() const;
  T &operator*();

  bool has_value() const;
  explicit operator bool() const;

  const T &value() const;
  T &value();

  template <typename U>
  T value_or(U &&default_value) const;

  void reset();

  template <typename... Args>
  T &emplace(Args &&... args);

  // Extra members //

  std::string toString() const;

 private:
  // Helper functions //

  void default_construct_storage_if_needed();

  // Data members //

  std::array<unsigned char, sizeof(T)> storage;
  bool hasValue{false};
};

// Inlined Optional definitions ///////////////////////////////////////////////

template <typename T>
inline Optional<T>::Optional(const Optional<T> &other)
{
  if (other.has_value())
    *this = other.value();
}

template <typename T>
template <typename U>
inline Optional<T>::Optional(const Optional<U> &other)
{
  static_assert(std::is_convertible<U, T>::value,
      "anari::Optional<T> requires the type"
      " parameter of an instance being copied-from be"
      " convertible to the type parameter of the destination"
      " Optional<>.");
  if (other.has_value())
    *this = other.value();
}

template <typename T>
inline Optional<T>::Optional(Optional<T> &&other)
{
  if (other.has_value()) {
    reset();
    value() = std::move(other.value());
    hasValue = true;
  }
}

template <typename T>
template <typename U>
inline Optional<T>::Optional(Optional<U> &&other)
{
  static_assert(std::is_convertible<U, T>::value,
      "anari::Optional<T> requires the type"
      " parameter of an instance being copied-from be"
      " convertible to the type parameter of the destination"
      " Optional<>.");

  if (other.has_value()) {
    reset();
    value() = std::move(static_cast<T>(other.value()));
    hasValue = true;
  }
}

template <typename T>
inline Optional<T>::Optional(const T &value)
{
  emplace(value);
}

template <typename T>
inline Optional<T>::~Optional()
{
  reset();
}

template <typename T>
inline Optional<T> &Optional<T>::operator=(const Optional &other)
{
  default_construct_storage_if_needed();
  value() = other.value();
  hasValue = true;
  return *this;
}

template <typename T>
inline Optional<T> &Optional<T>::operator=(Optional &&other)
{
  default_construct_storage_if_needed();
  value() = std::move(other.value());
  hasValue = true;
  return *this;
}

template <typename T>
template <typename U>
inline Optional<T> &Optional<T>::operator=(U &&rhs)
{
  static_assert(std::is_convertible<U, T>::value,
      "anari::Optional<T> requires the type"
      " being assigned from be convertible to the type parameter"
      " of the destination Optional<>.");
  default_construct_storage_if_needed();
  this->value() = static_cast<T>(rhs);
  hasValue = true;
  return *this;
}

template <typename T>
template <typename U>
inline Optional<T> &Optional<T>::operator=(const Optional<U> &other)
{
  static_assert(std::is_convertible<U, T>::value,
      "anari::Optional<T> requires the type"
      " parameter of an instance being copied-from be"
      " convertible to the type parameter of the destination"
      " Optional<>.");
  default_construct_storage_if_needed();
  value() = static_cast<T>(other.value());
  hasValue = true;
  return *this;
}

template <typename T>
template <typename U>
inline Optional<T> &Optional<T>::operator=(Optional<U> &&other)
{
  static_assert(std::is_convertible<U, T>::value,
      "anari::Optional<T> requires the type"
      " parameter of an instance being moved-from be"
      " convertible to the type parameter of the destination"
      " Optional<>.");
  default_construct_storage_if_needed();
  value() = static_cast<T>(other.value());
  hasValue = true;
  return *this;
}

template <typename T>
inline const T *Optional<T>::operator->() const
{
  return &value();
}

template <typename T>
inline T *Optional<T>::operator->()
{
  return &value();
}

template <typename T>
inline const T &Optional<T>::operator*() const
{
  return value();
}

template <typename T>
inline T &Optional<T>::operator*()
{
  return value();
}

template <typename T>
inline bool Optional<T>::has_value() const
{
  return hasValue;
}

template <typename T>
inline Optional<T>::operator bool() const
{
  return has_value();
}

template <typename T>
inline const T &Optional<T>::value() const
{
  return *(reinterpret_cast<const T *>(storage.data()));
}

template <typename T>
inline T &Optional<T>::value()
{
  return *(reinterpret_cast<T *>(storage.data()));
}

template <typename T>
template <typename U>
inline T Optional<T>::value_or(U &&default_value) const
{
  static_assert(std::is_convertible<U, T>::value,
      "anari::Optional<T> requires the type given"
      " to value_or() to be convertible to type T, the type"
      " parameter of Optional<>.");
  return has_value() ? value() : static_cast<T>(std::forward<U>(default_value));
}

template <typename T>
inline void Optional<T>::reset()
{
  if (!std::is_trivially_destructible<T>::value && has_value())
    value().~T();

  hasValue = false;
}

template <typename T>
template <typename... Args>
inline T &Optional<T>::emplace(Args &&... args)
{
  reset();
  new (storage.data()) T(std::forward<Args>(args)...);
  hasValue = true;
  return value();
}

template <typename T>
inline std::string Optional<T>::toString() const
{
  return "anari::Optional<T>";
}

template <typename T>
inline void Optional<T>::default_construct_storage_if_needed()
{
  if (!has_value())
    new (storage.data()) T();
}

// Comparison functions ///////////////////////////////////////////////////////

template <typename T, typename U>
inline bool operator==(const Optional<T> &lhs, const Optional<U> &rhs)
{
  return (lhs && rhs) && (*lhs == *rhs);
}

template <typename T, typename U>
inline bool operator!=(const Optional<T> &lhs, const Optional<U> &rhs)
{
  return !(lhs == rhs);
}

template <typename T, typename U>
inline bool operator<(const Optional<T> &lhs, const Optional<U> &rhs)
{
  return (lhs && rhs) && (*lhs < *rhs);
}

template <typename T, typename U>
inline bool operator<=(const Optional<T> &lhs, const Optional<U> &rhs)
{
  return (lhs && rhs) && (*lhs <= *rhs);
}

template <typename T, typename U>
inline bool operator>(const Optional<T> &lhs, const Optional<U> &rhs)
{
  return (lhs && rhs) && (*lhs > *rhs);
}

template <typename T, typename U>
inline bool operator>=(const Optional<T> &lhs, const Optional<U> &rhs)
{
  return (lhs && rhs) && (*lhs >= *rhs);
}

template <class T, class... Args>
inline Optional<T> make_optional(Args &&... args)
{
  Optional<T> ret;
  ret.emplace(std::forward<Args>(args)...);
  return ret;
}

} // namespace anari
