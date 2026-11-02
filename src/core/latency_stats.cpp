#include "md/core/latency_stats.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace md {

LatencyRecorder::LatencyRecorder(std::size_t capacity) { samples_.reserve(capacity); }

void LatencyRecorder::record(std::int64_t sample_ns) noexcept {
  // Never grow: growing would allocate on the hot path.
  if (samples_.size() == samples_.capacity()) {
    ++dropped_;
    return;
  }
  samples_.push_back(sample_ns);
}

void LatencyRecorder::reset() noexcept {
  samples_.clear();  // keeps capacity
  dropped_ = 0;
}

namespace {

// Nearest-rank percentile on sorted data: the smallest value such that at
// least p% of samples are <= it.
std::int64_t percentile(const std::vector<std::int64_t>& sorted, double p) {
  const auto n = static_cast<double>(sorted.size());
  auto rank = static_cast<std::size_t>(std::max(1.0, std::ceil(p / 100.0 * n)));
  return sorted[std::min(rank, sorted.size()) - 1];
}

}  // namespace

LatencySummary LatencyRecorder::summarize() const {
  LatencySummary s;
  s.count = samples_.size();
  s.dropped = dropped_;
  if (samples_.empty()) {
    return s;
  }
  std::vector<std::int64_t> sorted = samples_;
  std::sort(sorted.begin(), sorted.end());
  s.min_ns = sorted.front();
  s.max_ns = sorted.back();
  s.p50_ns = percentile(sorted, 50.0);
  s.p95_ns = percentile(sorted, 95.0);
  s.p99_ns = percentile(sorted, 99.0);
  const long double sum = std::accumulate(sorted.begin(), sorted.end(), 0.0L);
  s.mean_ns = static_cast<double>(sum / static_cast<long double>(sorted.size()));
  return s;
}

}  // namespace md
