// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Zero-copy array wrapper (SPEC §5.1): a custom HdDataSource leaf that holds a
// refcounted ANARI array handle and serves its memory to Hydra as a foreign-data
// VtArray at pull time. Bulk data is never copied at the ANARI<->Hydra boundary.
//
// Execution-verified in prototype 012: VtArray(&src, ptr, n) per pull, sharing
// one foreign source; pointer identity survives to
// HdSceneIndexAdapterSceneDelegate::Get.
//
// The foreign source owns the ANARI array reference and outlives both the data
// source and every VtArray it handed out: delegates may keep a pulled VtArray
// across Syncs (HdEmbreeMesh::_points does), after a Mirror rebuild has
// replaced the data source.

#pragma once

// helium
#include "helium/utility/IntrusivePtr.h"
#include "helium/array/Array.h"
// pxr
#include <pxr/imaging/hd/dataSource.h>
#include <pxr/base/vt/array.h>
#include <pxr/base/vt/value.h>

#include <vector>

namespace anari_hydra {

// Refcounted owner of one ANARI array reference, shared by the data source and
// the VtArrays it hands out. Deletes itself when the last of them lets go.
class ArrayForeignSource : public pxr::Vt_ArrayForeignDataSource
{
 public:
  // The creator holds the initial reference; drop it with release().
  explicit ArrayForeignSource(helium::Array *array)
      : pxr::Vt_ArrayForeignDataSource(&Detached, 1), m_arrayRef(array)
  {}

  ArrayForeignSource(const ArrayForeignSource &) = delete;
  ArrayForeignSource &operator=(const ArrayForeignSource &) = delete;

  void release()
  {
    if (_refCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
      delete this;
  }

 private:
  // VtArray calls this when its count reaches zero, i.e. after release().
  static void Detached(pxr::Vt_ArrayForeignDataSource *self)
  {
    delete static_cast<ArrayForeignSource *>(self);
  }

  helium::IntrusivePtr<helium::Array> m_arrayRef;
};

template <typename T>
class ZeroCopyDataSource
    : public pxr::HdTypedSampledDataSource<pxr::VtArray<T>>
{
 public:
  using Handle = std::shared_ptr<ZeroCopyDataSource<T>>;

  template <typename... Args>
  static Handle New(Args &&...args)
  {
    return Handle(new ZeroCopyDataSource<T>(std::forward<Args>(args)...));
  }

  ~ZeroCopyDataSource() override
  {
    m_foreignSource->release();
  }

  pxr::VtValue GetValue(pxr::HdSampledDataSource::Time shutterOffset) override
  {
    return pxr::VtValue(GetTypedValue(shutterOffset));
  }

  pxr::VtArray<T> GetTypedValue(pxr::HdSampledDataSource::Time) override
  {
    // Zero-copy wrap: no allocation, no memcpy. Copies of this VtArray share
    // the same foreign source, whose array ref keeps the memory live.
    return pxr::VtArray<T>(
        m_foreignSource, const_cast<T *>(m_data), m_count);
  }

  bool GetContributingSampleTimesForInterval(
      pxr::HdSampledDataSource::Time,
      pxr::HdSampledDataSource::Time,
      std::vector<pxr::HdSampledDataSource::Time> *) override
  {
    return false;   // not animated in v1
  }

 private:
  ZeroCopyDataSource(helium::Array *array, const T *data, size_t count)
      : m_foreignSource(new ArrayForeignSource(array)),
        m_data(data),
        m_count(count)
  {}

  ZeroCopyDataSource(const ZeroCopyDataSource &) = delete;
  ZeroCopyDataSource &operator=(const ZeroCopyDataSource &) = delete;

  ArrayForeignSource *m_foreignSource{nullptr};
  const T *m_data{nullptr};
  size_t m_count{0};
};

} // namespace anari_hydra
