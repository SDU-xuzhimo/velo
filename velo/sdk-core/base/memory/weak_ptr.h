#ifndef SDK_CORE_BASE_MEMORY_WEAK_PTR_H_
#define SDK_CORE_BASE_MEMORY_WEAK_PTR_H_

#include "../macros.h"
#include <memory.h>
#include <memory>

namespace base {

template <typename T> class WeakPtr {

public:
  WeakPtr() : ptr_(nullptr) {}
  WeakPtr(T *_ptr, std::shared_ptr<bool> _flag) : ptr_(_ptr), flag_(_flag) {}

  T *get() const { return (flag_ && *flag_) ? ptr_ : nullptr; }

  T *operator->() const { return ptr_; }
  explicit operator bool() const { return get() != nullptr; }

private:
  T *ptr_;
  std::shared_ptr<bool> flag_;
};


template <typename T>
class WeakPtrFactory {
 public:
  explicit WeakPtrFactory(T* ptr) : ptr_(ptr), flag_(std::make_shared<bool>(true)) {}
  
  ~WeakPtrFactory() { *flag_ = false; } // 析构时，令所有发出去的指针失效

  WeakPtr<T> GetWeakPtr() {
    return WeakPtr<T>(ptr_, flag_);
  }

 private:
  T* ptr_;
  std::shared_ptr<bool> flag_;

  DISALLOW_COPY_AND_ASSIGN(WeakPtrFactory);
};

} // namespace base

#endif