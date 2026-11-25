// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <deque>
#include <stack>
#include <vector>

namespace halcyon {

constexpr size_t INVALID_INDEX = ~size_t(0);

template <typename T>
struct IndexedVectorRef;

// Slots never move: ISPC objects hold raw pointers to each other's slots, so
// inserting must not relocate existing elements (std::deque only appends) and
// nothing may shrink the storage. Erased slots are reused by later insertions.
template <typename T>
struct IndexedVector
{
  using element_t = T;
  using storage_t = std::deque<element_t>;
  using marker_t = std::vector<bool>;
  using index_pool_t = std::stack<size_t>;

  IndexedVector() = default;
  ~IndexedVector() = default;

  T &operator[](size_t i) const; // raw access
  IndexedVectorRef<T> at(size_t i) const; // safe access

  size_t size() const;
  size_t capacity() const;
  bool empty() const;

  bool slot_empty(size_t i) const;

  IndexedVectorRef<T> insert(T &&v);
  template <typename... Args>
  IndexedVectorRef<T> emplace(Args &&...args);
  void erase(size_t i);

 private:
  mutable storage_t m_values;
  marker_t m_slots;
  index_pool_t m_freeIndices;
};

template <typename T>
struct IndexedVectorRef
{
  IndexedVectorRef() = default;
  IndexedVectorRef(const IndexedVector<T> &iv, size_t idx);

  size_t index() const;

  const IndexedVector<T> &storage() const;

  T value_or(T alt) const;

  T *data();
  const T *data() const;

  T &operator*();
  const T &operator*() const;
  T *operator->();
  const T *operator->() const;

  bool valid() const;
  operator bool() const;

  IndexedVectorRef(const IndexedVectorRef &) = default;
  IndexedVectorRef &operator=(const IndexedVectorRef &) = default;
  IndexedVectorRef(IndexedVectorRef &&) = default;
  IndexedVectorRef &operator=(IndexedVectorRef &&) = default;

 private:
  template <typename U>
  friend bool operator==(
      const IndexedVectorRef<U> &a, const IndexedVectorRef<U> &b);

  size_t m_idx{INVALID_INDEX};
  const IndexedVector<T> *m_iv{nullptr};
};

template <typename T>
bool operator==(const IndexedVectorRef<T> &a, const IndexedVectorRef<T> &b);
template <typename T>
bool operator!=(const IndexedVectorRef<T> &a, const IndexedVectorRef<T> &b);

template <typename T, typename FCN_T>
inline void foreach_item(IndexedVector<T> &iv, FCN_T &&fcn)
{
  for (size_t i = 0; i < iv.capacity(); i++)
    fcn(iv.slot_empty(i) ? nullptr : &iv[i]);
}

template <typename T, typename FCN_T>
inline void foreach_item_const(const IndexedVector<T> &iv, FCN_T &&fcn)
{
  for (size_t i = 0; i < iv.capacity(); i++)
    fcn(iv.slot_empty(i) ? nullptr : &iv[i]);
}

// Inlined definitions ////////////////////////////////////////////////////////

// IndexedVector //

template <typename T>
inline T &IndexedVector<T>::operator[](size_t i) const
{
  return m_values[i];
}

template <typename T>
inline IndexedVectorRef<T> IndexedVector<T>::at(size_t i) const
{
  return i >= capacity() || slot_empty(i) ? IndexedVectorRef<T>{}
                                          : IndexedVectorRef<T>(*this, i);
}

template <typename T>
inline size_t IndexedVector<T>::size() const
{
  return m_values.size() - m_freeIndices.size();
}

template <typename T>
inline size_t IndexedVector<T>::capacity() const
{
  return m_values.size();
}

template <typename T>
inline bool IndexedVector<T>::empty() const
{
  return size() == 0;
}

template <typename T>
inline bool IndexedVector<T>::slot_empty(size_t i) const
{
  return !m_slots[i];
}

template <typename T>
inline IndexedVectorRef<T> IndexedVector<T>::insert(T &&v)
{
  if (m_freeIndices.empty()) {
    m_values.emplace_back(std::move(v));
    m_slots.push_back(true);
    return at(m_values.size() - 1);
  } else {
    size_t i = m_freeIndices.top();
    m_freeIndices.pop();
    m_values[i] = std::move(v);
    m_slots[i] = true;
    return at(i);
  }
}

template <typename T>
template <typename... Args>
IndexedVectorRef<T> IndexedVector<T>::emplace(Args &&...args)
{
  return insert(std::move(T(std::forward<Args>(args)...)));
}

template <typename T>
inline void IndexedVector<T>::erase(size_t i)
{
  m_values[i] = {};
  m_slots[i] = false;
  m_freeIndices.push(i);
}

// IndexedVectorRef //

template <typename T>
inline IndexedVectorRef<T>::IndexedVectorRef(
    const IndexedVector<T> &iv, size_t idx)
    : m_iv(&iv), m_idx(idx)
{}

template <typename T>
inline size_t IndexedVectorRef<T>::index() const
{
  return m_idx;
}

template <typename T>
inline const IndexedVector<T> &IndexedVectorRef<T>::storage() const
{
  return *m_iv;
}

template <typename T>
inline T IndexedVectorRef<T>::value_or(T alt) const
{
  return valid() ? *data() : alt;
}

template <typename T>
inline T *IndexedVectorRef<T>::data()
{
  return valid() ? &storage()[index()] : nullptr;
}

template <typename T>
inline const T *IndexedVectorRef<T>::data() const
{
  return valid() ? &storage()[index()] : nullptr;
}

template <typename T>
inline T &IndexedVectorRef<T>::operator*()
{
  return *data();
}

template <typename T>
inline const T &IndexedVectorRef<T>::operator*() const
{
  return *data();
}

template <typename T>
inline T *IndexedVectorRef<T>::operator->()
{
  return &storage()[index()];
}

template <typename T>
inline const T *IndexedVectorRef<T>::operator->() const
{
  return &storage()[index()];
}

template <typename T>
inline bool IndexedVectorRef<T>::valid() const
{
  return m_idx != INVALID_INDEX && m_iv;
}

template <typename T>
inline IndexedVectorRef<T>::operator bool() const
{
  return valid();
}

template <typename T>
inline bool operator==(
    const IndexedVectorRef<T> &a, const IndexedVectorRef<T> &b)
{
  return a.m_iv == b.m_iv && a.m_idx == b.m_idx;
}

template <typename T>
inline bool operator!=(
    const IndexedVectorRef<T> &a, const IndexedVectorRef<T> &b)
{
  return !(a == b);
}

} // namespace halcyon
