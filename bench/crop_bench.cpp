#include "md/director/crop.hpp"

#include <benchmark/benchmark.h>

namespace {

// The director computes one crop per output frame; this confirms the cost is
// negligible against a 16.7 ms (60 fps) frame budget.
void BM_ComputeAndSnapCrop(benchmark::State& state) {
  constexpr md::Size src{3840, 1080};
  double cx = 0.0;
  for (auto _ : state) {
    cx += 7.3;
    if (cx > 3840.0) {
      cx = 0.0;
    }
    const md::RectF r = md::compute_crop(src, {cx, 540.0}, 1280.0);
    benchmark::DoNotOptimize(md::snap_to_even(r, src));
  }
}
BENCHMARK(BM_ComputeAndSnapCrop);

}  // namespace
