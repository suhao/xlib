///////////////////////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2017 The Authors of xlib(http:://xlib.org) . All Rights Reserved. 
// Use of this source code is governed by a BSD-style license that can be 
// found in the LICENSE file. 
//
///////////////////////////////////////////////////////////////////////////////////////////

#ifndef XLIB_BASE_UINTPTR_CAST_INCLUDE_H_
#define XLIB_BASE_UINTPTR_CAST_INCLUDE_H_

#include <cstdint>
#include <type_traits>

template <typename From, typename To,
          typename std::enable_if<std::is_void<From>::value &&
                                  std::is_void<To>::value>::type* = nullptr>
std::uintptr_t uintptr_cast(const std::uintptr_t& ptr) {
  return ptr;
}

template <typename From, typename To,
          typename std::enable_if<!std::is_void<From>::value &&
                                    !std::is_void<To>::value>::type* = nullptr>
std::uintptr_t uintptr_cast(const std::uintptr_t& ptr) {
  if (std::is_same<From, To>::value) {
    return ptr;
  }
  auto from = reinterpret_cast<From*>(ptr);
  auto to = dynamic_cast<To*>(from);
  return reinterpret_cast<std::uintptr_t>(to);
}

template <typename From, typename To,
          typename std::enable_if<std::is_void<From>::value &&
                                    !std::is_void<To>::value>::type* = nullptr>
std::uintptr_t uintptr_cast(const std::uintptr_t& ptr) {
  return ptr;
}

template <typename From, typename To,
          typename std::enable_if<!std::is_void<From>::value &&
                                    std::is_void<To>::value>::type* = nullptr>
std::uintptr_t uintptr_cast(const std::uintptr_t& ptr) {
  return ptr;
}

template <typename From, typename To,
          typename std::enable_if<!std::is_void<From>::value &&
                                  !std::is_void<To>::value>::type* = nullptr>
std::uintptr_t uintptr_cast(const From* from) {
  if (std::is_same<From, To>::value) {
    return reinterpret_cast<std::uintptr_t>(from);
  }
  auto to = dynamic_cast<To*>(from);
  return reinterpret_cast<std::uintptr_t>(to);
}

template <typename From, typename To,
          typename std::enable_if<std::is_void<From>::value ||
                                  std::is_void<To>::value>::type* = nullptr>
std::uintptr_t uintptr_cast(const From* from) {
  return reinterpret_cast<std::uintptr_t>(from);
}

inline std::uintptr_t uintptr_cast(const void* from) {
  return reinterpret_cast<std::uintptr_t>(from);
}

template <typename To>
const To uintptr_cast(const std::uintptr_t& ptr) {
  return reinterpret_cast<To>(ptr);
}

#endif // !XLIB_BASE_UINTPTR_CAST_INCLUDE_H_
