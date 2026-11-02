#pragma once

#include <chrono>
#include <cstdint>

namespace md {

// Monotonic nanoseconds. Every pipeline stage stamps frames with this so
// end-to-end latency (ingest -> output) can be measured on one clock.
[[nodiscard]] inline std::int64_t now_ns() noexcept {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

// Per-frame timestamps, one per pipeline stage. 0 means "not reached".
struct StageTimes {
  std::int64_t ingest_ns{0};     // packet read from input
  std::int64_t decoded_ns{0};    // decoder produced the frame
  std::int64_t inferred_ns{0};   // detector/tracker done
  std::int64_t composited_ns{0}; // crop + graphics rendered
  std::int64_t encoded_ns{0};    // encoder accepted/produced the packet
  std::int64_t output_ns{0};     // packet written to file/network
};

}  // namespace md
