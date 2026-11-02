#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace md {

struct LatencySummary {
  std::size_t count{0};
  std::size_t dropped{0};  // samples rejected because the buffer was full
  std::int64_t min_ns{0};
  std::int64_t p50_ns{0};
  std::int64_t p95_ns{0};
  std::int64_t p99_ns{0};
  std::int64_t max_ns{0};
  double mean_ns{0.0};
};

// Collects latency samples without allocating after construction.
// record() is cheap enough for a hot path; summarize() sorts a copy and is
// meant for the end of a run (or a reporting thread).
// Not thread-safe: use one recorder per thread and merge the results.
class LatencyRecorder {
 public:
  explicit LatencyRecorder(std::size_t capacity);

  void record(std::int64_t sample_ns) noexcept;
  void reset() noexcept;

  [[nodiscard]] LatencySummary summarize() const;
  [[nodiscard]] std::size_t size() const noexcept { return samples_.size(); }

 private:
  std::vector<std::int64_t> samples_;
  std::size_t dropped_{0};
};

}  // namespace md
