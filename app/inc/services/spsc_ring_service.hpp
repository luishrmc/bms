#include <atomic>
#include <vector>
#include <semaphore>
#include <climits>
#include <cassert>

#pragma once

template <typename T>
class SPSCQueue {
public:
  explicit SPSCQueue(size_t capacity)
      : capacity_(round_up_pow2(capacity)),
        mask_(capacity_ - 1),
        buffer_(capacity_),
        head_(0),
        tail_(0),
        items_(0),                                     // start empty
        slots_(static_cast<std::ptrdiff_t>(capacity_)) // all slots free
  {
    assert((capacity_ & (capacity_ - 1)) == 0 && "capacity must be power of two");
  }

  // Blocking push/pop
  void push(const T& x) {
    slots_.acquire();                 // wait for free slot
    auto t = tail_.load(std::memory_order_relaxed);
    buffer_[t & mask_] = x;
    tail_.store(t + 1, std::memory_order_release);
    items_.release();
  }
  void pop(T& out) {
    items_.acquire();                 // wait for item
    auto h = head_.load(std::memory_order_relaxed);
    out = buffer_[h & mask_];
    head_.store(h + 1, std::memory_order_release);
    slots_.release();
  }

  // Non-blocking variants
  bool try_push(const T& x) {
    if (!slots_.try_acquire()) return false;
    auto t = tail_.load(std::memory_order_relaxed);
    buffer_[t & mask_] = x;
    tail_.store(t + 1, std::memory_order_release);
    items_.release();
    return true;
  }
  bool try_pop(T& out) {
    if (!items_.try_acquire()) return false;
    auto h = head_.load(std::memory_order_relaxed);
    out = buffer_[h & mask_];
    head_.store(h + 1, std::memory_order_release);
    slots_.release();
    return true;
  }

  size_t size() const {
    auto h = head_.load(std::memory_order_acquire);
    auto t = tail_.load(std::memory_order_acquire);
    return t - h; // works with wraparound because size_t wraps
  }
  size_t capacity() const { return capacity_; }

private:
  static size_t round_up_pow2(size_t n) {
    size_t p = 1; while (p < n) p <<= 1; return p;
  }

  const size_t capacity_;
  const size_t mask_;
  std::vector<T> buffer_;

  alignas(64) std::atomic<size_t> head_;
  alignas(64) std::atomic<size_t> tail_;

  // Blocking primitives (C++20)
  std::counting_semaphore<INT_MAX> items_;
  std::counting_semaphore<INT_MAX> slots_;
};

