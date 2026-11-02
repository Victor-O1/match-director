#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <type_traits>
#include <utility>

namespace md {

// Fixed cache-line size. std::hardware_destructive_interference_size is not
// ABI-stable across GCC flags (it warns), and 64 bytes is correct for x86-64.
inline constexpr std::size_t kCacheLine = 64;

// Bounded, lock-free, single-producer / single-consumer queue.
//
// Exactly one thread may call try_push() and exactly one (other) thread may
// call try_pop(). Neither call blocks, allocates or takes a lock, so it is safe
// on real-time paths (decode -> inference -> compositor -> encoder).
//
// How it works:
//  * head_ is the next slot to read (written only by the consumer),
//    tail_ is the next slot to write (written only by the producer).
//    They grow forever; the slot index is `index & (Capacity - 1)`.
//    (Overflowing a 64-bit counter at 1e9 ops/s takes ~585 years.)
//  * The producer writes the slot, then publishes it with a release store to
//    tail_. The consumer reads tail_ with acquire, so it is guaranteed to see
//    the slot contents. The same pairing in reverse hands the slot back.
//  * Each side keeps a cached copy of the other side's index and only re-reads
//    the shared atomic when the cache says "full" / "empty". This avoids
//    bouncing the other side's cache line on every operation.
//  * head_ and tail_ live on separate cache lines to avoid false sharing.
//
// Slots are a std::array<T>, so T must be default-constructible and
// move-assignable. That keeps the code free of placement-new; for our use
// (frame handles, small structs) the trade-off is fine.
template <typename T, std::size_t Capacity>
class SpscQueue {
  static_assert(Capacity >= 2, "capacity must be at least 2");
  static_assert((Capacity & (Capacity - 1)) == 0, "capacity must be a power of two");
  static_assert(std::is_default_constructible_v<T>);
  static_assert(std::is_nothrow_move_assignable_v<T>);

 public:
  SpscQueue() = default;
  SpscQueue(const SpscQueue&) = delete;
  SpscQueue& operator=(const SpscQueue&) = delete;

  // Producer only. Returns false (and leaves `value` untouched) when full.
  [[nodiscard]] bool try_push(T&& value) noexcept {
    const std::size_t tail = tail_.load(std::memory_order_relaxed);  // we own it
    if (tail - cached_head_ == Capacity) {
      cached_head_ = head_.load(std::memory_order_acquire);
      if (tail - cached_head_ == Capacity) {
        return false;
      }
    }
    slots_[tail & kMask] = std::move(value);
    tail_.store(tail + 1, std::memory_order_release);
    return true;
  }

  // Consumer only. Returns false when empty.
  [[nodiscard]] bool try_pop(T& out) noexcept {
    const std::size_t head = head_.load(std::memory_order_relaxed);  // we own it
    if (head == cached_tail_) {
      cached_tail_ = tail_.load(std::memory_order_acquire);
      if (head == cached_tail_) {
        return false;
      }
    }
    out = std::move(slots_[head & kMask]);
    head_.store(head + 1, std::memory_order_release);
    return true;
  }

  // Approximate when called while the other thread is active.
  [[nodiscard]] std::size_t size_approx() const noexcept {
    const std::size_t tail = tail_.load(std::memory_order_acquire);
    const std::size_t head = head_.load(std::memory_order_acquire);
    return tail - head;
  }

  [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }

 private:
  static constexpr std::size_t kMask = Capacity - 1;

  // Consumer-owned line.
  alignas(kCacheLine) std::atomic<std::size_t> head_{0};
  std::size_t cached_tail_{0};

  // Producer-owned line.
  alignas(kCacheLine) std::atomic<std::size_t> tail_{0};
  std::size_t cached_head_{0};

  alignas(kCacheLine) std::array<T, Capacity> slots_{};
};

}  // namespace md
