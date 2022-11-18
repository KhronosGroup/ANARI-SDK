/*
 * Copyright (c) 2019-2022 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include "array/Array.h"

namespace helide {

struct Array1DMemoryDescriptor : public ArrayMemoryDescriptor
{
  uint64_t numItems{0};
  uint64_t byteStride{0};
};

bool isCompact(const Array1DMemoryDescriptor &d);

struct Array1D : public Array
{
  Array1D(HelideGlobalState *state, const Array1DMemoryDescriptor &d);

  void commit() override;

  size_t totalSize() const override;
  size_t totalCapacity() const override;

  void *begin() const;
  void *end() const;

  template <typename T>
  T *beginAs() const;
  template <typename T>
  T *endAs() const;

  size_t size() const;

  void privatize() override;

 private:
  size_t m_capacity{0};
  size_t m_begin{0};
  size_t m_end{0};
};

// Inlined definitions ////////////////////////////////////////////////////////

template <typename T>
inline T *Array1D::beginAs() const
{
  if (anari::ANARITypeFor<T>::value != elementType())
    throw std::runtime_error("incorrect element type queried for array");

  return (T *)data() + m_begin;
}

template <typename T>
inline T *Array1D::endAs() const
{
  if (anari::ANARITypeFor<T>::value != elementType())
    throw std::runtime_error("incorrect element type queried for array");

  return (T *)data() + m_end;
}

} // namespace helide

HELIDE_ANARI_TYPEFOR_SPECIALIZATION(helide::Array1D *, ANARI_ARRAY1D);
