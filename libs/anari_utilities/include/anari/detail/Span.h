// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

namespace anari {

template <typename T>
struct Span
{
  Span() = default;
  Span(T *, size_t size);

  size_t size() const;
  size_t size_bytes() const;

  T *data();
  T &operator[](size_t i);

  const T *data() const;
  const T &operator[](size_t i) const;

  operator bool() const;

  T *begin() const;
  T *end() const;

  void reset();

 private:
  T *m_data{nullptr};
  size_t m_size{0};
};

template <typename T>
Span<T> make_Span(T *ptr, size_t size);

// Inlined definitions ////////////////////////////////////////////////////////

template <typename T>
inline Span<T>::Span(T *d, size_t s) : m_data(d), m_size(s)
{}

template <typename T>
inline size_t Span<T>::size() const
{
  return m_size;
}

template <typename T>
inline size_t Span<T>::size_bytes() const
{
  return m_size * sizeof(T);
}

template <typename T>
inline T *Span<T>::data()
{
  return m_data;
}

template <typename T>
inline T &Span<T>::operator[](size_t i)
{
  return m_data[i];
}

template <typename T>
inline const T *Span<T>::data() const
{
  return m_data;
}

template <typename T>
inline const T &Span<T>::operator[](size_t i) const
{
  return m_data[i];
}

template <typename T>
inline Span<T>::operator bool() const
{
  return m_data != nullptr;
}

template <typename T>
inline T *Span<T>::begin() const
{
  return m_data;
}

template <typename T>
inline T *Span<T>::end() const
{
  return begin() + size();
}

template <typename T>
inline void Span<T>::reset()
{
  *this = Span<T>();
}

template <typename T>
inline Span<T> make_Span(T *ptr, size_t size)
{
  return Span<T>(ptr, size);
}

} // namespace anari