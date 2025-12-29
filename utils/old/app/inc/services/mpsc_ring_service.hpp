#include <atomic>
#include <vector>
#include <semaphore>
#include <climits>
#include <cassert>

#pragma once

template <typename T>
class MPSCQueue {
public:
  explicit MPSCQueue(size_t capacity)
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
    // Ensure space first (bounded buffer)
    slots_.acquire();

    // Reserve a unique slot for this producer
    size_t t = tail_.fetch_add(1, std::memory_order_acq_rel);

    // Write payload to the reserved slot
    buffer_[t & mask_] = x;

    // Publish the item to the consumer
    items_.release();
  }

  void pop(T& out) {
    // Wait until an item is available
    items_.acquire();

    // Single consumer: safe to use a plain load + increment
    size_t h = head_.load(std::memory_order_relaxed);
    out = buffer_[h & mask_];

    // Advance head and free a slot
    head_.store(h + 1, std::memory_order_release);
    slots_.release();
  }

  // Non-blocking variants
  bool try_push(const T& x) {
    if (!slots_.try_acquire()) return false;

    size_t t = tail_.fetch_add(1, std::memory_order_acq_rel);
    buffer_[t & mask_] = x;
    items_.release();
    return true;
  }

  bool try_pop(T& out) {
    if (!items_.try_acquire()) return false;

    size_t h = head_.load(std::memory_order_relaxed);
    out = buffer_[h & mask_];
    head_.store(h + 1, std::memory_order_release);
    slots_.release();
    return true;
  }

  size_t size() const {
    // Approximate size; safe for stats/monitoring
    auto h = head_.load(std::memory_order_acquire);
    auto t = tail_.load(std::memory_order_acquire);
    return t - h; // wrap-safe on size_t
  }

  size_t capacity() const { return capacity_; }

private:
  static size_t round_up_pow2(size_t n) {
    size_t p = 1; while (p < n) p <<= 1; return p;
  }

  const size_t capacity_;
  const size_t mask_;
  std::vector<T> buffer_;

  alignas(64) std::atomic<size_t> head_; // single consumer updates
  alignas(64) std::atomic<size_t> tail_; // multiple producers update via fetch_add

  // Blocking primitives (C++20)
  std::counting_semaphore<INT_MAX> items_; // counts filled slots
  std::counting_semaphore<INT_MAX> slots_; // counts free slots
};

