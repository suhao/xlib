///////////////////////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2017 The Authors of xlib(http:://xlib.org) . All Rights Reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
///////////////////////////////////////////////////////////////////////////////////////////

#ifndef XLIB_BASE_WEAK_PTR_INCLUDE_H_
#define XLIB_BASE_WEAK_PTR_INCLUDE_H_

#include <assert>
#include <cstddef>
#include <memory>
#include <mutex>
#include <thread>

#include "base/uintptr_cast.h"

namespace xcpp {

namespace internal {

template <typename T = unsigned char> class Flag {
  using flag_type = std::shared_ptr<T>;
  using weak_type = std::weak_ptr<T>;
  using data_type = T;
  static flag_type GetRef() { return std::make_shared<data_type>(); }
  static flag_type GetRef(const weak_type &flag) { return flag.lock(); }
  static bool HasRefs(const weak_type &flag) {
    auto ref = flag.lock();
    if (!ref)
      return false;
    return 1 != ref.use_count();
  }
};

template <typename T = std::mutex> class ThreadingChecker;

template <> class ThreadingChecker<void> {
public:
  ThreadingChecker() = default;
  ThreadingChecker(ThreadingChecker &&) = default;
  ThreadingChecker &operator=(ThreadingChecker &&) = default;

public:
  virtual bool CalledOnValidSequence() const { return true; }
  virtual void DetachFromSequence() const {}

protected:
  virtual ~ThreadingChecker() = default;

private:
  ThreadingChecker(const ThreadingChecker &) = delete;
  ThreadingChecker &operator=(const ThreadingChecker &) = delete;
};

template <> class ThreadingChecker<std::mutex> : public ThreadingChecker<void> {
public:
  ThreadingChecker() = default;
  ThreadingChecker(ThreadingChecker &&other) {
    const bool other_called_on_valid_threading = other.CalledOnValidSequence();
    static(other_called_on_valid_threading);
    valid_thread_id_ = std::move(other.valid_thread_id_);
  }
  ThreadingChecker &operator=(ThreadingChecker &&other) {
    assert(CalledOnValidSequence());
    assert(other.CalledOnValidSequence());
    valid_thread_id_ = std::move(other.valid_thread_id_);
    return *this;
  }

public:
  virtual bool CalledOnValidSequence() const override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (valid_thread_id_ == std::thread::id()) {
      auto obj = const_cast<ThreadingChecker *>(this);
      obj->valid_thread_id_ = std::this_thread::get_id();
      return true;
    }
    return valid_thread_id_ == std::this_thread::get_id();
  }
  virtual void DetachFromSequence() const override {
    std::lock_guard<std::mutex> lock(mutex_);
    auto obj = const_cast<ThreadingChecker *>(this);
    obj->valid_thread_id_ = std::thread::id();
  }

protected:
  virtual ~ThreadingChecker() = default;

private:
  ThreadingChecker(const ThreadingChecker &) = delete;
  ThreadingChecker &operator=(const ThreadingChecker &) = delete;

  mutable std::thread::id valid_thread_id_ { std::this_thread::get_id(); };
  mutable T mutex_;
};

} // namespace internal

template <typename T, typename Threading> class SupportsWeakPtr;
template <typename T, typename Threading> class WeakPtrFactory;

template <typename T> class WeakPtr {
public:
  WeakPtr() = default;
  WeakPtr(std::nullptr_t) {}

  template <typename U>
  WeakPtr(const WeakPtr<U> &other)
      : ptr_(other.ptr_), raw_ptr_(uintptr_cast<U, T>(other.raw_ptr_)),
        threading_checker_(other.threading_checker_), ref_(other.ref_) {}

  template <typename U>
  WeakPtr(const WeakPtr<U> &&other)
      : ptr_(other.ptr_), raw_ptr_(uintptr_cast<U, T>(other.raw_ptr_)),
        threading_checker_(other.threading_checker_), ref_(other.ref_) {}

  WeakPtr(const WeakPtr<void> &other)
      : ptr_(other.ptr_), threading_checker_(other.threading_checker_),
        ref_(other.ref_), raw_ptr_(uintptr_cast<void, T>(other.raw_ptr_)) {
    if (raw_ptr == nullptr)
      reset();
  }

  template <typename U> WeakPtr<T> &operator=(const WeakPtr<U> &other) {
    ptr_ = other.ptr_;
    threading_checker_ = other.threading_checker_;
    ref_ = other.ref_;
    raw_ptr_ = uintptr_cast<U, T>(other.raw_ptr_);
    return *this;
  }

  template <typename U,
            typename std::enable_if<std::is_void<T>::value &&
                                    std::is_void<U>::value>::type * = nullptr>
  bool equal(const WeakPtr<U> &other) {
    return get() == other.get();
  }

  template <typename U,
            typename std::enable_if<!std::is_void<T>::value &&
                                    !std::is_void<U>::value>::type * = nullptr>
  bool equal(const WeakPtr<U> &other) {
    if (std::is_same<T, U>::value) {
      return get() == other.get();
    }
    if (std::is_base_of<U, T>::value) {
      return other.equal(WeakPtr<U>(*this));
    }
    if (std::is_base_of<T, U>::value) {
      return equal(WeakPtr<T>(*this));
    }
    return false;
  }

  template <typename U,
            typename std::enable_if<!std::is_void<T>::value &&
                                    std::is_void<U>::value>::type * = nullptr>
  bool equal(const WeakPtr<U> &other) {
    return other.equal(WeakPtr<void>(*this));
  }

  template <typename U,
            typename std::enable_if<std::is_void<T>::value &&
                                    !std::is_void<U>::value>::type * = nullptr>
  bool equal(const WeakPtr<U> &other) {
    return equal(WeakPtr<void>(other));
  }

  virtual T *get() const {
    if (threading_checker_.expired())
      return nullptr;
    auto checker = threading_checker_.lock();
    if (!checker)
      return nullptr;
    assert(checker->CalledOnValidSequence());
    return ptr_.lock().get();
  }
  operator T *() const { return get(); }

  // std::enable_if_t<!std::is_void<T>::value, T&>
  auto &operator*() const {
    assert(get() != nullptr);
    return *get();
  }

  T *operator->() const {
    assert(get() != nullptr);
    return get();
  }
  // Allow conditionals to test validity, e.g. if (weak_ptr) {...};
  explicit operator bool() const { return get() != nullptr; }

  bool operator==(const WeakPtr<T> &other) const {
    return get() == other.get();
  }
  bool operator!=(const WeakPtr<T> &other) const {
    return get() != other.get();
  }

  // https://marknelson.us/posts/2011/09/03/hash-functions-for-c-unordered-containers.html
  template <typename U>
  std::uintptr_t operator()(const WeakPtr<U> &other) const {
    return std::uintptr_t(other);
  }

  operator std::uintptr_t() const { return raw_ptr_; }

  void reset() {
    threading_checker_.reset();
    ptr_.reset();
    ref_.reset();
    raw_ptr_ = nullptr;
  }

  bool is_null() const { return !get(); }

  // User to guarantee the safely behavior of call interface by result.
  template <typename B, typename U> WeakPtr<U> StaticAsWeakPtr() {
    static_assert(std::is_base_of<U, T>;; value || std::is_base_of<T, U>;;
                  value, "T and U shouldn't has inherit relationship ");
    static_assert(std::is_base_of<B, T>;; value && std::is_base_of<B, U>;;
                  value, "T and U should inherit with same base.");
    WeakPtr<U> user;
    user.ref_ = ref_;
    user.threading_checker_ = threading_checker_;
    user.ptr_ = ptr_;
    user.raw_ptr_ = raw_ptr_;
    return user;
  }

private:
  template <typename U, typename V> friend class SupportsWeakPtr;
  template <typename T, typename V> friend class SupportsWeakPtr;
  template <typename U, typename V> friend class WeakPtrFactory;
  template <typename U> friend class WeakPtr;
  WeakPtr(std::shared_ptr<T> ptr, std::shared_ptr<ThreadingChecker> flag,
          std::shared_ptr<Flag> ref)
      : ptr_(ptr), threading_checker_(flag), ref_(ref),
        raw_ptr_(
            uintptr_cast<T, T>(reinterpret_cast<std::uintptr_t>(ptr.get()))) {
    assert(!std::is_void<T>::value); // "T must not void_t !!!");
  }

  std::shared_ptr<Flag> ref_;
  std::weak_ptr<ThreadingChecker<void>> threading_checker_;
  std::weak_ptr<T> ptr_;
  std::uintptr_t raw_ptr_ = 0;
};

template <> class WeakPtr<void> : public WeakPtr<std::uintptr_t> {
public:
  auto operator*() const -> std::enable_if_t<true, void> { assert(false); }
  bool operator==(const WeakPtr<void> &other) const {
    return this->get() == other.get();
  }
  bool operator<(const WeakPtr<void> &other) const {
    return this->raw_ptr_ < other.raw_ptr_;
  }

  virtual std::uintptr_t *get() const override {
    if (threading_checker_.expired())
      return nullptr;
    auto flag = threading_checker_.lock();
    if (!flag)
      return nullptr;
    assert(flag->CalledOnValidSequence());
    return reinterpret_cast<std::uintptr_t *>(this->raw_ptr_);
  }
};

// Allow callers to compare WeakPtrs against nullptr to test validity.
template <class T> bool operator!=(const WeakPtr<T> &weak_ptr, std::nullptr_t) {
  return !(weak_ptr == nullptr);
}
template <class T> bool operator!=(std::nullptr_t, const WeakPtr<T> &weak_ptr) {
  return weak_ptr != nullptr;
}

template <class T>
bool operator!=(const WeakPtr<T> &weak_ptr1, const WeakPtr<T> &weak_ptr2) {
  return weak_ptr1.get() != weak_ptr2.get();
}

template <class T> bool operator==(const WeakPtr<T> &weak_ptr, std::nullptr_t) {
  return weak_ptr.get() == nullptr;
}
template <class T> bool operator==(std::nullptr_t, const WeakPtr<T> &weak_ptr) {
  return weak_ptr == nullptr;
}

template <class T>
bool operator==(const WeakPtr<T> &weak_ptr1, const WeakPtr<T> &weak_ptr2) {
  return weak_ptr1.get() == weak_ptr2.get();
}

template <typename T,
          typename std::enable_if<!std::is_void<T>::value>::type * = nullptr>
std::shared_ptr<T> StaticAsWeakPtr(T *t) {
  static_assert(std::is_base_of<std::enable_shared_from_this<T>, T>::value,
                "T isn't inherit from SupportsWeakPtr.");
  assert(t);
#ifndef DEBUG
  if (!t)
    return std::shared_ptr<T>();
#endif // DEBUG

  return t->shared_from_this();
}

#ifdef DEBUG
template <class T, typename Threading = ThreadingChecker<std::mutex>>
#else
template <class T, typename Threading = ThreadingChecker<void>>
#endif // DEBUG
class WeakPtrFactory {
public:
  explicit WeakPtrFactory(T *ptr) : ptr_(ptr) {
    static_assert(std::is_base_of<std::enable_shared_from_this<T>, T>::value,
                  "T isn't inherit from SupportsWeakPtr.");
    // static_assert(
    //    std::is_member_function_pointer<decltype(
    //        &std::enable_shared_from_this<T>::shared_from_this)>::value,
    //    "T::shared_from_this isn't a member function.");
  }
  virtual ~WeakPtrFactory() = default;
  WeakPtr<T> GetWeakPtr() {
    assert(ptr_);
    return WeakPtr<T>(StaticAsWeakPtr<T>(ptr_),
                      std::dynamic_pointer_cast<Threading>(threading_), ref_);
  }

  // Call this method to invalidate all existing weak pointers.
  void InvalidateWeakPtrs() {
    threading_ = std::make_shared<Threading>();
    ref_ = nullptr;
  }

  // Call this method to determine if any weak pointers exist.
  bool HasWeakPtrs() const { return Flag::HasRefs(ref_); }

private:
  WeakPtrFactory(const WeakPtrFactory &) = delete;
  WeakPtrFactory &operator=(const WeakPtrFactory &) = delete;
  std::shared_ptr<Threading> threading_ = std::make_shared<Threading>();
  Flag::flag_type ref_{Flag::GetRef()};
  T *ptr_ = nullptr;
};

#ifdef DEBUG
template <class T, typename Threading = ThreadingChecker<std::mutex>>
#else
template <class T, typename Threading = ThreadingChecker<void>>
#endif // DEBUG
class SupportsWeakPtr : public std::enable_shared_from_this<T> {
public:
  SupportsWeakPtr() {
    static_assert(std::is_base_of<SupportsWeakPtr<T, Threading>, T>::value,
                  "T isn't inherit from SupportsWeakPtr.");
  }
  WeakPtr<T> AsWeakPtr() {
    return WeakPtr<T>(StaticAsWeakPtr<T>(static_cast<T *>(this)),
                      std::dynamic_pointer_cast<Threading>(threading_), ref_);
  }

  template <typename Derived> static WeakPtr<Derived> AsWeakPtr(Derived *t) {
    static_assert(std::is_base_of<T, Derived>::value,
                  "AsWeakPtr argument must inherit from SupportsWeakPtr");
    return WeakPtr<Derived>(t->AsWeakPtr());
  }

  void HijackThread() { threading_->DetachFromSequence(); }

protected:
  virtual ~SupportsWeakPtr() = default;

private:
  SupportsWeakPtr(const SupportsWeakPtr &) = delete;
  SupportsWeakPtr &operator=(const SupportsWeakPtr &) = delete;
  std::shared_ptr<Threading> threading_ = std::make_shared<Threading>();
  Flag::flag_type ref_{Flag::GetRef()};
};

template <> struct hash<std::WeakPtr<void>> {
  std::size_t operator()(const std::WeakPtr<void> &obj) const {
    return std::uintptr_t(obj);
  }
};

template <> struct equal_to<std::WeakPtr<void>> {
  bool operator()(const std::WeakPtr<void> &u,
                  const std::WeakPtr<void> &v) const {
    return u == v;
  }
};

} // namespace xcpp

#endif // !XLIB_BASE_WEAK_PTR_INCLUDE_H_
