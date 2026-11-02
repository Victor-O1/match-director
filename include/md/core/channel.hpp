#pragma once

#include "md/core/spsc_queue.hpp"

#include <atomic>
#include <cstdint>
#include <utility>

namespace md {

// Blocking wrapper around SpscQueue for connecting pipeline threads.
//
// The fast path is the lock-free queue. Only when a side would otherwise spin
// (queue full for the producer, empty for the consumer) does it sleep, using
// C++20 std::atomic::wait (a futex on Linux). Each successful push/pop bumps a
// counter and calls notify_one(); libstdc++ skips the syscall when nobody is
// waiting, so the uncontended cost is two atomic RMWs.
//
// No lost wake-ups: the waiter reads the counter *before* re-checking the
// queue, and wait(old) returns immediately if the counter already moved.
//
// close() wakes everybody. After close, push() fails and pop() drains what is
// left, then fails. One producer thread and one consumer thread, like SpscQueue.
template <typename T, std::size_t Capacity>
class Channel {
 public:
  // Blocks while full. Returns false if the channel was closed.
  bool push(T&& value) {
    for (;;) {
      const std::uint32_t seen = popped_.load(std::memory_order_acquire);
      if (closed_.load(std::memory_order_acquire)) {
        return false;
      }
      if (queue_.try_push(std::move(value))) {
        pushed_.fetch_add(1, std::memory_order_release);
        pushed_.notify_one();
        return true;
      }
      popped_.wait(seen, std::memory_order_acquire);
    }
  }

  // Non-blocking push, for producers that must never stall.
  [[nodiscard]] bool try_push(T&& value) {
    if (closed_.load(std::memory_order_acquire) || !queue_.try_push(std::move(value))) {
      return false;
    }
    pushed_.fetch_add(1, std::memory_order_release);
    pushed_.notify_one();
    return true;
  }

  // Blocks while empty. Returns false once closed and drained.
  bool pop(T& out) {
    for (;;) {
      const std::uint32_t seen = pushed_.load(std::memory_order_acquire);
      if (queue_.try_pop(out)) {
        popped_.fetch_add(1, std::memory_order_release);
        popped_.notify_one();
        return true;
      }
      if (closed_.load(std::memory_order_acquire)) {
        // A push may have landed between try_pop and the closed check.
        if (queue_.try_pop(out)) {
          popped_.fetch_add(1, std::memory_order_release);
          popped_.notify_one();
          return true;
        }
        return false;
      }
      pushed_.wait(seen, std::memory_order_acquire);
    }
  }

  [[nodiscard]] bool try_pop(T& out) {
    if (!queue_.try_pop(out)) {
      return false;
    }
    popped_.fetch_add(1, std::memory_order_release);
    popped_.notify_one();
    return true;
  }

  // Callable from any thread.
  void close() {
    closed_.store(true, std::memory_order_release);
    pushed_.fetch_add(1, std::memory_order_release);
    popped_.fetch_add(1, std::memory_order_release);
    pushed_.notify_all();
    popped_.notify_all();
  }

  [[nodiscard]] bool closed() const { return closed_.load(std::memory_order_acquire); }
  [[nodiscard]] std::size_t size_approx() const { return queue_.size_approx(); }

 private:
  SpscQueue<T, Capacity> queue_;
  alignas(kCacheLine) std::atomic<std::uint32_t> pushed_{0};
  alignas(kCacheLine) std::atomic<std::uint32_t> popped_{0};
  std::atomic<bool> closed_{false};
};

}  // namespace md
