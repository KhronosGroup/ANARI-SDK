// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdlib>
#include <type_traits>

namespace anari {
namespace traits {

// C++14 traits for C++11 /////////////////////////////////////////////////////

template <bool B, class T = void>
using enable_if_t = typename std::enable_if<B, T>::type;

// Helper operators ///////////////////////////////////////////////////////////

template <typename T, typename Arg>
std::true_type operator==(const T &, const Arg &);

// type 'T' having '==' operator //////////////////////////////////////////////

template <typename T, typename Arg = T>
struct HasOperatorEqualsT
{
  enum
  {
    value =
        !std::is_same<decltype(*(T *)(0) == *(Arg *)(0)), std::true_type>::value
  };
};

template <typename T, typename TYPE>
using HasOperatorEquals =
    typename std::enable_if<HasOperatorEqualsT<T>::value, TYPE>::type;

template <typename T, typename TYPE>
using NoOperatorEquals =
    typename std::enable_if<!HasOperatorEqualsT<T>::value, TYPE>::type;

} // namespace traits
} // namespace anari
